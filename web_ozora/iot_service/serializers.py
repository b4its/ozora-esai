from rest_framework import serializers
from .models import SensorData

class SensorDataSerializer(serializers.ModelSerializer):
    class Meta:
        model = SensorData
        # Kita tidak menyertakan 'user' di fields input karena akan diambil dari request.user
        fields = ['red', 'green', 'blue', 'temp', 'lux', 'created']
        read_only_fields = ['created']