from django.db import models
from django.contrib.auth.models import User
from django.utils import timezone


class SensorData(models.Model):
    user      = models.ForeignKey(User, on_delete=models.CASCADE, related_name='sensor_data')
    raw_light = models.DecimalField(max_digits=20, decimal_places=2, null=True)
    red       = models.DecimalField(max_digits=20, decimal_places=2)
    green     = models.DecimalField(max_digits=20, decimal_places=2)
    blue      = models.DecimalField(max_digits=20, decimal_places=2)
    temp      = models.DecimalField(max_digits=20, decimal_places=2)
    lux       = models.DecimalField(max_digits=20, decimal_places=2)
    created   = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"Data Sensor dari {self.user.username} | pada waktu: {self.created}"


class IoTDevice(models.Model):
    """
    Menyimpan registrasi device ESP32 yang terhubung ke akun user.
    Device mendaftarkan diri secara otomatis via endpoint /api/device/heartbeat/
    setiap HEARTBEAT_INTERVAL detik (default 15 detik di firmware).
    Field last_seen diperbarui otomatis setiap kali heartbeat diterima.
    """
    user        = models.ForeignKey(User, on_delete=models.CASCADE, related_name='devices')
    device_id   = models.CharField(max_length=50, help_text="MAC Address ESP32")
    device_name = models.CharField(max_length=100, help_text="AP_SSID dari firmware")
    ip_local    = models.CharField(max_length=45,  help_text="IP lokal di jaringan WiFi")
    ssid        = models.CharField(max_length=100, blank=True, help_text="Nama WiFi yang dipakai")
    rssi        = models.IntegerField(default=0,   help_text="Kekuatan sinyal WiFi (dBm)")
    firmware    = models.CharField(max_length=20,  blank=True)
    last_seen   = models.DateTimeField(auto_now=True, help_text="Diperbarui otomatis tiap heartbeat")

    class Meta:
        unique_together = ('user', 'device_id')
        verbose_name        = 'IoT Device'
        verbose_name_plural = 'IoT Devices'
        ordering = ['-last_seen']

    def __str__(self):
        return f"{self.device_name} ({self.device_id}) — {self.user.username}"

    @property
    def is_online(self):
        """Device dianggap online jika heartbeat < 30 detik yang lalu."""
        return (timezone.now() - self.last_seen).total_seconds() < 30

    @property
    def seconds_since_seen(self):
        return int((timezone.now() - self.last_seen).total_seconds())