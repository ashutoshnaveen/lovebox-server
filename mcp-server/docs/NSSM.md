Running the MCP server as a Windows service with NSSM

This document explains how to use the provided helper scripts to install the MCP Node.js process as a Windows service using NSSM.

Files
- install-nssm-service.ps1  — Downloads NSSM, installs the service, sets AppDirectory, optionally configures MCP_TOKEN, and starts the service.
- uninstall-nssm-service.ps1 — Stops and removes the service using NSSM.

Quick steps
1) Open an elevated PowerShell (Run as Administrator).
2) Change to the project folder:
   cd 'D:\lovebox-server-with-secrets\mcp-server'
3) (Optional) set the MCP token in environment for the current shell so the installer can capture it:
   $env:MCP_TOKEN = 'your-strong-token'
4) Install the service (example):
   .\install-nssm-service.ps1 -ServiceName MCPServer -AppDir 'D:\lovebox-server-with-secrets\mcp-server' -McpToken 'your-strong-token'

What the installer does
- Downloads NSSM from the official site release (nssm-2.24.zip).
- Extracts the appropriate win32/win64 nssm.exe.
- Runs: nssm install <ServiceName> <node.exe> <index.js>
- Sets AppDirectory for the service to the mcp-server folder.
- Adds the MCP_TOKEN into the service environment (if provided) so the running Node process sees it.
- Sets the service start type to Automatic and attempts to start it.

Uninstalling
- To remove the service:
   .\uninstall-nssm-service.ps1 -ServiceName MCPServer

Notes & troubleshooting
- The script looks for node on PATH (Get-Command node). If node is not found, pass -NodePath to the installer pointing to node.exe.
- If the download fails due to network restrictions, download nssm manually and place nssm.exe on PATH or in the temp extraction path.
- NSSM keeps the service configuration in the Windows registry; removing with nssm remove <name> will clean it up.
- Check the Windows Event Log or NSSM's log if the service fails to start.

Security
- Keep the service bound to 127.0.0.1 in config.json by default and protect the MCP_TOKEN. Do not expose this service on untrusted networks without additional protection (TLS/mTLS/local firewall rules, etc.).

If desired next steps
- Add a scheduled restart policy or log rotation for stdout/stderr.
- Create an installer PowerShell script that sets up firewall rules to restrict access to localhost only (if binding to non-loopback is required).
