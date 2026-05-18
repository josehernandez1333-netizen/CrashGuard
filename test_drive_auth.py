from __future__ import print_function
import os
from googleapiclient.discovery import build
from google.oauth2.credentials import Credentials

SCOPES = ["https://www.googleapis.com/auth/drive.file"]

BASE_DIR = "/home/pi/CrashGuard"
TOKEN_PATH = os.path.join(BASE_DIR, "token.json")

def main():
    if not os.path.exists(TOKEN_PATH):
        raise FileNotFoundError(f"No existe {TOKEN_PATH}")

    creds = Credentials.from_authorized_user_file(TOKEN_PATH, SCOPES)
    service = build("drive", "v3", credentials=creds)

    results = service.files().list(pageSize=5, fields="files(id, name)").execute()
    items = results.get("files", [])

    if not items:
        print("No se encontraron archivos.")
    else:
        print("Archivos encontrados:")
        for item in items:
            print(f"{item['name']} ({item['id']})")

if __name__ == "__main__":
    main()
