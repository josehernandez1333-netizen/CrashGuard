import os
import sys
import json
from googleapiclient.discovery import build
from googleapiclient.http import MediaFileUpload
from google.oauth2.credentials import Credentials

SCOPES = ["https://www.googleapis.com/auth/drive.file"]
BASE_DIR = "/home/pi/CrashGuard"
TOKEN_PATH = os.path.join(BASE_DIR, "token.json")

def build_service():
    if not os.path.exists(TOKEN_PATH):
        raise FileNotFoundError(f"No existe {TOKEN_PATH}")
    creds = Credentials.from_authorized_user_file(TOKEN_PATH, SCOPES)
    return build("drive", "v3", credentials=creds)

def upload_file(file_path: str, event_id: str):
    if not os.path.exists(file_path):
        raise FileNotFoundError(f"No existe el archivo: {file_path}")

    service = build_service()

    filename = os.path.basename(file_path)
    drive_name = f"CrashGuard_evento_{event_id}_{filename}"

    metadata = {
        "name": drive_name
    }

    media = MediaFileUpload(file_path, mimetype="video/mp4", resumable=True)

    created = service.files().create(
        body=metadata,
        media_body=media,
        fields="id,name"
    ).execute()

    file_id = created["id"]

    service.permissions().create(
        fileId=file_id,
        body={
            "type": "anyone",
            "role": "reader"
        }
    ).execute()

    refreshed = service.files().get(
        fileId=file_id,
        fields="id,name,webViewLink,webContentLink"
    ).execute()

    public_link = refreshed.get("webViewLink")
    if not public_link:
        public_link = f"https://drive.google.com/file/d/{file_id}/view?usp=sharing"

    return {
        "file_id": file_id,
        "name": refreshed.get("name"),
        "video_url": public_link
    }

def main():
    if len(sys.argv) < 3:
        print("Uso: python3 upload_to_drive.py <ruta_mp4> <event_id>")
        sys.exit(1)

    file_path = sys.argv[1]
    event_id = sys.argv[2]

    result = upload_file(file_path, event_id)
    print(json.dumps(result, ensure_ascii=False))

if __name__ == "__main__":
    main()
