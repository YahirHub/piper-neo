export type RoutePath = '/' | '/setup' | '/models' | '/history' | '/settings';

export interface AppSettings {
  apiUrl: string;
  token: string;
  useToken: boolean;
  connected: boolean;
  selectedModel: string;
  speakerId: number;
  autoPlay: boolean;
  saveHistory: boolean;
  lastRoute: RoutePath;
}

export interface PiperResponse<T> {
  success: boolean;
  message?: string;
  error?: string;
  data?: T;
}

export interface PiperStatus {
  server?: string;
  status?: string;
  model_loaded?: boolean;
  active_model?: string;
  models_dir?: string;
  output_dir?: string;
  auth?: {
    enabled?: boolean;
    header?: string;
  };
  limits?: Record<string, unknown>;
  time?: string;
}

export interface PiperModel {
  file: string;
  name?: string;
  format?: 'onnx' | 'neo' | string;
  config_file?: string;
  available?: boolean;
  has_config?: boolean;
  config_valid?: boolean;
  language?: string;
  language_info?: { code?: string };
  has_image?: boolean;
  image_url?: string;
  dataset?: string;
  audio?: {
    sample_rate?: number;
    quality?: string;
  };
  espeak?: {
    voice?: string;
  };
  inference?: {
    noise_scale?: number;
    length_scale?: number;
    noise_w?: number;
  };
  num_speakers?: number;
  piper_version?: string;
  modelcard?: {
    id?: string;
    name?: string;
    description?: string;
    language?: string;
    voiceprompt?: string;
    sha256?: string;
  };
  speaker_id_map?: Record<string, number>;
  neo?: {
    version?: number;
    model_compression?: string;
    model_bytes?: number;
    stored_model_bytes?: number;
  };
}

export interface ModelsPayload {
  total: number;
  include: string;
  cached?: boolean;
  refresh_seconds?: number;
  models: PiperModel[];
}

export interface TtsPayload {
  file: string;
  model: string;
  url: string;
  format: string;
  chunks?: number;
  bytes?: number;
  audio_seconds?: number;
  infer_seconds?: number;
  real_time_factor?: number;
}


export interface CurrentAudioState {
  result: TtsPayload | null;
  blob: Blob | null;
  text: string;
  modelName?: string;
  createdAt?: number;
}

export interface AudioHistoryItem {
  id: string;
  createdAt: number;
  text: string;
  model: string;
  modelName?: string;
  blob: Blob;
  file: string;
  format: string;
  bytes?: number;
  audioSeconds?: number;
  inferSeconds?: number;
  realTimeFactor?: number;
}
