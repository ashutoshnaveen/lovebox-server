/* MCP Server (minimal)
 * - Listens only on configured host (default 127.0.0.1) and port
 * - Uses a token (env MCP_TOKEN or config.json) for admin endpoints
 * - Whitelist lives in whitelist.json and can be modified via admin endpoints
 * - Exec requests must be explicitly whitelisted
 *
 * Security notes (read README.md): run locally, protect token, only add safe commands
 */

const express = require('express');
const fs = require('fs');
const fsp = fs.promises;
const path = require('path');
const { spawn } = require('child_process');

const ROOT = path.join(__dirname);
const CONFIG_PATH = path.join(ROOT, 'config.json');
const WHITELIST_PATH = path.join(ROOT, 'whitelist.json');

let config = {
  host: '127.0.0.1',
  port: 8765,
  token: process.env.MCP_TOKEN || 'change-me'
};

let whitelist = [];

async function loadConfig(){
  try{
    const raw = await fsp.readFile(CONFIG_PATH, 'utf8');
    const c = JSON.parse(raw);
    config = Object.assign(config, c);
    if (process.env.MCP_TOKEN) config.token = process.env.MCP_TOKEN; // env overrides
    console.log('Loaded config from', CONFIG_PATH);
  }catch(e){
    console.log('Using default config (no config.json present)');
  }
}

async function loadWhitelist(){
  try{
    const raw = await fsp.readFile(WHITELIST_PATH, 'utf8');
    whitelist = JSON.parse(raw) || [];
    console.log('Loaded whitelist (', whitelist.length, 'entries )');
  }catch(e){
    console.log('No whitelist.json found — starting with empty whitelist');
    whitelist = [];
  }
}

async function saveWhitelist(){
  await fsp.writeFile(WHITELIST_PATH, JSON.stringify(whitelist, null, 2), 'utf8');
}

function requireToken(req){
  const t = req.get('x-mcp-token') || req.query.token;
  return t && t === config.token;
}

function findWhitelistEntry(command){
  // command is the base command, e.g. 'git' or 'powershell'
  return whitelist.find(e => e.cmd === command || (e.cmd.endsWith('*') && command.startsWith(e.cmd.slice(0,-1))));
}

function isAllowed(command, args){
  // command: base command string, args: array
  const entry = findWhitelistEntry(command);
  if(!entry) return null;
  if(entry.allowArgs) return entry;
  // if not allowArgs, only allow if no args provided
  if(!args || args.length === 0) return entry;
  return null;
}

async function startServer(){
  await loadConfig();
  await loadWhitelist();

  const app = express();
  app.use(express.json({limit: '1mb'}));

  // Simple health
  app.get('/_health', (req,res)=> res.json({ok:true, host: config.host, port: config.port}));

  // Exec endpoint: {command: "git", args: ["status"]}
  app.post('/exec', async (req,res)=>{
    const { command, args } = req.body || {};
    if(!command || typeof command !== 'string') return res.status(400).json({error:'command required (string)'});
    const parsedArgs = Array.isArray(args) ? args : [];
    const entry = isAllowed(command, parsedArgs);
    if(!entry) return res.status(403).json({error:'command not allowed by whitelist'});

    try{
      // For safety, if entry.useShell is true, spawn with shell, otherwise without
      const useShell = !!entry.useShell;
      const child = spawn(command, parsedArgs, { shell: useShell });
      let stdout='';
      let stderr='';
      child.stdout.on('data', d=> { stdout += d.toString(); });
      child.stderr.on('data', d=> { stderr += d.toString(); });
      child.on('error', err=>{
        return res.status(500).json({error: 'failed to start command', detail: err.message});
      });
      child.on('close', code=>{
        res.json({code, stdout, stderr});
      });
    }catch(err){
      res.status(500).json({error: err.message});
    }
  });

  // Admin endpoints (require token in header x-mcp-token or ?token=)
  app.get('/whitelist/list', (req,res)=>{
    if(!requireToken(req)) return res.status(401).json({error:'invalid token'});
    res.json(whitelist);
  });

  app.post('/whitelist/add', async (req,res)=>{
    if(!requireToken(req)) return res.status(401).json({error:'invalid token'});
    const e = req.body || {};
    if(!e.cmd || typeof e.cmd !== 'string') return res.status(400).json({error:'cmd required'});
    // normalize
    const entry = {
      cmd: e.cmd,
      allowArgs: !!e.allowArgs,
      useShell: !!e.useShell,
      description: e.description || ''
    };
    // avoid duplicates
    const existingIndex = whitelist.findIndex(w => w.cmd === entry.cmd);
    if(existingIndex !== -1){
      whitelist[existingIndex] = entry;
    }else{
      whitelist.push(entry);
    }
    await saveWhitelist();
    res.json({ok:true, entry});
  });

  app.post('/whitelist/remove', async (req,res)=>{
    if(!requireToken(req)) return res.status(401).json({error:'invalid token'});
    const { cmd } = req.body || {};
    if(!cmd) return res.status(400).json({error:'cmd required'});
    const before = whitelist.length;
    whitelist = whitelist.filter(w => w.cmd !== cmd);
    await saveWhitelist();
    res.json({ok:true, removed: before - whitelist.length});
  });

  app.post('/whitelist/reload', async (req,res)=>{
    if(!requireToken(req)) return res.status(401).json({error:'invalid token'});
    await loadWhitelist();
    res.json({ok:true, count: whitelist.length});
  });

  // Optionally allow updating config (token/port) via token — use cautiously
  app.post('/config/update', async (req,res)=>{
    if(!requireToken(req)) return res.status(401).json({error:'invalid token'});
    const allowed = ['port','host','token'];
    const body = req.body || {};
    for(const k of allowed){
      if(k in body) config[k] = body[k];
    }
    await fsp.writeFile(CONFIG_PATH, JSON.stringify(config, null, 2), 'utf8');
    res.json({ok:true, config});
  });

  app.listen(config.port, config.host, ()=>{
    console.log(`MCP server listening on http://${config.host}:${config.port}`);
    console.log('Protect the token in config.json or set MCP_TOKEN environment variable');
  });
}

startServer().catch(err=>{
  console.error('Failed to start server:', err);
  process.exit(1);
});
