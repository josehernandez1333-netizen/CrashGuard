#ifndef IMU_H
#define IMU_H

class IMU {
public:
    IMU(int bus, int address);
    ~IMU();

    bool init();
    bool readAccel(float &ax, float &ay, float &az);

private:
    int file;
    int addr;
};

#endif
