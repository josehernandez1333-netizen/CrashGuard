#include <iostream>
#include <unistd.h>
#include <cmath>
#include <ctime>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <regex>
#include <cstdio>
#include <chrono>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <curl/curl.h>

#include "imu/imu.h"
#include "modem/modem.h"
#include "cloud/cloud_client.h"

static const char* VIDEO_LINK_READY_FILE = "/home/pi/CrashGuard/runtime/video_link_ready.json";
static const char* IMPACT_TRIGGER_FILE   = "/home/pi/CrashGuard/runtime/impact_trigger.json";

static float calcularMagnitudG(float ax, float ay, float az) {
    return std::sqrt(ax * ax + ay * ay + az * az);
}

static long long generarEventId() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    return static_cast<long long>(ms);
}

static void asegurarDirectorios() {
    mkdir("/home/pi/CrashGuard/runtime", 0755);
    mkdir("/home/pi/CrashGuard/evidence", 0755);
    mkdir("/home/pi/CrashGuard/camera_buffer", 0755);
    mkdir("/home/pi/CrashGuard/uploads", 0755);
}

static void borrarArchivo(const char* ruta) {
    std::remove(ruta);
}

static void limpiarRuntimeInicial() {
    // IMPORTANTE:
    // NO borrar VIDEO_LINK_READY_FILE aquí.
    // Si el daemon deja un segundo SMS pendiente, el main debe poder leerlo.
    borrarArchivo(IMPACT_TRIGGER_FILE);
    std::cout << "🧹 Runtime inicial limpiado (sin borrar video_link_ready.json)" << std::endl;
}

static bool extraerCampoString(const std::string& json, const std::string& campo, std::string& valor) {
    try {
        std::regex re("\"" + campo + "\"\\s*:\\s*\"([^\"]*)\"");
        std::smatch m;
        if (std::regex_search(json, m, re)) {
            valor = m[1];
            return true;
        }
    } catch (...) {}
    return false;
}

static bool extraerCampoLongLong(const std::string& json, const std::string& campo, long long& valor) {
    try {
        std::regex re("\"" + campo + "\"\\s*:\\s*([0-9]+)");
        std::smatch m;
        if (std::regex_search(json, m, re)) {
            valor = std::stoll(m[1]);
            return true;
        }
    } catch (...) {}
    return false;
}

static bool escribirTriggerImpacto(long long eventId,
                                   const std::string& lat,
                                   const std::string& lon,
                                   float ax,
                                   float ay,
                                   float az,
                                   float magnitud,
                                   bool gpsOk) {
    asegurarDirectorios();

    std::time_t ahora = std::time(nullptr);

    std::ofstream file(IMPACT_TRIGGER_FILE, std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "❌ No se pudo crear impact_trigger.json" << std::endl;
        return false;
    }

    file << "{\n";
    file << "  \"event_id\": " << eventId << ",\n";
    file << "  \"timestamp\": " << ahora << ",\n";
    file << "  \"gps_ok\": " << (gpsOk ? "true" : "false") << ",\n";
    file << "  \"lat\": \"" << lat << "\",\n";
    file << "  \"lon\": \"" << lon << "\",\n";
    file << "  \"ax\": " << ax << ",\n";
    file << "  \"ay\": " << ay << ",\n";
    file << "  \"az\": " << az << ",\n";
    file << "  \"magnitud\": " << magnitud << "\n";
    file << "}\n";

    file.close();
    std::cout << "🎥 Trigger de cámara escrito correctamente." << std::endl;
    return true;
}

static int ejecutarAlarmaImpacto() {
    int ret = system("/usr/bin/python3 /home/pi/CrashGuard/alarm_manager.py >> /home/pi/CrashGuard/alarm_manager.log 2>&1");

    if (ret == -1) {
        std::cerr << "⚠️ No se pudo ejecutar alarm_manager.py" << std::endl;
        return -1;
    }

    if (WIFEXITED(ret)) {
        int code = WEXITSTATUS(ret);
        std::cout << "🔔 alarm_manager.py terminó con código: " << code << std::endl;
        return code;
    }

    std::cerr << "⚠️ alarm_manager.py terminó de forma no esperada" << std::endl;
    return -1;
}

