#!/home/pi/CrashGuard/venv/bin/python3
import sys
import time
import serial
import requests

SERIAL_PORT = "/dev/ttyAMA0"
BAUDRATE = 115200
CONFIG_URL = "http://127.0.0.1:5000/api/config"

def read_all(ser, seconds=1.5):
    end = time.time() + seconds
    out = b""
    while time.time() < end:
        n = ser.in_waiting
        if n:
            out += ser.read(n)
        time.sleep(0.05)
    return out.decode(errors="ignore")

def at(ser, cmd, wait=1.5):
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    ser.write((cmd + "\r").encode())
    ser.flush()
    time.sleep(0.2)
    return read_all(ser, wait)

def modem_init(ser):
    cmds = [
        ("AT", 1.5),
        ("ATE0", 1.5),
        ("AT+CPIN?", 2.0),
        ("AT+CREG?", 2.0),
        ("AT+CSQ", 2.0),
        ("AT+CMEE=2", 1.5),
        ("AT+CMGF=1", 1.5),
        ('AT+CSCS="GSM"', 1.5),
        ("AT+CSCA?", 2.0),
    ]
    for cmd, wait in cmds:
        resp = at(ser, cmd, wait)
        if "ERROR" in resp:
            raise RuntimeError(f"Error en comando {cmd}: {resp}")

    if "READY" not in at(ser, "AT+CPIN?", 2.0):
        raise RuntimeError("La SIM no está lista")

    creg = at(ser, "AT+CREG?", 2.0)
    if "+CREG: 0,1" not in creg and "+CREG: 0,5" not in creg:
        raise RuntimeError("El módem no está registrado en red")

def get_number_from_interface():
    r = requests.get(CONFIG_URL, timeout=5)
    r.raise_for_status()
    data = r.json()
    number = str(data.get("numero_destino", "")).strip()
    if not number:
        raise RuntimeError("La interfaz no devolvió numero_destino")
    return number

def send_sms(ser, number, message):
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    ser.write((f'AT+CMGS="{number}"\r').encode())
    ser.flush()
    time.sleep(1.2)

    prompt = read_all(ser, 4)
    if ">" not in prompt:
        raise RuntimeError(f"No apareció el prompt >. Respuesta: {prompt}")

    ser.write(message.encode() + b"\x1A")
    ser.flush()

    final = read_all(ser, 25)
    if "+CMGS:" in final and "OK" in final:
        return True

    raise RuntimeError(f"El módem no confirmó el envío: {final}")

def main():
    if len(sys.argv) < 2:
        raise RuntimeError("Uso: send_second_sms.py <video_link>")

    link = sys.argv[1].strip()
    number = get_number_from_interface()
    message = f"Descarga para ver:\n{link}"

    ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=0.2)
    time.sleep(1.0)

    try:
        modem_init(ser)
        ok = send_sms(ser, number, message)
        if ok:
            print("✅ Segundo SMS enviado correctamente")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
