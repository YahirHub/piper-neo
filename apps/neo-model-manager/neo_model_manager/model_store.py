from __future__ import annotations

import base64
import copy
import hashlib
import json
import mimetypes
import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any


LANGUAGES: dict[str, str] = {
    "es_MX": "Español - México",
    "es_AR": "Español - Argentina",
    "es_ES": "Español - España",
    "es_LA": "Español - Latinoamérica",
    "en_US": "English - United States",
    "en_GB": "English - Great Britain",
    "pt_BR": "Português - Brasil",
    "fr_FR": "Français - France",
    "de_DE": "Deutsch - Deutschland",
    "it_IT": "Italiano - Italia",
}

VOICE_PROMPTS: dict[str, str] = {
    "Narrador profesional": "Voz profesional de narrador, clara, estable y útil para videos educativos, notas informativas y contenido de redes sociales.",
    "Presentador de noticias": "Voz firme y confiable para notas periodísticas, reportes y contenido informativo.",
    "Asistente virtual": "Voz amigable y precisa para respuestas cortas, asistentes locales y sistemas de atención.",
    "Locutor comercial": "Voz dinámica y persuasiva para anuncios, promociones y videos cortos.",
    "Profesor carismático": "Voz clara, paciente y didáctica para explicar temas complejos de forma sencilla.",
    "Narrador de documentales": "Voz pausada, descriptiva y seria para documentales, historia, ciencia y naturaleza.",
    "Voz técnica": "Voz precisa y ordenada para tutoriales, documentación y contenido técnico.",
    "Voz relajante": "Voz suave y calmada para narraciones tranquilas, bienestar y lectura pausada.",
}

DEFAULT_REPLACEMENTS: list[dict[str, Any]] = [
    {
        "from": "Amazon Prime",
        "to": "Amazon Praim",
        "case_sensitive": False,
        "whole_word": True,
        "priority": 100,
        "note": "Marca completa antes que palabra suelta",
    },
    {
        "from": "Facebook",
        "to": "Feisbuk",
        "case_sensitive": False,
        "whole_word": True,
        "priority": 90,
        "note": "Pronunciación aproximada en español",
    },
    {
        "from": "Prime",
        "to": "Praim",
        "case_sensitive": False,
        "whole_word": True,
        "priority": 80,
        "note": "Pronunciación aproximada en español",
    },
    {
        "from": "YouTube",
        "to": "Yutub",
        "case_sensitive": False,
        "whole_word": True,
        "priority": 80,
        "note": "Pronunciación aproximada en español",
    },
]


@dataclass
class ModelRecord:
    source_id: str
    onnx_path: Path | None
    config_path: Path
    data: dict[str, Any]

    @property
    def modelcard(self) -> dict[str, Any]:
        value = self.data.setdefault("modelcard", {})
        if not isinstance(value, dict):
            value = {}
            self.data["modelcard"] = value
        return value

    @property
    def neo(self) -> dict[str, Any]:
        value = self.data.setdefault("neo", {})
        if not isinstance(value, dict):
            value = {}
            self.data["neo"] = value
        return value

    @property
    def display_id(self) -> str:
        return str(self.modelcard.get("id") or self.source_id)

    @property
    def display_name(self) -> str:
        return str(self.modelcard.get("name") or self.source_id)

    @property
    def language(self) -> str:
        return str(self.modelcard.get("language") or self.detected_language() or "")

    @property
    def image_data_uri(self) -> str:
        image = self.modelcard.get("image")
        return image if isinstance(image, str) else ""

    def detected_language(self) -> str:
        # Common Piper file names start with xx_YY-voice-name.
        match = re.match(r"^([a-z]{2}_[A-Z]{2})", self.source_id)
        return match.group(1) if match else ""

    def replacements(self) -> list[dict[str, Any]]:
        normalization = get_text_normalization(self.data)
        value = normalization.setdefault("replacements", [])
        if not isinstance(value, list):
            value = []
            normalization["replacements"] = value
        migrated: list[dict[str, Any]] = []
        changed = False
        for item in value:
            normalized = normalize_replacement_rule(item)
            if normalized is None:
                changed = True
                continue
            migrated.append(normalized)
            if normalized is not item:
                changed = True
        if changed:
            normalization["replacements"] = migrated
        return normalization["replacements"]


