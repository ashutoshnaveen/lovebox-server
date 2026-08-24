MCP Server (local, Node.js)

Purpose
- Provide a minimal, local-only MCP server that accepts exec requests and runs whitelisted commands.
- Admin endpoints allow adding/removing/listing whitelist entries.

Security first (please read)
- This server executes commands on your machine. Do NOT expose it to the public internet.
- Default config binds to 127.0.0.1. Keep it that way unless you fully understand the security implications.
- Protect the admin token. Either set MCP_TOKEN env or update config.json.
- The whitelist controls what can be run. Start with an empty whitelist and only add safe commands with known behavior.

Quick start (Windows)
1) Install Node.js (v16+): https://nodejs.org/
2) Open PowerShell and run:
   cd "d:\\lovebox-server-with-secrets\\mcp-server"
   npm install
3) Edit config.json and change token from "change-me" to a strong secret OR set environment variable:
   $env:MCP_TOKEN = 'your-secret'
4) Run the server:
   node index.js

Using the CLI to manage whitelist
- The small CLI calls the admin endpoints. Example:
   node manage-whitelist.js add git --allowArgs=true --useShell=false --token=your-secret
   node manage-whitelist.js list --token=your-secret
   node manage-whitelist.js remove git --token=your-secret

Exec endpoint (for agent integration)
- POST http://127.0.0.1:8765/exec with JSON body { "command": "git", "args": ["status"] }
- The server will only run if the base command is whitelisted and either allowArgs=true or no args are provided.
- Example using curl:
   curl -X POST "http://127.0.0.1:8765/exec" -H "Content-Type: application/json" -d "{\"command\":\"whoami\"}"

Admin endpoints (require x-mcp-token header)
- GET /whitelist/list
- POST /whitelist/add {cmd, allowArgs, useShell, description}
- POST /whitelist/remove {cmd}
- POST /whitelist/reload
- POST /config/update {port, host, token} (use carefully)

Running as a Windows service
- Tools like NSSM (https://nssm.cc/) or node-windows can be used to run node index.js as a service.

Extending / hardening suggestions
- Use TLS and mutual TLS if you must bind to a non-local interface.
- Use OS-level auth or integrated Windows auth for admin operations where possible.
- Add logging and rate-limiting.
- Consider restricting commands to absolute paths to avoid path hijacking.

If you'd like, next steps I can do for you
- Add an example VS Code task or extension snippet that calls /exec when the agent needs to run a command.
- Add service wrapper instructions (NSSM) and a sample PowerShell script to install/uninstall the service.
- Harden whitelist matching (regex support, explicit arg patterns).

