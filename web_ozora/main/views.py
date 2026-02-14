from django.shortcuts import render
from django.contrib.auth.decorators import login_required
from rest_framework.authtoken.models import Token

@login_required # Memastikan hanya user login yang bisa lihat token
def home(request):
    # get_or_create mengembalikan tuple: (objek, boolean_apakah_baru)
    token_obj, created = Token.objects.get_or_create(user=request.user)
    
    # Kita tidak butuh pengecekan 'if token_obj is None' karena 
    # get_or_create pasti mengembalikan objek token atau raise error.
    
    context = {
        'api_token': token_obj.key,
        'is_new': created
    }
    
    return render(request, 'index.html', context)