def model_id_from_config_path(path: Path) -> str:
    name = path.name
    if name.endswith(".onnx.json"):
        return name[: -len(".onnx.json")]
    if name.endswith(".json"):
        return name[: -len(".json")]
    return path.stem


def calculate_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError("El archivo JSON no contiene un objeto raíz válido")
    return value


def save_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(data, handle, ensure_ascii=False, indent=2)
        handle.write("\n")


def scan_models(folder: Path) -> list[ModelRecord]:
    records: list[ModelRecord] = []
    if not folder.exists():
        return records

    for config_path in sorted(folder.glob("*.onnx.json")):
        source_id = model_id_from_config_path(config_path)
        onnx_path = folder / f"{source_id}.onnx"
        data = load_json(config_path)
        ensure_modelcard_defaults(data, source_id, onnx_path if onnx_path.exists() else None)
        ensure_text_normalization_defaults(data)
        migrate_legacy_replacements(data)
        records.append(ModelRecord(source_id, onnx_path if onnx_path.exists() else None, config_path, data))
    return records


def ensure_modelcard_defaults(data: dict[str, Any], source_id: str, onnx_path: Path | None = None) -> dict[str, Any]:
    modelcard = data.setdefault("modelcard", {})
    if not isinstance(modelcard, dict):
        modelcard = {}
        data["modelcard"] = modelcard

    modelcard.setdefault("id", source_id)
    modelcard.setdefault("name", source_id)
    modelcard.setdefault("description", f"Modelo de voz Piper Neo {source_id}.")

    detected = ""
    match = re.match(r"^([a-z]{2}_[A-Z]{2})", source_id)
    if match:
        detected = match.group(1)
    modelcard.setdefault("language", detected or "es_MX")
    modelcard.setdefault("voiceprompt", VOICE_PROMPTS["Narrador profesional"])

    if onnx_path and onnx_path.exists():
        try:
            modelcard["sha256"] = calculate_sha256(onnx_path)
        except OSError:
            pass
    return modelcard


def default_text_normalization(locale: str = "es-MX") -> dict[str, Any]:
    return {
        "enabled": True,
        "locale": locale,
        "builtin": {
            "decimals": True,
            "versions": True,
            "percentages": True,
            "currency": True,
            "urls": True,
            "emails": True,
        },
        "replacements": [],
    }


def get_text_normalization(data: dict[str, Any]) -> dict[str, Any]:
    neo = data.setdefault("neo", {})
    if not isinstance(neo, dict):
        neo = {}
        data["neo"] = neo
    normalization = neo.setdefault("text_normalization", default_text_normalization())
    if not isinstance(normalization, dict):
        normalization = default_text_normalization()
        neo["text_normalization"] = normalization
    return normalization


def ensure_text_normalization_defaults(data: dict[str, Any]) -> dict[str, Any]:
    normalization = get_text_normalization(data)
    defaults = default_text_normalization()
    normalization.setdefault("enabled", defaults["enabled"])
    normalization.setdefault("locale", defaults["locale"])
    builtin = normalization.setdefault("builtin", {})
    if not isinstance(builtin, dict):
        builtin = {}
        normalization["builtin"] = builtin
    for key, value in defaults["builtin"].items():
        builtin.setdefault(key, value)
    normalization.setdefault("replacements", [])
    return normalization


