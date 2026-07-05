from rest_framework import serializers
from .models import SensorData, IoTDevice, ExperimentRoom


class ExperimentDataSerializer(serializers.ModelSerializer):
    ph = serializers.SerializerMethodField()

    class Meta:
        model = SensorData
        fields = ['id', 'experiment', 'raw_light', 'red', 'green', 'blue', 'temp', 'lux', 'ph', 'created']
        read_only_fields = ['created']

    def get_ph(self, obj):
        try:
            from .views import calculate_ph
            return calculate_ph(
                float(obj.red), float(obj.green), float(obj.blue),
                float(obj.temp), float(obj.lux)
            )
        except Exception:
            return None


class ExperimentRoomSerializer(serializers.ModelSerializer):
    device_name = serializers.CharField(source='device_id.device_name', read_only=True, allow_null=True)
    sensor_count = serializers.SerializerMethodField()

    class Meta:
        model = ExperimentRoom
        fields = ['id', 'user', 'device_id', 'device_name', 'name', 'created', 'sensor_count']
        read_only_fields = ['user', 'created', 'sensor_count']
        extra_kwargs = {
            'device_id': {'allow_null': True, 'required': False},
        }

    def get_sensor_count(self, obj):
        return obj.experiment_sensor.count()


class SensorDataSerializer(serializers.ModelSerializer):
    class Meta:
        model = SensorData
        fields = ['experiment', 'raw_light', 'red', 'green', 'blue', 'temp', 'lux', 'created']
        read_only_fields = ['created']


class IoTDeviceSerializer(serializers.ModelSerializer):
    is_online = serializers.BooleanField(read_only=True)
    seconds_since_seen = serializers.IntegerField(read_only=True)

    class Meta:
        model = IoTDevice
        fields = [
            'device_id', 'device_name', 'ip_local',
            'ssid', 'rssi', 'firmware',
            'last_seen', 'is_online', 'seconds_since_seen'
        ]
        read_only_fields = ['last_seen', 'is_online', 'seconds_since_seen']
