// Generates a valid 320x240 solid-color PNG and sends it to the Lovebox backend.
const fs = require('fs');
const zlib = require('zlib');

const W = 320, H = 240;

// Build raw RGBA rows
const raw = Buffer.alloc(H * (1 + W * 4));
for (let y = 0; y < H; y++) {
  const rowStart = y * (1 + W * 4);
  raw[rowStart] = 0; // filter none
  for (let x = 0; x < W; x++) {
    const o = rowStart + 1 + x * 4;
    raw[o] = 0xE6;     // R
    raw[o + 1] = 0x39; // G
    raw[o + 2] = 0x46; // B
    raw[o + 3] = 0xFF; // A
  }
}

function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const typeBuf = Buffer.from(type, 'ascii');
  const crcBuf = Buffer.concat([typeBuf, data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(zlib.crc32 ? zlib.crc32(crcBuf) : crc32(crcBuf));
  return Buffer.concat([len, typeBuf, data, crc]);
}

function crc32(buf) {
  let table = crc32.table;
  if (!table) {
    table = crc32.table = new Int32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      table[n] = c;
    }
  }
  let crc = -1;
  for (let i = 0; i < buf.length; i++) crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >>> 8);
  return (crc ^ -1) >>> 0;
}

const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(W, 0);
ihdr.writeUInt32BE(H, 4);
ihdr[8] = 8;  // bit depth
ihdr[9] = 6;  // color type RGBA

const png = Buffer.concat([
  Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
  chunk('IHDR', ihdr),
  chunk('IDAT', zlib.deflateSync(raw)),
  chunk('IEND', Buffer.alloc(0)),
]);

// Load env
const env = Object.fromEntries(
  fs.readFileSync('.env', 'utf8').split(/\r?\n/)
    .filter(l => l && !l.startsWith('#'))
    .map(l => { const i = l.indexOf('='); return [l.slice(0, i), l.slice(i + 1)]; })
);

(async () => {
  const form = new FormData();
  form.append('deviceId', 'lovebox-001');
  form.append('senderName', 'Lovebox Fix');
  form.append('caption', 'Screen restored!');
  form.append('image', new Blob([png], { type: 'image/png' }), 'restore.png');

  const res = await fetch('https://effervescent-scone-29511f.netlify.app/.netlify/functions/lovebox-send', {
    method: 'POST',
    headers: { 'X-Lovebox-Passcode': env.LOVEBOX_PASSCODE },
    body: form,
  });
  const text = await res.text();
  console.log('send HTTP', res.status);
  console.log(text);
  process.exit(res.ok ? 0 : 1);
})().catch(e => { console.error(e); process.exit(1); });
