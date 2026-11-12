# touchpad_probe.ps1 - memantau SEMUA sinyal touchpad dan mencatat mana yang berubah saat F10 ditekan / toggle Settings.
# Jalankan (disarankan PowerShell sebagai Administrator):
#   powershell -ExecutionPolicy Bypass -File touchpad_probe.ps1
# Lalu: tekan F10 beberapa kali, matikan/nyalakan touchpad lewat Settings (Win+I > Bluetooth & devices > Touchpad),
# gesek jari di touchpad. Berhenti otomatis setelah -Seconds detik (default 90) atau Ctrl+C.
# Hasil juga disimpan di %APPDATA%\NitroOSD\touchpad_probe.log
param([int]$Seconds = 90)

$ErrorActionPreference = 'SilentlyContinue'
$dir = Join-Path $env:APPDATA 'NitroOSD'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$log = Join-Path $dir 'touchpad_probe.log'
Set-Content -Path $log -Value "touchpad_probe $(Get-Date -Format o)" -Encoding UTF8

function Out2([string]$t) {
    $line = "{0}  {1}" -f (Get-Date -Format 'HH:mm:ss.fff'), $t
    Write-Host $line
    Add-Content -Path $log -Value $line -Encoding UTF8
}

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class TPProbe {
    [DllImport("user32.dll", SetLastError=true)]
    static extern bool SystemParametersInfo(uint a, uint b, byte[] p, uint f);
    [StructLayout(LayoutKind.Sequential)]
    struct RAWINPUTDEVICELIST { public IntPtr hDevice; public uint dwType; }
    [DllImport("user32.dll", SetLastError=true)]
    static extern uint GetRawInputDeviceList(IntPtr list, ref uint n, uint sz);
    [DllImport("user32.dll", SetLastError=true)]
    static extern uint GetRawInputDeviceList([Out] RAWINPUTDEVICELIST[] list, ref uint n, uint sz);
    [DllImport("user32.dll", SetLastError=true)]
    static extern uint GetRawInputDeviceInfo(IntPtr h, uint cmd, IntPtr data, ref uint sz);

    public static string Spi() {
        for (uint sz = 32; sz <= 200; sz += 4) {
            byte[] b = new byte[512];
            BitConverter.GetBytes((uint)1).CopyTo(b, 0);
            if (SystemParametersInfo(0xAE, sz, b, 0)) {
                uint bits = BitConverter.ToUInt32(b, 12);
                return string.Format("present={0} enabled={1} active={2} bits=0x{3:X}", bits & 1, (bits >> 3) & 1, (bits >> 4) & 1, bits);
            }
        }
        return "tidak tersedia";
    }

    public static int RawTouchpads() {
        uint n = 0, sz = (uint)Marshal.SizeOf(typeof(RAWINPUTDEVICELIST));
        if (GetRawInputDeviceList(IntPtr.Zero, ref n, sz) == uint.MaxValue || n == 0) return 0;
        RAWINPUTDEVICELIST[] l = new RAWINPUTDEVICELIST[n];
        if (GetRawInputDeviceList(l, ref n, sz) == uint.MaxValue) return 0;
        int cnt = 0;
        IntPtr buf = Marshal.AllocHGlobal(64);
        for (int i = 0; i < n; i++) {
            if (l[i].dwType != 2) continue;  // RIM_TYPEHID
            Marshal.WriteInt32(buf, 0, 32);
            uint cb = 32;
            if (GetRawInputDeviceInfo(l[i].hDevice, 0x2000000b, buf, ref cb) == uint.MaxValue) continue;
            int page = (ushort)Marshal.ReadInt16(buf, 20), usage = (ushort)Marshal.ReadInt16(buf, 22);
            if (page == 0x0D && usage == 0x05) cnt++;
        }
        Marshal.FreeHGlobal(buf);
        return cnt;
    }
}
"@

function Get-Signals {
    $s = [ordered]@{}
    $s['SPI(Windows touchpad)'] = [TPProbe]::Spi()
    $s['RawInput(jumlah touchpad)'] = [TPProbe]::RawTouchpads()
    $regs = @()
    foreach ($p in 'HKCU:\Software\Microsoft\Windows\CurrentVersion\PrecisionTouchPad', 'HKCU:\Software\Microsoft\Windows\CurrentVersion\PrecisionTouchPad\Status') {
        $k = Get-ItemProperty -Path $p
        if ($k) { foreach ($pr in $k.PSObject.Properties) { if ($pr.Name -notlike 'PS*') { $regs += ("{0}\{1}={2}" -f ($p -replace '^.*CurrentVersion\\', ''), $pr.Name, $pr.Value) } } }
    }
    $s['Registry'] = ($regs | Sort-Object) -join ' | '
    $dev = Get-PnpDevice -PresentOnly | Where-Object { ($_.HardwareID -join ' ') -match 'UP:000D_U:0005' }
    $s['Device(PnP)'] = if ($dev) { ($dev | ForEach-Object { "{0} status={1} err={2}" -f $_.InstanceId, $_.Status, $_.ConfigManagerErrorCode }) -join ' ; ' } else { 'tidak ditemukan' }
    $s['Tombol Fn(mouse/keyboard ext)'] = (Get-PnpDevice -Class Mouse -PresentOnly | Select-Object -ExpandProperty FriendlyName) -join ', '
    return $s
}

Out2 "=== Snapshot awal ==="
$prev = Get-Signals
foreach ($k in $prev.Keys) { Out2 ("{0}: {1}" -f $k, $prev[$k]) }

$wmiOk = $false
try {
    Register-WmiEvent -Namespace 'root\wmi' -Class 'APGeEvent' -SourceIdentifier 'tpprobe_ge' -ErrorAction Stop | Out-Null
    $wmiOk = $true
    Out2 "WMI APGeEvent: ok (event tombol Acer akan dicatat)"
} catch { Out2 "WMI APGeEvent tidak bisa didaftarkan (coba jalankan sebagai Administrator): $($_.Exception.Message)" }

Out2 "=== Pemantauan $Seconds detik: tekan F10 / toggle Settings / gesek touchpad ==="
$end = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $end) {
    if ($wmiOk) {
        $ev = Get-Event -SourceIdentifier 'tpprobe_ge'
        foreach ($e in $ev) {
            $d = $e.SourceEventArgs.NewEvent.EventDetail
            Out2 ("*** EVENT Acer: EventDetail = " + ($d -join ','))
            Remove-Event -EventIdentifier $e.EventIdentifier
        }
    }
    $cur = Get-Signals
    foreach ($k in $cur.Keys) {
        if ("$($cur[$k])" -ne "$($prev[$k])") {
            Out2 ("UBAH  {0}:`n        sebelum: {1}`n        sesudah: {2}" -f $k, $prev[$k], $cur[$k])
        }
    }
    $prev = $cur
    Start-Sleep -Milliseconds 150
}
if ($wmiOk) { Unregister-Event -SourceIdentifier 'tpprobe_ge' }
Out2 "=== Selesai. Kirim file: $log ==="
