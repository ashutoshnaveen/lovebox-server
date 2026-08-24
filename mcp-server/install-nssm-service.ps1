<#
Install MCP server as a Windows service using NSSM (Non-Sucking Service Manager).

Usage (PowerShell, elevated recommended):
  cd 'D:\lovebox-server-with-secrets\mcp-server'
  .\install-nssm-service.ps1 -ServiceName MCPServer -AppDir 'D:\lovebox-server-with-secrets\mcp-server' -McpToken 'your-secret'

Parameters:
 -ServiceName: Name of the Windows service to create (default: MCPServer)
 -AppDir: Directory where index.js lives (default: this repo's mcp-server folder)
 -NodePath: Optional explicit path to node.exe. If omitted the script will try to locate node on PATH.
 -McpToken: Optional token to inject into the service environment (recommended)

This script downloads NSSM, extracts the correct win32/win64 binary, installs the service,
configures the AppDirectory, sets MCP_TOKEN in the service environment (if provided),
and starts the service.

Security note: run this locally; do not expose the service to untrusted networks.
#>

param(
  [string]$ServiceName = 'MCPServer',
  [string]$AppDir = 'D:\lovebox-server-with-secrets\mcp-server',
  [string]$NodePath = $( (Get-Command node -ErrorAction SilentlyContinue).Path ),
  [string]$McpToken = $env:MCP_TOKEN
)

Set-StrictMode -Version Latest

function Fail([string]$msg){
  Write-Error $msg
  exit 1
}

if(-not (Test-Path $AppDir)){
  Fail "AppDir '$AppDir' does not exist. Change -AppDir to the mcp-server folder."
}

if(-not $NodePath){
  # try common install location
  $common = 'C:\Program Files\nodejs\node.exe'
  if(Test-Path $common){ $NodePath = $common }
}

if(-not $NodePath -or -not (Test-Path $NodePath)){
  Fail "node.exe not found. Provide -NodePath or ensure node is on PATH and rerun in a new shell."
}

Write-Host "Using node: $NodePath"

# Decide NSSM arch
$osArch = (Get-CimInstance Win32_OperatingSystem).OSArchitecture
if($osArch -match '64') { $nssmArch = 'win64' } else { $nssmArch = 'win32' }

# Download NSSM
$nssmVersion = 'nssm-2.24'
$nssmUrl = "https://nssm.cc/release/$nssmVersion.zip"
$tempZip = Join-Path $env:TEMP "$nssmVersion.zip"
$extractDir = Join-Path $env:TEMP "$nssmVersion"

Write-Host "Downloading NSSM from $nssmUrl to $tempZip..."
try{
  Invoke-WebRequest -Uri $nssmUrl -OutFile $tempZip -UseBasicParsing -ErrorAction Stop
}catch{
  Fail "Failed to download NSSM: $($_.Exception.Message)"
}

# Clean and extract
if(Test-Path $extractDir){ Remove-Item -Recurse -Force $extractDir }
Expand-Archive -Path $tempZip -DestinationPath $extractDir -Force

# Locate nssm.exe
$nssmExe = Join-Path $extractDir "$nssmVersion\$nssmArch\nssm.exe"
if(-not (Test-Path $nssmExe)){
  # try alternate pattern
  $maybe = Get-ChildItem -Path $extractDir -Recurse -Filter 'nssm.exe' -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match $nssmArch } | Select-Object -First 1
  if($maybe){ $nssmExe = $maybe.FullName }
}
if(-not (Test-Path $nssmExe)){
  Fail "nssm.exe not found after extraction. Extract folder: $extractDir"
}

Write-Host "Found nssm: $nssmExe"

$nodeArgs = Join-Path $AppDir 'index.js'

# Install service
Write-Host "Installing service '$ServiceName' -> $NodePath $nodeArgs"
& $nssmExe install $ServiceName $NodePath $nodeArgs
if($LASTEXITCODE -ne 0){ Fail "nssm install failed (exit $LASTEXITCODE)" }

# Set working directory
& $nssmExe set $ServiceName AppDirectory $AppDir

# Configure environment token if provided
if($McpToken){
  Write-Host "Configuring MCP_TOKEN in service environment"
  & $nssmExe set $ServiceName AppEnvironmentExtra "MCP_TOKEN=$McpToken"
}

# Set service to automatic
& $nssmExe set $ServiceName Start SERVICE_AUTO_START

# Start the service
Write-Host "Starting service $ServiceName..."
& $nssmExe start $ServiceName

if($LASTEXITCODE -ne 0){
  Write-Warning "nssm reported non-zero exit code ($LASTEXITCODE) when starting the service. Check 'nssm status $ServiceName' or Windows Event Log."
} else {
  Write-Host "Service '$ServiceName' installed and started."
}

Write-Host "Temporary files are kept in $extractDir and $tempZip. You can remove them if desired."
