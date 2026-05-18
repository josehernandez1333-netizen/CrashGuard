from flask import Flask, request, jsonify, render_template
from flask_cors import CORS
import mysql.connector
from datetime import datetime
import os

# =========================
# INICIALIZAR FLASK
# =========================
app = Flask(__name__)
CORS(app)

# =========================
# CONFIGURACIÓN BASE DE DATOS
# =========================
db_config = {
    "host": "localhost",
    "user": "crashuser",
    "password": "1234",
    "database": "crashguard"
}

# =========================
# CONFIGURACIÓN GENERAL
# =========================
configuracion = {
    "numero_destino": "+523310944966",
    "mensaje_alerta": "ALERTA CrashGuard: posible accidente detectado"
}

# =========================
# CONEXIÓN MYSQL
# =========================
def conectar_db():
    return mysql.connector.connect(**db_config)

# =========================
# INTERFAZ PRINCIPAL
# =========================
@app.route("/")
def home():
    return render_template("index.html")

# =========================
# TEST SERVIDOR
# =========================
@app.route("/test", methods=["GET"])
def test():
    return "Servidor funcionando correctamente 🏍️"

# =========================
# RECIBIR ALERTA
# =========================
@app.route("/api/alert", methods=["POST"])
def recibir_alerta():

    try:

        data = request.get_json(silent=True)

        if not data:
            return jsonify({
                "status": "error",
                "detalle": "No se recibió JSON válido"
            }), 400

        event_id = int(data.get("event_id", 0))

        lat = str(data.get("lat", "0"))
        lon = str(data.get("lon", "0"))

        ax = float(data.get("ax", 0))
        ay = float(data.get("ay", 0))
        az = float(data.get("az", 0))

        magnitud = float(data.get("magnitud", 0))

        conn = conectar_db()
        cursor = conn.cursor()

        # =========================
        # VERIFICAR SI EXISTE EVENTO
        # =========================
        cursor.execute("""
            SELECT id
            FROM alertas
            WHERE event_id = %s
            ORDER BY id DESC
            LIMIT 1
        """, (event_id,))

        row = cursor.fetchone()

        # =========================
        # UPDATE
        # =========================
        if row:

            cursor.execute("""
                UPDATE alertas
                SET lat = %s,
                    lon = %s,
                    ax = %s,
                    ay = %s,
                    az = %s,
                    magnitud = %s,
                    fecha = %s
                WHERE event_id = %s
            """, (
                lat,
                lon,
                ax,
                ay,
                az,
                magnitud,
                datetime.now(),
                event_id
            ))

        # =========================
        # INSERT
        # =========================
        else:

            cursor.execute("""
                INSERT INTO alertas (
                    event_id,
                    lat,
                    lon,
                    ax,
                    ay,
                    az,
                    magnitud,
                    fecha,
                    video_url
                )
                VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s)
            """, (
                event_id,
                lat,
                lon,
                ax,
                ay,
                az,
                magnitud,
                datetime.now(),
                None
            ))

        conn.commit()

        cursor.close()
        conn.close()

        return jsonify({
            "status": "ok"
        })

    except Exception as e:

        return jsonify({
            "status": "error",
            "detalle": str(e)
        }), 500

# =========================
# OBTENER ALERTAS
# =========================
@app.route("/api/datos", methods=["GET"])
def obtener_datos():

    try:

        conn = conectar_db()

        cursor = conn.cursor(dictionary=True)

        cursor.execute("""
            SELECT
                id,
                event_id,
                lat,
                lon,
                ax,
                ay,
                az,
                magnitud,
                fecha,
                video_url
            FROM alertas
            ORDER BY id DESC
            LIMIT 50
        """)

        datos = cursor.fetchall()

        cursor.close()
        conn.close()

        return jsonify(datos)

    except Exception as e:

        return jsonify({
            "status": "error",
            "detalle": str(e)
        }), 500

# =========================
# OBTENER CONFIGURACIÓN
# =========================
@app.route("/api/config", methods=["GET"])
def get_config():

    return jsonify({
        "numero_destino": configuracion["numero_destino"],
        "mensaje_alerta": configuracion["mensaje_alerta"]
    })

# =========================
# GUARDAR CONFIGURACIÓN
# =========================
@app.route("/api/config", methods=["POST"])
def set_config():

    try:

        data = request.get_json(silent=True)

        if not data:
            return jsonify({
                "status": "error",
                "detalle": "No se recibió JSON válido"
            }), 400

        nuevo_numero = data.get("numero") or data.get("numero_destino")
        nuevo_mensaje = data.get("mensaje") or data.get("mensaje_alerta")

        if nuevo_numero:
            configuracion["numero_destino"] = str(nuevo_numero).strip()

        if nuevo_mensaje:
            configuracion["mensaje_alerta"] = str(nuevo_mensaje).strip()

        return jsonify({
            "status": "ok",
            "configuracion": {
                "numero_destino": configuracion["numero_destino"],
                "mensaje_alerta": configuracion["mensaje_alerta"]
            }
        })

    except Exception as e:

        return jsonify({
            "status": "error",
            "detalle": str(e)
        }), 500

# =========================
# INICIAR SERVIDOR
# =========================
if __name__ == "__main__":

    port = int(os.environ.get("PORT", 5000))

    app.run(
        host="0.0.0.0",
        port=port,
        debug=False
    )
