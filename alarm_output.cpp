#include "alarm_output.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <array>

AlarmOutput::AlarmOutput(int ledGpio, int buzzerGpio, int buttonGpio)
    : ledPin(ledGpio),
      buzzerPin(buzzerGpio),
      buttonPin(buttonGpio),
      running(false),
      stopRequested(false) {}

AlarmOutput::~AlarmOutput() {
    stop();
}

int AlarmOutput::execCommand(const char* cmd) {
    return std::system(cmd);
}

bool AlarmOutput::gpioModeOut(int gpio) {
    char cmd[128];
    std::snprintf(cmd, sizeof(cmd), "raspi-gpio set %d op", gpio);
    return execCommand(cmd) == 0;
}

bool AlarmOutput::gpioModeInPullUp(int gpio) {
    char cmd1[128];
    char cmd2[128];
    std::snprintf(cmd1, sizeof(cmd1), "raspi-gpio set %d ip", gpio);
    std::snprintf(cmd2, sizeof(cmd2), "raspi-gpio set %d pu", gpio);
    return execCommand(cmd1) == 0 && execCommand(cmd2) == 0;
}

bool AlarmOutput::gpioWrite(int gpio, bool high) {
    char cmd[128];
    std::snprintf(cmd, sizeof(cmd), "raspi-gpio set %d %s", gpio, high ? "dh" : "dl");
    return execCommand(cmd) == 0;
}

bool AlarmOutput::gpioRead(int gpio) {
    char cmd[128];
    std::snprintf(cmd, sizeof(cmd), "raspi-gpio get %d", gpio);

    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        return true;
    }

    std::array<char, 256> buffer{};
    std::string output;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);

    // level=0 => botón presionado si está con pull-up
    if (output.find("level=0") != std::string::npos) {
        return false;
    }
    return true;
}

bool AlarmOutput::init() {
    bool ok = true;

    ok &= gpioModeOut(ledPin);
    ok &= gpioModeOut(buzzerPin);
    ok &= gpioModeInPullUp(buttonPin);

    ok &= gpioWrite(ledPin, false);
    ok &= gpioWrite(buzzerPin, false);

    if (!ok) {
        std::cerr << "❌ Error inicializando GPIO de alarma" << std::endl;
        return false;
    }

    std::cout << "✅ GPIO de alarma inicializado" << std::endl;
    return true;
}

bool AlarmOutput::isRunning() const {
    return running.load();
}

void AlarmOutput::start(int durationSeconds) {
    if (running.load()) {
        return;
    }

    stopRequested.store(false);
    running.store(true);

    worker = std::thread(&AlarmOutput::alarmLoop, this, durationSeconds);
}

void AlarmOutput::stop() {
    stopRequested.store(true);

    if (worker.joinable()) {
        worker.join();
    }

    gpioWrite(ledPin, false);
    gpioWrite(buzzerPin, false);
    running.store(false);
}

void AlarmOutput::alarmLoop(int durationSeconds) {
    using namespace std::chrono;

    auto startTime = steady_clock::now();

    // Patrón:
    // 3 pitidos largos:
    // 700 ms ON / 300 ms OFF
    // LED parpadeando en el mismo ciclo
    while (!stopRequested.load()) {
        auto now = steady_clock::now();
        auto elapsed = duration_cast<seconds>(now - startTime).count();

        if (elapsed >= durationSeconds) {
            break;
        }

        // botón con pull-up:
        // false = presionado
        bool buttonReleased = gpioRead(buttonPin);
        if (!buttonReleased) {
            std::cout << "🛑 Alarma desactivada por botón" << std::endl;
            break;
        }

        for (int i = 0; i < 3; ++i) {
            if (stopRequested.load()) {
                break;
            }

            bool stillReleased = gpioRead(buttonPin);
            if (!stillReleased) {
                std::cout << "🛑 Alarma desactivada por botón" << std::endl;
                stopRequested.store(true);
                break;
            }

            gpioWrite(buzzerPin, true);
            gpioWrite(ledPin, true);
            std::this_thread::sleep_for(milliseconds(700));

            gpioWrite(buzzerPin, false);
            gpioWrite(ledPin, false);
            std::this_thread::sleep_for(milliseconds(300));

            now = steady_clock::now();
            elapsed = duration_cast<seconds>(now - startTime).count();
            if (elapsed >= durationSeconds) {
                stopRequested.store(true);
                break;
            }
        }

        // pequeño descanso entre bloques
        for (int j = 0; j < 4; ++j) {
            if (stopRequested.load()) {
                break;
            }

            bool stillReleased = gpioRead(buttonPin);
            if (!stillReleased) {
                std::cout << "🛑 Alarma desactivada por botón" << std::endl;
                stopRequested.store(true);
                break;
            }

            gpioWrite(ledPin, true);
            std::this_thread::sleep_for(milliseconds(150));
            gpioWrite(ledPin, false);
            std::this_thread::sleep_for(milliseconds(150));
        }
    }

    gpioWrite(buzzerPin, false);
    gpioWrite(ledPin, false);
    running.store(false);

    std::cout << "✅ Alarma local finalizada" << std::endl;
}
