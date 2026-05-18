#include "cloud_client.h"
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <regex>

CloudClient::CloudClient(const std::string& url) : apiUrl(url) {}

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t totalSize = size * nmemb;
    std::string *response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

bool CloudClient::sendAlert(long long eventId,
                            const std::string& lat,
                            const std::string& lon,
                            float ax,
                            float ay,
                            float az,
                            float magnitud) {
    CURL *curl = curl_easy_init();

    if (!curl) {
        std::cerr << "❌ No se pudo inicializar CURL" << std::endl;
        return false;
    }

    std::ostringstream json;
    json << "{"
         << "\"event_id\":" << eventId << ","
         << "\"lat\":\"" << lat << "\","
         << "\"lon\":\"" << lon << "\","
         << "\"ax\":" << ax << ","
         << "\"ay\":" << ay << ","
         << "\"az\":" << az << ","
         << "\"magnitud\":" << magnitud
         << "}";

    std::string jsonStr = json.str();

    std::cout << "📤 JSON enviado: " << jsonStr << std::endl;

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonStr.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "❌ CURL error enviando alerta: " << curl_easy_strerror(res) << std::endl;
        return false;
    }

    std::cout << response << std::endl;

    if (httpCode >= 200 && httpCode < 300) {
        std::cout << "✅ Alerta enviada correctamente al servidor" << std::endl;
        return true;
    } else {
        std::cerr << "❌ Error HTTP enviando alerta: " << httpCode << std::endl;
        return false;
    }
}

bool CloudClient::getConfig(std::string &numero, std::string &mensaje) {
    CURL *curl = curl_easy_init();

    if (!curl) {
        std::cerr << "❌ Error inicializando CURL para config" << std::endl;
        return false;
    }

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, "http://127.0.0.1:5000/api/config");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "❌ Error obteniendo config: " << curl_easy_strerror(res) << std::endl;
        return false;
    }

    if (httpCode < 200 || httpCode >= 300) {
        std::cerr << "❌ HTTP error obteniendo config: " << httpCode << std::endl;
        return false;
    }

    try {
        std::regex numeroRegex("\"numero_destino\"\\s*:\\s*\"([^\"]*)\"");
        std::regex mensajeRegex("\"mensaje_alerta\"\\s*:\\s*\"([^\"]*)\"");

        std::smatch matchNumero;
        std::smatch matchMensaje;

        if (std::regex_search(response, matchNumero, numeroRegex)) {
            numero = matchNumero[1];
        } else {
            std::cerr << "❌ No se encontró numero_destino en config" << std::endl;
            return false;
        }

        if (std::regex_search(response, matchMensaje, mensajeRegex)) {
            mensaje = matchMensaje[1];
        } else {
            std::cerr << "❌ No se encontró mensaje_alerta en config" << std::endl;
            return false;
        }

        std::cout << "📥 Config recibida desde Flask: " << response << std::endl;
        std::cout << "✅ Número cargado: " << numero << std::endl;
        std::cout << "✅ Mensaje cargado: " << mensaje << std::endl;

        return true;

    } catch (const std::exception& e) {
        std::cerr << "❌ Error parseando configuración: " << e.what() << std::endl;
        return false;
    }
}
