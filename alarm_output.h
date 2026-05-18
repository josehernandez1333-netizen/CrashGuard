#ifndef ALARM_OUTPUT_H
#define ALARM_OUTPUT_H

#include <atomic>
#include <thread>

class AlarmOutput {
public:
    AlarmOutput(int ledGpio, int buzzerGpio, int buttonGpio);
    ~AlarmOutput();

    bool init();
    void start(int durationSeconds = 120);
    void stop();
    bool isRunning() const;

private:
    int ledPin;
    int buzzerPin;
    int buttonPin;

    std::atomic<bool> running;
    std::atomic<bool> stopRequested;
    std::thread worker;

    void alarmLoop(int durationSeconds);

    bool gpioModeOut(int gpio);
    bool gpioModeInPullUp(int gpio);
    bool gpioWrite(int gpio, bool high);
    bool gpioRead(int gpio);

    static int execCommand(const char* cmd);
};

#endif
