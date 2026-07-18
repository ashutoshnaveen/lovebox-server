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

let selectedFile = null;
let feedbackSignature = null;

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

startFeedbackPolling();

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

  try {
    const response = await fetch('/.netlify/functions/lovebox-send', {
      method: 'POST',
      headers: {
        'X-Lovebox-Passcode': passcodeInput.value,
      },
      body: formData,
    });

    const result = await response.json();

    if (response.ok && result.ok) {
      showStatus('Sent! Your Lovebox will display it soon.', 'success');
      updateLastSent(result.data);
      form.reset();
      preview.classList.add('hidden');
      previewImg.src = '';
      selectedFile = null;
      captionCount.textContent = '0';
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
