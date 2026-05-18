#!/usr/bin/env python3
import json
import shutil
import signal
import subprocess
import time
from pathlib import Path

BASE_DIR = Path("/home/pi/CrashGuard")
BUFFER_DIR = BASE_DIR / "camera_buffer"
EVIDENCE_DIR = BASE_DIR / "evidence"
RUNTIME_DIR = BASE_DIR / "runtime"

TRIGGER_FILE = RUNTIME_DIR / "impact_trigger.json"
VIDEO_LINK_READY = RUNTIME_DIR / "video_link_ready.json"

SEGMENT_SECONDS = 10
PRE_SECONDS = 30
POST_SECONDS = 30
PRE_SEGMENTS = PRE_SECONDS // SEGMENT_SECONDS
POST_SEGMENTS = POST_SECONDS // SEGMENT_SECONDS

WIDTH = 1280
HEIGHT = 720
FRAMERATE = 15
BITRATE = 2000000

running = True
recorder_proc = None


def ensure_dirs():
    BUFFER_DIR.mkdir(parents=True, exist_ok=True)
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    RUNTIME_DIR.mkdir(parents=True, exist_ok=True)


def handle_signal(sig, frame):
    global running
    running = False
    if recorder_proc and recorder_proc.poll() is None:
        recorder_proc.terminate()


signal.signal(signal.SIGINT, handle_signal)
signal.signal(signal.SIGTERM, handle_signal)


def start_recorder():
    cmd = [
        "rpicam-vid",
        "-t", "0",
        "-n",
        "--width", str(WIDTH),
        "--height", str(HEIGHT),
        "--framerate", str(FRAMERATE),
        "--codec", "h264",
        "--bitrate", str(BITRATE),
        "--segment", str(SEGMENT_SECONDS * 1000),
        "--wrap", "18",
        "--inline",
        "--flush",
        "-o", str(BUFFER_DIR / "chunk_%04d.h264"),
    ]
    print("🎥 Iniciando grabación circular...")
    return subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def list_segments():
    return sorted(BUFFER_DIR.glob("chunk_*.h264"), key=lambda p: p.stat().st_mtime)


def copy_segments_for_event(trigger_data):
    event_id = trigger_data["event_id"]
    event_dir = EVIDENCE_DIR / f"event_{event_id}"
    event_dir.mkdir(parents=True, exist_ok=True)

    with open(event_dir / "metadata.json", "w", encoding="utf-8") as f:
        json.dump(trigger_data, f, indent=2, ensure_ascii=False)

    trigger_time = time.time()
    segments = list_segments()

    pre = segments[-PRE_SEGMENTS:] if len(segments) >= PRE_SEGMENTS else segments[:]

    copied = []
    for idx, seg in enumerate(pre, start=1):
        dst = event_dir / f"{idx:02d}_pre_{seg.name}"
        shutil.copy2(seg, dst)
        copied.append(dst)

    print(f"📦 Copiados {len(pre)} segmentos previos")

    post_copied = 0
    known_names = {p.name for p in pre}
    idx = len(copied) + 1
    deadline = time.time() + POST_SECONDS + 20

    while time.time() < deadline and post_copied < POST_SEGMENTS:
        time.sleep(1)
        for seg in list_segments():
            if seg.name in known_names:
                continue
            if seg.stat().st_mtime >= trigger_time:
                dst = event_dir / f"{idx:02d}_post_{seg.name}"
                shutil.copy2(seg, dst)
                copied.append(dst)
                known_names.add(seg.name)
                post_copied += 1
                idx += 1
                print(f"📦 Copiado segmento post {seg.name}")
                if post_copied >= POST_SEGMENTS:
                    break

    return event_dir


