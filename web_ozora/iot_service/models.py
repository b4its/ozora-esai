from django.db import models
from django.contrib.auth.models import User
from django.utils import timezone

class ExperimentRoom(models.Model):
    user = models.ForeignKey(User, on_delete=models.CASCADE, related_name='experiment_user')
    # Menggunakan string 'IoTDevice' agar tidak error NameError
    device_id = models.ForeignKey('IoTDevice', on_delete=models.CASCADE, related_name='experiment_device', null=True, blank=True)
    name = models.CharField(max_length=255, blank=True, help_text="Nama Eksperimen")
    created = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"Eksperimen dari: {self.user.username} | bernama: {self.name}"
        
class SensorData(models.Model):
    experiment = models.ForeignKey('ExperimentRoom', on_delete=models.CASCADE, related_name='experiment_sensor', null=True, blank=True)
    device = models.ForeignKey('IoTDevice', on_delete=models.CASCADE, related_name='sensor_device', null=True, blank=True)
    user = models.ForeignKey(User, on_delete=models.CASCADE, related_name='sensor_user')
    raw_light = models.DecimalField(max_digits=20, decimal_places=2, null=True)
    red = models.DecimalField(max_digits=20, decimal_places=2)
    green = models.DecimalField(max_digits=20, decimal_places=2)
    blue = models.DecimalField(max_digits=20, decimal_places=2)
    temp = models.DecimalField(max_digits=20, decimal_places=2)
    lux = models.DecimalField(max_digits=20, decimal_places=2)
    created = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"Data Sensor dari {self.user.username} | pada waktu: {self.created}"

class IoTDevice(models.Model):
    user = models.ForeignKey(User, on_delete=models.CASCADE, related_name='devices')
    device_id = models.CharField(max_length=50, help_text="MAC Address ESP32")
    device_name = models.CharField(max_length=100, help_text="AP_SSID dari firmware")
    ip_local = models.CharField(max_length=45,  help_text="IP lokal di jaringan WiFi")
    ssid = models.CharField(max_length=100, blank=True, help_text="Nama WiFi yang dipakai")
    rssi = models.IntegerField(default=0,   help_text="Kekuatan sinyal WiFi (dBm)")
    firmware = models.CharField(max_length=20,  blank=True)
    last_seen = models.DateTimeField(auto_now=True, help_text="Diperbarui otomatis tiap heartbeat")
    
    # FIELD BARU UNTUK STATE POLLING KONTROL WEB
    # Default False (STANDBY): perangkat yang baru terhubung TIDAK langsung
    # mengirim data — menunggu perintah ON dari website (tombol proc-btn).
    target_status = models.BooleanField(default=False, help_text="True = Mesin ON, False = Mesin OFF/standby")

    # FIELD UNTUK DETEKSI STERIL / JERNIH
    is_sterile = models.BooleanField(default=False, help_text="True jika air terdeteksi sudah jernih/steril")
    sterile_lux_min      = models.FloatField(default=300.0,  help_text="Lux minimum agar dianggap jernih")
    sterile_raw_min      = models.FloatField(default=800.0,  help_text="Raw light minimum agar dianggap jernih")
    sterile_balance_max  = models.FloatField(default=0.15,   help_text="Maks selisih rasio RGB (0.0–1.0) untuk dianggap seimbang/jernih")
    sterile_confirm_count = models.IntegerField(default=3,   help_text="Berapa kali berturut-turut harus terdeteksi jernih sebelum mesin dimatikan")
    sterile_current_count = models.IntegerField(default=0,   help_text="Counter saat ini — reset saat tidak jernih")

    class Meta:
        unique_together = ('user', 'device_id')
        verbose_name = 'IoT Device'
        verbose_name_plural = 'IoT Devices'
        ordering = ['-last_seen']

    def __str__(self):
        return f"{self.device_name} ({self.device_id}) — {self.user.username}"

    @property
    def is_online(self):
        return (timezone.now() - self.last_seen).total_seconds() < 30

    @property
    def seconds_since_seen(self):
        return int((timezone.now() - self.last_seen).total_seconds())

    def check_sterile(self, red, green, blue, lux, raw_light):
        """
        Cek apakah air jernih berdasarkan threshold yang dikonfigurasi.
        Return True jika kondisi jernih sudah terpenuhi `sterile_confirm_count` kali berturut-turut.
        Side-effect: update sterile_current_count, is_sterile, dan target_status di database.
        """
        total = red + green + blue
        if total == 0:
            self.sterile_current_count = 0
            self.is_sterile = False
            self.save(update_fields=['sterile_current_count', 'is_sterile'])
            return False

        r_ratio = red   / total
        g_ratio = green / total
        b_ratio = blue  / total

        # Selisih maks antara komponen RGB — semakin kecil = semakin seimbang = semakin jernih
        balance = max(r_ratio, g_ratio, b_ratio) - min(r_ratio, g_ratio, b_ratio)

        kondisi_jernih = (
            lux       >= self.sterile_lux_min and
            raw_light >= self.sterile_raw_min and
            balance   <= self.sterile_balance_max
        )

        if kondisi_jernih:
            self.sterile_current_count += 1
        else:
            self.sterile_current_count = 0
            self.is_sterile = False
            self.save(update_fields=['sterile_current_count', 'is_sterile'])
            return False

        if self.sterile_current_count >= self.sterile_confirm_count:
            self.is_sterile    = True
            self.target_status = False  # Matikan mesin otomatis
            self.save(update_fields=['sterile_current_count', 'is_sterile', 'target_status'])
            return True

        self.save(update_fields=['sterile_current_count', 'is_sterile'])
        return False