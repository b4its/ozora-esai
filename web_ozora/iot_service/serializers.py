from rest_framework import serializers
from .models import SensorData, IoTDevice


class SensorDataSerializer(serializers.ModelSerializer):
    class Meta:
        model  = SensorData
        fields = ['raw_light', 'red', 'green', 'blue', 'temp', 'lux', 'created']
        read_only_fields = ['created']


class IoTDeviceSerializer(serializers.ModelSerializer):
    is_online          = serializers.BooleanField(read_only=True)
    seconds_since_seen = serializers.IntegerField(read_only=True)

    class Meta:
        model  = IoTDevice
        fields = [
            'device_id', 'device_name', 'ip_local',
            'ssid', 'rssi', 'firmware',
            'last_seen', 'is_online', 'seconds_since_seen'
        ]
        read_only_fields = ['last_seen', 'is_online', 'seconds_since_seen']