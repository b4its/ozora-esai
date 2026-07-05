from django.urls import path
from .import views

urlpatterns = [
    # Experiment Room CRUD
    path('experiments/',            views.experiment_list,     name='experimentList'),
    path('experiments/<int:pk>/',    views.experiment_detail,   name='experimentDetail'),
    path('experiments/<int:pk>/data/', views.experiment_data,   name='experimentData'),

    # Data sensor dari ESP32
    path('receive-data/',      views.receive_iot_data, name='receiveData'),

    # Device Discovery
    path('device/heartbeat/',  views.device_heartbeat, name='deviceHeartbeat'),
    path('devices/online/',    views.devices_online,   name='devicesOnline'),
    path('devices/probe/',     views.probe_devices,    name='probeDevices'),

    # ENDPOINT BARU UNTUK KONTROL DARI WEB
    path('device/control/',        views.toggle_device_status, name='deviceControl'),

    # Eksperimen aktif per-device (auto-tag data masuk)
    path('device/active-experiment/', views.set_active_experiment, name='setActiveExperiment'),

    # ENDPOINT STERIL / AUTO-OFF
    path('device/sterile-check/',  views.sterile_check,  name='sterileCheck'),
    path('device/sterile-config/', views.sterile_config, name='sterileConfig'),
]
