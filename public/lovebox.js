const form = document.getElementById('sendForm');
const passcodeInput = document.getElementById('passcode');
const imageInput = document.getElementById('image');
const preview = document.getElementById('preview');
const previewImg = document.getElementById('previewImg');
const captionInput = document.getElementById('caption');
const captionCount = document.getElementById('captionCount');
const sendBtn = document.getElementById('sendBtn');
const btnText = sendBtn.querySelector('.btn-text');
const btnSpinner = sendBtn.querySelector('.btn-spinner');
const statusEl = document.getElementById('status');
const lastSent = document.getElementById('lastSent');
const lastSentMeta = document.getElementById('lastSentMeta');
const feedbackEl = document.getElementById('feedback');
const feedbackStatus = document.getElementById('feedbackStatus');
const feedbackTimeline = document.getElementById('feedbackTimeline');
const refreshFeedbackBtn = document.getElementById('refreshFeedback');
const recordBtn = document.getElementById('recordBtn');
const audioInput = document.getElementById('audio');
const audioPreview = document.getElementById('audioPreview');
const audioStatus = document.getElementById('audioStatus');
const deviceHealth = document.getElementById('deviceHealth');
const healthStatus = document.getElementById('healthStatus');
const healthGrid = document.getElementById('healthGrid');
const refreshHealthBtn = document.getElementById('refreshHealth');

let selectedFile = null;
let feedbackSignature = null;
let audioWavBlob = null;
let mediaRecorder = null;
let mediaChunks = [];

passcodeInput.value = localStorage.getItem('lovebox_passcode') || '';
document.getElementById('senderName').value = localStorage.getItem('lovebox_sender') || '';

const FEEDBACK_ICONS = {
  like: '❤️',
  draw: '✍️',
};

function startFeedbackPolling() {
  checkFeedback();
  setInterval(checkFeedback, 5000);
}

function startHealthPolling() {
  checkHealth();
  setInterval(checkHealth, 30000);
}

async function checkHealth() {
  const deviceId = document.getElementById('deviceId').value;
  const passcode = passcodeInput.value;
  if (!passcode || !deviceId) {
    deviceHealth.classList.add('hidden');
    return;
  }

  deviceHealth.classList.remove('hidden');
  try {
    const response = await fetch(`/.netlify/functions/lovebox-health?deviceId=${encodeURIComponent(deviceId)}`, {
      headers: { 'X-Lovebox-Passcode': passcode },
    });
    const result = await response.json();
    if (!response.ok || !result.ok) {
      showHealthStatus(result.error || `Server error ${response.status}`, true);
      return;
    }
    if (!result.data) {
      showHealthStatus('No health report received yet.', false);
      healthGrid.replaceChildren();
      return;
    }
    healthStatus.classList.add('hidden');
    renderHealth(result.data, result.backendVersion);
  } catch (err) {
    console.error('Health check failed', err);
    showHealthStatus('Network error. Check connection.', true);
  }
}

function renderHealth(health, backendVersion) {
  const rows = [
    ['Firmware', health.firmwareVersion],
    ['Backend', backendVersion || 'unknown'],
    ['Wi-Fi', `${health.wifiRssi} dBm`],
    ['Free heap', `${Math.round(health.freeHeap / 1024)} KB`],
    ['FFat', health.ffatMounted ? `${Math.round(health.ffatUsed / 1024)} / ${Math.round(health.ffatTotal / 1024)} KB` : 'Not mounted'],
    ['Display', health.displayReady ? 'Ready' : 'Unavailable'],
    ['Touch', health.touchReady ? 'Ready' : 'Unavailable'],
    ['Servo', health.servoReady ? 'Ready' : 'Unavailable'],
    ['Last report', new Date(health.reportedAt).toLocaleString()],
  ];
  healthGrid.replaceChildren(...rows.flatMap(([label, value]) => {
    const term = document.createElement('dt');
    term.textContent = label;
    const detail = document.createElement('dd');
    detail.textContent = value;
    return [term, detail];
  }));
}

function showHealthStatus(text, isError) {
  healthStatus.textContent = text;
  healthStatus.classList.remove('hidden', 'error');
  if (isError) healthStatus.classList.add('error');
}

