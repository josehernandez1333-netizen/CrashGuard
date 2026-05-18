#include "modem.h"

#include <iostream>
#include <sstream>
#include <regex>
#include <vector>
#include <chrono>
#include <thread>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

Modem::Modem(const std::string& port, int baud)
    : serialPort(port), baudrate(baud), fd(-1) {}

Modem::~Modem() {
    closePort();
}

bool Modem::openPort() {
    closePort();

    fd = open(serialPort.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        std::cerr << "❌ No se pudo abrir puerto serial " << serialPort
                  << ": " << strerror(errno) << std::endl;
        return false;
    }

    return configurePort();
}

void Modem::closePort() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

bool Modem::configurePort() {
    if (fd < 0) return false;

    struct termios tty;
    memset(&tty, 0, sizeof tty);

    if (tcgetattr(fd, &tty) != 0) {
        std::cerr << "❌ tcgetattr fallo: " << strerror(errno) << std::endl;
        return false;
    }

    speed_t speed = B115200;
    if (baudrate == 9600) speed = B9600;
    else if (baudrate == 19200) speed = B19200;
    else if (baudrate == 38400) speed = B38400;
    else if (baudrate == 57600) speed = B57600;
    else if (baudrate == 115200) speed = B115200;

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::cerr << "❌ tcsetattr fallo: " << strerror(errno) << std::endl;
        return false;
    }

    tcflush(fd, TCIOFLUSH);
    return true;
}

void Modem::flushSerial() {
    if (fd >= 0) {
        tcflush(fd, TCIOFLUSH);
    }
}

