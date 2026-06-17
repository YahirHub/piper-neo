import { Component, type ErrorInfo, type ReactNode } from 'react';
import { Icon } from '../lib/icons';

interface ErrorBoundaryProps {
  children: ReactNode;
}

interface ErrorBoundaryState {
  error: Error | null;
}

export class ErrorBoundary extends Component<ErrorBoundaryProps, ErrorBoundaryState> {
  state: ErrorBoundaryState = { error: null };

  static getDerivedStateFromError(error: Error): ErrorBoundaryState {
    return { error };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    console.error('Error en la interfaz de Piper Neo:', error, info);
  }

  render() {
    if (!this.state.error) return this.props.children;

    return (
      <section className="app-error-boundary" role="alert">
        <div className="panel app-error-card">
          <Icon name="alert" />
          <h1>No se pudo mostrar esta pantalla</h1>
          <p>
            La interfaz encontró un error inesperado, pero la aplicación sigue activa.
            Revisa la configuración reciente o recarga la pantalla.
          </p>
          <small>{this.state.error.message}</small>
          <button className="primary-button" type="button" onClick={() => window.location.reload()}>
            Recargar interfaz
          </button>
        </div>
      </section>
    );
  }
}
