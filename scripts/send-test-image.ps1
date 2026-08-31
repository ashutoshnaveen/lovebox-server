Set-Location 'D:\lovebox-server-with-secrets'

# Load passcode from .env
$pass = (Get-Content .env | Where-Object { $_ -match '^LOVEBOX_PASSCODE=' }) -replace '^LOVEBOX_PASSCODE=', ''

# Small solid-color test PNG
$png = [Convert]::FromBase64String('iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAAAT0lEQVR42u3XMQEAIAzAsIF/z0MIBQ6Smuzudt9b7n3fJPDs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7D4G2QEBaRyBNwAAAABJRU5ErkJggg==')

$boundary = [Guid]::NewGuid().ToString()
$ms = New-Object System.IO.MemoryStream

function Add-Field($name, $value) {
  $sw = New-Object System.IO.StreamWriter($ms)
  $sw.Write("--$boundary`r`nContent-Disposition: form-data; name=`"$name`"`r`n`r`n$value`r`n")
  $sw.Flush()
}

Add-Field 'deviceId' 'lovebox-001'
Add-Field 'senderName' 'Lovebox Fix'
Add-Field 'caption' 'Screen restored!'

$sw = New-Object System.IO.StreamWriter($ms)
$sw.Write("--$boundary`r`nContent-Disposition: form-data; name=`"image`"; filename=`"test.png`"; type=`"image/png`"`r`n")
$sw.Flush()
$ms.Write($png, 0, $png.Length)
$sw = New-Object System.IO.StreamWriter($ms)
$sw.Write("`r`n--$boundary--`r`n")
$sw.Flush()

$result = Invoke-RestMethod -Uri 'https://effervescent-scone-29511f.netlify.app/.netlify/functions/lovebox-send' `
  -Method Post `
  -Headers @{ 'X-Lovebox-Passcode' = $pass } `
  -ContentType "multipart/form-data; boundary=$boundary" `
  -Body $ms.ToArray()

$result | ConvertTo-Json -Depth 5
