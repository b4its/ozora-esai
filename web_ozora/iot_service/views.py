from rest_framework.decorators import api_view, authentication_classes, permission_classes
from rest_framework.authentication import TokenAuthentication
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from rest_framework import status
from .serializers import SensorDataSerializer

@api_view(['POST'])
@authentication_classes([TokenAuthentication])
@permission_classes([IsAuthenticated])
def receive_iot_data(request):
    serializer = SensorDataSerializer(data=request.data)
    if serializer.is_valid():
        # Simpan data sambil menyematkan user yang sedang login via token
        serializer.save(user=request.user)
        return Response({"message": "Data tersimpan untuk user: " + request.user.username}, status=status.HTTP_201_CREATED)
    return Response(serializer.errors, status=status.HTTP_400_BAD_REQUEST)