def normalize_replacement_rule(item: Any) -> dict[str, Any] | None:
    if isinstance(item, dict):
        from_value = str(item.get("from", "")).strip()
        if not from_value:
            return None
        return {
            "from": from_value,
            "to": str(item.get("to", "")),
            "case_sensitive": bool(item.get("case_sensitive", False)),
            "whole_word": bool(item.get("whole_word", True)),
            "priority": int(item.get("priority", 0) or 0),
            "note": str(item.get("note", "")),
        }
    if isinstance(item, (list, tuple)) and len(item) >= 2:
        from_value = str(item[0]).strip()
        if not from_value:
            return None
        return {
            "from": from_value,
            "to": str(item[1]),
            "case_sensitive": False,
            "whole_word": True,
            "priority": 0,
            "note": "Migrado desde modelcard.replacements",
        }
    return None


def migrate_legacy_replacements(data: dict[str, Any]) -> bool:
    modelcard = data.get("modelcard")
    if not isinstance(modelcard, dict):
        return False
    legacy = modelcard.get("replacements")
    if not isinstance(legacy, list) or not legacy:
        return False

    normalization = ensure_text_normalization_defaults(data)
    current = normalization.setdefault("replacements", [])
    if not isinstance(current, list):
        current = []
        normalization["replacements"] = current
    existing = {str(item.get("from", "")).lower() for item in current if isinstance(item, dict)}

    changed = False
    for item in legacy:
        normalized = normalize_replacement_rule(item)
        if not normalized:
            continue
        key = normalized["from"].lower()
        if key not in existing:
            current.append(normalized)
            existing.add(key)
            changed = True
    return changed


def encode_image_to_data_uri(path: Path, max_bytes_warning: int = 512 * 1024) -> tuple[str, int]:
    mime_type, _ = mimetypes.guess_type(path.name)
    if not mime_type or not mime_type.startswith("image/"):
        raise ValueError("El archivo seleccionado no parece ser una imagen válida")
    data = path.read_bytes()
    encoded = base64.b64encode(data).decode("ascii")
    return f"data:{mime_type};base64,{encoded}", len(data)


def decode_data_uri(data_uri: str) -> tuple[str, bytes]:
    if not data_uri.startswith("data:image/") or "," not in data_uri:
        raise ValueError("Imagen base64 inválida")
    meta, payload = data_uri.split(",", 1)
    if ";base64" not in meta.lower():
        raise ValueError("Imagen base64 inválida")
    content_type = meta[5:].split(";", 1)[0]
    return content_type, base64.b64decode(payload)


def clone_without_embedded_image(data: dict[str, Any]) -> tuple[dict[str, Any], tuple[str, bytes] | None]:
    metadata = copy.deepcopy(data)
    image_payload: tuple[str, bytes] | None = None
    modelcard = metadata.get("modelcard")
    if isinstance(modelcard, dict):
        image = modelcard.pop("image", None)
        if isinstance(image, str) and image:
            image_payload = decode_data_uri(image)
    return metadata, image_payload


def save_record(record: ModelRecord, rename_to: str | None = None) -> ModelRecord:
    target_id = rename_to.strip() if rename_to else record.source_id
    if not target_id:
        raise ValueError("El ID del modelo no puede quedar vacío")
    if any(ch in target_id for ch in '<>:"/\\|?*'):
        raise ValueError("El ID contiene caracteres no permitidos para nombre de archivo")

    target_config = record.config_path.with_name(f"{target_id}.onnx.json")
    target_onnx = record.config_path.with_name(f"{target_id}.onnx")

    if target_id != record.source_id:
        if target_config.exists() or target_onnx.exists():
            raise FileExistsError("Ya existe un modelo con ese ID")
        if record.onnx_path and record.onnx_path.exists():
            os.replace(record.onnx_path, target_onnx)
        save_json(target_config, record.data)
        record.config_path.unlink(missing_ok=True)
        return ModelRecord(target_id, target_onnx if target_onnx.exists() else None, target_config, record.data)

    save_json(record.config_path, record.data)
    return record
