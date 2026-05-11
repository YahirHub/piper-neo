const DB_NAME = 'piper-neo-model-image-cache';
const STORE_NAME = 'images';
const DB_VERSION = 1;
const MAX_AGE_MS = 1000 * 60 * 60 * 24 * 30;

interface CachedModelImage {
  key: string;
  blob: Blob;
  savedAt: number;
}

let dbPromise: Promise<IDBDatabase> | null = null;

function openDb(): Promise<IDBDatabase> {
  if (dbPromise) return dbPromise;

  dbPromise = new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, DB_VERSION);

    request.onupgradeneeded = () => {
      const db = request.result;
      if (!db.objectStoreNames.contains(STORE_NAME)) {
        db.createObjectStore(STORE_NAME, { keyPath: 'key' });
      }
    };

    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error ?? new Error('No se pudo abrir la cache de imágenes.'));
  });

  return dbPromise;
}

export async function getCachedModelImage(key: string): Promise<Blob | null> {
  try {
    const db = await openDb();
    return await new Promise((resolve, reject) => {
      const tx = db.transaction(STORE_NAME, 'readonly');
      const request = tx.objectStore(STORE_NAME).get(key);
      request.onsuccess = () => {
        const item = request.result as CachedModelImage | undefined;
        if (!item?.blob) {
          resolve(null);
          return;
        }

        if (Date.now() - item.savedAt > MAX_AGE_MS) {
          void deleteCachedModelImage(key);
          resolve(null);
          return;
        }

        resolve(item.blob);
      };
      request.onerror = () => reject(request.error ?? new Error('No se pudo leer la imagen cacheada.'));
    });
  } catch {
    return null;
  }
}

export async function setCachedModelImage(key: string, blob: Blob): Promise<void> {
  try {
    const db = await openDb();
    await new Promise<void>((resolve, reject) => {
      const tx = db.transaction(STORE_NAME, 'readwrite');
      tx.objectStore(STORE_NAME).put({ key, blob, savedAt: Date.now() } satisfies CachedModelImage);
      tx.oncomplete = () => resolve();
      tx.onerror = () => reject(tx.error ?? new Error('No se pudo guardar la imagen cacheada.'));
    });
  } catch {
    // La cache es una optimización. Si IndexedDB falla, la app sigue funcionando.
  }
}

export async function deleteCachedModelImage(key: string): Promise<void> {
  try {
    const db = await openDb();
    await new Promise<void>((resolve, reject) => {
      const tx = db.transaction(STORE_NAME, 'readwrite');
      tx.objectStore(STORE_NAME).delete(key);
      tx.oncomplete = () => resolve();
      tx.onerror = () => reject(tx.error ?? new Error('No se pudo eliminar la imagen cacheada.'));
    });
  } catch {}
}

export function modelImageCacheKey(baseUrl: string, modelFile: string, imageUrl?: string): string {
  return `${baseUrl.replace(/\/+$/, '')}::${modelFile}::${imageUrl ?? ''}`;
}
