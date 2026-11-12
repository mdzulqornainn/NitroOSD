NITRO OSD 1.1  (Acer Nitro Lite 16)

OSD ubin ikon putih (teks bahasa Inggris) untuk: Caps Lock, Num Lock, Microphone, Touchpad, Backlight.
Klik ikon keycap di tray untuk membuka jendela pengaturan (Keys, durasi, posisi, start at login).

CAPS / NUM LOCK   Otomatis. Caps = tuts huruf (A / a), Num = keypad 3x3.
MICROPHONE        Mute semua mikrofon aktif (Windows Core Audio). Pelajari tombol F4 lewat Settings > Learn.
TOUCHPAD          Tombol F10 dikirim firmware sebagai event WMI Acer (APGeEvent 1,131,x) dan sudah dikenali.
                  Toggle lewat SetupAPI (cepat), butuh Administrator.
BACKLIGHT         Di Nitro NL16-71G antarmuka WMI backlight mengembalikan nol, jadi level tidak bisa dibaca/diatur.
                  OSD hanya muncul bila tombolnya terdeteksi (Learn / Key diagnostics).
HOTKEY CADANGAN   Ctrl+Alt+M = mic, Ctrl+Alt+T = touchpad, Ctrl+Alt+B = backlight.
Pulihkan          NitroOSD.exe --restore  (aktifkan touchpad & unmute mic)
Baris perintah    --tray  --demo  --settings  --restore  --task-on  --task-off
Setelan & log     %APPDATA%\NitroOSD\settings.ini , keys.log

[Touchpad v1.1.1]
- Memakai SetupAPI (cepat) dengan fallback pnputil; status diverifikasi lewat status perangkat.
- Log alur ada di %APPDATA%\NitroOSD\touch.log. Jika masih tidak sinkron, tekan F10 beberapa kali lalu kirim file ini.

[Touchpad v1.1.2]
- F10 dijalankan firmware Acer (tanpa delay). Event WMI membawa keadaannya: EventDetail[2] = 0 -> touchpad MATI, 2 -> NYALA.
- Aplikasi tidak lagi menyentuh perangkat touchpad; ia hanya menampilkan OSD sesuai event itu. Log: %APPDATA%\NitroOSD\touch.log
- touchpad_probe.ps1 dapat dipakai untuk memantau sinyal touchpad.
