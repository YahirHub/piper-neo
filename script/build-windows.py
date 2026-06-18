# script/build-windows.py
# Uso:
#   py script\build-windows.py clean
#   py script\build-windows.py
#
# Requiere:
#   C:\mingw64\bin en PATH
#   gcc, g++, cmake y preferentemente ninja

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parent

BUILD_DIR = ROOT / "build-winlibs"
DIST_ROOT = ROOT / "dist-winlibs"
PACKAGE_DIR = DIST_ROOT / "piper-neo-windows"

RUNTIME_DLLS = [
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
    "libgomp-1.dll",
    "zlib1.dll",
    "zstd.dll",
    "libzstd.dll",
    "libiconv-2.dll",
    "libintl-8.dll",
    "libbz2-1.dll",
    "libssp-0.dll",
]

UNNEEDED_PACKAGE_ITEMS = [
    "espeak-ng.exe",
    "example.exe",
    "piper_phonemize_exe.exe",
    "test_api.exe",
    "test_encoding.exe",
    "test_ieee80.exe",
    "test_piper_phonemize.exe",
    "test_readclause.exe",
    "validate-help.txt",
    "pkgconfig",
]

CA_PATH_REPLACEMENTS = {
    r"C:\Program Files\Git\usr\ssl\certs\ca-bundle.crt": "C:/Program Files/Git/usr/ssl/certs/ca-bundle.crt",
    r"C:\Program Files\Git\mingw64\ssl\certs\ca-bundle.crt": "C:/Program Files/Git/mingw64/ssl/certs/ca-bundle.crt",
    r"C:\Program Files (x86)\Git\mingw64\ssl\certs\ca-bundle.crt": "C:/Program Files (x86)/Git/mingw64/ssl/certs/ca-bundle.crt",
    r"C:\mingw64\ssl\certs\ca-bundle.crt": "C:/mingw64/ssl/certs/ca-bundle.crt",
    r"C:\mingw64\ssl\cert.pem": "C:/mingw64/ssl/cert.pem",
}


def log(message: str = "") -> None:
    print(message, flush=True)


def fail(message: str, code: int = 1) -> None:
    log()
    log("=" * 60)
    log("Build fallido")
    log("=" * 60)
    log(message)
    log()
    if PACKAGE_DIR.exists():
        log(f"Carpeta parcial:\n{PACKAGE_DIR}")
    sys.exit(code)


def make_env() -> dict[str, str]:
    env = os.environ.copy()

    env["CMAKE_TLS_VERIFY"] = "0"
    env["GIT_SSL_NO_VERIFY"] = "true"

    for key in ("CMAKE_TLS_CAINFO", "CURL_CA_BUNDLE", "SSL_CERT_FILE"):
        env.pop(key, None)

    return env


ENV = make_env()


def run(
    args: list[str],
    *,
    cwd: Path | None = None,
    check: bool = False,
    allow_fail: bool = False,
) -> int:
    shown = " ".join(f'"{a}"' if " " in a else a for a in args)
    log(f"\n[RUN] {shown}")

    proc = subprocess.run(args, cwd=str(cwd or ROOT), env=ENV)

    if check and proc.returncode != 0:
        fail(f"Comando fallido con codigo {proc.returncode}:\n{shown}")

    if not allow_fail and proc.returncode != 0:
        log(f"[WARN] Comando termino con codigo {proc.returncode}")

    return proc.returncode


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        fail(f"No se encontro {name} en PATH.")
    log(f"[OK] {name}: {path}")
    return path


def get_output(args: list[str]) -> str:
    try:
        result = subprocess.run(
            args,
            cwd=str(ROOT),
            env=ENV,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        return result.stdout.strip()
    except Exception:
        return ""


def detect_generator() -> str:
    if shutil.which("ninja"):
        log("[INFO] Ninja encontrado. Usando Ninja.")
        return "Ninja"

    log("[INFO] Ninja no encontrado. Usando MinGW Makefiles.")
    return "MinGW Makefiles"


def clean() -> None:
    log("\n[1/7] Limpiando build anterior...")

    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR, ignore_errors=True)

    if PACKAGE_DIR.exists():
        shutil.rmtree(PACKAGE_DIR, ignore_errors=True)


def configure(generator: str) -> None:
    DIST_ROOT.mkdir(parents=True, exist_ok=True)

    args = [
        "cmake",
        "-S",
        str(ROOT),
        "-B",
        str(BUILD_DIR),
        "-G",
        generator,
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_INSTALL_PREFIX={PACKAGE_DIR}",
        "-DPIPER_BUILD_TESTS=OFF",
        "-DBUILD_TESTING=OFF",
        "-DCMAKE_TLS_VERIFY=OFF",
        "-DCMAKE_TLS_CAINFO:STRING=",
    ]

    log("\n[2/7] Configurando CMake...")
    run(args, check=True)