def merge_to_mp4(event_dir: Path):
    h264_files = sorted(event_dir.glob("*.h264"))
    if not h264_files:
        raise RuntimeError("No hay segmentos para unir")

    merged_h264 = event_dir / "evento_completo.h264"
    final_mp4 = event_dir / "evento_completo.mp4"

    with open(merged_h264, "wb") as merged:
        for f in h264_files:
            with open(f, "rb") as part:
                shutil.copyfileobj(part, merged)

    cmd = [
        "ffmpeg",
        "-y",
        "-framerate", str(FRAMERATE),
        "-i", str(merged_h264),
        "-c", "copy",
        str(final_mp4)
    ]

    print("🎬 Generando MP4 final...")
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        print(result.stderr)
        raise RuntimeError("ffmpeg falló al generar MP4")

    return final_mp4


def upload_video_to_drive(mp4_path: Path, trigger_data: dict):
    event_id = str(trigger_data.get("event_id", ""))

    cmd = [
        "/home/pi/CrashGuard/venv/bin/python3",
        "/home/pi/CrashGuard/upload_to_drive.py",
        str(mp4_path),
        event_id
    ]

    print("☁️ Subiendo video a Google Drive...")
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    if result.returncode != 0:
        print(result.stderr)
        raise RuntimeError(f"upload_to_drive.py falló: {result.stderr.strip()}")

    stdout = result.stdout.strip()
    if not stdout:
        raise RuntimeError("upload_to_drive.py no devolvió salida")

    payload = json.loads(stdout)
    video_url = payload.get("video_url")
    if not video_url:
        raise RuntimeError(f"upload_to_drive.py no devolvió video_url: {payload}")

    return video_url


def update_video_url_in_db(trigger_data: dict, public_link: str):
    event_id = str(trigger_data.get("event_id", ""))

    cmd = [
        "/home/pi/CrashGuard/venv/bin/python3",
        "/home/pi/CrashGuard/update_video_url_db.py",
        event_id,
        public_link
    ]

    print("🗄️ Actualizando video_url en MySQL...")
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    if result.returncode != 0:
        print(result.stderr)
        raise RuntimeError(f"update_video_url_db.py falló: {result.stderr.strip()}")

    if result.stdout.strip():
        print(result.stdout.strip())


def write_video_link_ready(trigger_data: dict, public_link: str):
    payload = {
        "event_id": trigger_data.get("event_id"),
        "lat": str(trigger_data.get("lat", "0")),
        "lon": str(trigger_data.get("lon", "0")),
        "magnitud": str(trigger_data.get("magnitud", "0")),
        "video_url": public_link,
        "short_url": public_link
    }

    with open(VIDEO_LINK_READY, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, ensure_ascii=False)


def main():
    global recorder_proc

    ensure_dirs()
    recorder_proc = start_recorder()
    last_trigger_mtime = None

    while running:
        if recorder_proc.poll() is not None:
            print("⚠️ rpicam-vid se detuvo. Reiniciando...")
            time.sleep(2)
            recorder_proc = start_recorder()

        if TRIGGER_FILE.exists():
            try:
                mtime = TRIGGER_FILE.stat().st_mtime
                if last_trigger_mtime is None or mtime != last_trigger_mtime:
                    last_trigger_mtime = mtime

                    with open(TRIGGER_FILE, "r", encoding="utf-8") as f:
                        trigger_data = json.load(f)

                    print(f"🚨 Trigger real detectado para event_id={trigger_data.get('event_id')}")
                    event_dir = copy_segments_for_event(trigger_data)
                    final_mp4 = merge_to_mp4(event_dir)

                    public_link = upload_video_to_drive(final_mp4, trigger_data)
                    update_video_url_in_db(trigger_data, public_link)
                    write_video_link_ready(trigger_data, public_link)

                    print(f"✅ Video subido a Drive: {public_link}")

                    try:
                        TRIGGER_FILE.unlink()
                    except FileNotFoundError:
                        pass

            except Exception as e:
                print(f"❌ Error procesando evento de cámara: {e}")

        time.sleep(1)

    if recorder_proc and recorder_proc.poll() is None:
        recorder_proc.terminate()


if __name__ == "__main__":
    main()