async function checkFeedback() {
  const deviceId = document.getElementById('deviceId').value;
  const passcode = passcodeInput.value;
  if (!passcode || !deviceId) {
    feedbackEl.classList.add('hidden');
    return;
  }

  feedbackEl.classList.remove('hidden');
  try {
    const response = await fetch(`/.netlify/functions/lovebox-feedback?deviceId=${encodeURIComponent(deviceId)}`, {
      headers: { 'X-Lovebox-Passcode': passcode },
    });
    const result = await response.json();

    if (!response.ok) {
      showFeedbackStatus(result.error || `Server error ${response.status}`, true);
      return;
    }
    if (!result.ok) {
      showFeedbackStatus(result.error || 'No feedback data', false);
      return;
    }
    if (!result.data.length) {
      showFeedbackStatus('No feedback yet. Send a like or drawing from the Lovebox.', false);
      feedbackTimeline.replaceChildren();
      feedbackSignature = null;
      return;
    }

    const signature = result.data.map((event) => event.id).join(':');
    if (signature === feedbackSignature) return;
    feedbackSignature = signature;
    feedbackStatus.classList.add('hidden');
    renderFeedbackTimeline(result.data);
  } catch (err) {
    console.error('Feedback check failed', err);
    showFeedbackStatus('Network error. Check connection.', true);
  }
}

function renderFeedbackTimeline(events) {
  feedbackTimeline.replaceChildren(...events.map((event) => {
    const item = document.createElement('article');
    item.className = 'feedback-event';

    const details = document.createElement('div');
    details.className = 'feedback-event-details';

    const summary = document.createElement('p');
    summary.className = 'feedback-event-summary';
    summary.textContent = event.type === 'like'
      ? `${FEEDBACK_ICONS.like} She liked a photo`
      : `${FEEDBACK_ICONS.draw} She drew a response`;

    const meta = document.createElement('p');
    meta.className = 'feedback-event-meta';
    meta.textContent = `${new Date(event.createdAt).toLocaleString()} · Image ${event.messageId}`;

    details.append(summary, meta);
    item.append(details);

    if (event.type === 'draw' && event.imageData) {
      const image = document.createElement('img');
      image.className = 'feedback-image';
      image.src = event.imageData;
      image.alt = `Drawing response to image ${event.messageId}`;
      item.append(image);
    }

    return item;
  }));
}

function showFeedbackStatus(text, isError) {
  feedbackStatus.textContent = text;
  feedbackStatus.classList.remove('hidden', 'error');
  if (isError) feedbackStatus.classList.add('error');
}

refreshFeedbackBtn.addEventListener('click', () => {
  feedbackSignature = null;
  checkFeedback();
});

refreshHealthBtn.addEventListener('click', checkHealth);

startFeedbackPolling();
startHealthPolling();

function showAudioStatus(text, isError) {
  audioStatus.textContent = text;
  audioStatus.classList.remove('hidden', 'error');
  if (isError) audioStatus.classList.add('error');
}

function setAudioPreview(blob) {
  audioWavBlob = blob;
  if (blob) {
    audioPreview.src = URL.createObjectURL(blob);
    audioPreview.classList.remove('hidden');
  } else {
    audioPreview.src = '';
    audioPreview.classList.add('hidden');
  }
  updateSendButton();
}

// Decode any audio (recorded or uploaded MP3/WAV/AAC/M4A/OGG) and normalize to a loud, clear
// 16-bit PCM WAV at 16 kHz mono so the ESP32 can stream it straight to the I2S DAC.
const MAX_AUDIO_BYTES = 10 * 1024 * 1024; // 10 MB source audio limit
const MAX_AUDIO_SECONDS = 120; // cap converted length to avoid huge WAVs

async function normalizeAudioToWav(arrayBuffer) {
  const AudioCtx = window.AudioContext || window.webkitAudioContext;
  const ctx = new AudioCtx();
  await ctx.resume();
  try {
    const decoded = await decodeAudioBuffer(ctx, arrayBuffer);
    const targetRate = 16000;
    const maxSamples = MAX_AUDIO_SECONDS * targetRate;
    const samples = decoded.numberOfChannels === 1
      ? decoded.getChannelData(0)
      : decodeMono(decoded);
    const useCount = Math.min(samples.length, maxSamples);
    const trimmed = samples.subarray ? samples.subarray(0, useCount) : samples.slice(0, useCount);
    return encodeWav(trimmed, targetRate);
  } finally {
    ctx.close();
  }
}

