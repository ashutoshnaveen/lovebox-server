import { timingSafeEqual } from "crypto";

export function validateSenderPasscode(passcode: string | null): boolean {
  const expected = process.env.LOVEBOX_PASSCODE;
  if (!expected || !passcode) return false;
  return safeCompare(passcode, expected);
}

export function validateDeviceKey(deviceId: string, key: string | null): boolean {
  if (!key || !deviceId) return false;
  const envName = `DEVICE_KEY_${deviceId.replace(/-/g, "_").toUpperCase()}`;
  const expected = process.env[envName];
  if (!expected) return false;
  return safeCompare(key, expected);
}

function safeCompare(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  const ab = Buffer.from(a);
  const bb = Buffer.from(b);
  if (ab.length !== bb.length) return false;
  return timingSafeEqual(ab, bb);
}
