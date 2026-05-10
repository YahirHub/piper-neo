import bannerUrl from '../assets/piper-neo-banner.png';

export function SplashScreen() {
  return (
    <div className="splash-screen" role="status" aria-label="Cargando Piper Neo Client">
      <div className="splash-glow" />
      <div className="splash-card">
        <img src={bannerUrl} alt="Piper Neo" />
        <div className="splash-copy">
          <strong>Piper Neo Client</strong>
          <span>Preparando estudio de voz local...</span>
        </div>
        <div className="splash-loader" aria-hidden="true">
          <span />
          <span />
          <span />
        </div>
      </div>
    </div>
  );
}
