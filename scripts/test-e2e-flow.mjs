import fs from 'node:fs';
import zlib from 'node:zlib';

// ---------------------------------------------------------------------------
// 1. Helpers for generating test assets (PNG image + PCM WAV audio)
// ---------------------------------------------------------------------------
function generateTestPng(width = 320, height = 240) {
  const raw = Buffer.alloc(height * (1 + width * 4));
  for (let y = 0; y < height; y++) {
    const rowStart = y * (1 + width * 4);
    raw[rowStart] = 0; // Filter: none
    for (let x = 0; x < width; x++) {
      const o = rowStart + 1 + x * 4;
      raw[o] = 255;     // R
      raw[o + 1] = 77;  // G
      raw[o + 2] = 109; // B
      raw[o + 3] = 255; // A
    }
  }

  function chunk(type, data) {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(data.length);
    const typeBuf = Buffer.from(type, 'ascii');
    const crcBuf = Buffer.concat([typeBuf, data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(zlib.crc32(crcBuf));
    return Buffer.concat([len, typeBuf, data, crc]);
  }

  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; // Bit depth
  ihdr[9] = 6; // Color type RGBA

  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw)),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

function generateTestWav(durationSeconds = 0.5, sampleRate = 16000) {
  const numSamples = Math.floor(durationSeconds * sampleRate);
  const dataSize = numSamples * 2; // 16-bit mono = 2 bytes per sample
  const buffer = Buffer.alloc(44 + dataSize);

  // RIFF header
  buffer.write('RIFF', 0);
  buffer.writeUInt32LE(36 + dataSize, 4);
  buffer.write('WAVE', 8);
  buffer.write('fmt ', 12);
  buffer.writeUInt32LE(16, 16);          // Subchunk1Size
  buffer.writeUInt16LE(1, 20);           // AudioFormat PCM
  buffer.writeUInt16LE(1, 22);           // NumChannels (mono)
  buffer.writeUInt32LE(sampleRate, 24);  // SampleRate
  buffer.writeUInt32LE(sampleRate * 2, 28); // ByteRate
  buffer.writeUInt16LE(2, 32);           // BlockAlign
  buffer.writeUInt16LE(16, 34);          // BitsPerSample
  buffer.write('data', 36);
  buffer.writeUInt32LE(dataSize, 40);

  // Sine wave 440 Hz
  for (let i = 0; i < numSamples; i++) {
    const t = i / sampleRate;
    const sample = Math.sin(2 * Math.PI * 440 * t);
    const intSample = Math.floor(sample * 32767);
    buffer.writeInt16LE(intSample, 44 + i * 2);
  }

  return buffer;
}

// ---------------------------------------------------------------------------
// 2. Load environment config
// ---------------------------------------------------------------------------
const env = Object.fromEntries(
  fs.readFileSync('.env', 'utf8')
    .split(/\r?\n/)
    .filter((line) => line && !line.startsWith('#'))
    .map((line) => {
      const idx = line.indexOf('=');
      return [line.slice(0, idx).trim(), line.slice(idx + 1).trim()];
    })
);

const BASE_URL = 'https://effervescent-scone-29511f.netlify.app';
const DEVICE_ID = 'lovebox-001';
const PASSCODE = env.LOVEBOX_PASSCODE;
const DEVICE_KEY = env.DEVICE_KEY_LOVEBOX_001;

console.log('='.repeat(70));
console.log(`Starting Lovebox End-to-End Test Suite`);
console.log(`Target: ${BASE_URL}`);
console.log(`Device: ${DEVICE_ID}`);
console.log('='.repeat(70));

let passed = 0;
let failed = 0;

function assert(condition, message) {
  if (condition) {
    console.log(`  [PASS] ${message}`);
    passed++;
  } else {
    console.error(`  [FAIL] ${message}`);
    failed++;
  }
}

// ---------------------------------------------------------------------------
// 3. Test Runner
// ---------------------------------------------------------------------------
async function runTests() {
  try {
    // -----------------------------------------------------------------------
    // TEST 1: Device Telemetry / Health Reporting (POST & GET)
    // -----------------------------------------------------------------------
    console.log('\n1. Testing Device Health Reporting & Query (lovebox-health)...');
    const healthPayload = {
      deviceId: DEVICE_ID,
      firmwareVersion: '1.0.3',
      uptimeMs: 123456,
      wifiRssi: -58,
      freeHeap: 245760,
      psramTotal: 8388608,
      psramFree: 6291456,
      ffatMounted: true,
      ffatTotal: 9437184,
      ffatUsed: 153600,
      resetReason: 1,
      lastSuccessfulCommunicationMs: Date.now(),
      lastMessageId: 'test-init',
      displayReady: true,
      touchReady: true,
      audioReady: true,
      servoReady: true,
    };

    const postHealthRes = await fetch(`${BASE_URL}/.netlify/functions/lovebox-health?deviceId=${DEVICE_ID}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'X-Device-Key': DEVICE_KEY,
      },
      body: JSON.stringify(healthPayload),
    });
    const postHealthJson = await postHealthRes.json();
    assert(postHealthRes.status === 200 && postHealthJson.ok === true, 'Device successfully posts telemetry report');

    const getHealthRes = await fetch(`${BASE_URL}/.netlify/functions/lovebox-health?deviceId=${DEVICE_ID}`, {
      headers: { 'X-Lovebox-Passcode': PASSCODE },
    });
    const getHealthJson = await getHealthRes.json();
    assert(
      getHealthRes.status === 200 &&
      getHealthJson.ok === true &&
      getHealthJson.data?.wifiRssi === -58 &&
      getHealthJson.data?.audioReady === true,
      'Sender dashboard retrieves valid device health data'
    );

    // -----------------------------------------------------------------------
    // TEST 2: Send Message with Photo, Audio WAV & Caption (lovebox-send)
    // -----------------------------------------------------------------------
    console.log('\n2. Testing Message Send with Image & Voice Note (lovebox-send)...');
    const testPng = generateTestPng(320, 240);
    const testWav = generateTestWav(0.5, 16000);

    const formData = new FormData();
    formData.append('deviceId', DEVICE_ID);
    formData.append('senderName', 'Automated E2E Test');
    formData.append('caption', 'Loving test message with voice note');
    formData.append('image', new Blob([testPng], { type: 'image/png' }), 'test.png');
    formData.append('audio', new Blob([testWav], { type: 'audio/wav' }), 'voice.wav');

    const sendRes = await fetch(`${BASE_URL}/.netlify/functions/lovebox-send`, {
      method: 'POST',
      headers: { 'X-Lovebox-Passcode': PASSCODE },
      body: formData,
    });
    const sendText = await sendRes.text();
    let sendJson = {};
    try {
      sendJson = JSON.parse(sendText);
    } catch {
      console.error('Invalid JSON response:', sendText);
    }
    if (!sendJson.ok) {
      console.error('lovebox-send failed with:', sendText);
    }

    assert(sendRes.status === 200 && sendJson.ok === true, 'POST lovebox-send succeeds');
    assert(
      sendJson.data?.imageId && sendJson.data?.imageSize === 320 * 240 * 2,
      `Image processed to RGB565 binary (${sendJson.data?.imageSize} bytes = 153.6 KB)`
    );
    assert(
      sendJson.data?.audioId && sendJson.data?.audioSize > 0,
      `Voice note processed and stored (audioId: ${sendJson.data?.audioId}, size: ${sendJson.data?.audioSize} bytes)`
    );

    const sentMessage = sendJson.data;

    // -----------------------------------------------------------------------
    // TEST 3: Device Poll for Latest Message (lovebox-latest)
    // -----------------------------------------------------------------------
    console.log('\n3. Testing Device Polling for Latest Message (lovebox-latest)...');
    const latestRes = await fetch(`${BASE_URL}/.netlify/functions/lovebox-latest?deviceId=${DEVICE_ID}`, {
      headers: { 'X-Device-Key': DEVICE_KEY },
    });
    const latestJson = await latestRes.json();

    assert(
      latestRes.status === 200 &&
      latestJson.ok === true &&
      latestJson.data?.id === sentMessage.id &&
      latestJson.data?.imageId === sentMessage.imageId &&
      latestJson.data?.audioId === sentMessage.audioId,
      'Device polls and retrieves matching latest message metadata'
    );

    // -----------------------------------------------------------------------
    // TEST 4: Device Download Image Binary (lovebox-image)
    // -----------------------------------------------------------------------
    console.log('\n4. Testing Device Image Binary Download (lovebox-image)...');
    const imageRes = await fetch(
      `${BASE_URL}/.netlify/functions/lovebox-image?deviceId=${DEVICE_ID}&imageId=${sentMessage.imageId}`,
      { headers: { 'X-Device-Key': DEVICE_KEY } }
    );
    const imageBuf = Buffer.from(await imageRes.arrayBuffer());

    assert(
      imageRes.status === 200 && imageBuf.length === 320 * 240 * 2,
      `Image binary downloaded: exactly ${imageBuf.length} bytes (320x240 RGB565)`
    );

    // -----------------------------------------------------------------------
    // TEST 5: Device Download Audio Binary (lovebox-audio)
    // -----------------------------------------------------------------------
    console.log('\n5. Testing Device Voice-Note Download (lovebox-audio)...');
    const audioRes = await fetch(
      `${BASE_URL}/.netlify/functions/lovebox-audio?deviceId=${DEVICE_ID}&audioId=${sentMessage.audioId}`,
      { headers: { 'X-Device-Key': DEVICE_KEY } }
    );
    const audioBuf = Buffer.from(await audioRes.arrayBuffer());

    assert(
      audioRes.status === 200 &&
      audioBuf.length === sentMessage.audioSize &&
      audioBuf.subarray(0, 4).toString('ascii') === 'RIFF',
      `Voice note downloaded: valid RIFF/WAV header with ${audioBuf.length} bytes`
    );

    // -----------------------------------------------------------------------
    // TEST 6: Device Acknowledgment (lovebox-ack)
    // -----------------------------------------------------------------------
    console.log('\n6. Testing Delivery Acknowledgment (lovebox-ack)...');
    const ackRes = await fetch(`${BASE_URL}/.netlify/functions/lovebox-ack?deviceId=${DEVICE_ID}`, {
      method: 'POST',
      headers: { 'X-Device-Key': DEVICE_KEY },
    });
    const ackJson = await ackRes.json();
    assert(ackRes.status === 200 && ackJson.ok === true, 'Device successfully acknowledges delivery');

    // Verify metadata updated with acknowledgedAt
    const verifyAckRes = await fetch(`${BASE_URL}/.netlify/functions/lovebox-latest?deviceId=${DEVICE_ID}`, {
      headers: { 'X-Device-Key': DEVICE_KEY },
    });
    const verifyAckJson = await verifyAckRes.json();
    assert(Boolean(verifyAckJson.data?.acknowledgedAt), 'Message acknowledgedAt timestamp recorded');

    // -----------------------------------------------------------------------
    // TEST 7: Recipient Feedback - Heart Like (lovebox-feedback)
    // -----------------------------------------------------------------------
    console.log('\n7. Testing Recipient Heart Like Feedback (lovebox-feedback)...');
    const likeRes = await fetch(`${BASE_URL}/.netlify/functions/lovebox-feedback?deviceId=${DEVICE_ID}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'X-Device-Key': DEVICE_KEY,
        'X-Feedback-Type': 'like',
      },
      body: JSON.stringify({ messageId: sentMessage.id }),
    });
    const likeJson = await likeRes.json();
    assert(likeRes.status === 200 && likeJson.ok === true && likeJson.data?.type === 'like', 'Device posts Like reaction');

    // -----------------------------------------------------------------------
    // TEST 8: Recipient Feedback - Drawing Overlay (lovebox-feedback)
    // -----------------------------------------------------------------------
    console.log('\n8. Testing Recipient Drawing Feedback (lovebox-feedback)...');
    // Create simulated 320x240 RGB565 drawing buffer (white box in center)
    const drawRgb565 = Buffer.alloc(320 * 240 * 2);
    for (let x = 100; x < 220; x++) {
      for (let y = 110; y < 130; y++) {
        const idx = (y * 320 + x) * 2;
        drawRgb565.writeUInt16LE(0xFFFF, idx); // White pixel
      }
    }

    const drawRes = await fetch(`${BASE_URL}/.netlify/functions/lovebox-feedback?deviceId=${DEVICE_ID}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/octet-stream',
        'X-Device-Key': DEVICE_KEY,
        'X-Feedback-Type': 'draw',
        'X-Message-Id': sentMessage.id,
      },
      body: drawRgb565,
    });
    const drawJson = await drawRes.json();
    assert(drawRes.status === 200 && drawJson.ok === true && drawJson.data?.imageId, 'Device posts Drawing feedback');

    // -----------------------------------------------------------------------
    // TEST 9: Sender Feedback History Timeline (lovebox-feedback GET)
    // -----------------------------------------------------------------------
    console.log('\n9. Testing Sender Feedback Timeline Query (lovebox-feedback GET)...');
    const feedbackListRes = await fetch(`${BASE_URL}/.netlify/functions/lovebox-feedback?deviceId=${DEVICE_ID}`, {
      headers: { 'X-Lovebox-Passcode': PASSCODE },
    });
    const feedbackListJson = await feedbackListRes.json();

    const hasLike = feedbackListJson.data?.some((item) => item.type === 'like' && item.messageId === sentMessage.id);
    const hasDraw = feedbackListJson.data?.some(
      (item) => item.type === 'draw' && item.messageId === sentMessage.id && item.imageData?.startsWith('data:image/png;base64,')
    );

    assert(
      feedbackListRes.status === 200 && feedbackListJson.ok === true && hasLike && hasDraw,
      'Feedback timeline contains both Like and Doodle with rendered PNG data URL'
    );

    // -----------------------------------------------------------------------
    // TEST 10: Firmware Update Check (lovebox-firmware)
    // -----------------------------------------------------------------------
    console.log('\n10. Testing Firmware Manifest Query (lovebox-firmware)...');
    const firmwareRes = await fetch(`${BASE_URL}/.netlify/functions/lovebox-firmware?deviceId=${DEVICE_ID}`, {
      headers: { 'X-Device-Key': DEVICE_KEY },
    });
    const firmwareJson = await firmwareRes.json();

    assert(
      firmwareRes.status === 200 && firmwareJson.ok === true,
      'Firmware endpoint returns 200 with valid schema'
    );

    // -----------------------------------------------------------------------
    // Summary
    // -----------------------------------------------------------------------
    console.log('\n' + '='.repeat(70));
    console.log(`Test Execution Finished: ${passed} Passed, ${failed} Failed`);
    console.log('='.repeat(70));

    process.exit(failed > 0 ? 1 : 0);
  } catch (err) {
    console.error('Fatal test error:', err);
    process.exit(1);
  }
}

runTests();
