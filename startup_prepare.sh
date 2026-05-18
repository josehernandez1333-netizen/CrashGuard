#!/bin/bash
set -e

mkdir -p /home/pi/CrashGuard/runtime
mkdir -p /home/pi/CrashGuard/evidence
mkdir -p /home/pi/CrashGuard/camera_buffer
mkdir -p /home/pi/CrashGuard/uploads

rm -f /home/pi/CrashGuard/runtime/video_link_ready.json
rm -f /home/pi/CrashGuard/runtime/impact_trigger.json

chown -R pi:pi /home/pi/CrashGuard
chmod -R u+rwX /home/pi/CrashGuard

# Asegura MariaDB arriba al preparar
systemctl start mariadb
