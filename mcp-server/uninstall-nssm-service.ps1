<#
Uninstall the NSSM-backed MCP service created by install-nssm-service.ps1

Usage (PowerShell, elevated recommended):
  .\uninstall-nssm-service.ps1 -ServiceName MCPServer

This script attempts to stop the service and then remove it using the nssm binary
found in a temp extraction (same location used by the installer).
#>

param(
  [string]$ServiceName = 'MCPServer'
)

Set-StrictMode -Version Latest

function Fail([string]$msg){
  Write-Error $msg; exit 1
}

# Try to find nssm on PATH first
$nssmExe = (Get-Command nssm -ErrorAction SilentlyContinue)?.Source
if(-not $nssmExe){
  # fallback to temp extraction pattern used by installer
  $extractDir = Join-Path $env:TEMP 'nssm-2.24'
  if(Test-Path $extractDir){
    $nssmExe = Get-ChildItem -Path $extractDir -Recurse -Filter 'nssm.exe' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
  }
}

if(-not $nssmExe){
  Write-Warning "nssm executable not found on PATH or in temp extraction. Please ensure nssm is available and run: nssm remove $ServiceName confirm"
  exit 2
}

Write-Host "Using nssm: $nssmExe"

# Stop if running
try{
  & $nssmExe stop $ServiceName 2>&1 | Out-Null
}catch{}

# Remove the service
& $nssmExe remove $ServiceName confirm
if($LASTEXITCODE -ne 0){
  Write-Warning "nssm remove reported exit code $LASTEXITCODE. You may need to remove the service manually."
} else {
  Write-Host "Service $ServiceName removed (nssm)."
}
