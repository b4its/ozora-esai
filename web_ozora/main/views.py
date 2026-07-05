from django.shortcuts import render
from django.contrib.auth.decorators import login_required
from rest_framework.authtoken.models import Token
from iot_service.models import SensorData, ExperimentRoom, IoTDevice
from django.http import JsonResponse
from iot_service.views import calculate_ph



@login_required(login_url='customerLogin')
def home(request):
    token_obj, created = Token.objects.get_or_create(user=request.user)
    experiment_id = request.GET.get('experiment')

    # ── HANDLE AJAX (REAL-TIME POLLING) ──
    if request.headers.get('X-Requested-With') == 'XMLHttpRequest':
        since_id = int(request.GET.get('since_id', 0))
        queryset = SensorData.objects.filter(
            user=request.user, id__gt=since_id
        )
        if experiment_id:
            queryset = queryset.filter(experiment_id=experiment_id)
        queryset = queryset.order_by('-created')[:50]

        data = []
        for s in queryset:
            ph_val = calculate_ph(
                float(s.red), float(s.green), float(s.blue),
                float(s.temp), float(s.lux)
            )
            data.append({
                'id':      s.pk,
                'created': s.created.strftime('%Y-%m-%d %H:%M:%S'),
                'red':     float(s.red),
                'green':   float(s.green),
                'blue':    float(s.blue),
                'temp':    float(s.temp),
                'lux':     float(s.lux),
                'ph':      ph_val,
            })
        return JsonResponse({'sensor_data': data})

    # ── HANDLE INITIAL LOAD ──
    raw_sensor_data = SensorData.objects.filter(user=request.user)
    if experiment_id:
        raw_sensor_data = raw_sensor_data.filter(experiment_id=experiment_id)
    raw_sensor_data = raw_sensor_data.order_by('-created')[:100]

    for s in raw_sensor_data:
        s.ph = calculate_ph(
            float(s.red), float(s.green), float(s.blue),
            float(s.temp), float(s.lux)
        )

    experiments = ExperimentRoom.objects.filter(user=request.user).order_by('-created')
    user_devices = IoTDevice.objects.filter(user=request.user).order_by('-last_seen')

    # Hitung data unassigned (default experiment)
    default_count = SensorData.objects.filter(user=request.user, experiment__isnull=True).count()

    context = {
        'api_token':         token_obj.key,
        'dataSensor':        raw_sensor_data,
        'experiments':       experiments,
        'user_devices':      user_devices,
        'active_experiment': experiment_id,
        'default_count':     default_count,
        'is_new':            created,
    }
    return render(request, 'index.html', context)
