$b = @{}
Get-Content 'D:\lovebox-server-with-secrets\.env' | ForEach-Object {
  $i = $_.IndexOf('=')
  if ($i -gt 0) { $b[$_.Substring(0, $i)] = $_.Substring($i + 1) }
}
$h = @{ 'X-Lovebox-Passcode' = $b['LOVEBOX_PASSCODE'] }
try {
  $r = Invoke-RestMethod -Uri 'https://effervescent-scone-29511f.netlify.app/.netlify/functions/lovebox-health?deviceId=lovebox-001' -Headers $h
  if ($r.data) {
    $totalMb = [math]::Round($r.data.ffatTotal / 1MB, 1)
    $freeMb = [math]::Round(($r.data.ffatTotal - $r.data.ffatUsed) / 1MB, 1)
    Write-Output ("REPORT: fw=" + $r.data.firmwareVersion + " ffat=" + $totalMb + "MB free=" + $freeMb + "MB display=" + $r.data.displayReady + " touch=" + $r.data.touchReady + " servo=" + $r.data.servoReady)
  } else {
    Write-Output 'NO REPORT YET'
  }
} catch {
  Write-Output ('HTTP ' + $_.Exception.Response.StatusCode.value__)
}
