import requests as req_lib

from django.utils import timezone

from rest_framework.decorators import api_view, authentication_classes, permission_classes
from rest_framework.authentication import TokenAuthentication
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from rest_framework import status
from rest_framework.authtoken.models import Token

from .serializers import SensorDataSerializer, IoTDeviceSerializer
from .models import SensorData, IoTDevice


# ============================================================
#  HELPER: Kalkulasi pH dari data sensor warna
# ============================================================

def calculate_ph(red, green, blue, temp, lux):
    total = red + green + blue
    if total == 0:
        return 7.0

    r = red   / total
    g = green / total
    b = blue  / total

    mx = max(r, g, b)
    mn = min(r, g, b)
    df = mx - mn

    if mx == mn:
        hue = 0
    elif mx == r:
        hue = (60 * ((g - b) / df) + 360) % 360
    elif mx == g:
        hue = (60 * ((b - r) / df) + 120) % 360
    else:
        hue = (60 * ((r - g) / df) + 240) % 360

    estimated_ph   = 7.0 + (hue - 120) * (3 / 120)
    temp_correction = (temp - 25) * 0.01
    final_ph        = estimated_ph + temp_correction

    return round(max(0, min(14, final_ph)), 2)


# ============================================================
#  ENDPOINT 1: Terima data sensor dari ESP32
#  POST /api/receive-data/
# ============================================================

@api_view(['POST'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def receive_iot_data(request):
    serializer = SensorDataSerializer(data=request.data)
    if serializer.is_valid():
        serializer.save(user=request.user)
        return Response({
            "status":  "success",
            "message": f"Data diterima untuk {request.user.username}"
        }, status=status.HTTP_201_CREATED)

    print(serializer.errors)
    return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)


# ============================================================
#  ENDPOINT 2: ESP32 register / heartbeat ke Django
#  POST /api/device/heartbeat/
#  Dipanggil firmware setiap 15 detik secara otomatis.
#  Menggunakan update_or_create → last_seen diperbarui tiap call.
# ============================================================

@api_view(['POST'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def device_heartbeat(request):
    d = request.data

    device_id = d.get('device_id', '').strip()
    if not device_id:
        return Response(
            {"error": "device_id (MAC address) wajib diisi"},
            status=status.HTTP_400_BAD_REQUEST
        )

    device, created = IoTDevice.objects.update_or_create(
        user      = request.user,
        device_id = device_id,
        defaults  = {
            'device_name': d.get('device_name', 'Unknown Device'),
            'ip_local':    d.get('ip_local',    ''),
            'ssid':        d.get('ssid',        ''),
            'rssi':        int(d.get('rssi', 0)),
            'firmware':    d.get('firmware',    ''),
        }
    )

    return Response({
        'status':     'ok',
        'device_id':  device.device_id,
        'registered': created,
        'message':    'Device registered' if created else 'Heartbeat received'
    }, status=status.HTTP_200_OK)


# ============================================================
#  ENDPOINT 3: Daftar device online milik user
#  GET /api/devices/online/
#  Frontend poll endpoint ini saat tombol CONNECT ditekan.
#  "Online" = last_seen dalam 30 detik terakhir.
# ============================================================

@api_view(['GET'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def devices_online(request):
    cutoff  = timezone.now() - timezone.timedelta(seconds=30)
    devices = IoTDevice.objects.filter(
        user      = request.user,
        last_seen__gte = cutoff
    )

    data = [{
        'device_id':          d.device_id,
        'device_name':        d.device_name,
        'ip_local':           d.ip_local,
        'ssid':               d.ssid,
        'rssi':               d.rssi,
        'firmware':           d.firmware,
        'last_seen':          d.last_seen.strftime('%H:%M:%S'),
        'seconds_since_seen': d.seconds_since_seen,
        'source':             'registry',
    } for d in devices]

    return Response({
        'devices': data,
        'count':   len(data)
    })


# ============================================================
#  ENDPOINT 4: Django sebagai proxy ke ESP32 lokal
#  GET /api/devices/probe/
#
#  Kegunaan: Bypass browser Mixed-Content block.
#  Browser tidak bisa fetch http://esp32.local dari halaman HTTPS,
#  tapi Django (server) bisa fetch HTTP ke LAN.
#
#  Alur:
#    1. Ambil IP device dari registry (heartbeat terakhir)
#    2. Coba fetch http://<ip>/info  (timeout 2 detik per device)
#    3. Fallback: coba http://esp32sensor.local/info
#    4. Return merged list ke frontend
# ============================================================

@api_view(['GET'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def probe_devices(request):
    # Kumpulkan target URL dari registry (last seen maks 60 detik)
    cutoff  = timezone.now() - timezone.timedelta(seconds=60)
    devices = IoTDevice.objects.filter(user=request.user, last_seen__gte=cutoff)

    targets = []
    for d in devices:
        if d.ip_local:
            targets.append({
                'url':         f"http://{d.ip_local}/info",
                'source_hint': 'proxy_ip'
            })

    # Fallback mDNS (mungkin resolve di jaringan server)
    targets.append({
        'url':         'http://esp32sensor.local/info',
        'source_hint': 'proxy_mdns'
    })

    found   = []
    seen_ids = set()

    for t in targets:
        try:
            r = req_lib.get(t['url'], timeout=2)
            if r.status_code == 200:
                info = r.json()
                dev_id = info.get('device_id') or info.get('ip') or t['url']
                if dev_id not in seen_ids:
                    seen_ids.add(dev_id)
                    info['source']        = 'proxy'
                    info['discovery_url'] = t['url']
                    found.append(info)
        except Exception:
            # Device tidak reachable dari server → skip
            continue

    return Response({
        'devices': found,
        'count':   len(found)
    })