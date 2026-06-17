from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

from PySide6.QtCore import Qt, QSize
from PySide6.QtGui import QAction, QPixmap
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFileDialog,
    QFormLayout,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QTabWidget,
    QTextBrowser,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from .model_store import (
    DEFAULT_REPLACEMENTS,
    LANGUAGES,
    VOICE_PROMPTS,
    ModelRecord,
    calculate_sha256,
    encode_image_to_data_uri,
    ensure_modelcard_defaults,
    ensure_text_normalization_defaults,
    get_text_normalization,
    save_record,
    scan_models,
)
from .neo_package import export_records


APP_TITLE = "Piper Neo Model Manager"


COMPLETE_HELP_HTML = """
<h1>Piper Neo Model Manager · Ayuda completa</h1>
<p><b>Objetivo:</b> este programa prepara modelos fuente de Piper Neo antes de exportarlos a <code>.neo</code>. Trabaja sobre pares <code>.onnx</code> + <code>.onnx.json</code>. No está pensado para editar paquetes <code>.neo</code> ya exportados.</p>

<h2>1. Flujo correcto de trabajo</h2>
<ol>
  <li>Selecciona una carpeta con modelos fuente.</li>
  <li>Edita metadata, imagen, parámetros Piper y normalización.</li>
  <li>Guarda los JSON fuente.</li>
  <li>Exporta uno o todos los modelos a <code>.neo</code>.</li>
  <li>Usa los <code>.neo</code> en Piper Neo.</li>
</ol>
<p><b>Regla importante:</b> si después quieres cambiar nombre, imagen, reemplazos o parámetros, vuelve al modelo fuente y exporta de nuevo. El archivo <code>.neo</code> es una salida final.</p>

<h2>2. Qué es el modelo fuente</h2>
<p>Un modelo fuente normalmente está formado por:</p>
<pre>modelo.onnx
modelo.onnx.json</pre>
<p>El <code>.onnx</code> contiene la red neuronal de voz. El <code>.onnx.json</code> contiene configuración de audio, phonemizer, inferencia y metadata adicional que Piper Neo puede aprovechar.</p>

<h2>3. Metadata del modelo</h2>
<ul>
  <li><b>ID / nombre de archivo:</b> identificador principal. Si lo cambias, también se renombran <code>.onnx</code> y <code>.onnx.json</code>.</li>
  <li><b>Nombre visible:</b> nombre humano que aparece en clientes como Electron.</li>
  <li><b>Idioma:</b> ayuda a clasificar la voz y elegir reglas de normalización. Ejemplo: <code>es_MX</code>.</li>
  <li><b>Descripción:</b> explica para qué sirve la voz: narración, noticias, tutoriales, redes sociales, etc.</li>
  <li><b>Voice prompt:</b> descripción del estilo de voz. Piper clásico puede ignorarlo; Piper Neo lo usa como metadata útil para clientes y organización.</li>
  <li><b>SHA256:</b> huella del archivo <code>.onnx</code>. Sirve para saber si el binario del modelo cambió.</li>
</ul>

<h2>4. Imagen base64</h2>
<p>La imagen sirve como avatar del modelo en clientes visuales. En el JSON fuente se guarda como <code>modelcard.image</code> en formato base64:</p>
<pre>data:image/png;base64,...</pre>
<p>Al exportar a <code>.neo</code>, Piper Neo mueve esa imagen a una sección binaria del paquete para que el metadata exportado quede más limpio.</p>
<ul>
  <li><b>Agregar / cambiar imagen:</b> reemplaza la imagen actual.</li>
  <li><b>Eliminar imagen:</b> quita el avatar del JSON fuente.</li>
  <li><b>Recomendación:</b> usa imágenes pequeñas. Imágenes muy pesadas inflan el JSON fuente.</li>
</ul>

<h2>5. Parámetros Piper por defecto</h2>
<p>Estos valores quedan en <code>inference</code> y sirven como configuración base del modelo:</p>
<ul>
  <li><b>noise_scale:</b> controla variación/expresividad. Menor suele sonar más estable; mayor puede sonar más variable.</li>
  <li><b>length_scale:</b> controla duración/velocidad. Menor suele ir más rápido; mayor suele ir más lento.</li>
  <li><b>noise_w:</b> ajuste adicional de variación usado por Piper.</li>
  <li><b>sentence_silence:</b> silencio base entre oraciones, en segundos.</li>
</ul>
<p>Estos parámetros pueden ser sobrescritos en Piper Neo usando etiquetas TTS como <code>&lt;model="Voz" length_scale="1.05"&gt;</code>.</p>

<h2>6. Normalización de texto</h2>
<p>La normalización se guarda en <code>neo.text_normalization</code>. Su función es preparar el texto antes del phonemizer para mejorar pronunciación.</p>
<pre>{
  "neo": {
    "text_normalization": {
      "enabled": true,
      "locale": "es-MX",
      "builtin": { "decimals": true, "versions": true },
      "replacements": []
    }
  }
}</pre>
<p><b>Activar normalización:</b> permite que Piper Neo aplique reglas de reemplazo y futuras reglas inteligentes. Si está desactivada, el modelo conserva su texto casi sin tocar.</p>
<p><b>Locale:</b> indica variante regional. Ejemplo: <code>es-MX</code>, <code>es-AR</code>. Sirve para decidir cómo leer moneda, números o expresiones locales.</p>

<h2>7. Reglas inteligentes futuras</h2>
<p>Estas casillas no son reemplazos manuales. Son banderas para que el core de Piper Neo pueda aplicar reglas seguras por tipo de texto.</p>
<ul>
  <li><b>Decimales:</b> <code>3.5</code> debería leerse como <code>tres punto cinco</code>, no como <code>tres cinco</code>.</li>
  <li><b>Versiones:</b> <code>1.0.3</code> debe conservar sus puntos como versión, no como cortes de oración.</li>
  <li><b>Porcentajes:</b> <code>10.25%</code> debe leerse como porcentaje, no como número aislado.</li>
  <li><b>Moneda:</b> <code>$10.25</code> puede leerse como dinero según el locale.</li>
  <li><b>URLs:</b> evita romper dominios como <code>openai.com</code> o <code>thowilabs.com</code>.</li>
  <li><b>Correos:</b> evita reemplazos peligrosos dentro de direcciones como <code>demo@dominio.com</code>.</li>
</ul>

<h2>8. Reemplazos personalizados</h2>
<p>Un reemplazo personalizado cambia texto exacto antes del phonemizer. Sirve para marcas, nombres, palabras extranjeras o pronunciaciones difíciles.</p>
<pre>Facebook → Feisbuk
Amazon Prime → Amazon Praim
Prime → Praim
YouTube → Yutub</pre>
<p>Úsalos cuando una palabra se pronuncia mal de forma constante. No conviene usarlos para todo; números, URLs y moneda deben tratarse con reglas inteligentes o por criterio del modelo.</p>

<h2>9. Buscar y reemplazar</h2>
<ul>
  <li><b>Buscar:</b> texto original que puede aparecer en el guion.</li>
  <li><b>Reemplazar por:</b> texto que se enviará al phonemizer.</li>
</ul>
<p>Ejemplo:</p>
<pre>Buscar: Facebook
Reemplazar por: Feisbuk</pre>
<p>Cuando el guion diga <code>Facebook</code>, Piper Neo podrá convertirlo internamente a <code>Feisbuk</code> para mejorar la pronunciación.</p>

<h2>10. Prioridad</h2>
<p>La prioridad decide qué regla se aplica primero. Las reglas con número mayor se aplican antes.</p>
<p>Esto es importante cuando una regla contiene a otra:</p>
<pre>Amazon Prime → Amazon Praim    prioridad 100
Prime → Praim                  prioridad 80</pre>
<p>Si <code>Prime</code> se aplicara primero, podría interferir con <code>Amazon Prime</code>. Por eso las frases largas o más específicas deben tener mayor prioridad.</p>

<h2>11. Coincidir palabra completa</h2>
<p>Cuando está activado, el reemplazo solo ocurre si el texto aparece como palabra independiente.</p>
<pre>Prime → Praim</pre>
<ul>
  <li>Con palabra completa: cambia <code>Prime</code>.</li>
  <li>No cambia partes internas de palabras más largas.</li>
</ul>
<p>Recomendación: déjalo activado para marcas y palabras comunes. Desactívalo solo si sabes que necesitas reemplazar fragmentos internos.</p>

<h2>12. Distinguir mayúsculas/minúsculas</h2>
<p>Sirve para decidir si una regla debe respetar exactamente cómo está escrito el texto.</p>
<p><b>Desactivado:</b> reemplaza <code>Facebook</code>, <code>facebook</code>, <code>FACEBOOK</code> y <code>FaceBook</code>.</p>
<p><b>Activado:</b> solo reemplaza si coincide exactamente con el texto configurado.</p>
<p>Recomendación general:</p>
<ul>
  <li>Para marcas comunes: desactivado.</li>
  <li>Para siglas, códigos, nombres técnicos o palabras ambiguas: activado.</li>
</ul>

<h2>13. Nota</h2>
<p>La nota no afecta al audio. Sirve para documentar por qué existe la regla.</p>
<pre>Facebook → Feisbuk
Nota: pronunciación aproximada para voces en español</pre>

<h2>14. Orden seguro de normalización</h2>
<p>Cuando el core lo aplique, el orden recomendado será proteger primero elementos técnicos y después aplicar reemplazos:</p>
<ol>
  <li>Proteger correos, URLs y dominios.</li>
  <li>Detectar versiones.</li>
  <li>Detectar moneda, porcentajes y decimales.</li>
  <li>Aplicar reemplazos personalizados por prioridad.</li>
  <li>Enviar texto limpio al phonemizer.</li>
</ol>
<p>Esto evita que un dominio, correo o versión sea modificado por accidente.</p>

<h2>15. Qué significa exportar a .neo</h2>
<p>Exportar a <code>.neo</code> crea un paquete compatible con Piper Neo. El paquete incluye metadata, modelo ONNX e imagen opcional.</p>
<p>El objetivo es distribuir o cargar modelos de forma más limpia que con archivos sueltos.</p>
<p><b>No edites el .neo exportado.</b> Modifica el fuente y exporta otra vez.</p>

<h2>16. Qué ya hace esta app y qué falta en el core</h2>
<p>Esta app ya guarda metadata, imagen y reglas de normalización en el JSON fuente. El siguiente paso del proyecto es que el core de Piper Neo lea <code>neo.text_normalization</code> antes de <code>phonemize</code> para aplicar esas reglas durante la síntesis.</p>

<h2>17. Recomendaciones rápidas</h2>
<ul>
  <li>Usa prioridad alta para frases completas.</li>
  <li>Usa prioridad menor para palabras sueltas.</li>
  <li>Deja <b>palabra completa</b> activado casi siempre.</li>
  <li>Deja <b>mayúsculas/minúsculas</b> desactivado para marcas comunes.</li>
  <li>Activa mayúsculas/minúsculas para siglas o códigos delicados.</li>
  <li>No uses reemplazos manuales para URLs o correos si puede resolverse con reglas inteligentes.</li>
</ul>
"""


