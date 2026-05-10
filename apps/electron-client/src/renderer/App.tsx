import { useEffect, useMemo, useState } from 'react';
import { isRoutePath, loadSettings, saveSettings } from './lib/settings';
import type { AppSettings, RoutePath } from './lib/types';
import { Shell } from './components/Shell';
import { Onboarding } from './components/Onboarding';
import { ModelsPage } from './components/ModelsPage';
import { StudioPage } from './components/StudioPage';
import { HistoryPage } from './components/HistoryPage';
import { SettingsPage } from './components/SettingsPage';
import { SplashScreen } from './components/SplashScreen';

function currentRoute(): RoutePath {
  const pathname = window.location.pathname;
  if (isRoutePath(pathname)) return pathname;
  return '/';
}

export function App() {
  const [settings, setSettings] = useState<AppSettings>(() => loadSettings());
  const [route, setRoute] = useState<RoutePath>(() => currentRoute());
  const [showSplash, setShowSplash] = useState(true);

  useEffect(() => {
    const timer = window.setTimeout(() => setShowSplash(false), 1700);
    return () => window.clearTimeout(timer);
  }, []);

  useEffect(() => {
    saveSettings(settings);
  }, [settings]);

  useEffect(() => {
    const onPop = () => setRoute(currentRoute());
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
    window.history.pushState({}, '', path);
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
      case '/history':
        return <HistoryPage />;
      case '/settings':
        return <SettingsPage settings={settings} updateSettings={updateSettings} replaceSettings={replaceSettings} navigate={navigate} />;
      case '/':
      default:
        return <StudioPage settings={settings} updateSettings={updateSettings} navigate={navigate} />;
    }
  }, [route, settings]);

  if (showSplash) {
    return <SplashScreen />;
  }

  return (
    <Shell route={route} connected={settings.connected} selectedModel={settings.selectedModel} navigate={navigate}>
      {page}
    </Shell>
  );
}
