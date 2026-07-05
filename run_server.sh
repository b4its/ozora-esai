#!/bin/bash
# Jalankan Django server agar bisa diakses dari ESP32 di jaringan lokal
# Usage: ./run_server.sh

cd "$(dirname "$0")"

# Aktifkan virtualenv
source env/bin/activate

# Tampilkan IP yang akan dipakai ESP32
echo ""
echo "╔══════════════════════════════════════════╗"
echo "║   OZORA Django Server — IoT Mode         ║"
echo "╚══════════════════════════════════════════╝"
echo ""
echo "  Accessible from ESP32 at:"
ip addr show | grep 'inet ' | grep -v '127.0.0.1' | grep -v '172\.' | awk '{print "  → http://"$2}' | sed 's|/[0-9]*|:8000|'
echo ""
echo "  Update SERVER_URL di CSensortitu.ino ke IP di atas + :8000"
echo "  Contoh: http://192.168.x.x:8000/api/receive-data/"
echo ""

cd web_ozora
python manage.py runserver 0.0.0.0:8000
