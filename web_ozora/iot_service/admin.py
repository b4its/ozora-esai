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
    list_display = ['id', 'name', 'user', 'device_id', 'created']
    list_filter = ['user', 'created']
    search_fields = ['name', 'user__username', 'device_id__device_name']
    inlines = [SensorDataInline]


@admin.register(SensorData)
class SensorDataAdmin(admin.ModelAdmin):
    # Tampilkan relasi device & experiment room secara jelas
    list_display = ['id', 'created', 'user', 'device', 'experiment',
                    'red', 'green', 'blue', 'lux', 'temp']
    list_filter = ['user', 'device', 'experiment']
    search_fields = ['user__username', 'device__device_name', 'device__device_id',
                     'experiment__name']
    list_select_related = ['user', 'device', 'experiment']
    readonly_fields = ['created']


@admin.register(IoTDevice)
class IoTDeviceAdmin(admin.ModelAdmin):
    list_display = ['device_name', 'device_id', 'user', 'target_status',
                    'active_experiment', 'is_sterile', 'last_seen']
    list_filter = ['user', 'target_status', 'is_sterile']
    search_fields = ['device_name', 'device_id', 'user__username']
