#include "imu.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <cstdio>
#include <cstdint>

IMU::IMU(int bus, int address) : file(-1), addr(address) {
    char filename[20];
    snprintf(filename, sizeof(filename), "/dev/i2c-%d", bus);
    file = open(filename, O_RDWR);
}

IMU::~IMU() {
    if (file >= 0) {
        close(file);
    }
}

bool IMU::init() {
    if (file < 0) {
        perror("Error abriendo I2C");
        return false;
    }

    if (ioctl(file, I2C_SLAVE, addr) < 0) {
        perror("Error en ioctl I2C_SLAVE");
        return false;
    }

    char buf[2];
    buf[0] = 0x6B;
    buf[1] = 0x00;

    if (write(file, buf, 2) != 2) {
        perror("Error escribiendo en IMU");
        return false;
    }

    return true;
}

bool IMU::readAccel(float &ax, float &ay, float &az) {
    char reg = 0x3B;
    char data[6];

    if (write(file, &reg, 1) != 1) {
        perror("Error posicionando IMU");
        return false;
    }

    if (read(file, data, 6) != 6) {
        perror("Error leyendo IMU");
        return false;
    }

    int16_t raw_ax = (static_cast<int16_t>(data[0]) << 8) | static_cast<uint8_t>(data[1]);
    int16_t raw_ay = (static_cast<int16_t>(data[2]) << 8) | static_cast<uint8_t>(data[3]);
    int16_t raw_az = (static_cast<int16_t>(data[4]) << 8) | static_cast<uint8_t>(data[5]);

    ax = raw_ax / 16384.0f;
    ay = raw_ay / 16384.0f;
    az = raw_az / 16384.0f;

    return true;
}