function decodeMono(decoded) {
  const left = decoded.getChannelData(0);
  if (decoded.numberOfChannels === 1) return left;
  const right = decoded.numberOfChannels > 1 ? decoded.getChannelData(1) : left;
  const out = new Float32Array(left.length);
  for (let i = 0; i < left.length; i++) out[i] = (left[i] + right[i]) / 2;
  return out;
}

function decodeAudioBuffer(ctx, arrayBuffer) {
  return new Promise((resolve, reject) => {
    try {
      const res = ctx.decodeAudioData(
        arrayBuffer.slice(0),
        (buf) => resolve(buf),
        (err) => reject(err || new Error('Failed to decode audio data'))
      );
      if (res && typeof res.then === 'function') {
        res.then(resolve).catch(reject);
      }
    } catch (err) {
      reject(err);
    }
  });
}

function encodeWav(samples, sampleRate) {
  // Peak amplitude calculation for automatic gain normalization
  let peak = 0;
  for (let i = 0; i < samples.length; i++) {
    const abs = Math.abs(samples[i]);
    if (abs > peak) peak = abs;
  }
  // Amplify quiet microphone audio up to full clean scale (0.95 peak)
  const gain = peak > 0.001 ? Math.min(10.0, 0.95 / peak) : 1.0;

  const bytesPerSample = 2;
  const dataSize = samples.length * bytesPerSample;
  const buffer = new ArrayBuffer(44 + dataSize);
  const view = new DataView(buffer);
  const writeString = (offset, str) => {
    for (let i = 0; i < str.length; i++) view.setUint8(offset + i, str.charCodeAt(i));
  };
  writeString(0, 'RIFF');
  view.setUint32(4, 36 + dataSize, true);
  writeString(8, 'WAVE');
  writeString(12, 'fmt ');
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true);
  view.setUint16(22, 1, true);
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, sampleRate * bytesPerSample, true);
  view.setUint16(32, bytesPerSample, true);
  view.setUint16(34, 16, true);
  writeString(36, 'data');
  view.setUint32(40, dataSize, true);
  let offset = 44;
  for (let i = 0; i < samples.length; i++) {
    let s = Math.max(-1, Math.min(1, samples[i] * gain));
    view.setInt16(offset, s < 0 ? s * 0x8000 : s * 0x7fff, true);
    offset += 2;
  }
  return new Blob([buffer], { type: 'audio/wav' });
}

audioInput.addEventListener('change', async (e) => {
  const file = e.target.files[0];
  if (!file) {
    setAudioPreview(null);
    return;
  }
  if (file.size > MAX_AUDIO_BYTES) {
    showAudioStatus('Audio file is too large. Use a file under 10 MB.', true);
    setAudioPreview(null);
    return;
  }
  showAudioStatus('Processing audio...', false);
  try {
    const buf = await file.arrayBuffer();
    const wav = await normalizeAudioToWav(buf);
    setAudioPreview(wav);
    showAudioStatus('Voice note ready.', false);
  } catch (err) {
    console.error('Audio processing failed', err);
    setAudioPreview(null);
    showAudioStatus('Could not read that audio file.', true);
  }
});

recordBtn.addEventListener('click', async () => {
  if (mediaRecorder && mediaRecorder.state === 'recording') {
    mediaRecorder.stop();
    return;
  }
  if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
    showAudioStatus('Recording not supported on this device.', true);
    return;
  }
  try {
    const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
    mediaChunks = [];
    mediaRecorder = new MediaRecorder(stream);
    mediaRecorder.ondataavailable = (ev) => mediaChunks.push(ev.data);
    mediaRecorder.onstop = async () => {
      stream.getTracks().forEach((t) => t.stop());
      recordBtn.classList.remove('recording');
      recordBtn.textContent = 'Record';
      showAudioStatus('Processing recording...', false);
      try {
        const blob = new Blob(mediaChunks, { type: mediaRecorder.mimeType || 'audio/webm' });
        const wav = await normalizeAudioToWav(await blob.arrayBuffer());
        setAudioPreview(wav);
        showAudioStatus('Voice note ready.', false);
      } catch (err) {
        console.error('Recording processing failed', err);
        setAudioPreview(null);
        showAudioStatus('Could not process recording.', true);
      }
    };
    mediaRecorder.start();
    recordBtn.classList.add('recording');
    recordBtn.textContent = 'Stop';
    showAudioStatus('Recording... tap Stop when done.', false);
  } catch (err) {
    console.error('Microphone access failed', err);
    showAudioStatus('Microphone permission denied.', true);
  }
});

