# Fecha
2026-06-17

# Objetivo
Corregir la normalización automática de URLs, dominios y correos para que Piper Neo no convierta texto técnico por decisión global del servidor o del core. La pronunciación de enlaces, correos, moneda, porcentajes y otros tokens especiales queda bajo control explícito de cada modelo mediante `neo.text_normalization`.

# Problema detectado
Durante pruebas con textos técnicos se observaron conversiones no deseadas:

```txt
apt install docker.io
→ apt install enlace a docker punto io

https://github.com
→ enlace a github punto com

soporte@ejemplo.com
→ correo electronico
```

Esto ocurría por dos caminos:

1. El sanitizador del servidor resumía URLs, dominios sueltos y correos antes de llegar al modelo.
2. Las reglas inteligentes de `neo.text_normalization` se inicializaban activas por defecto en modelos editados con el Model Manager.

# Decisiones tomadas
- El servidor C++ ya no reemplaza URLs por frases tipo `enlace a ...`.
- El servidor C++ ya no reemplaza correos por `correo electronico`.
- Los dominios sueltos como `docker.io` ya no se tratan como enlaces globales del servidor.
- Las reglas inteligentes del core quedan apagadas por defecto aunque exista `neo.text_normalization`.
- El Model Manager genera valores por defecto en `builtin` como `false`.
- Si un modelo necesita leer URLs, correos, decimales, moneda o porcentajes de forma especial, debe activar la bandera correspondiente en su propio JSON.
- La normalización de URLs del core, cuando se active por modelo, ya no agrega el prefijo fijo `enlace`; solo convierte signos técnicos como `.` o `/` a palabras pronunciables.

# Resultado esperado

Sin activar reglas inteligentes del modelo:

```txt
apt install docker.io
→ se conserva como texto de entrada para eSpeak/modelo

https://github.com
→ se conserva como texto de entrada para eSpeak/modelo

soporte@ejemplo.com
→ se conserva como texto de entrada para eSpeak/modelo
```

Activando `neo.text_normalization.builtin.urls=true` en un modelo:

```txt
https://github.com
→ github punto com

https://youtube.com
→ youtube punto com
```

Activando `neo.text_normalization.builtin.emails=true`:

```txt
soporte@ejemplo.com
→ soporte arroba ejemplo punto com
```

# Archivos modificados
- `src/cpp/server.cpp`
- `src/cpp/text_normalizer.cpp`
- `apps/neo-model-manager/neo_model_manager/model_store.py`
- `apps/neo-model-manager/neo_model_manager/app.py`
- `docs/text-normalization.md`
- `docs/text-preprocessing.md`
- `apps/neo-model-manager/README.md`
- `contexto/13-normalizacion-controlada-por-modelo.md`

# Riesgos
- Modelos exportados anteriormente con `builtin.urls=true` seguirán normalizando URLs hasta que su JSON se edite y se cambie a `false`.
- Modelos que dependían de la frase fija `enlace a` deberán agregar un reemplazo o regla propia en su configuración si quieren conservar ese estilo.
- El sanitizador del servidor sigue limpiando HTML, Markdown, código, Unicode raro y cadenas peligrosas; solo deja de reescribir URLs/correos.

# Próximos pasos
- Regenerar o editar el JSON de los modelos afectados para desactivar `builtin.urls` si no se desea tocar enlaces.
- Evitar reglas manuales globales para dominios como `docker.io`, `github.com` o `youtube.com`.
- Probar textos reales en servidor con `apt install docker.io`, `https://github.com`, `https://youtube.com` y correos electrónicos.
