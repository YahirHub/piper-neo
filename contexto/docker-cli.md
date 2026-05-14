# Docker CLI

Artefacto:

```txt
/mnt/data/piper-neo-master-cli-docker.zip
```

Incluye:

- `Dockerfile.cli`
- `docker-compose-cli.yml`
- `models/.gitkeep`

Comportamiento:

- Construye Piper Neo CLI/server para Linux según arquitectura Docker/Buildx.
- Instala runtime en `/app`.
- Crea `/app/models` y `/app/outputs`.
- `VOLUME` comentados para evitar volúmenes anónimos.
- Expone 8080.

Comando por defecto:

```bash
/app/piper --server --host 0.0.0.0 --port 8080 --models /app/models --output_dir /app/outputs
```

Compose monta:

```yaml
./models:/app/models
```

`./outputs` y `./.env` quedan comentados.
