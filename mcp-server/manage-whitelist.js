/* CLI for managing whitelist via admin endpoints
   Usage examples:
     node manage-whitelist.js add git --allowArgs=true --useShell=false --token=change-me
     node manage-whitelist.js remove git --token=change-me
     node manage-whitelist.js list --token=change-me
   The server must be running locally. Token can be passed with --token or via env MCP_TOKEN.
*/

const http = require('http');
const { URL } = require('url');

function parseArgs(){
  const argv = process.argv.slice(2);
  const out = { _: [] };
  for(const a of argv){
    if(a.startsWith('--')){
      const [k,v] = a.slice(2).split('=');
      out[k] = typeof v === 'undefined' ? true : v;
    }else{
      out._.push(a);
    }
  }
  return out;
}

async function request(path, method='GET', body=null, token){
  const url = new URL(path);
  const opts = {
    hostname: url.hostname,
    port: url.port,
    path: url.pathname + (url.search || ''),
    method,
    headers: {
      'Content-Type': 'application/json'
    }
  };
  if(token) opts.headers['x-mcp-token'] = token;

  return new Promise((resolve, reject)=>{
    const req = http.request(opts, res=>{
      let data='';
      res.on('data', d=> data += d.toString());
      res.on('end', ()=>{
        try{ resolve(JSON.parse(data || '{}')); }
        catch(e){ resolve(data); }
      });
    });
    req.on('error', reject);
    if(body) req.write(JSON.stringify(body));
    req.end();
  });
}

(async ()=>{
  const a = parseArgs();
  const cmd = a._[0];
  const server = a.server || 'http://127.0.0.1:8765';
  const token = a.token || process.env.MCP_TOKEN;
  if(!cmd){ console.error('Command required: add|remove|list'); process.exit(2); }

  try{
    if(cmd === 'list'){
      const r = await request(server + '/whitelist/list', 'GET', null, token);
      console.log(JSON.stringify(r, null, 2));
    }else if(cmd === 'add'){
      const cmdName = a._[1];
      if(!cmdName){ console.error('usage: add <cmd> [--allowArgs=true] [--useShell=true]'); process.exit(2); }
      const body = { cmd: cmdName, allowArgs: a.allowArgs === 'true' || a.allowArgs === true, useShell: a.useShell === 'true' || a.useShell === true, description: a.description || '' };
      const r = await request(server + '/whitelist/add', 'POST', body, token);
      console.log(JSON.stringify(r, null, 2));
    }else if(cmd === 'remove'){
      const cmdName = a._[1];
      if(!cmdName){ console.error('usage: remove <cmd>'); process.exit(2); }
      const body = { cmd: cmdName };
      const r = await request(server + '/whitelist/remove', 'POST', body, token);
      console.log(JSON.stringify(r, null, 2));
    }else{
      console.error('unknown command', cmd); process.exit(2);
    }
  }catch(err){
    console.error('Request failed:', err.message || err);
    process.exit(1);
  }
})();