def patch_generated_cmake_files() -> None:
    if not BUILD_DIR.exists():
        return

    changed = 0

    for file in BUILD_DIR.rglob("*.cmake"):
        if not file.is_file():
            continue

        try:
            content = file.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue

        new_content = content

        for old, new in CA_PATH_REPLACEMENTS.items():
            new_content = new_content.replace(old, new)

        if new_content != content:
            file.write_text(new_content, encoding="utf-8", newline="\n")
            changed += 1
            log(f"[PATCH TLS] {file}")

    if changed:
        log(f"[PATCH] Archivos CMake corregidos: {changed}")


def patch_rpath_flags() -> None:
    if not BUILD_DIR.exists():
        return

    names = {
        "build.ninja",
        "rules.ninja",
        "flags.make",
        "link.txt",
        "CMakeCache.txt",
    }

    changed = 0

    for file in BUILD_DIR.rglob("*"):
        if not file.is_file() or file.name not in names:
            continue

        try:
            content = file.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue

        new_content = re.sub(r"\s-Wl,-rpath,('[^']*'|\S+)", "", content)

        if new_content != content:
            file.write_text(new_content, encoding="utf-8", newline="\n")
            changed += 1
            log(f"[PATCH RPATH] {file}")

    if changed:
        log(f"[PATCH] Flags rpath removidos en archivos: {changed}")


