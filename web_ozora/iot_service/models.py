from django.db import models
from django.contrib.auth.models import User

# Create your models here.
class SensorData(models.Model):
    user = models.ForeignKey(User, on_delete=models.CASCADE, related_name='sensor_data')
    raw_light = models.DecimalField(max_digits=20, decimal_places=2, null=True)
    red = models.DecimalField(max_digits=20, decimal_places=2)
    green = models.DecimalField(max_digits=20, decimal_places=2)
    blue = models.DecimalField(max_digits=20, decimal_places=2)
    temp = models.DecimalField(max_digits=20, decimal_places=2)
    lux = models.DecimalField(max_digits=20, decimal_places=2)
    created = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"Data Sensor dari {self.user.username} | pada waktu: {self.created}"