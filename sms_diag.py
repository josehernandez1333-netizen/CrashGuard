#!/usr/bin/env python3
import serial
import time
import sys

PORT = "/dev/ttyAMA0"
BAUD = 115200

def read_all(ser, seconds=1.5):
    end = time.time() + seconds
    out = b""
    while time.time() < end:
        n = ser.in_waiting
        if n:
            out += ser.read(n)
        time.sleep(0.05)
    return out.decode(errors="ignore")

def cmd(ser, at, wait=1.5):
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    ser.write((at + "\r").encode())
    ser.flush()
    time.sleep(0.2)
    resp = read_all(ser, wait)
    print(f"\n===== {at} =====")
    print(resp if resp else "(sin respuesta)")
    return resp

def send_test_sms(ser, number, message):
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    print(f"\n===== AT+CMGS=\"{number}\" =====")
    ser.write((f'AT+CMGS="{number}"\r').encode())
    ser.flush()
    time.sleep(1.5)

    prompt = read_all(ser, 3)
    print(prompt if prompt else "(sin prompt)")

    if ">" not in prompt:
        print("❌ No apareció el prompt >")
        return False

    ser.write(message.encode() + b"\x1A")
    ser.flush()

    final = read_all(ser, 20)
    print("\n===== RESPUESTA FINAL SMS =====")
    print(final if final else "(sin respuesta final)")

    ok = ("+CMGS:" in final and "OK" in final)
    if ok:
        print("✅ El módem reporta SMS aceptado por la red")
    else:
        print("❌ El módem NO confirmó envío")
    return ok

def main():
    if len(sys.argv) < 2:
        print("Uso: python3 sms_diag.py +521XXXXXXXXXX")
        sys.exit(1)

    number = sys.argv[1]

    ser = serial.Serial(PORT, BAUD, timeout=0.2)
    time.sleep(1)

    cmd(ser, "AT", 1)
    cmd(ser, "ATE0", 1)
    cmd(ser, "AT+CPIN?", 2)
    cmd(ser, "AT+CREG?", 2)
    cmd(ser, "AT+CSQ", 2)
    cmd(ser, "AT+COPS?", 3)
    cmd(ser, "AT+CMGF=1", 1)
    cmd(ser, 'AT+CSCS="GSM"', 1)
    cmd(ser, "AT+CSCA?", 2)
    cmd(ser, "AT+CSMP=17,167,0,0", 1)

    send_test_sms(ser, number, "TEST CRASHGUARD")

    ser.close()

if __name__ == "__main__":
    main()
