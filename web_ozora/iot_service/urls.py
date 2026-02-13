from django.urls import path
from .import views


urlpatterns = [
    
    path('receive-data/', views.receive_iot_data, name="receiveData"),

]