std::string Modem::readChunk(int timeoutMs) {
    std::string out;
    if (fd < 0) return out;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        int bytesAvailable = 0;
        ioctl(fd, FIONREAD, &bytesAvailable);

        if (bytesAvailable > 0) {
            std::vector<char> buffer(bytesAvailable + 1, 0);
            int n = read(fd, buffer.data(), bytesAvailable);
            if (n > 0) {
                out.append(buffer.data(), n);
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return out;
}

bool Modem::waitFor(const std::string& expected,
                    int timeoutMs,
                    std::string& fullResponse) {
    fullResponse.clear();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        std::string chunk = readChunk(300);
        if (!chunk.empty()) {
            fullResponse += chunk;

            if (fullResponse.find(expected) != std::string::npos) {
                return true;
            }

            if (fullResponse.find("ERROR") != std::string::npos) {
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return fullResponse.find(expected) != std::string::npos;
}

bool Modem::sendAT(const std::string& cmd,
                   const std::string& expected,
                   int timeoutMs,
                   std::string* fullResponse) {
    if (fd < 0 && !openPort()) {
        return false;
    }

    flushSerial();

    std::string finalCmd = cmd + "\r";
    int written = write(fd, finalCmd.c_str(), finalCmd.size());
    if (written < 0) {
        std::cerr << "❌ Error escribiendo comando AT " << cmd
                  << ": " << strerror(errno) << std::endl;
        return false;
    }

    tcdrain(fd);

    std::string response;
    bool ok = waitFor(expected, timeoutMs, response);

    if (fullResponse) {
        *fullResponse = response;
    }

    std::cout << cmd << " ->" << std::endl;
    if (!response.empty()) {
        std::cout << response << std::endl;
    }

    return ok;
}

bool Modem::init() {
    if (!openPort()) {
        return false;
    }

    std::string response;

    if (!sendAT("AT", "OK", 2000, &response)) return false;
    if (!sendAT("ATE0", "OK", 2000, &response)) return false;
    if (!sendAT("AT+CPIN?", "OK", 3000, &response)) return false;
    if (response.find("READY") == std::string::npos) return false;
    if (!sendAT("AT+CREG?", "OK", 3000, &response)) return false;
    if (!sendAT("AT+CSQ", "OK", 3000, &response)) return false;
    if (!sendAT("AT+CMEE=2", "OK", 2000, &response)) return false;
    if (!sendAT("AT+CMGF=1", "OK", 2000, &response)) return false;
    if (!sendAT("AT+CSCS=\"GSM\"", "OK", 2000, &response)) return false;

    return true;
}

bool Modem::sendSMS(const std::string& number, const std::string& message) {
    if (fd < 0 && !openPort()) {
        return false;
    }

    std::string response;

    if (!sendAT("AT+CREG?", "OK", 3000, &response)) {
        std::cerr << "❌ No se pudo validar registro en red" << std::endl;
        return false;
    }

    if (response.find("+CREG: 0,1") == std::string::npos &&
        response.find("+CREG: 0,5") == std::string::npos) {
        std::cerr << "❌ Modem no registrado en red" << std::endl;
        return false;
    }

    if (!sendAT("AT+CSQ", "OK", 3000, &response)) {
        std::cerr << "❌ No se pudo validar señal" << std::endl;
        return false;
    }

    if (!sendAT("AT+CMGF=1", "OK", 2000, &response)) {
        std::cerr << "❌ No se pudo poner modo texto SMS" << std::endl;
        return false;
    }

    if (!sendAT("AT+CSCS=\"GSM\"", "OK", 2000, &response)) {
        std::cerr << "❌ No se pudo configurar tabla de caracteres" << std::endl;
        return false;
    }

    flushSerial();

    std::string cmd = "AT+CMGS=\"" + number + "\"\r";
    std::cout << "CMD SMS -> " << cmd;

    int written = write(fd, cmd.c_str(), cmd.size());
    if (written < 0) {
        std::cerr << "❌ Error enviando AT+CMGS: " << strerror(errno) << std::endl;
        return false;
    }
    tcdrain(fd);

    std::string promptResponse;
    bool gotPrompt = waitFor(">", 5000, promptResponse);

    std::cout << "PROMPT SMS -> " << std::endl;
    if (!promptResponse.empty()) {
        std::cout << promptResponse << std::endl;
    }

    if (!gotPrompt) {
        std::cerr << "❌ El modem no devolvio el prompt >" << std::endl;
        return false;
    }

    std::string payload = message;
    payload.push_back(26);

    written = write(fd, payload.c_str(), payload.size());
    if (written < 0) {
        std::cerr << "❌ Error escribiendo cuerpo del SMS: " << strerror(errno) << std::endl;
        return false;
    }
    tcdrain(fd);

    std::string finalResponse;
    bool ok = waitFor("OK", 25000, finalResponse);

    std::cout << "RESPUESTA GSM:" << std::endl;
    if (!finalResponse.empty()) {
        std::cout << finalResponse << std::endl;
    }

    if (!ok) {
        std::cerr << "❌ No llego OK final del SMS" << std::endl;
        return false;
    }

    if (finalResponse.find("+CMGS:") == std::string::npos) {
        std::cerr << "❌ El modem no confirmo +CMGS" << std::endl;
        return false;
    }

    return true;
}

double Modem::convertNMEAToDecimal(const std::string& value, bool isLongitude) {
    if (value.empty()) return 0.0;

    int degDigits = isLongitude ? 3 : 2;
    if ((int)value.size() <= degDigits) return 0.0;

    double degrees = std::stod(value.substr(0, degDigits));
    double minutes = std::stod(value.substr(degDigits));

    return degrees + (minutes / 60.0);
}

bool Modem::getCoordinates(std::string& lat, std::string& lon) {
    lat = "0";
    lon = "0";

    if (fd < 0 && !openPort()) {
        return false;
    }

    std::string response;

    sendAT("AT+CGPS=1,1", "OK", 2000, &response);

    if (!sendAT("AT+CGPSINFO", "OK", 4000, &response)) {
        return false;
    }

    std::regex re("\\+CGPSINFO:\\s*([0-9\\.]*),([NS])?,([0-9\\.]*),([EW])?");
    std::smatch m;

    if (!std::regex_search(response, m, re)) {
        return false;
    }

    std::string rawLat = m[1];
    std::string ns = m[2];
    std::string rawLon = m[3];
    std::string ew = m[4];

    if (rawLat.empty() || rawLon.empty() || ns.empty() || ew.empty()) {
        return false;
    }

    double dLat = convertNMEAToDecimal(rawLat, false);
    double dLon = convertNMEAToDecimal(rawLon, true);

    if (ns == "S") dLat = -dLat;
    if (ew == "W") dLon = -dLon;

    std::ostringstream latStream, lonStream;
    latStream.setf(std::ios::fixed);
    lonStream.setf(std::ios::fixed);
    latStream.precision(8);
    lonStream.precision(8);

    latStream << dLat;
    lonStream << dLon;

    lat = latStream.str();
    lon = lonStream.str();

    return true;
}
