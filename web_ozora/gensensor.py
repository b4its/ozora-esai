import requests
import time
import random
import socket

def get_my_ip():
    """Fungsi untuk mengambil IP Address lokal perangkat ini secara otomatis"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # Mencoba menyambung ke IP publik (tidak benar-benar mengirim data) 
        # untuk melihat interface mana yang aktif
        s.connect(('8.8.8.8', 1))
        ip = s.getsockname()[0]
    except Exception:
        ip = '127.0.0.1'
    finally:
        s.close()
    return ip

# Mengambil IP otomatis
my_ip = get_my_ip()

# Memasukkan IP otomatis ke dalam string URL
url = f"http://{my_ip}:8000/api/receive-data/"

headers = {
    "Authorization": "Token f62c86fa011bf25b8eab00129c9d56e92a3623e2",
    "Content-Type": "application/json"
}

print(f"Target URL otomatis: {url}")

for i in range(1, 21):
    # Membuat data sensor acak agar simulasi lebih nyata
    payload = {
        "raw_light": random.randint(400, 600),
        "red": random.randint(0, 255),
        "green": random.randint(0, 255),
        "blue": random.randint(0, 255),
        "temp": round(random.uniform(25.0, 30.0), 1),
        "lux": random.randint(200, 500)
    }
    
    try:
        response = requests.post(url, json=payload, headers=headers)
        
        if response.status_code == 201:
            print(f"[{i}] Sukses: {response.json().get('message')}")
        else:
            print(f"[{i}] Gagal: {response.status_code} - {response.text}")
    except requests.exceptions.ConnectionError:
        print(f"[{i}] Gagal: Tidak bisa terhubung ke {url}. Pastikan server API sudah jalan.")
    
    # Beri jeda 1 detik tiap pengiriman agar server tidak kaget
    time.sleep(1)