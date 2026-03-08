from django.contrib import admin
from .models import SensorData, IoTDevice

admin.site.register(SensorData)
admin.site.register(IoTDevice)