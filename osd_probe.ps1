# Probe tombol Fn / WMI untuk Nitro OSD.  Jalankan sebagai Administrator:
#   powershell -ExecutionPolicy Bypass -File .\osd_probe.ps1
# Saat skrip meminta, tekan F4, F10, F11 (dan Fn+F4/F10/F11) beberapa kali.
$out = New-Object System.Collections.Generic.List[string]
function Log($t) { Write-Host $t; $out.Add([string]$t) }
Log ("Model: " + (Get-CimInstance Win32_ComputerSystem).Model + "  BIOS: " + (Get-CimInstance Win32_BIOS).SMBIOSBIOSVersion)

Log "`n== Kelas WMI (root/wmi) yang relevan: metode + parameter =="
$cl = Get-CimClass -Namespace root/wmi
foreach ($c in $cl) {
  $g = $c.CimClassQualifiers['guid']
  $isEvent = $c.CimSuperClassName -eq 'WmiEvent'
  if ($c.CimClassName -match 'Acer|Gaming|Keyboard|Backlight|Kbd|Predator|Nitro|Touch|Mic|Hotkey|Button|Illum|Light|LED' -or ($g -and $isEvent -and $c.CimClassName -notmatch '^(MSNdis|MSStor|MSFT|Win32|WMI|MSAcpi|MSPower|Processor|Thermal|BatteryStatus|Battery|MSSmBios|MSDm|Msvm)')) {
    Log ("Kelas: " + $c.CimClassName + "   guid=" + $g.Value + "   event=" + $isEvent)
    foreach ($m in $c.CimClassMethods) {
      $ps = @(); foreach ($p in $m.Parameters) { $d=@(); if($p.Qualifiers['in']){$d+='in'}; if($p.Qualifiers['out']){$d+='out'}; $ps += ($p.Name + ':' + $p.CimType + '(' + ($d -join '/') + ')') }
      Log ("    metode " + $m.Name + " id=" + $m.Qualifiers['WmiMethodId'].Value + "  " + ($ps -join ', '))
    }
  }
}

Log "`n== Perangkat touchpad / mikrofon =="
Get-PnpDevice -PresentOnly | Where-Object { $_.FriendlyName -match 'touch ?pad|Precision|I2C HID|Microphone|Mikrofon|Array' } |
  ForEach-Object { Log ($_.Class + " | " + $_.FriendlyName + " | " + $_.Status + " | " + $_.InstanceId) }

Log "`n== Mendengarkan event WMI selama 40 detik. TEKAN F4, F10, F11 dan Fn+F4/F10/F11 sekarang =="
$subs = @()
foreach ($c in $cl | Where-Object { $_.CimSuperClassName -eq 'WmiEvent' }) {
  if ($c.CimClassName -match '^(MSNdis|MSStor|MSFT|Win32|WMI|MSAcpi_|Processor|Thermal|MSPower|Msvm|BatteryStatus|MSSmBios)') { continue }
  try {
    $id = 'ev_' + $c.CimClassName
    Register-CimIndicationEvent -Namespace root/wmi -ClassName $c.CimClassName -SourceIdentifier $id -ErrorAction Stop | Out-Null
    $subs += $id
  } catch {}
}
Log ("Berlangganan ke " + $subs.Count + " kelas event")
$end = (Get-Date).AddSeconds(40)
while ((Get-Date) -lt $end) {
  $e = Wait-Event -Timeout 1
  if ($e) {
    $ne = $e.SourceEventArgs.NewEvent
    $props = ($ne.CimInstanceProperties | Where-Object { $_.Name -notmatch 'InstanceName|Active|TIME_CREATED' } | ForEach-Object { $_.Name + '=' + (($_.Value) -join ',') }) -join ' '
    Log ((Get-Date -Format 'HH:mm:ss.fff') + "  " + $e.SourceIdentifier + "  " + $props)
    Remove-Event -EventIdentifier $e.EventIdentifier
  }
}
$subs | ForEach-Object { Unregister-Event -SourceIdentifier $_ -ErrorAction SilentlyContinue }
$path = Join-Path $PSScriptRoot 'osd_probe_result.txt'
$out | Set-Content -Path $path -Encoding UTF8
Write-Host "`nDisimpan: $path"
