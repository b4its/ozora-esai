from django.shortcuts import render
from django.contrib.auth.decorators import login_required
from rest_framework.authtoken.models import Token
from iot_service.models import SensorData
from django.http import JsonResponse
from iot_service.views import calculate_ph



@login_required(login_url='customerLogin')
def home(request):
    token_obj, created = Token.objects.get_or_create(user=request.user)

    # ── HANDLE AJAX (REAL-TIME POLLING) ──
    if request.headers.get('X-Requested-With') == 'XMLHttpRequest':
        since_id = int(request.GET.get('since_id', 0))
        queryset = SensorData.objects.filter(
            user=request.user, id__gt=since_id
        ).order_by('-created')[:50]

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
    raw_sensor_data = SensorData.objects.filter(user=request.user).order_by('-created')[:100]

    for s in raw_sensor_data:
        s.ph = calculate_ph(
            float(s.red), float(s.green), float(s.blue),
            float(s.temp), float(s.lux)
        )

    context = {
        'api_token':  token_obj.key,
        'dataSensor': raw_sensor_data,
        'is_new':     created,
    }
    return render(request, 'index.html', context)

