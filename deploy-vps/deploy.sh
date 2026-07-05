#!/bin/bash
# Deploy Ozora ke VPS - jalankan dari folder ini
# Usage: ./deploy.sh

set -e

VPS_USER="root"
VPS_HOST="srv1141370"
VPS_PATH="/root/deploy/ozora/site"

echo "=== Copy project ke VPS ==="
scp -r ../web_ozora "$VPS_USER@$VPS_HOST:$VPS_PATH/"
scp -r ../env "$VPS_USER@$VPS_HOST:$VPS_PATH/"

echo "=== Copy konfigurasi ==="
scp nginx/ozora.b4its.cloud "$VPS_USER@$VPS_HOST:/etc/nginx/sites-available/"
scp systemd/ozora-gunicorn.service "$VPS_USER@$VPS_HOST:/etc/systemd/system/"

echo "=== Eksekusi di VPS ==="
ssh "$VPS_USER@$VPS_HOST" << 'EOF'
    set -e

    # systemd
    systemctl daemon-reload
    systemctl enable --now ozora-gunicorn

    # nginx
    ln -sf /etc/nginx/sites-available/ozora.b4its.cloud /etc/nginx/sites-enabled/
    nginx -t && systemctl restart nginx

    # django setup
    source /root/deploy/ozora/site/env/bin/activate
    cd /root/deploy/ozora/site/web_ozora
    export DJANGO_SECRET_KEY=$(python -c "import secrets; print(secrets.token_urlsafe(50))")

    python manage.py migrate
    python manage.py collectstatic --noinput

    systemctl restart ozora-gunicorn

    # ssl
    certbot --nginx -d ozora.b4its.cloud --non-interactive --agree-tos -m admin@ozora.b4its.cloud || true

    echo "=== Deploy selesai ==="
EOF
