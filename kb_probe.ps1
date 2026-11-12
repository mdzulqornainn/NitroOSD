# Probe backlight keyboard Acer (HANYA MEMBACA, tidak mengubah apa pun).
# Jalankan sebagai Administrator:
#   powershell -ExecutionPolicy Bypass -File .\kb_probe.ps1
# Skrip akan meminta kamu mengatur backlight (Mati, Level 1, Level 2) dengan Fn+F11,
# lalu membaca nilai mentahnya untuk tiap level.
$out = New-Object System.Collections.Generic.List[string]
function Log($t) { Write-Host $t; $out.Add([string]$t) }
$cls = 'AcerGamingFunction'
$inst = Get-CimInstance -Namespace root/wmi -ClassName $cls | Select-Object -First 1
if (-not $inst) { Log "Kelas $cls tidak ditemukan"; exit }
function Read-KB($inp) {
  try {
    $r = Invoke-CimMethod -InputObject $inst -MethodName GetGamingKBBacklight -Arguments @{ gmInput = [uint32]$inp }
    return ('gmInput={0}  out=[{1}]  ret={2}' -f $inp, (($r.gmOutput | ForEach-Object { '{0:X2}' -f $_ }) -join ' '), $r.gmReturn)
  } catch { return ('gmInput={0}  GALAT: {1}' -f $inp, $_.Exception.Message) }
}
function Read-LED($inp) {
  try {
    $r = Invoke-CimMethod -InputObject $inst -MethodName GetGamingLED -Arguments @{ gmInput = [uint32]$inp }
    return ('LED gmInput={0}  out=[{1}]  ret={2}' -f $inp, (($r.gmOutput | ForEach-Object { '{0:X2}' -f $_ }) -join ' '), $r.gmReturn)
  } catch { return ('LED gmInput={0}  GALAT: {1}' -f $inp, $_.Exception.Message) }
}
Log ("Model: " + (Get-CimInstance Win32_ComputerSystem).Model)
foreach ($stage in @('MATI', 'LEVEL 1', 'LEVEL 2', 'MATI (lagi)')) {
  Read-Host "`nAtur backlight keyboard ke [$stage] dengan Fn+F11, lalu tekan Enter di sini"
  Log "=== Backlight: $stage ==="
  foreach ($i in 0, 1, 2, 3) { Log (Read-KB $i) }
  foreach ($i in 0, 1) { Log (Read-LED $i) }
}
$path = Join-Path $PSScriptRoot 'kb_probe_result.txt'
$out | Set-Content -Path $path -Encoding UTF8
Write-Host "`nDisimpan: $path"