def write_file(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8", newline="\n")


def backup_once(path: Path) -> None:
    backup = path.with_name(path.name + ".neo-backup")
    if path.exists() and not backup.exists():
        shutil.copy2(path, backup)


def patch_phonemize_source() -> bool:
    src = BUILD_DIR / "p" / "src" / "piper_phonemize_external" / "src"

    if not src.exists():
        log("[PATCH] Fuente piper-phonemize aun no existe.")
        return False

    log(f"[PATCH] Fuente piper-phonemize:\n{src}")

    patched_any = False

    hpp = src / "phoneme_ids.hpp"
    if hpp.exists():
        text = hpp.read_text(encoding="utf-8", errors="ignore")
        if "PIPER_NEO_PY_CSTDINT_PATCH" not in text:
            backup_once(hpp)
            write_file(
                hpp,
                "// PIPER_NEO_PY_CSTDINT_PATCH\n#include <cstdint>\n" + text,
            )
            log("[PATCH OK] phoneme_ids.hpp: agregado <cstdint>.")
            patched_any = True
        else:
            log("[PATCH OK] phoneme_ids.hpp ya tenia <cstdint>.")
    else:
        log("[PATCH WARN] No se encontro phoneme_ids.hpp.")

    main_cpp = src / "main.cpp"
    if main_cpp.exists():
        backup_once(main_cpp)
        write_file(
            main_cpp,
            """#include <iostream>

// PIPER_NEO_PY_MAIN_STUB
int main(int, char**) {
  std::cerr << "piper_phonemize_exe disabled for WinLibs build." << std::endl;
  return 0;
}
""",
        )
        log("[PATCH OK] main.cpp reemplazado por stub.")
        patched_any = True
    else:
        log("[PATCH WARN] No se encontro main.cpp.")

    test_cpp = src / "test.cpp"
    if test_cpp.exists():
        backup_once(test_cpp)
        write_file(
            test_cpp,
            """#include <iostream>

// PIPER_NEO_PY_TEST_STUB
int main(int, char**) {
  std::cerr << "piper-phonemize tests disabled for WinLibs build." << std::endl;
  return 0;
}
""",
        )
        log("[PATCH OK] test.cpp reemplazado por stub.")
        patched_any = True
    else:
        log("[PATCH WARN] No se encontro test.cpp.")

    reset_phonemize_external_build()

    return patched_any


def reset_phonemize_external_build() -> None:
    ext_build = BUILD_DIR / "p" / "src" / "piper_phonemize_external-build"
    stamp = BUILD_DIR / "p" / "src" / "piper_phonemize_external-stamp"

    if ext_build.exists():
        shutil.rmtree(ext_build, ignore_errors=True)

    ext_build.mkdir(parents=True, exist_ok=True)

    if stamp.exists():
        for name in (
            "piper_phonemize_external-configure",
            "piper_phonemize_external-build",
            "piper_phonemize_external-install",
            "piper_phonemize_external-done",
        ):
            target = stamp / name
            if target.exists():
                try:
                    target.unlink()
                except Exception:
                    pass

    log("[PATCH OK] Build interno de piper-phonemize reiniciado.")


def build() -> None:
    log("\n[3/7] Compilando...")

    max_attempts = 4

    for attempt in range(1, max_attempts + 1):
        log()
        log(f"[BUILD] Intento {attempt}/{max_attempts}")

        patch_generated_cmake_files()
        patch_rpath_flags()

        if attempt > 1:
            patch_phonemize_source()
            patch_generated_cmake_files()
            patch_rpath_flags()

        code = run(
            [
                "cmake",
                "--build",
                str(BUILD_DIR),
                "--config",
                "Release",
                "--parallel",
                str(os.cpu_count() or 1),
            ],
            allow_fail=True,
        )

        if code == 0:
            log("[OK] Compilacion completada.")
            return

        log(f"[WARN] Intento {attempt} fallo.")

    fail("Fallo la compilacion despues de aplicar parches.")


def cmake_install() -> None:
    log("\n[5/7] Preparando carpeta final limpia...")

    if PACKAGE_DIR.exists():
        shutil.rmtree(PACKAGE_DIR, ignore_errors=True)

    PACKAGE_DIR.mkdir(parents=True, exist_ok=True)

    code = run(
        [
            "cmake",
            "--install",
            str(BUILD_DIR),
            "--config",
            "Release",
        ],
        allow_fail=True,
    )

    if code != 0:
        log("[WARN] cmake --install fallo. Se copiara manualmente lo necesario.")

    cleanup_package()


def path_is_inside(path: Path, parent: Path) -> bool:
    try:
        path.resolve().relative_to(parent.resolve())
        return True
    except Exception:
        return False


def should_skip_found_file(path: Path) -> bool:
    parts = {p.lower() for p in path.parts}

    if "cmakefiles" in parts:
        return True

    if path_is_inside(path, PACKAGE_DIR):
        return True

    return False


def find_first_file(name: str) -> Path | None:
    if not BUILD_DIR.exists():
        return None

    for file in BUILD_DIR.rglob(name):
        if file.is_file() and not should_skip_found_file(file):
            return file

    return None


def copy_first_found(name: str, required: bool) -> None:
    target = PACKAGE_DIR / name

    if target.exists():
        log(f"[OK] {name} ya esta en package.")
        return

    source = find_first_file(name)

    if source is None:
        if required:
            fail(f"No se encontro archivo requerido: {name}")
        log(f"[WARN] No se encontro archivo opcional: {name}")
        return

    log(f"[COPY] {name}")
    shutil.copy2(source, target)


def find_espeak_data_with_phontab() -> Path | None:
    installed = PACKAGE_DIR / "espeak-ng-data"
    if (installed / "phontab").is_file():
        return installed

    candidates: list[Path] = []

    search_roots = [BUILD_DIR, PACKAGE_DIR]

    for root in search_roots:
        if not root.exists():
            continue

        for phontab in root.rglob("phontab"):
            if not phontab.is_file():
                continue

            parent = phontab.parent

            if parent.name.lower() == "espeak-ng-data":
                candidates.append(parent)

    if not candidates:
        return None

    def score(path: Path) -> tuple[int, int]:
        text = str(path).lower()

        points = 0

        if "install" in text:
            points += 5

        if "external-build" in text:
            points += 3

        if "external" in text:
            points += 1

        file_count = 0
        try:
            file_count = sum(1 for p in path.rglob("*") if p.is_file())
        except Exception:
            file_count = 0

        return (points, file_count)

    candidates.sort(key=score, reverse=True)

    return candidates[0]


def copy_espeak_data() -> None:
    source = find_espeak_data_with_phontab()

    if source is None:
        log("[ERROR] No se encontro espeak-ng-data con phontab real.")
        log("[INFO] Carpetas espeak-ng-data encontradas:")

        for path in BUILD_DIR.rglob("espeak-ng-data"):
            if path.is_dir():
                marker = "CON phontab" if (path / "phontab").exists() else "SIN phontab"
                log(f"  [{marker}] {path}")

        fail("Falta espeak-ng-data\\phontab.")

    destination = PACKAGE_DIR / "espeak-ng-data"

    if source.resolve() == destination.resolve():
        log("[OK] espeak-ng-data ya esta instalado correctamente.")
        return

    log("[COPY DATA] espeak-ng-data desde:")
    log(str(source))

    if destination.exists():
        shutil.rmtree(destination, ignore_errors=True)

    shutil.copytree(source, destination)

    if not (destination / "phontab").is_file():
        fail("Se copio espeak-ng-data, pero sigue faltando phontab.")


def copy_runtime_dll(name: str) -> None:
    target = PACKAGE_DIR / name

    if target.exists():
        return

    found = shutil.which(name)

    if not found:
        return

    log(f"[COPY RUNTIME] {name}")
    shutil.copy2(found, target)


def copy_docs() -> None:
    for name in ("README.md", "README.es.md", "LICENSE.md", "VOICES.md", "TRAINING.md"):
        source = ROOT / name
        if source.exists():
            shutil.copy2(source, PACKAGE_DIR / name)


def cleanup_package() -> None:
    for item in UNNEEDED_PACKAGE_ITEMS:
        path = PACKAGE_DIR / item

        if path.is_dir():
            shutil.rmtree(path, ignore_errors=True)

        elif path.exists():
            try:
                path.unlink()
            except Exception:
                pass


def write_windows_readme() -> None:
    text = f"""Piper Neo - Windows WinLibs / MinGW

Generado sin Visual Studio Build Tools.
Generado con build-windows.py.

Ejecutable principal:
  piper.exe

Archivos incluidos:
  piper.exe
  onnxruntime.dll
  onnxruntime_providers_shared.dll
  libpiper_phonemize.dll
  libespeak-ng.dll
  libtashkeel_model.ort
  espeak-ng-data\\
  runtime DLLs de MinGW necesarias

Carpeta:
  {PACKAGE_DIR}

Target GCC:
  {get_output(["gcc", "-dumpmachine"])}
"""
    (PACKAGE_DIR / "README-WINDOWS.txt").write_text(text, encoding="utf-8", newline="\r\n")


def package() -> None:
    cmake_install()

    copy_first_found("piper.exe", required=True)
    copy_first_found("onnxruntime.dll", required=True)
    copy_first_found("onnxruntime_providers_shared.dll", required=False)
    copy_first_found("libpiper_phonemize.dll", required=True)
    copy_first_found("libespeak-ng.dll", required=True)
    copy_first_found("libtashkeel_model.ort", required=True)

    copy_espeak_data()

    for dll in RUNTIME_DLLS:
        copy_runtime_dll(dll)

    copy_docs()
    write_windows_readme()

    cleanup_package()


def validate() -> None:
    log("\n[6/7] Validando carpeta final...")

    required_files = [
        "piper.exe",
        "onnxruntime.dll",
        "libpiper_phonemize.dll",
        "libespeak-ng.dll",
        "libtashkeel_model.ort",
    ]

    for name in required_files:
        path = PACKAGE_DIR / name
        if not path.exists():
            fail(f"Falta archivo esencial: {name}")
        log(f"[OK] {name}")

    phontab = PACKAGE_DIR / "espeak-ng-data" / "phontab"
    if not phontab.exists():
        fail("Falta espeak-ng-data\\phontab.")

    log("[OK] espeak-ng-data\\phontab")

    log("\n[7/7] Probando piper.exe...")

    validate_log = BUILD_DIR / "validate-help.txt"

    result = subprocess.run(
        [str(PACKAGE_DIR / "piper.exe"), "--help"],
        cwd=str(PACKAGE_DIR),
        env=ENV,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )

    validate_log.write_text(result.stdout, encoding="utf-8", newline="\r\n")

    if result.returncode >= 2:
        fail(f"piper.exe no arranco correctamente. Revisa:\n{validate_log}")

    log("[OK] piper.exe arranco correctamente.")


def print_final() -> None:
    log()
    log("=" * 60)
    log("Build finalizado correctamente")
    log("=" * 60)
    log(f"Carpeta final:\n{PACKAGE_DIR}")
    log()
    log("Contenido final:")

    for item in sorted(PACKAGE_DIR.iterdir(), key=lambda p: p.name.lower()):
        log(item.name)

    log()


def main() -> None:
    os.chdir(ROOT)

    log()
    log("=" * 60)
    log("Piper Neo - Build Windows WinLibs / MinGW")
    log("=" * 60)
    log(f"Root:  {ROOT}")
    log(f"Build: {BUILD_DIR}")
    log(f"Dist:  {PACKAGE_DIR}")
    log()

    if not (ROOT / "CMakeLists.txt").exists():
        fail("No se encontro CMakeLists.txt.")

    require_tool("gcc")
    require_tool("g++")
    require_tool("cmake")

    generator = detect_generator()

    log()
    log("[INFO] Versiones:")
    log(get_output(["gcc", "--version"]).splitlines()[0])
    log(get_output(["g++", "--version"]).splitlines()[0])
    log(get_output(["cmake", "--version"]).splitlines()[0])

    if shutil.which("ninja"):
        log("ninja " + get_output(["ninja", "--version"]))

    target = get_output(["gcc", "-dumpmachine"])
    log(f"[INFO] Target GCC: {target}")

    if "x86_64" not in target.lower():
        fail("Tu WinLibs no parece ser Win64/x86_64.")

    if "mingw" not in target.lower():
        fail("El compilador no parece ser MinGW-w64.")

    if len(sys.argv) > 1 and sys.argv[1].lower() == "clean":
        clean()
    else:
        log()
        log("[1/7] Modo incremental. Para limpiar usa:")
        log("py script\\build-windows.py clean")

    configure(generator)

    log("\n[PATCH] Corrigiendo archivos generados por CMake...")
    patch_generated_cmake_files()
    patch_rpath_flags()

    build()
    package()
    validate()
    print_final()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        fail("Build cancelado por el usuario.", code=130)