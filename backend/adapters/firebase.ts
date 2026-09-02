import fs from "node:fs/promises";
import {
  Firestore,
  getFirestore,
  collection,
  doc,
  setDoc,
  getDoc,
  getDocs,
  query,
  where,
  orderBy,
  limit,
  writeBatch,
} from "firebase/firestore";
import { getStorage, ref, uploadBytes, getBlob } from "firebase/storage";
import { initializeApp, cert } from "firebase-admin/app";
import type {
  BackendAdapter,
  LoveboxMessage,
  LoveboxFeedback,
  LoveboxHealth,
} from "../adapter.js";

let db: Firestore;
let storage: ReturnType<typeof getStorage>;

export function createFirebaseAdapter(options?: { projectId?: string; credentialsPath?: string }) {
  const projectId = options?.projectId || process.env.FIREBASE_PROJECT_ID;
  const credentialsPath = options?.credentialsPath || process.env.GOOGLE_APPLICATION_CREDENTIALS || process.env.FIREBASE_CREDENTIALS;

  if (!projectId) {
    throw new Error("FIREBASE_PROJECT_ID is required for Firebase adapter");
  }

  if (!credentialsPath) {
    throw new Error("GOOGLE_APPLICATION_CREDENTIALS or FIREBASE_CREDENTIALS is required for Firebase adapter");
  }

  const credentials = JSON.parse(await fs.readFile(credentialsPath, "utf-8"));

  const app = initializeApp({
    credential: cert(credentials),
    projectId,
  });

  db = getFirestore(app);
  storage = getStorage(app);

  return {
    async sendMessage(deviceId: string, message: LoveboxMessage, imageBuffer: ArrayBuffer) {
      const batch = writeBatch(db);

      const messageRef = doc(collection(db, "devices", deviceId, "messages"));
      batch.set(messageRef, message);

      const imageRef = ref(storage, `devices/${deviceId}/images/${message.imageId}.rgb565`);
      await uploadBytes(imageRef, Buffer.from(imageBuffer));

      await batch.commit();
    },

    async getLatestMessage(deviceId: string): Promise<LoveboxMessage | null> {
      const q = query(
        collection(db, "devices", deviceId, "messages"),
        orderBy("createdAt", "desc"),
        limit(1)
      );
      const snapshot = await getDocs(q);
      if (snapshot.empty) return null;
      return { id: snapshot.docs[0].id, ...snapshot.docs[0].data() } as LoveboxMessage;
    },

    async getImage(deviceId: string, imageId: string): Promise<ArrayBuffer | null> {
      try {
        const imageRef = ref(storage, `devices/${deviceId}/images/${imageId}.rgb565`);
        const blob = await getBlob(imageRef);
        return Buffer.from(await blob.arrayBuffer());
      } catch {
        return null;
      }
    },

    async saveAudio(audioId: string, buffer: ArrayBuffer) {
      const audioRef = ref(storage, `audio/${audioId}.wav`);
      await uploadBytes(audioRef, Buffer.from(buffer));
    },

    async getAudio(audioId: string): Promise<ArrayBuffer | null> {
      try {
        const audioRef = ref(storage, `audio/${audioId}.wav`);
        const blob = await getBlob(audioRef);
        return Buffer.from(await blob.arrayBuffer());
      } catch {
        return null;
      }
    },

    async saveFeedback(feedback: LoveboxFeedback) {
      const feedbackRef = doc(collection(db, "feedback"));
      await setDoc(feedbackRef, feedback);
    },

    async getFeedbackHistory(deviceId: string, limit = 20): Promise<LoveboxFeedback[]> {
      const q = query(
        collection(db, "feedback"),
        where("deviceId", "==", deviceId),
        orderBy("createdAt", "desc"),
        limit
      );
      const snapshot = await getDocs(q);
      return snapshot.docs.map((docSnapshot) => ({
        id: docSnapshot.id,
        ...docSnapshot.data(),
      })) as LoveboxFeedback[];
    },

    async getFeedbackImage(imageId: string): Promise<ArrayBuffer | null> {
      try {
        const imageRef = ref(storage, `feedback-images/${imageId}.png`);
        const blob = await getBlob(imageRef);
        return Buffer.from(await blob.arrayBuffer());
      } catch {
        return null;
      }
    },

    async saveDeviceHealth(health: LoveboxHealth) {
      const healthRef = doc(collection(db, "devices", health.deviceId, "health"));
      await setDoc(healthRef, health);
    },

    async getDeviceHealth(deviceId: string): Promise<LoveboxHealth | null> {
      const healthRef = doc(db, "devices", deviceId, "health");
      const snapshot = await getDoc(healthRef);
      if (!snapshot.exists()) return null;
      return snapshot.data() as LoveboxHealth;
    },

    async acknowledgeMessage(deviceId: string, acknowledgedAt: string) {
      const messagesRef = collection(db, "devices", deviceId, "messages");
      const q = query(messagesRef, orderBy("createdAt", "desc"), limit(1));
      const snapshot = await getDocs(q);
      if (snapshot.empty) return;
      const latest = snapshot.docs[0];
      await setDoc(latest.ref, { acknowledgedAt }, { merge: true });
    },
  };
}
