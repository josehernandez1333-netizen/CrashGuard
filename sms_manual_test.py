#!/usr/bin/env python3
import sys
import time
import json
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

def at(ser, cmd, wait=1.5, print_output=True):
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    ser.write((cmd + "\r").encode())
    ser.flush()
    time.sleep(0.2)
    resp = read_all(ser, wait)
    if print_output:
        print(f"\n===== {cmd} =====")
        print(resp if resp else "(sin respuesta)")
    return resp

def modem_init(ser):
    checks = [
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

    for cmd, wait in checks:
        resp = at(ser, cmd, wait)
        if "ERROR" in resp:
            raise RuntimeError(f"Error en comando {cmd}")

    cpin = at(ser, "AT+CPIN?", 2.0, print_output=False)
    if "READY" not in cpin:
        raise RuntimeError("La SIM no está lista")

    creg = at(ser, "AT+CREG?", 2.0, print_output=False)
    if "+CREG: 0,1" not in creg and "+CREG: 0,5" not in creg:
        raise RuntimeError("El módem no está registrado en red")

def send_sms(ser, number, message):
    print(f"\n===== AT+CMGS=\"{number}\" =====")
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    ser.write((f'AT+CMGS="{number}"\r').encode())
    ser.flush()
    time.sleep(1.2)

    prompt = read_all(ser, 4)
    print(prompt if prompt else "(sin prompt)")

    if ">" not in prompt:
        raise RuntimeError("El módem no devolvió el prompt >")

    payload = message.encode() + b"\x1A"
    ser.write(payload)
    ser.flush()

    final = read_all(ser, 25)

    print("\n===== RESPUESTA FINAL SMS =====")
    print(final if final else "(sin respuesta final)")

    if "+CMGS:" in final and "OK" in final:
        return True

    raise RuntimeError("El módem no confirmó el envío del SMS")

def get_number_from_interface():
    try:
        r = requests.get(CONFIG_URL, timeout=5)
        r.raise_for_status()
        data = r.json()
        number = data.get("numero_destino", "").strip()
        if not number:
            raise RuntimeError("La interfaz no devolvió numero_destino")
        return number
    except Exception as e:
        raise RuntimeError(f"No se pudo leer el número desde la interfaz: {e}")

def main():
    if len(sys.argv) >= 2:
        number = sys.argv[1].strip()
    else:
        number = get_number_from_interface()

    if len(sys.argv) >= 3:
        message = sys.argv[2]
    else:
        message = "PRUEBA SMS CRASHGUARD"

    print("===================================")
    print("PRUEBA MANUAL DE SMS CRASHGUARD")
    print("===================================")
    print(f"Numero destino: {number}")
    print(f"Mensaje: {message}")

    ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=0.2)
    time.sleep(1.0)

    try:
        modem_init(ser)
        ok = send_sms(ser, number, message)
        if ok:
            print("\n✅ SMS enviado manualmente correctamente")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
