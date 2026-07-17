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

let selectedFile = null;

passcodeInput.value = localStorage.getItem('lovebox_passcode') || '';
document.getElementById('senderName').value = localStorage.getItem('lovebox_sender') || '';

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
