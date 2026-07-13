import requests as req_lib

from django.utils import timezone
from django.shortcuts import get_object_or_404

from rest_framework.decorators import api_view, authentication_classes, permission_classes
from rest_framework.authentication import TokenAuthentication
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from rest_framework import status
from rest_framework.authtoken.models import Token

from .serializers import SensorDataSerializer, IoTDeviceSerializer, ExperimentRoomSerializer, ExperimentDataSerializer
from .models import SensorData, IoTDevice, ExperimentRoom


# ============================================================
#  EXPERIMENT ROOM CRUD
# ============================================================
@api_view(['GET', 'POST'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def experiment_list(request):
    if request.method == 'GET':
        rooms = ExperimentRoom.objects.filter(user=request.user).order_by('-created')
        serializer = ExperimentRoomSerializer(rooms, many=True)
        return Response({'experiments': serializer.data})
    
    elif request.method == 'POST':
        serializer = ExperimentRoomSerializer(data=request.data)
        if serializer.is_valid():
            serializer.save(user=request.user)
            return Response(serializer.data, status=status.HTTP_201_CREATED)
        return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)


@api_view(['GET', 'PUT', 'DELETE'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def experiment_detail(request, pk):
    room = get_object_or_404(ExperimentRoom, pk=pk, user=request.user)

    if request.method == 'GET':
        serializer = ExperimentRoomSerializer(room)
        return Response(serializer.data)

    elif request.method == 'PUT':
        serializer = ExperimentRoomSerializer(room, data=request.data, partial=True)
        if serializer.is_valid():
            serializer.save()
            return Response(serializer.data)
        return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)

    elif request.method == 'DELETE':
        room.delete()
        return Response({'status': 'deleted'}, status=status.HTTP_204_NO_CONTENT)


# ============================================================
#  ENDPOINT: Lihat data sensor milik experiment room
#  GET /api/experiments/<id>/data/
#  GET /api/experiments/0/data/  -> data yang tidak punya experiment (default)
# ============================================================
@api_view(['GET'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def experiment_data(request, pk):
    queryset = SensorData.objects.filter(user=request.user)

    pk = int(pk)
    if pk == 0:
        # Virtual "Default" experiment: data tanpa experiment
        queryset = queryset.filter(experiment__isnull=True)
        exp_name = "Eksperimen Default"
    else:
        room = get_object_or_404(ExperimentRoom, pk=pk, user=request.user)
        queryset = queryset.filter(experiment=room)
        exp_name = room.name or "Unnamed"

    queryset = queryset.order_by('-created')[:200]

    serializer = ExperimentDataSerializer(queryset, many=True)
    return Response({
        'experiment_id':   pk if pk != 0 else None,
        'experiment_name': exp_name,
        'sensor_data':     serializer.data,
        'count':           len(serializer.data),
    })


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
        # Kaitkan data ke perangkat pengirim + experiment room-nya, agar
        # experiment room tersinkron dengan data sensor yang masuk.
        save_kwargs = {'user': request.user}
        device_id = (request.data.get('device_id') or '').strip()
        device = None
        if device_id:
            device = IoTDevice.objects.filter(user=request.user, device_id=device_id).first()
        if device:
            save_kwargs['device'] = device

        # Tentukan experiment:
        #  1) dari payload IoT (dipandu frontend via heartbeat) — divalidasi milik user
        #  2) fallback ke active_experiment perangkat
        experiment = None
        exp_raw = request.data.get('experiment')
        if exp_raw not in (None, '', 0, '0'):
            experiment = ExperimentRoom.objects.filter(pk=exp_raw, user=request.user).first()
        if experiment is None and device and device.active_experiment_id:
            experiment = device.active_experiment
        if experiment:
            save_kwargs['experiment'] = experiment

        serializer.save(**save_kwargs)
        return Response({
            "status":  "success",
            "message": f"Data diterima untuk {request.user.username}",
            "experiment_id": experiment.id if experiment else None,
        }, status=status.HTTP_201_CREATED)

    print(serializer.errors)
    return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)


# ============================================================
#  ENDPOINT: Set eksperimen aktif untuk sebuah perangkat
#  POST /api/device/active-experiment/
#  body: { device_id, experiment_id }  (experiment_id 0/null = kosongkan)
# ============================================================
@api_view(['POST'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def set_active_experiment(request):
    device_id = (request.data.get('device_id') or '').strip()
    if not device_id:
        return Response({'error': 'device_id wajib diisi'}, status=status.HTTP_400_BAD_REQUEST)

    device = get_object_or_404(IoTDevice, user=request.user, device_id=device_id)

    experiment_id = request.data.get('experiment_id')
    if experiment_id in (None, '', 0, '0'):
        device.active_experiment = None
    else:
        room = get_object_or_404(ExperimentRoom, pk=experiment_id, user=request.user)
        device.active_experiment = room
    device.save(update_fields=['active_experiment'])

    return Response({
        'status':            'ok',
        'device_id':         device.device_id,
        'active_experiment': device.active_experiment_id,
    })


# ============================================================
#  ENDPOINT 2: ESP32 register / heartbeat ke Django
#  POST /api/device/heartbeat/
#  Dipanggil firmware setiap 15 detik secara otomatis.
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

    # ESP32 akan membaca target_status dari response ini untuk menyalakan/mematikan mesin
    # dan active_experiment untuk menandai data yang dikirim ke experiment room.
    return Response({
        'status':            'ok',
        'device_id':         device.device_id,
        'target_status':     device.target_status,
        'is_sterile':        device.is_sterile,
        'active_experiment': device.active_experiment_id or 0,
        'registered':        created,
        'message':           'Device registered' if created else 'Heartbeat received'
    }, status=status.HTTP_200_OK)


# ============================================================
#  ENDPOINT BARU: Web mengirim perintah Kontrol
#  POST /api/device/control/
#  Menyimpan target_status dari dashboard ke database
# ============================================================
@api_view(['POST'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def toggle_device_status(request):
    device_id = request.data.get('device_id')
    new_status = request.data.get('target_status') # Boolean True/False

    device = get_object_or_404(IoTDevice, user=request.user, device_id=device_id)
    device.target_status = new_status
    # Kalau dinyalakan kembali manual, reset status steril supaya bisa deteksi ulang
    if new_status:
        device.is_sterile = False
        device.sterile_current_count = 0
    device.save()

    return Response({
        'status': 'success',
        'message': f"Device {device_id} diset ke {'ON' if new_status else 'OFF'}",
        'target_status': device.target_status
    })


# ============================================================
#  ENDPOINT BARU: ESP32 kirim data sensor untuk cek steril
#  POST /api/device/sterile-check/
#  Dipanggil firmware setelah kirim data sensor.
#  Response berisi apakah mesin harus mati.
# ============================================================
@api_view(['POST'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def sterile_check(request):
    device_id = request.data.get('device_id', '').strip()
    if not device_id:
        return Response({'error': 'device_id wajib diisi'}, status=status.HTTP_400_BAD_REQUEST)

    try:
        red       = float(request.data.get('red',       0))
        green     = float(request.data.get('green',     0))
        blue      = float(request.data.get('blue',      0))
        lux       = float(request.data.get('lux',       0))
        raw_light = float(request.data.get('raw_light', 0))
    except (TypeError, ValueError):
        return Response({'error': 'Data sensor tidak valid'}, status=status.HTTP_400_BAD_REQUEST)

    device = get_object_or_404(IoTDevice, user=request.user, device_id=device_id)

    just_sterile = device.check_sterile(red, green, blue, lux, raw_light)

    return Response({
        'status':          'ok',
        'device_id':       device.device_id,
        'target_status':   device.target_status,
        'is_sterile':      device.is_sterile,
        'just_triggered':  just_sterile,        # True = baru saja mencapai threshold
        'confirm_count':   device.sterile_current_count,
        'confirm_needed':  device.sterile_confirm_count,
        'message':         'Air sudah jernih! Mesin dimatikan.' if just_sterile else
                           f'Belum jernih ({device.sterile_current_count}/{device.sterile_confirm_count})',
    }, status=status.HTTP_200_OK)


# ============================================================
#  ENDPOINT BARU: Konfigurasi threshold steril dari dashboard
#  PATCH /api/device/sterile-config/
# ============================================================
@api_view(['GET', 'PATCH'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def sterile_config(request):
    device_id = request.query_params.get('device_id') or request.data.get('device_id')
    device = get_object_or_404(IoTDevice, user=request.user, device_id=device_id)

    if request.method == 'GET':
        return Response({
            'device_id':            device.device_id,
            'sterile_lux_min':      device.sterile_lux_min,
            'sterile_raw_min':      device.sterile_raw_min,
            'sterile_balance_max':  device.sterile_balance_max,
            'sterile_confirm_count': device.sterile_confirm_count,
            'is_sterile':           device.is_sterile,
            'sterile_current_count': device.sterile_current_count,
        })

    # PATCH: update threshold
    fields = ['sterile_lux_min', 'sterile_raw_min', 'sterile_balance_max', 'sterile_confirm_count']
    updated = []
    for field in fields:
        if field in request.data:
            setattr(device, field, float(request.data[field]))
            updated.append(field)
    if updated:
        device.save(update_fields=updated)

    return Response({
        'status':  'updated',
        'updated': updated,
        'sterile_lux_min':       device.sterile_lux_min,
        'sterile_raw_min':       device.sterile_raw_min,
        'sterile_balance_max':   device.sterile_balance_max,
        'sterile_confirm_count': device.sterile_confirm_count,
    })


# ============================================================
#  ENDPOINT 3: Daftar device online milik user
#  GET /api/devices/online/
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
        'target_status':      d.target_status,
        'is_sterile':         d.is_sterile,
        'sterile_current_count': d.sterile_current_count,
        'sterile_confirm_count': d.sterile_confirm_count,
        'last_seen':          d.last_seen.strftime('%H:%M:%S'),
        'seconds_since_seen': d.seconds_since_seen,
        'source':             'registry',
    } for d in devices]

    return Response({
        'devices': data,
        'count':   len(data)
    })


# ============================================================
#  ENDPOINT 4: Django proxy ke ESP32 lokal
#  GET /api/devices/probe/
# ============================================================
@api_view(['GET'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def probe_devices(request):
    cutoff  = timezone.now() - timezone.timedelta(seconds=60)
    devices = IoTDevice.objects.filter(user=request.user, last_seen__gte=cutoff)

    targets = []
    for d in devices:
        if d.ip_local:
            targets.append({
                'url':         f"http://{d.ip_local}/info",
                'source_hint': 'proxy_ip'
            })

    targets.append({
        'url':         'http://esp32ozora.local/info',
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
            continue

    return Response({
        'devices': found,
        'count':   len(found)
    })
