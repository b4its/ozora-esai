from django.contrib import admin
from .models import SensorData, IoTDevice, ExperimentRoom


class SensorDataInline(admin.TabularInline):
    model = SensorData
    extra = 0
    fields = ['raw_light', 'red', 'green', 'blue', 'temp', 'lux', 'created']
    readonly_fields = ['created']
    show_change_link = True


@admin.register(ExperimentRoom)
class ExperimentRoomAdmin(admin.ModelAdmin):
    list_display = ['name', 'user', 'device_id', 'created']
    list_filter = ['user', 'created']
    search_fields = ['name', 'user__username', 'device_id__device_name']
    inlines = [SensorDataInline]


admin.site.register(SensorData)
admin.site.register(IoTDevice)
