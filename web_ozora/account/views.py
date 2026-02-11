from django.shortcuts import render,redirect
from django.http import HttpResponse
from .forms import RegistrationForm 
# Create your views here.
from django.contrib import messages

# authenticated function
from django.contrib.auth.decorators import login_required
from django.contrib.auth import login,logout,authenticate

def register(request):
    if request.user.is_authenticated:
        return HttpResponse("you're authenticated ")
    else:
        form = RegistrationForm()
        if request.method == 'post' or request.method == 'POST':
            form = RegistrationForm(request.POST)
            if form.is_valid():
                form.save()
                messages.success(request,'Proses registrasi anda telah berhasil, silahkan untuk login!')
                return redirect('customerlogin')

            else:
                messages.error(request,'Password kamu mungkin tidak sama atau password sesuai dengan username!')
                return redirect('register')
    context = {
        'form':form
    }
    return render(request,'register.html',context)

def customerlogin (request):
    if request.user.is_authenticated:
        return HttpResponse("you're already login ")
    else:
        if request.method == 'post' or request.method == 'POST':
            username = request.POST.get('username')    
            password = request.POST.get('password')    
            customer = authenticate(request,username=username,password=password)
            if customer is not None:
                login(request,customer)
                messages.info(request,'Selamat Datang '+str(request.user)+'!')
                return redirect('home')
            else:
                messages.error(request,'Username atau password kamu salah!')
                return redirect('home')

    return render(request,'login.html')

@login_required
def logout_view(request):
	if request.method =="POST":
		logout(request)
		return redirect("customerLogin")
	return render(request, "logout.html")