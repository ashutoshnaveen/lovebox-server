export function sanitizeDeviceId(deviceId: string | null): string | null {
  if (!deviceId) return null;
  const cleaned = deviceId.trim().toLowerCase();
  if (!/^[a-z0-9-]+$/.test(cleaned)) return null;
  if (cleaned.length > 64) return null;
  return cleaned;
}

export function sanitizeCaption(caption: string | null): string {
  if (!caption) return "";
  return caption.trim().slice(0, 120);
}

export function sanitizeSenderName(name: string | null): string {
  if (!name) return "";
  return name.trim().slice(0, 60);
}

export function generateId(): string {
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}

export const MAX_IMAGE_SIZE = 10 * 1024 * 1024; // 10 MB upload limit
