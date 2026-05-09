# Prompt para recuperar contexto en una nueva conversación

Usa este texto si se abre una nueva conversación:

```text
Lee el proyecto completo y especialmente `neo-docs/`. Este repositorio es Piper Neo, una evolución optimizada de Piper TTS. Necesito que recuperes el contexto técnico, formato `.neo`, API server, scheduler justo por chunks, retención de audios, cache de modelos y decisiones de arquitectura antes de proponer cambios.
```

Después de leer:

1. Revisar `neo-docs/README.md`.
2. Revisar `neo-docs/neo-format.md`.
3. Revisar `neo-docs/server-architecture.md`.
4. Revisar `docs/api-server.md` y `docs/new-piper-usage.md`.
5. Revisar últimos commits con `git log --oneline -10`.
