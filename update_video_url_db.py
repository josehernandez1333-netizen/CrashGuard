import sys
import mysql.connector

db_config = {
    "host": "localhost",
    "user": "crashuser",
    "password": "1234",
    "database": "crashguard"
}

def main():
    if len(sys.argv) < 3:
        print("Uso: python3 update_video_url_db.py <event_id> <video_url>")
        sys.exit(1)

    event_id = int(sys.argv[1])
    video_url = sys.argv[2]

    conn = mysql.connector.connect(**db_config)
    cursor = conn.cursor()

    cursor.execute("""
        UPDATE alertas
        SET video_url = %s
        WHERE event_id = %s
    """, (video_url, event_id))

    conn.commit()
    print(f"Filas actualizadas: {cursor.rowcount}")

    cursor.close()
    conn.close()

if __name__ == "__main__":
    main()
