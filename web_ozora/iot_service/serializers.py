from rest_framework import serializers
from .models import SensorData, IoTDevice, ExperimentRoom


class ExperimentDataSerializer(serializers.ModelSerializer):
    class Meta:
        model = SensorData
        fields = ['id', 'experiment', 'raw_light', 'red', 'green', 'blue', 'temp', 'lux', 'created']
        read_only_fields = ['created']


class ExperimentRoomSerializer(serializers.ModelSerializer):
    device_name = serializers.CharField(source='device_id.device_name', read_only=True, allow_null=True)
    sensor_count = serializers.SerializerMethodField()

    class Meta:
        model = ExperimentRoom
        fields = ['id', 'user', 'device_id', 'device_name', 'name', 'suhu', 'flow_speed', 'ph_target', 'created', 'sensor_count']
        read_only_fields = ['user', 'created', 'sensor_count']
        extra_kwargs = {
            'device_id': {'allow_null': True, 'required': False},
            'suhu': {'required': False, 'allow_null': True},
            'flow_speed': {'required': False, 'allow_null': True},
            'ph_target': {'required': False, 'allow_null': True},
        }

    def get_sensor_count(self, obj):
        return obj.experiment_sensor.count()


class SensorDataSerializer(serializers.ModelSerializer):
    class Meta:
        model = SensorData
        fields = ['experiment', 'raw_light', 'red', 'green', 'blue', 'temp', 'lux', 'created']
        # experiment ditentukan & divalidasi di view (kepemilikan user),
        # bukan langsung dari payload perangkat.
        read_only_fields = ['created', 'experiment']


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
