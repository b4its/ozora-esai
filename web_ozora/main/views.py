from django.shortcuts import render
from django.contrib.auth.decorators import login_required
from rest_framework.authtoken.models import Token

@login_required # Memastikan hanya user login yang bisa lihat token
def home(request):
    # Logika: Ambil token milik user yang sedang login (request.user)
    # Jika belum punya token, kita buatkan secara otomatis (get_or_create)
    token_obj, created = Token.objects.get_or_create(user=request.user)
    
    context = {
        'api_token': token_obj.key,
        'is_new': created
    }
    
    return render(request, 'index.html', context)   