import RPi.GPIO as GPIO
import time

BUZZER = 22

GPIO.setmode(GPIO.BCM)
GPIO.setup(BUZZER, GPIO.OUT)

pwm = GPIO.PWM(BUZZER, 2000)  # 2 kHz
pwm.start(50)  # duty cycle 50%

try:
    for _ in range(3):   # 3 pitidos
        pwm.ChangeDutyCycle(50)
        time.sleep(0.3)
        pwm.ChangeDutyCycle(0)
        time.sleep(0.2)
finally:
    pwm.stop()
    GPIO.cleanup()