imageInput.addEventListener('change', (e) => {
  selectedFile = e.target.files[0] || null;
  if (selectedFile) {
    const url = URL.createObjectURL(selectedFile);
    previewImg.src = url;
    preview.classList.remove('hidden');
  } else {
    preview.classList.add('hidden');
    previewImg.src = '';
  }
  updateSendButton();
});

captionInput.addEventListener('input', () => {
  captionCount.textContent = captionInput.value.length;
});

passcodeInput.addEventListener('input', () => {
  localStorage.setItem('lovebox_passcode', passcodeInput.value);
});

document.getElementById('senderName').addEventListener('input', (e) => {
  localStorage.setItem('lovebox_sender', e.target.value);
});

form.addEventListener('submit', async (e) => {
  e.preventDefault();
  if (!selectedFile) return;

  setSending(true);
  showStatus('', null);

  const formData = new FormData();
  formData.append('deviceId', document.getElementById('deviceId').value);
  formData.append('senderName', document.getElementById('senderName').value);
  formData.append('caption', captionInput.value);
  formData.append('image', selectedFile);
  if (audioWavBlob) {
    if (audioWavBlob.size > MAX_AUDIO_BYTES) {
      showStatus('Audio is too large after conversion. Use a shorter clip.', 'error');
      setSending(false);
      updateSendButton();
      return;
    }
    formData.append('audio', audioWavBlob, 'voice-note.wav');
  }

  try {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 120000);
    let response;
    try {
      response = await fetch('/.netlify/functions/lovebox-send', {
        method: 'POST',
        headers: {
          'X-Lovebox-Passcode': passcodeInput.value,
        },
        body: formData,
        signal: controller.signal,
      });
    } catch (err) {
      if (err.name === 'AbortError') {
        showStatus('Upload timed out. Try a smaller file or better connection.', 'error');
      } else {
        console.error('Upload failed', err);
        showStatus('Network error. Please check your connection.', 'error');
      }
      throw err;
    } finally {
      clearTimeout(timeoutId);
    }

    let result;
    try {
      result = await response.json();
    } catch (err) {
      console.error('Invalid server response', err);
      showStatus('Server returned an invalid response. Please try again.', 'error');
      return;
    }

    if (response.ok && result.ok) {
      showStatus('Sent! Your Lovebox will display it soon.', 'success');
      updateLastSent(result.data);
      form.reset();
      preview.classList.add('hidden');
      previewImg.src = '';
      selectedFile = null;
      captionCount.textContent = '0';
      setAudioPreview(null);
      audioStatus.classList.add('hidden');
      audioInput.value = '';
      passcodeInput.value = localStorage.getItem('lovebox_passcode') || '';
      document.getElementById('senderName').value = localStorage.getItem('lovebox_sender') || '';
    } else {
      showStatus(result.error || 'Failed to send. Please try again.', 'error');
    }
  } catch (err) {
    console.error(err);
    showStatus('Network error. Please check your connection.', 'error');
  } finally {
    setSending(false);
    updateSendButton();
  }
});

function updateSendButton() {
  sendBtn.disabled = !selectedFile || !passcodeInput.value;
}

function setSending(sending) {
  sendBtn.disabled = sending;
  btnText.classList.toggle('hidden', sending);
  btnSpinner.classList.toggle('hidden', !sending);
}

function showStatus(message, type) {
  if (!message) {
    statusEl.classList.add('hidden');
    return;
  }
  statusEl.textContent = message;
  statusEl.className = 'status ' + (type || '');
}

function updateLastSent(data) {
  if (!data) return;
  const date = new Date(data.createdAt).toLocaleString();
  lastSentMeta.textContent = `${data.senderName || 'Someone'} sent an image at ${date}.`;
  lastSent.classList.remove('hidden');
}

if ('serviceWorker' in navigator) {
  navigator.serviceWorker.register('/sw.js').catch(console.error);
}

passcodeInput.addEventListener('input', updateSendButton);
