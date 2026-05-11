import { useEffect, useMemo, useState } from 'react';
import { isRoutePath, loadSettings, saveSettings } from './lib/settings';
import type { AppSettings, ChatAudioState, CurrentAudioState, RoutePath } from './lib/types';
import { Shell } from './components/Shell';
import { Onboarding } from './components/Onboarding';
import { ModelsPage } from './components/ModelsPage';
import { StudioPage } from './components/StudioPage';
import { HistoryPage } from './components/HistoryPage';
import { ChatPage } from './components/ChatPage';
import { SettingsPage } from './components/SettingsPage';
import { SplashScreen } from './components/SplashScreen';

function currentRoute(fallback: RoutePath): RoutePath {
  const pathname = window.location.pathname;

  // In dev/http the clean routes (/models, /settings, etc.) are safe.
  if (isRoutePath(pathname)) return pathname;

  // In packaged Electron the app is loaded from file://.../index.html, so
  // pathname is the physical path to the file, not an app route. Keep the
  // last persisted route instead of forcing '/' on every start.
  return fallback;
}

function canUseCleanBrowserRoutes(): boolean {
  return window.location.protocol === 'http:' || window.location.protocol === 'https:';
}

export function App() {
  const [settings, setSettings] = useState<AppSettings>(() => loadSettings());
  const [route, setRoute] = useState<RoutePath>(() => {
    const loaded = loadSettings();
    return currentRoute(loaded.lastRoute);
  });
  const [showSplash, setShowSplash] = useState(true);
  const [currentAudio, setCurrentAudio] = useState<CurrentAudioState>({
    result: null,
    blob: null,
    text: ''
  });
  const [chatAudio, setChatAudio] = useState<ChatAudioState>({ blobs: {} });

  useEffect(() => {
    const timer = window.setTimeout(() => setShowSplash(false), 1700);
    return () => window.clearTimeout(timer);
  }, []);

  useEffect(() => {
    saveSettings(settings);
  }, [settings]);

  useEffect(() => {
    const onPop = () => setRoute(currentRoute(loadSettings().lastRoute));
    window.addEventListener('popstate', onPop);
    return () => window.removeEventListener('popstate', onPop);
  }, []);

  useEffect(() => {
    if (!settings.connected && route !== '/setup') {
      navigate('/setup', false);
    }
    if (settings.connected && route === '/setup') {
      navigate(settings.selectedModel ? '/' : '/models', false);
    }
  }, []);

  function navigate(path: RoutePath, persist = true) {
    if (canUseCleanBrowserRoutes()) {
      window.history.pushState({}, '', path);
    }
    setRoute(path);
    if (persist) setSettings((prev) => ({ ...prev, lastRoute: path }));
  }

  function updateSettings(next: Partial<AppSettings>) {
    setSettings((prev) => ({ ...prev, ...next }));
  }

  function replaceSettings(next: AppSettings) {
    setSettings(next);
  }

  const page = useMemo(() => {
    if (!settings.connected || route === '/setup') {
      return <Onboarding settings={settings} updateSettings={updateSettings} navigate={navigate} />;
    }

    switch (route) {
      case '/models':
        return <ModelsPage settings={settings} updateSettings={updateSettings} navigate={navigate} />;
      case '/chat':
        return (
          <ChatPage
            settings={settings}
            updateSettings={updateSettings}
            navigate={navigate}
            chatAudioBlobs={chatAudio.blobs}
            setChatAudioBlobs={(blobs) => setChatAudio({ blobs })}
          />
        );
      case '/history':
        return <HistoryPage />;
      case '/settings':
        return <SettingsPage settings={settings} updateSettings={updateSettings} replaceSettings={replaceSettings} navigate={navigate} />;
      case '/':
      default:
        return <StudioPage settings={settings} updateSettings={updateSettings} navigate={navigate} currentAudio={currentAudio} setCurrentAudio={setCurrentAudio} />;
    }
  }, [route, settings, currentAudio, chatAudio]);

  if (showSplash) {
    return <SplashScreen />;
  }

  if (settings.connected && route === '/chat') {
    return <>{page}</>;
  }

  return (
    <Shell route={route} connected={settings.connected} selectedModel={settings.selectedModel} navigate={navigate}>
      {page}
    </Shell>
  );
}
