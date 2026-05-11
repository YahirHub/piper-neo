import { useEffect, useState } from 'react';
import type { PiperModel } from '../lib/types';
import { PiperApiClient } from '../lib/api';
import { Icon } from '../lib/icons';
import { formatBytes, modelDisplayName, modelLanguage } from '../lib/format';
import { getCachedModelImage, modelImageCacheKey, setCachedModelImage } from '../lib/modelImageCache';

export function ModelCard({ model, selected, client, onSelect }: { model: PiperModel; selected: boolean; client: PiperApiClient; onSelect: () => void }) {
  const [imageUrl, setImageUrl] = useState('');

  useEffect(() => {
    let alive = true;
    let objectUrl = '';

    async function useBlob(blob: Blob) {
      if (!alive) return;
      objectUrl = URL.createObjectURL(blob);
      setImageUrl(objectUrl);
    }

    async function load() {
      if (!model.image_url && !model.has_image) return;

      const fallbackUrl = `/api/v1/models/${encodeURIComponent(model.file)}/image`;
      const sourceUrl = model.image_url || fallbackUrl;
      const cacheKey = modelImageCacheKey(client.baseUrl, model.file, sourceUrl);

      const cached = await getCachedModelImage(cacheKey);
      if (cached) {
        await useBlob(cached);
        return;
      }

      try {
        const blob = await client.modelImage(sourceUrl);
        await setCachedModelImage(cacheKey, blob);
        await useBlob(blob);
      } catch {
        if (alive) setImageUrl('');
      }
    }

    void load();
    return () => {
      alive = false;
      if (objectUrl) URL.revokeObjectURL(objectUrl);
    };
  }, [client, model.file, model.has_image, model.image_url]);

  const description = model.modelcard?.description || model.modelcard?.voiceprompt || 'Modelo Piper Neo listo para síntesis local.';

  return (
    <button className={`model-card ${selected ? 'selected' : ''}`} onClick={onSelect}>
      <div className="model-art">
        {imageUrl ? <img src={imageUrl} alt={modelDisplayName(model)} /> : <Icon name="image" />}
        <span className="model-format">{model.format || (model.file.endsWith('.neo') ? 'neo' : 'onnx')}</span>
      </div>
      <div className="model-body">
        <div className="model-heading">
          <strong>{modelDisplayName(model)}</strong>
          {selected && <span className="check-badge"><Icon name="check" /></span>}
        </div>
        <p>{description}</p>
        <div className="model-meta">
          <span>{modelLanguage(model)}</span>
          <span>{model.audio?.sample_rate ? `${model.audio.sample_rate} Hz` : 'sample rate —'}</span>
          <span>{model.num_speakers ?? 1} speaker</span>
          {model.neo?.stored_model_bytes && <span>{formatBytes(model.neo.stored_model_bytes)}</span>}
        </div>
      </div>
    </button>
  );
}
