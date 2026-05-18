#ifndef MODEM_H
#define MODEM_H

#include <string>

class Modem {
public:
    explicit Modem(const std::string& port, int baudrate = 115200);
    ~Modem();

    bool init();
    bool sendSMS(const std::string& number, const std::string& message);
    bool getCoordinates(std::string& lat, std::string& lon);

private:
    std::string serialPort;
    int baudrate;
    int fd;

    bool openPort();
    void closePort();
    bool configurePort();
    void flushSerial();

    bool sendAT(const std::string& cmd,
                const std::string& expected,
                int timeoutMs,
                std::string* fullResponse = nullptr);

    bool waitFor(const std::string& expected,
                 int timeoutMs,
                 std::string& fullResponse);

    std::string readChunk(int timeoutMs);

    double convertNMEAToDecimal(const std::string& value, bool isLongitude);
};

#endif