def show_help_dialog(parent: QWidget, title: str = "Ayuda Piper Neo Model Manager") -> None:
    dialog = QDialog(parent)
    dialog.setWindowTitle(title)
    dialog.setMinimumSize(880, 680)
    dialog.setWindowFlag(Qt.WindowType.WindowMaximizeButtonHint, True)
    layout = QVBoxLayout(dialog)
    browser = QTextBrowser()
    browser.setOpenExternalLinks(True)
    browser.setHtml(COMPLETE_HELP_HTML)
    layout.addWidget(browser, 1)
    buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Close)
    buttons.rejected.connect(dialog.reject)
    buttons.accepted.connect(dialog.accept)
    layout.addWidget(buttons)
    dialog.exec()


class ImageLabel(QLabel):
    def __init__(self, size: int = 96, parent: QWidget | None = None):
        super().__init__(parent)
        self._size = size
        self.setFixedSize(size, size)
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setObjectName("imagePreview")
        self.setText("Sin\nimagen")

    def set_data_uri(self, data_uri: str) -> None:
        self.clear()
        if not data_uri:
            self.setText("Sin\nimagen")
            return
        try:
            payload = data_uri.split(",", 1)[1] if "," in data_uri else data_uri
            import base64

            raw = base64.b64decode(payload)
            pixmap = QPixmap()
            if not pixmap.loadFromData(raw):
                self.setText("Imagen\ninválida")
                return
            scaled = pixmap.scaled(
                self._size - 10,
                self._size - 10,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
            self.setPixmap(scaled)
        except Exception:
            self.setText("Imagen\ninválida")


class ReplacementEditDialog(QDialog):
    def __init__(self, rule: dict[str, Any] | None = None, parent: QWidget | None = None):
        super().__init__(parent)
        self.setWindowTitle("Regla de reemplazo")
        self.setMinimumWidth(560)
        self.rule = rule or {}

        layout = QVBoxLayout(self)
        form = QFormLayout()

        self.from_edit = QLineEdit(str(self.rule.get("from", "")))
        self.to_edit = QLineEdit(str(self.rule.get("to", "")))
        self.priority_spin = QSpinBox()
        self.priority_spin.setRange(-9999, 9999)
        self.priority_spin.setValue(int(self.rule.get("priority", 0) or 0))
        self.case_check = QCheckBox("Distinguir mayúsculas/minúsculas")
        self.case_check.setChecked(bool(self.rule.get("case_sensitive", False)))
        self.word_check = QCheckBox("Coincidir palabra completa")
        self.word_check.setChecked(bool(self.rule.get("whole_word", True)))
        self.note_edit = QLineEdit(str(self.rule.get("note", "")))

        form.addRow("Buscar", self.from_edit)
        form.addRow("Reemplazar por", self.to_edit)
        form.addRow("Prioridad", self.priority_spin)
        form.addRow("Opciones", self.case_check)
        form.addRow("", self.word_check)
        form.addRow("Nota", self.note_edit)
        layout.addLayout(form)

        help_text = QLabel(
            "Las reglas con mayor prioridad se aplican primero. Usa prioridad alta para frases completas como "
            "Amazon Prime y menor prioridad para palabras sueltas como Prime."
        )
        help_text.setWordWrap(True)
        help_text.setObjectName("hint")
        layout.addWidget(help_text)

        help_btn = QPushButton("Ayuda: ¿qué significa cada opción?")
        help_btn.clicked.connect(lambda: show_help_dialog(self, "Ayuda sobre reemplazos y normalización"))
        layout.addWidget(help_btn)

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self._validate_and_accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def _validate_and_accept(self) -> None:
        if not self.from_edit.text().strip():
            QMessageBox.warning(self, "Validación", "El texto a buscar no puede estar vacío.")
            return
        self.accept()

    def get_rule(self) -> dict[str, Any]:
        return {
            "from": self.from_edit.text().strip(),
            "to": self.to_edit.text(),
            "case_sensitive": self.case_check.isChecked(),
            "whole_word": self.word_check.isChecked(),
            "priority": int(self.priority_spin.value()),
            "note": self.note_edit.text().strip(),
        }


class ModelEditDialog(QDialog):
    def __init__(self, record: ModelRecord, parent: QWidget | None = None):
        super().__init__(parent)
        self.record = record
        self.setWindowTitle(f"Editar modelo · {record.source_id}")
        self.setMinimumSize(980, 720)
        self.setWindowFlag(Qt.WindowType.WindowMaximizeButtonHint, True)
        self.setWindowFlag(Qt.WindowType.WindowMinimizeButtonHint, True)

        self._build_ui()
        self._load_record()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        header_row = QHBoxLayout()
        header = QLabel("Personalización de modelo fuente")
        header.setObjectName("dialogTitle")
        header_row.addWidget(header, 1)
        help_btn = QPushButton("Ayuda")
        help_btn.clicked.connect(lambda: show_help_dialog(self, "Ayuda del editor de modelos"))
        header_row.addWidget(help_btn)
        root.addLayout(header_row)

        self.tabs = QTabWidget()
        root.addWidget(self.tabs, 1)

        self.tabs.addTab(self._metadata_tab(), "Datos")
        self.tabs.addTab(self._normalization_tab(), "Normalización")
        self.tabs.addTab(self._help_tab(), "Ayuda")

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self._validate_and_accept)
        buttons.rejected.connect(self.reject)
        root.addWidget(buttons)

    def _metadata_tab(self) -> QWidget:
        page = QWidget()
        layout = QGridLayout(page)
        layout.setColumnStretch(0, 1)
        layout.setColumnStretch(1, 0)

        form_group = QGroupBox("Metadata Piper Neo")
        form = QFormLayout(form_group)

        self.id_edit = QLineEdit()
        self.name_edit = QLineEdit()
        self.language_combo = QComboBox()
        self.language_combo.setEditable(True)
        for code, label in LANGUAGES.items():
            self.language_combo.addItem(f"{code} · {label}", code)
        self.description_edit = QTextEdit()
        self.description_edit.setMinimumHeight(90)
        self.voice_prompt_combo = QComboBox()
        self.voice_prompt_combo.setEditable(True)
        self.voice_prompt_combo.addItems(sorted(VOICE_PROMPTS.keys()))
        self.voice_prompt_combo.currentTextChanged.connect(self._voice_prompt_changed)
        self.voice_prompt_edit = QTextEdit()
        self.voice_prompt_edit.setMinimumHeight(90)
        self.sha_edit = QLineEdit()
        self.sha_edit.setReadOnly(True)

        form.addRow("ID / nombre de archivo", self.id_edit)
        form.addRow("Nombre visible", self.name_edit)
        form.addRow("Idioma", self.language_combo)
        form.addRow("Descripción", self.description_edit)
        form.addRow("Plantilla de voz", self.voice_prompt_combo)
        form.addRow("Voice prompt", self.voice_prompt_edit)
        form.addRow("SHA256", self.sha_edit)

        inference_group = QGroupBox("Parámetros Piper por defecto")
        inference = QFormLayout(inference_group)
        self.noise_scale_edit = QLineEdit()
        self.length_scale_edit = QLineEdit()
        self.noise_w_edit = QLineEdit()
        self.sentence_silence_edit = QLineEdit()
        inference.addRow("noise_scale", self.noise_scale_edit)
        inference.addRow("length_scale", self.length_scale_edit)
        inference.addRow("noise_w", self.noise_w_edit)
        inference.addRow("sentence_silence", self.sentence_silence_edit)

        left = QVBoxLayout()
        left.addWidget(form_group)
        left.addWidget(inference_group)
        left.addStretch(1)
        layout.addLayout(left, 0, 0)

        image_group = QGroupBox("Imagen del modelo")
        image_layout = QVBoxLayout(image_group)
        self.image_preview = ImageLabel(180)
        image_layout.addWidget(self.image_preview, 0, Qt.AlignmentFlag.AlignCenter)
        self.image_info = QLabel("La imagen se guarda como base64 en el JSON fuente. Al exportar .neo se mueve a sección binaria.")
        self.image_info.setWordWrap(True)
        self.image_info.setObjectName("hint")
        image_layout.addWidget(self.image_info)
        choose_btn = QPushButton("Agregar / cambiar imagen")
        choose_btn.clicked.connect(self._choose_image)
        remove_btn = QPushButton("Eliminar imagen")
        remove_btn.setObjectName("danger")
        remove_btn.clicked.connect(self._remove_image)
        image_layout.addWidget(choose_btn)
        image_layout.addWidget(remove_btn)
        layout.addWidget(image_group, 0, 1)
        return page

    def _normalization_tab(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)

        top = QHBoxLayout()
        self.normalization_enabled = QCheckBox("Activar normalización de texto para este modelo")
        self.normalization_enabled.setChecked(True)
        self.locale_edit = QLineEdit("es-MX")
        top.addWidget(self.normalization_enabled, 1)
        top.addWidget(QLabel("Locale"))
        top.addWidget(self.locale_edit)
        help_btn = QPushButton("Ayuda")
        help_btn.clicked.connect(lambda: show_help_dialog(self, "Ayuda de normalización de texto"))
        top.addWidget(help_btn)
        layout.addLayout(top)

        builtins_group = QGroupBox("Reglas inteligentes futuras")
        builtins = QGridLayout(builtins_group)
        self.builtin_checks: dict[str, QCheckBox] = {}
        labels = {
            "decimals": "Decimales: 3.5 → tres punto cinco",
            "versions": "Versiones: 1.0.3 → versión uno punto cero punto tres",
            "percentages": "Porcentajes: 10.25%",
            "currency": "Moneda: $10.25",
            "urls": "URLs y dominios",
            "emails": "Correos electrónicos",
        }
        for i, (key, label) in enumerate(labels.items()):
            check = QCheckBox(label)
            self.builtin_checks[key] = check
            builtins.addWidget(check, i // 2, i % 2)
        layout.addWidget(builtins_group)

        replacements_group = QGroupBox("Reemplazos personalizados")
        replacements_layout = QVBoxLayout(replacements_group)
        toolbar = QHBoxLayout()
        add_btn = QPushButton("Agregar")
        add_btn.clicked.connect(self._add_replacement)
        edit_btn = QPushButton("Editar")
        edit_btn.clicked.connect(self._edit_replacement)
        delete_btn = QPushButton("Eliminar")
        delete_btn.setObjectName("danger")
        delete_btn.clicked.connect(self._delete_replacement)
        default_btn = QPushButton("Agregar ejemplos recomendados")
        default_btn.clicked.connect(self._add_default_replacements)
        toolbar.addWidget(add_btn)
        toolbar.addWidget(edit_btn)
        toolbar.addWidget(delete_btn)
        toolbar.addStretch(1)
        toolbar.addWidget(default_btn)
        replacements_layout.addLayout(toolbar)

        self.replacements_table = QTableWidget(0, 6)
        self.replacements_table.setHorizontalHeaderLabels(["Buscar", "Reemplazar", "Prioridad", "Palabra", "Mayúsculas", "Nota"])
        self.replacements_table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self.replacements_table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        self.replacements_table.horizontalHeader().setSectionResizeMode(5, QHeaderView.ResizeMode.Stretch)
        self.replacements_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.replacements_table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.replacements_table.doubleClicked.connect(self._edit_replacement)
        replacements_layout.addWidget(self.replacements_table, 1)
        layout.addWidget(replacements_group, 1)
        return page

    def _help_tab(self) -> QWidget:
        browser = QTextBrowser()
        browser.setOpenExternalLinks(True)
        browser.setHtml(COMPLETE_HELP_HTML)
        return browser

    def _load_record(self) -> None:
        card = self.record.modelcard
        ensure_modelcard_defaults(self.record.data, self.record.source_id, self.record.onnx_path)
        normalization = ensure_text_normalization_defaults(self.record.data)

        self.id_edit.setText(str(card.get("id", self.record.source_id)))
        self.name_edit.setText(str(card.get("name", self.record.source_id)))
        language = str(card.get("language", self.record.detected_language() or "es_MX"))
        for i in range(self.language_combo.count()):
            if self.language_combo.itemData(i) == language:
                self.language_combo.setCurrentIndex(i)
                break
        else:
            self.language_combo.setEditText(language)
        self.description_edit.setPlainText(str(card.get("description", "")))
        current_prompt = str(card.get("voiceprompt", ""))
        prompt_key = next((key for key, value in VOICE_PROMPTS.items() if value == current_prompt), "")
        if prompt_key:
            self.voice_prompt_combo.setCurrentText(prompt_key)
        else:
            self.voice_prompt_combo.setEditText("Personalizado")
        self.voice_prompt_edit.setPlainText(current_prompt)
        self.sha_edit.setText(str(card.get("sha256", "")))
        self.image_preview.set_data_uri(self.record.image_data_uri)

        inference = self.record.data.setdefault("inference", {})
        if not isinstance(inference, dict):
            inference = {}
            self.record.data["inference"] = inference
        self.noise_scale_edit.setText(str(inference.get("noise_scale", "")))
        self.length_scale_edit.setText(str(inference.get("length_scale", "")))
        self.noise_w_edit.setText(str(inference.get("noise_w", "")))
        self.sentence_silence_edit.setText(str(inference.get("sentence_silence", inference.get("sentence_silence_seconds", ""))))

        self.normalization_enabled.setChecked(bool(normalization.get("enabled", True)))
        self.locale_edit.setText(str(normalization.get("locale", "es-MX")))
        builtin = normalization.get("builtin", {}) if isinstance(normalization.get("builtin"), dict) else {}
        for key, check in self.builtin_checks.items():
            check.setChecked(bool(builtin.get(key, True)))
        self._refresh_replacements_table()

    def _voice_prompt_changed(self, text: str) -> None:
        if text in VOICE_PROMPTS:
            self.voice_prompt_edit.setPlainText(VOICE_PROMPTS[text])

    def _choose_image(self) -> None:
        path_text, _ = QFileDialog.getOpenFileName(
            self,
            "Seleccionar imagen",
            "",
            "Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;Todos los archivos (*.*)",
        )
        if not path_text:
            return
        path = Path(path_text)
        try:
            data_uri, size = encode_image_to_data_uri(path)
        except Exception as exc:
            QMessageBox.critical(self, "Imagen inválida", str(exc))
            return
        if size > 512 * 1024:
            answer = QMessageBox.question(
                self,
                "Imagen grande",
                "La imagen pesa más de 500 KB y hará crecer el JSON fuente. ¿Deseas usarla de todos modos?",
            )
            if answer != QMessageBox.StandardButton.Yes:
                return
        self.record.modelcard["image"] = data_uri
        self.image_preview.set_data_uri(data_uri)

    def _remove_image(self) -> None:
        self.record.modelcard.pop("image", None)
        self.image_preview.set_data_uri("")

    def _current_replacements(self) -> list[dict[str, Any]]:
        return self.record.replacements()

    def _refresh_replacements_table(self) -> None:
        rules = sorted(self._current_replacements(), key=lambda r: (-int(r.get("priority", 0)), -len(str(r.get("from", "")))))
        normalization = get_text_normalization(self.record.data)
        normalization["replacements"] = rules
        self.replacements_table.setRowCount(0)
        for rule in rules:
            row = self.replacements_table.rowCount()
            self.replacements_table.insertRow(row)
            values = [
                str(rule.get("from", "")),
                str(rule.get("to", "")),
                str(rule.get("priority", 0)),
                "Sí" if rule.get("whole_word", True) else "No",
                "Sí" if rule.get("case_sensitive", False) else "No",
                str(rule.get("note", "")),
            ]
            for col, value in enumerate(values):
                self.replacements_table.setItem(row, col, QTableWidgetItem(value))

    def _selected_replacement_index(self) -> int | None:
        selected = self.replacements_table.selectionModel().selectedRows()
        if not selected:
            return None
        find_value = self.replacements_table.item(selected[0].row(), 0).text()
        for i, rule in enumerate(self._current_replacements()):
            if str(rule.get("from", "")) == find_value:
                return i
        return None

    def _add_replacement(self) -> None:
        dialog = ReplacementEditDialog(parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            rule = dialog.get_rule()
            if any(str(item.get("from", "")).lower() == rule["from"].lower() for item in self._current_replacements()):
                QMessageBox.warning(self, "Duplicado", "Ya existe una regla con ese texto de búsqueda.")
                return
            self._current_replacements().append(rule)
            self._refresh_replacements_table()

    def _edit_replacement(self) -> None:
        index = self._selected_replacement_index()
        if index is None:
            QMessageBox.information(self, "Selecciona una regla", "Selecciona una regla para editar.")
            return
        rules = self._current_replacements()
        dialog = ReplacementEditDialog(rules[index], self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            rules[index] = dialog.get_rule()
            self._refresh_replacements_table()

    def _delete_replacement(self) -> None:
        index = self._selected_replacement_index()
        if index is None:
            return
        rules = self._current_replacements()
        answer = QMessageBox.question(self, "Eliminar", f"¿Eliminar la regla '{rules[index].get('from')}'?")
        if answer == QMessageBox.StandardButton.Yes:
            del rules[index]
            self._refresh_replacements_table()

    def _add_default_replacements(self) -> None:
        rules = self._current_replacements()
        existing = {str(rule.get("from", "")).lower() for rule in rules}
        added = 0
        for rule in DEFAULT_REPLACEMENTS:
            if rule["from"].lower() not in existing:
                rules.append(dict(rule))
                existing.add(rule["from"].lower())
                added += 1
        self._refresh_replacements_table()
        QMessageBox.information(self, "Reemplazos", f"Se agregaron {added} reemplazos recomendados.")

    def _apply_form_to_record(self) -> str:
        new_id = self.id_edit.text().strip()
        card = self.record.modelcard
        card["id"] = new_id
        card["name"] = self.name_edit.text().strip()
        card["description"] = self.description_edit.toPlainText().strip()
        lang = self.language_combo.currentData()
        card["language"] = str(lang or self.language_combo.currentText().split(" · ", 1)[0]).strip()
        card["voiceprompt"] = self.voice_prompt_edit.toPlainText().strip()
        if self.record.onnx_path and self.record.onnx_path.exists():
            card["sha256"] = calculate_sha256(self.record.onnx_path)

        inference = self.record.data.setdefault("inference", {})
        for key, editor in [
            ("noise_scale", self.noise_scale_edit),
            ("length_scale", self.length_scale_edit),
            ("noise_w", self.noise_w_edit),
            ("sentence_silence", self.sentence_silence_edit),
        ]:
            raw = editor.text().strip()
            if raw:
                try:
                    inference[key] = float(raw)
                except ValueError as exc:
                    raise ValueError(f"El parámetro {key} debe ser numérico") from exc
            else:
                inference.pop(key, None)

        normalization = ensure_text_normalization_defaults(self.record.data)
        normalization["enabled"] = self.normalization_enabled.isChecked()
        normalization["locale"] = self.locale_edit.text().strip() or "es-MX"
        builtin = normalization.setdefault("builtin", {})
        for key, check in self.builtin_checks.items():
            builtin[key] = check.isChecked()
        self._refresh_replacements_table()
        return new_id

    def _validate_and_accept(self) -> None:
        try:
            new_id = self._apply_form_to_record()
        except Exception as exc:
            QMessageBox.critical(self, "Validación", str(exc))
            return
        if not new_id:
            QMessageBox.warning(self, "Validación", "El ID del modelo no puede quedar vacío.")
            return
        if not self.name_edit.text().strip():
            QMessageBox.warning(self, "Validación", "El nombre visible no puede quedar vacío.")
            return
        self.accept()


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(APP_TITLE)
        self.resize(1360, 820)
        self.models_dir: Path | None = None
        self.records: list[ModelRecord] = []
        self._build_ui()
        self._apply_styles()

    def _build_ui(self) -> None:
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)

        header = QHBoxLayout()
        title_box = QVBoxLayout()
        title = QLabel("Piper Neo Model Manager")
        title.setObjectName("title")
        subtitle = QLabel("Personaliza modelos fuente .onnx antes de exportarlos a .neo")
        subtitle.setObjectName("subtitle")
        title_box.addWidget(title)
        title_box.addWidget(subtitle)
        header.addLayout(title_box, 1)
        help_btn = QPushButton("Ayuda completa")
        help_btn.clicked.connect(self._show_quick_help)
        header.addWidget(help_btn)
        layout.addLayout(header)

        folder_box = QGroupBox("Carpeta de modelos fuente")
        folder_layout = QHBoxLayout(folder_box)
        self.folder_edit = QLineEdit()
        self.folder_edit.setPlaceholderText("Selecciona una carpeta con archivos .onnx y .onnx.json")
        browse_btn = QPushButton("Seleccionar")
        browse_btn.clicked.connect(self._select_folder)
        refresh_btn = QPushButton("Actualizar")
        refresh_btn.clicked.connect(self._load_models)
        folder_layout.addWidget(self.folder_edit, 1)
        folder_layout.addWidget(browse_btn)
        folder_layout.addWidget(refresh_btn)
        layout.addWidget(folder_box)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        layout.addWidget(splitter, 1)

        left = QWidget()
        left_layout = QVBoxLayout(left)
        self.status_label = QLabel("Sin modelos cargados")
        self.status_label.setObjectName("status")
        left_layout.addWidget(self.status_label)
        self.table = QTableWidget(0, 9)
        self.table.setHorizontalHeaderLabels([
            "Imagen", "Archivo", "ID", "Nombre", "Idioma", "Reemplazos", "Normalización", "ONNX", "Estado"
        ])
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Fixed)
        self.table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        self.table.setColumnWidth(0, 74)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.doubleClicked.connect(self._edit_selected)
        left_layout.addWidget(self.table, 1)

        actions = QHBoxLayout()
        edit_btn = QPushButton("Editar seleccionado")
        edit_btn.clicked.connect(self._edit_selected)
        save_all_btn = QPushButton("Guardar JSONs")
        save_all_btn.clicked.connect(self._save_all)
        export_one_btn = QPushButton("Exportar seleccionado .neo")
        export_one_btn.clicked.connect(self._export_selected)
        export_all_btn = QPushButton("Exportar todos .neo")
        export_all_btn.clicked.connect(self._export_all)
        actions.addWidget(edit_btn)
        actions.addWidget(save_all_btn)
        actions.addStretch(1)
        actions.addWidget(export_one_btn)
        actions.addWidget(export_all_btn)
        left_layout.addLayout(actions)
        splitter.addWidget(left)

        right = QWidget()
        right_layout = QVBoxLayout(right)
        explanation = QTextBrowser()
        explanation.setHtml(
            """
            <h3>Flujo Piper Neo</h3>
            <ol>
              <li>Edita el modelo fuente <code>.onnx.json</code>.</li>
              <li>Agrega imagen base64 y metadata visual.</li>
              <li>Configura reemplazos y normalización para pronunciación.</li>
              <li>Exporta a <code>.neo</code> cuando el modelo esté listo.</li>
            </ol>
            <p><b>Importante:</b> el manager no edita paquetes <code>.neo</code>. El <code>.neo</code> es una salida final para Piper Neo.</p>
            <h4>Ejemplos de reemplazos</h4>
            <pre>Amazon Prime → Amazon Praim\nFacebook → Feisbuk\nPrime → Praim</pre>
            <h4>Reglas inteligentes</h4>
            <p>Decimales, versiones, porcentajes, monedas, URLs y correos se guardan como flags para que el core pueda normalizarlos sin romper casos técnicos.</p>
            """
        )
        right_layout.addWidget(explanation, 1)
        splitter.addWidget(right)
        splitter.setSizes([980, 380])

        menu = self.menuBar().addMenu("Archivo")
        open_action = QAction("Seleccionar carpeta", self)
        open_action.triggered.connect(self._select_folder)
        menu.addAction(open_action)
        refresh_action = QAction("Actualizar", self)
        refresh_action.triggered.connect(self._load_models)
        menu.addAction(refresh_action)

    def _apply_styles(self) -> None:
        self.setStyleSheet(
            """
            QMainWindow, QWidget { background: #0b0f14; color: #e7eef7; font-family: Inter, Segoe UI, Arial; font-size: 14px; }
            QLabel#title { font-size: 24px; font-weight: 800; color: #ffffff; }
            QLabel#subtitle, QLabel#hint, QLabel#status { color: #96a3b4; }
            QGroupBox { border: 1px solid #1f2a37; border-radius: 12px; margin-top: 12px; padding: 12px; background: #101720; font-weight: 700; }
            QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #b7c5d8; }
            QLineEdit, QTextEdit, QTextBrowser, QComboBox, QTableWidget, QTabWidget::pane { background: #111827; border: 1px solid #253245; border-radius: 10px; color: #e7eef7; selection-background-color: #2dd4bf; selection-color: #00110f; }
            QTextBrowser { padding: 10px; }
            QPushButton { background: #172233; border: 1px solid #2b3a50; border-radius: 10px; padding: 9px 14px; color: #eef6ff; font-weight: 700; }
            QPushButton:hover { background: #223249; border-color: #3b82f6; }
            QPushButton:pressed { background: #0f766e; }
            QPushButton#danger { background: #3a1420; border-color: #7f1d1d; color: #fecaca; }
            QHeaderView::section { background: #0f172a; color: #b7c5d8; border: none; border-bottom: 1px solid #253245; padding: 8px; }
            QTableWidget { gridline-color: #1f2a37; alternate-background-color: #0d131c; }
            QTableWidget::item { padding: 8px; }
            QTableWidget::item:selected { background: #0f766e; color: #ffffff; }
            QLabel#imagePreview { border: 1px dashed #40506a; border-radius: 16px; background: #0f172a; color: #96a3b4; }
            QTabBar::tab { background: #111827; border: 1px solid #253245; border-bottom: none; padding: 8px 14px; border-top-left-radius: 8px; border-top-right-radius: 8px; }
            QTabBar::tab:selected { background: #182235; color: #ffffff; }
            """
        )

    def _show_quick_help(self) -> None:
        show_help_dialog(self, "Ayuda completa de Piper Neo Model Manager")

    def _select_folder(self) -> None:
        folder = QFileDialog.getExistingDirectory(self, "Seleccionar carpeta de modelos")
        if folder:
            self.folder_edit.setText(folder)
            self._load_models()

    def _load_models(self) -> None:
        folder = Path(self.folder_edit.text().strip())
        if not folder.exists():
            QMessageBox.warning(self, "Carpeta inválida", "Selecciona una carpeta existente.")
            return
        try:
            self.records = scan_models(folder)
            self.models_dir = folder
        except Exception as exc:
            QMessageBox.critical(self, "Error al cargar", str(exc))
            return
        self._refresh_table()

    def _refresh_table(self) -> None:
        self.table.setRowCount(0)
        for record in self.records:
            row = self.table.rowCount()
            self.table.insertRow(row)
            self.table.setRowHeight(row, 68)

            image = ImageLabel(52)
            image.set_data_uri(record.image_data_uri)
            self.table.setCellWidget(row, 0, image)

            normalization = get_text_normalization(record.data)
            values = [
                record.source_id,
                record.display_id,
                record.display_name,
                record.language,
                str(len(record.replacements())),
                "Activa" if normalization.get("enabled", True) else "Inactiva",
                "Sí" if record.onnx_path and record.onnx_path.exists() else "No",
                self._record_status(record),
            ]
            for col, value in enumerate(values, start=1):
                self.table.setItem(row, col, QTableWidgetItem(value))
        missing = sum(1 for record in self.records if not record.onnx_path)
        self.status_label.setText(f"{len(self.records)} modelos cargados · {missing} sin ONNX")

    def _record_status(self, record: ModelRecord) -> str:
        card = record.modelcard
        required = ["id", "name", "description", "language", "voiceprompt"]
        if not record.onnx_path:
            return "Falta ONNX"
        if all(card.get(key) for key in required):
            return "Listo"
        return "Incompleto"

    def _selected_record(self) -> ModelRecord | None:
        selected = self.table.selectionModel().selectedRows()
        if not selected:
            return None
        row = selected[0].row()
        if 0 <= row < len(self.records):
            return self.records[row]
        return None

    def _edit_selected(self) -> None:
        record = self._selected_record()
        if not record:
            QMessageBox.information(self, "Selecciona un modelo", "Selecciona un modelo para editar.")
            return
        dialog = ModelEditDialog(record, self)
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return
        new_id = record.modelcard.get("id", record.source_id)
        try:
            updated = save_record(record, str(new_id))
        except Exception as exc:
            QMessageBox.critical(self, "Error al guardar", str(exc))
            return
        index = self.records.index(record)
        self.records[index] = updated
        self._refresh_table()
        QMessageBox.information(self, "Guardado", "El modelo fuente fue actualizado correctamente.")

    def _save_all(self) -> None:
        if not self.records:
            return
        try:
            for record in self.records:
                ensure_modelcard_defaults(record.data, record.source_id, record.onnx_path)
                ensure_text_normalization_defaults(record.data)
                save_record(record)
        except Exception as exc:
            QMessageBox.critical(self, "Error al guardar", str(exc))
            return
        QMessageBox.information(self, "Guardado", "Todos los JSON fuente fueron guardados.")

    def _export_selected(self) -> None:
        record = self._selected_record()
        if not record:
            QMessageBox.information(self, "Selecciona un modelo", "Selecciona un modelo para exportar.")
            return
        self._export_records([record])

    def _export_all(self) -> None:
        if not self.records:
            return
        self._export_records(self.records)

    def _export_records(self, records: list[ModelRecord]) -> None:
        output = QFileDialog.getExistingDirectory(self, "Seleccionar carpeta de salida .neo")
        if not output:
            return
        try:
            # Persist current JSON before packaging, so the source remains aligned with the exported .neo.
            for record in records:
                save_record(record)
            exported = export_records(records, Path(output))
        except Exception as exc:
            QMessageBox.critical(self, "Error al exportar", str(exc))
            return
        QMessageBox.information(self, "Exportación completa", f"Se exportaron {len(exported)} archivo(s) .neo en:\n{output}")


def main() -> None:
    app = QApplication(sys.argv)
    app.setApplicationName(APP_TITLE)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())