static bool enviarSMSConReintentos(Modem& modem,
                                   const std::string& numero,
                                   const std::string& mensaje,
                                   const std::string& etiqueta,
                                   int intentos = 3,
                                   int esperaSeg = 8) {
    for (int i = 1; i <= intentos; ++i) {
        std::cout << "📲 " << etiqueta << " intento " << i << " de " << intentos << "..." << std::endl;

        if (i > 1) {
            std::cout << "🔄 Reintentando inicialización de módem..." << std::endl;
            modem.init();
            sleep(2);
        }

        bool ok = modem.sendSMS(numero, mensaje);
        if (ok) {
            std::cout << "✅ " << etiqueta << " enviado correctamente" << std::endl;
            return true;
        }

        std::cerr << "❌ " << etiqueta << " falló en intento " << i << std::endl;

        if (i < intentos) {
            sleep(esperaSeg);
        }
    }

    return false;
}

static void revisarYEnviarLinkVideo(Modem& modem, CloudClient& cloud) {
    std::ifstream file(VIDEO_LINK_READY_FILE);
    if (!file.is_open()) {
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    file.close();

    std::string link;
    long long eventId = 0;

    if (!extraerCampoString(json, "video_url", link)) {
        if (!extraerCampoString(json, "short_url", link)) {
            std::cerr << "⚠️ video_link_ready.json sin URL válida" << std::endl;
            return;
        }
    }

    extraerCampoLongLong(json, "event_id", eventId);

    std::string numero = "";
    std::string mensajeBase = "Video CrashGuard";
    cloud.getConfig(numero, mensajeBase);

    std::string sms = "Descarga para ver:\n" + link;

    std::cout << "📨 Link enviado por segundo SMS: " << link << std::endl;

    bool ok = enviarSMSConReintentos(modem, numero, sms, "SEGUNDO SMS", 3, 10);

    if (ok) {
        borrarArchivo(VIDEO_LINK_READY_FILE);
    } else {
        std::cerr << "⚠️ Se conserva video_link_ready.json para reintentar luego." << std::endl;
    }
}

int main() {
    std::cout << "==============================" << std::endl;
    std::cout << "🚀 Iniciando CrashGuard..." << std::endl;
    std::cout << "==============================" << std::endl;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    asegurarDirectorios();
    limpiarRuntimeInicial();

    Modem modem("/dev/ttyAMA0");
    IMU imu(1, 0x68);
    CloudClient cloud("http://127.0.0.1:5000/api/alert");

    std::cout << "🔧 Inicializando IMU..." << std::endl;
    if (!imu.init()) {
        std::cerr << "❌ Error inicializando IMU" << std::endl;
        curl_global_cleanup();
        return -1;
    }
    std::cout << "✅ IMU inicializada correctamente" << std::endl;

    std::cout << "🔧 Inicializando módem..." << std::endl;
    if (!modem.init()) {
        std::cerr << "❌ Error inicializando módem" << std::endl;
        curl_global_cleanup();
        return -1;
    }
    std::cout << "✅ Módem inicializado correctamente" << std::endl;

    const float DELTA_UMBRAL = 0.18f;
    const float SALTO_UMBRAL = 0.12f;
    const int LECTURAS_CONSECUTIVAS_REQUERIDAS = 2;
    const int COOLDOWN_SEGUNDOS = 60;

    time_t ultimaAlerta = 0;
    float magnitudAnterior = 1.0f;
    bool primeraLectura = true;
    int contadorImpacto = 0;

    std::cout << "✅ Sistema listo para detectar impactos reales" << std::endl;
    std::cout << "📌 Delta umbral: " << DELTA_UMBRAL << std::endl;
    std::cout << "📌 Salto umbral: " << SALTO_UMBRAL << std::endl;
    std::cout << "📌 Lecturas consecutivas requeridas: " << LECTURAS_CONSECUTIVAS_REQUERIDAS << std::endl;
    std::cout << "⏱️ Cooldown entre alertas: " << COOLDOWN_SEGUNDOS << " segundos" << std::endl;
    std::cout << "------------------------------" << std::endl;

    while (true) {
        revisarYEnviarLinkVideo(modem, cloud);

        float ax = 0.0f;
        float ay = 0.0f;
        float az = 0.0f;

        if (!imu.readAccel(ax, ay, az)) {
            std::cerr << "❌ Error leyendo IMU" << std::endl;
            sleep(1);
            continue;
        }

        float magnitud = calcularMagnitudG(ax, ay, az);
        float delta1G = std::fabs(magnitud - 1.0f);
        float salto = primeraLectura ? 0.0f : std::fabs(magnitud - magnitudAnterior);

        std::cout << "IMU -> AX: " << ax
                  << " AY: " << ay
                  << " AZ: " << az
                  << " | G: " << magnitud
                  << " | DELTA: " << delta1G
                  << " | SALTO: " << salto
                  << std::endl;

        bool condicionImpacto = (delta1G >= DELTA_UMBRAL) && (salto >= SALTO_UMBRAL);

        if (condicionImpacto) {
            contadorImpacto++;
        } else {
            contadorImpacto = 0;
        }

        time_t ahora = time(nullptr);
        bool enCooldown = difftime(ahora, ultimaAlerta) < COOLDOWN_SEGUNDOS;

        if (contadorImpacto >= LECTURAS_CONSECUTIVAS_REQUERIDAS && !enCooldown) {
            long long eventId = generarEventId();

            std::cout << std::endl;
            std::cout << "🚨 IMPACTO REAL DETECTADO 🚨" << std::endl;
            std::cout << "🆔 Event ID: " << eventId << std::endl;
            std::cout << "📊 Magnitud detectada: " << magnitud << " G" << std::endl;
            std::cout << "📊 Delta detectado: " << delta1G << std::endl;
            std::cout << "📊 Salto detectado: " << salto << std::endl;

            contadorImpacto = 0;

            int resultadoAlarma = ejecutarAlarmaImpacto();

            if (resultadoAlarma == 2) {
                std::cout << "🛑 Falsa alarma confirmada. Se cancela SMS, video y proceso." << std::endl;
                ultimaAlerta = ahora;
                std::cout << "------------------------------" << std::endl;
                magnitudAnterior = magnitud;
                primeraLectura = false;
                sleep(2);
                continue;
            }

            std::string numero = "";
            std::string mensajeBase = "ALERTA CrashGuard: posible accidente detectado";
            cloud.getConfig(numero, mensajeBase);

            std::string lat = "0";
            std::string lon = "0";
            bool gpsOk = modem.getCoordinates(lat, lon);

            if (gpsOk) {
                std::cout << "✅ GPS obtenido correctamente" << std::endl;
                std::cout << "📍 Latitud: " << lat << std::endl;
                std::cout << "📍 Longitud: " << lon << std::endl;
            } else {
                std::cerr << "⚠️ No se pudo obtener GPS" << std::endl;
                std::cerr << "⚠️ Se registrará el evento sin GPS válido" << std::endl;
            }

            bool triggerOk = escribirTriggerImpacto(eventId, lat, lon, ax, ay, az, magnitud, gpsOk);

            if (triggerOk) {
                std::cout << "✅ Trigger de cámara generado para evento" << std::endl;
            } else {
                std::cerr << "⚠️ No se pudo generar trigger de cámara" << std::endl;
            }

            std::string mensajeSMS;
            if (gpsOk) {
                mensajeSMS = mensajeBase +
                             "\nUbicacion:\nhttps://maps.google.com/?q=" +
                             lat + "," + lon +
                             "\nVideo en proceso...";
            } else {
                mensajeSMS = mensajeBase +
                             "\nUbicacion no disponible por el momento." +
                             "\nVideo en proceso...";
            }

            enviarSMSConReintentos(modem, numero, mensajeSMS, "PRIMER SMS", 3, 8);

            std::cout << "☁️ Enviando alerta base al servidor Flask..." << std::endl;
            bool cloudOk = cloud.sendAlert(eventId, lat, lon, ax, ay, az, magnitud);

            if (cloudOk) {
                std::cout << "✅ Alerta enviada correctamente al servidor" << std::endl;
            } else {
                std::cerr << "❌ Error enviando alerta al servidor" << std::endl;
            }

            ultimaAlerta = ahora;
            std::cout << "📹 El segundo SMS se enviará cuando el video esté listo." << std::endl;
            std::cout << "------------------------------" << std::endl;
        } else if (contadorImpacto >= LECTURAS_CONSECUTIVAS_REQUERIDAS && enCooldown) {
            contadorImpacto = 0;
            std::cout << "⚠️ Impacto detectado, pero el sistema está en cooldown" << std::endl;
        }

        magnitudAnterior = magnitud;
        primeraLectura = false;

        std::cout << "----------------------" << std::endl;
        usleep(150000);
    }

    curl_global_cleanup();
    return 0;
}
