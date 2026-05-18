#include <iostream>
#include <unistd.h>
#include <cmath>
#include <ctime>
#include "imu/imu.h"

static float calcularMagnitudG(float ax, float ay, float az) {
    return std::sqrt(ax * ax + ay * ay + az * az);
}

int main() {
    IMU imu(1, 0x68);

    std::cout << "===================================" << std::endl;
    std::cout << "PRUEBA AISLADA DE IMU - CRASHGUARD" << std::endl;
    std::cout << "===================================" << std::endl;

    if (!imu.init()) {
        std::cerr << "❌ Error inicializando IMU" << std::endl;
        return -1;
    }

    std::cout << "✅ IMU inicializada correctamente" << std::endl;
    std::cout << "Movelo y observa los valores." << std::endl;
    std::cout << "Ctrl+C para salir." << std::endl;
    std::cout << "-----------------------------------" << std::endl;

    while (true) {
        float ax = 0.0f;
        float ay = 0.0f;
        float az = 0.0f;

        if (!imu.readAccel(ax, ay, az)) {
            std::cerr << "❌ Error leyendo IMU" << std::endl;
            sleep(1);
            continue;
        }

        float magnitud = calcularMagnitudG(ax, ay, az);
        float delta = std::fabs(magnitud - 1.0f);

        std::cout << "AX: " << ax
                  << " | AY: " << ay
                  << " | AZ: " << az
                  << " | G: " << magnitud
                  << " | DELTA_1G: " << delta
                  << std::endl;

        usleep(150000);
    }

    return 0;
}
