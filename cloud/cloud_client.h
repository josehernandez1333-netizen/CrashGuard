#ifndef CLOUD_CLIENT_H
#define CLOUD_CLIENT_H

#include <string>

class CloudClient {
private:
    std::string apiUrl;

public:
    explicit CloudClient(const std::string& url);

    bool sendAlert(long long eventId,
                   const std::string& lat,
                   const std::string& lon,
                   float ax,
                   float ay,
                   float az,
                   float magnitud);

    bool getConfig(std::string &numero,
                   std::string &mensaje);
};

#endif
