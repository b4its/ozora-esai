# Cara Deploy Ozora ke VPS

## Struktur VPS
```
/root/deploy/ozora/
├── site/
│   ├── web_ozora/         # <-- Django project (copy-an dari origin)
│   │   ├── db.sqlite3
│   │   ├── staticfiles/   # hasil collectstatic
│   │   ├── media/
│   │   └── ozora.sock     # socket gunicorn
│   └── env/               # virtualenv
├── nginx/
│   └── ozora.b4its.cloud
└── systemd/
    └── ozora-gunicorn.service
```

## Langkah

### 1. Copy project ke VPS
```bash
scp -r /home/xmitsu/programming/python/eksperimen/ozora-esai/web_ozora root@srv1141370:/root/deploy/ozora/site/web_ozora
scp -r /home/xmitsu/programming/python/eksperimen/ozora-esai/env root@srv1141370:/root/deploy/ozora/site/env
```

### 2. SSH ke VPS
```bash
ssh root@srv1141370
cd /root/deploy/ozora/site/web_ozora
```

### 3. Edit settings.py
Ubah hal berikut di `web_ozora/settings.py`:
```
- DEBUG = True                   -> DEBUG = False
- ALLOWED_HOSTS = ['*']          -> ALLOWED_HOSTS = ['ozora.b4its.cloud']
- SECRET_KEY hardcode            -> SECRET_KEY = os.environ.get('DJANGO_SECRET_KEY')
- STATICFILES_DIRS               -> STATICFILES_DIRS = [BASE_DIR / 'static']
                                   STATIC_ROOT = BASE_DIR / 'staticfiles'
- (comment urlpatterns static)   -> di urls.py, comment baris static() dan media()
```

### 4. Setup systemd
```bash
cp deploy-vps/systemd/ozora-gunicorn.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now ozora-gunicorn
```

### 5. Setup nginx
```bash
cp deploy-vps/nginx/ozora.b4its.cloud /etc/nginx/sites-available/
ln -s /etc/nginx/sites-available/ozora.b4its.cloud /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl restart nginx
```

### 6. Migrate & collectstatic
```bash
source /root/deploy/ozora/site/env/bin/activate
cd /root/deploy/ozora/site/web_ozora
export DJANGO_SECRET_KEY='isi-dengan-random-key'
python manage.py migrate
python manage.py collectstatic --noinput
```

### 7. Restart & SSL
```bash
sudo systemctl restart ozora-gunicorn
certbot --nginx -d ozora.b4its.cloud
```

## Perbedaan settings.py (origin vs production)

| Setting              | Origin (lokal)          | Production (VPS)         |
|----------------------|-------------------------|--------------------------|
| DEBUG                | True                    | False                    |
| ALLOWED_HOSTS        | ['*']                   | ['ozora.b4its.cloud']    |
| SECRET_KEY           | hardcode                | env variable             |
| STATICFILES_DIRS     | `(BASE_DIR, 'static')`  | `[BASE_DIR / 'static']`  |
| STATIC_ROOT          | (comment)               | `BASE_DIR / 'staticfiles'` |
| urls.py static/media | aktif                   | comment/dihapus          |
