from django.apps import AppConfig


class AccountsConfig(AppConfig):
    default_auto_field = 'django.db.models.BigAutoField'
    name = 'account' # Sesuaikan dengan nama aplikasi kamu

    def ready(self):
        # Impor sinyal di sini agar aktif saat Django start
        import account.models  # Jika kode ada di models.py