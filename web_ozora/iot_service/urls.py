from django.urls import path
from .import views

urlpatterns = [
    # ── Data sensor dari ESP32 ──
    path('receive-data/',      views.receive_iot_data, name='receiveData'),

    # ── Device Discovery ──
    path('device/heartbeat/',  views.device_heartbeat, name='deviceHeartbeat'),
    path('devices/online/',    views.devices_online,   name='devicesOnline'),
    path('devices/probe/',     views.probe_devices,    name='probeDevices'),
]