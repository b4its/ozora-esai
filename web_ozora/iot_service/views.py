from rest_framework.decorators import api_view, authentication_classes, permission_classes
from rest_framework.authentication import TokenAuthentication
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from rest_framework import status
from .serializers import SensorDataSerializer
from rest_framework.authtoken.models import Token
from .models import SensorData

@api_view(['POST'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def receive_iot_data(request):
    serializer = SensorDataSerializer(data=request.data)
    if serializer.is_valid():
        serializer.save(user=request.user)
        return Response({
            "status": "success",
            "message": f"Data diterima untuk {request.user.username}"
        }, status=status.HTTP_201_CREATED)
    
    # Jika error, kirim detail field mana yang salah (misal: "temp" harus angka)
    print(serializer.errors) # Muncul di terminal Django untuk debug
    return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)


def calculate_ph(red, green, blue, temp, lux):
    # 1. Normalisasi RGB (Chromaticity)
    total = red + green + blue
    if total == 0: return 7.0  # Default neutral jika tidak ada cahaya
    
    r = red / total
    g = green / total
    b = blue / total

    # 2. Konversi ke Hue (Derajat Warna 0-360)
    # Ini seringkali lebih akurat untuk indikator pH universal
    mx = max(r, g, b)
    mn = min(r, g, b)
    df = mx - mn
    if mx == mn:
        hue = 0
    elif mx == r:
        hue = (60 * ((g - b) / df) + 360) % 360
    elif mx == g:
        hue = (60 * ((b - r) / df) + 120) % 360
    elif mx == b:
        hue = (60 * ((r - g) / df) + 240) % 360

    # 3. Mapping Hue ke pH (Contoh Linear Mapping)
    # Anda perlu menyesuaikan rentang ini berdasarkan hasil kalibrasi alat Anda
    # Misal: Hue 0 (Merah) = pH 4, Hue 120 (Hijau) = pH 7, Hue 240 (Biru) = pH 10
    # Contoh rumus regresi sederhana:
    estimated_ph = 7.0 + (hue - 120) * (3 / 120) 
    
    # 4. Kompensasi Suhu (Sederhana)
    # Koreksi pH sekitar 0.01 unit per derajat Celsius dari standar 25°C
    temp_correction = (temp - 25) * 0.01
    final_ph = estimated_ph + temp_correction

    return round(max(0, min(14, final_ph)), 2)
