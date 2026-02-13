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
                messages.success(request,'Your registration process has been successful, please log in!')
                return redirect('customerLogin')

            else:
                messages.error(request,'Your password may not be the same or the password may not match the username!')
                return redirect('register')
    context = {
        'form':form
    }
    return render(request,'register.html',context)

def customerlogin (request):
    if request.user.is_authenticated:
        messages.info(request,"you're already login")
        return redirect('home')
    else:
        if request.method == 'post' or request.method == 'POST':
            username = request.POST.get('username')    
            password = request.POST.get('password')    
            customer = authenticate(request,username=username,password=password)
            if customer is not None:
                login(request,customer)
                messages.info(request,'Welcome '+str(request.user)+'!')
                return redirect('home')
            else:
                messages.error(request,"Username or Password that you've entered is incorrect.!")
                return redirect('customerLogin')

    return render(request,'login.html')

def logout_view(request):
    # Proses logout user
    logout(request)
    
    # Berikan pesan sukses, bukan error
    messages.info(request, "please login to your account")
    
    # Arahkan kembali ke halaman login atau beranda
    return redirect("customerLogin")