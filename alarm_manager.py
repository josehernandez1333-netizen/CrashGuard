#!/usr/bin/env python3
import RPi.GPIO as GPIO
import time
import sys

BUZZER_PIN = 22
LED_PIN = 27
BUTTON_PIN = 23

TOTAL_SECONDS = 120.0
LONG_PRESS_SECONDS = 10.0

def set_outputs(buzzer_on: bool, led_on: bool):
    GPIO.output(BUZZER_PIN, GPIO.HIGH if buzzer_on else GPIO.LOW)
    GPIO.output(LED_PIN, GPIO.HIGH if led_on else GPIO.LOW)

def short_cancel_pattern():
    for _ in range(3):
        GPIO.output(BUZZER_PIN, GPIO.HIGH)
        GPIO.output(LED_PIN, GPIO.HIGH)
        time.sleep(0.18)
        GPIO.output(BUZZER_PIN, GPIO.LOW)
        GPIO.output(LED_PIN, GPIO.LOW)
        time.sleep(0.12)

def detect_idle_state(samples=20, delay=0.01):
    values = []
    for _ in range(samples):
        values.append(GPIO.input(BUTTON_PIN))
        time.sleep(delay)
    return 1 if sum(values) >= (len(values) / 2) else 0

def main():
    GPIO.setwarnings(False)
    GPIO.setmode(GPIO.BCM)

    GPIO.setup(BUZZER_PIN, GPIO.OUT, initial=GPIO.LOW)
    GPIO.setup(LED_PIN, GPIO.OUT, initial=GPIO.LOW)
    GPIO.setup(BUTTON_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)

    idle_state = detect_idle_state()
    pressed_state = GPIO.LOW if idle_state == GPIO.HIGH else GPIO.HIGH

    start_time = time.time()
    press_start = None

    try:
        while (time.time() - start_time) < TOTAL_SECONDS:
            now = time.time()
            phase = (now - start_time) % 1.2

            buzzer_on = phase < 0.85
            led_on = int((now - start_time) * 10) % 2 == 0

            set_outputs(buzzer_on, led_on)

            current = GPIO.input(BUTTON_PIN)

            if current == pressed_state:
                if press_start is None:
                    press_start = now

                held = now - press_start

                if held >= LONG_PRESS_SECONDS:
                    set_outputs(False, False)
                    short_cancel_pattern()
                    set_outputs(False, False)
                    sys.exit(2)
            else:
                if press_start is not None:
                    held = now - press_start
                    press_start = None

                    if held < LONG_PRESS_SECONDS:
                        set_outputs(False, False)
                        sys.exit(0)

            time.sleep(0.03)

        set_outputs(False, False)
        sys.exit(0)

    finally:
        set_outputs(False, False)
        GPIO.cleanup()

if __name__ == "__main__":
    main()
