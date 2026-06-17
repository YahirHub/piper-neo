from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .model_store import ModelRecord, clone_without_embedded_image

MAGIC = b"PIPERNEO"
FORMAT_VERSION = 1
COMPRESSION_NONE = 0


@dataclass
class Section:
    name: str
    content_type: str
    payload: bytes
    compression: int = COMPRESSION_NONE
    offset: int = 0


def _write_u32(value: int) -> bytes:
    return struct.pack("<I", value)


def _write_u64(value: int) -> bytes:
    return struct.pack("<Q", value)


def _write_string(value: str) -> bytes:
    raw = value.encode("utf-8")
    return _write_u32(len(raw)) + raw


def _directory_size(sections: list[Section]) -> int:
    size = len(MAGIC) + 4 + 4
    for section in sections:
        size += 4 + len(section.name.encode("utf-8"))
        size += 4 + len(section.content_type.encode("utf-8"))
        size += 4 + 8 + 8 + 8
    return size


def write_neo_package(record: ModelRecord, output_path: Path) -> None:
    if not record.onnx_path or not record.onnx_path.exists():
        raise FileNotFoundError(f"No se encontró el archivo ONNX para {record.source_id}")

    metadata, image_payload = clone_without_embedded_image(record.data)
    metadata["piper_neo"] = {
        "format": "piper-neo",
        "format_version": FORMAT_VERSION,
        "model_section": "model.onnx",
        "compression": "none",
        "exported_by": "neo-model-manager",
    }

    model_bytes = record.onnx_path.read_bytes()
    metadata_bytes = json.dumps(metadata, ensure_ascii=False, indent=2).encode("utf-8")

    sections = [
        Section("metadata.json", "application/json", metadata_bytes),
        Section("model.onnx", "application/onnx", model_bytes),
    ]

    if image_payload:
        content_type, image_bytes = image_payload
        sections.append(Section("image", content_type, image_bytes))

    offset = _directory_size(sections)
    for section in sections:
        section.offset = offset
        offset += len(section.payload)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as handle:
        handle.write(MAGIC)
        handle.write(_write_u32(FORMAT_VERSION))
        handle.write(_write_u32(len(sections)))
        for section in sections:
            handle.write(_write_string(section.name))
            handle.write(_write_string(section.content_type))
            handle.write(_write_u32(section.compression))
            handle.write(_write_u64(len(section.payload)))
            handle.write(_write_u64(len(section.payload)))
            handle.write(_write_u64(section.offset))
        for section in sections:
            handle.write(section.payload)


def export_records(records: list[ModelRecord], output_dir: Path) -> list[Path]:
    exported: list[Path] = []
    for record in records:
        model_id = str(record.modelcard.get("id") or record.source_id)
        if model_id.endswith(".neo"):
            filename = model_id
        else:
            filename = f"{model_id}.neo"
        safe_name = filename.replace("/", "_").replace("\\", "_")
        output_path = output_dir / safe_name
        write_neo_package(record, output_path)
        exported.append(output_path)
    return exported
