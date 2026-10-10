#!/usr/bin/env bash
# ============================================================================
# KutEditor · instalador para Bazzite (y cualquier Fedora Atomic / SteamOS-like)
#
# Bazzite es inmutable: no se instalan paquetes en el sistema. Este script compila KutEditor
# dentro de un contenedor Distrobox (Fedora) y exporta el lanzador al menú de aplicaciones
# del sistema. Tu carpeta personal se comparte con el contenedor (proyectos, plugins y ajustes
# son los de siempre) y el audio va por PipeWire, igual que cualquier aplicación nativa.
#
#   ./bazzite-install.sh              instala (o actualiza) con transcripción Whisper
#   ./bazzite-install.sh --no-whisper sin Whisper (compila más rápido)
#   ./bazzite-install.sh --branch X   rama del repositorio (por defecto feature/plugin-manager)
#   ./bazzite-install.sh --uninstall  quita el lanzador y el contenedor (no toca tus proyectos)
#
# Variables: KUTEDITOR_SRC (dónde guardar el código, por defecto ~/KutEditor-src)
#            KUTEDITOR_REPO (por defecto https://github.com/Fonsos/kuteditor)
# ============================================================================
set -euo pipefail

BOX=kuteditor
IMAGE=registry.fedoraproject.org/fedora-toolbox:latest
REPO="${KUTEDITOR_REPO:-https://github.com/Fonsos/kuteditor}"
SRC="${KUTEDITOR_SRC:-$HOME/KutEditor-src}"
BRANCH=feature/plugin-manager
WHISPER=1
UNINSTALL=0

while [ $# -gt 0 ]; do
  case "$1" in
    --no-whisper) WHISPER=0 ;;
    --whisper) WHISPER=1 ;;
    --branch) BRANCH="${2:?falta el nombre de la rama}"; shift ;;
    --uninstall) UNINSTALL=1 ;;
    -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
    *) echo "Opción desconocida: $1" >&2; exit 1 ;;
  esac
  shift
done

[ "$(id -u)" -ne 0 ] || { echo "Ejecútalo como tu usuario normal, no como root." >&2; exit 1; }
command -v distrobox >/dev/null || { echo "No se encuentra distrobox (viene instalado en Bazzite)." >&2; exit 1; }

box() { distrobox enter "$BOX" -- bash -lc "$1"; }

if [ "$UNINSTALL" = 1 ]; then
  box 'distrobox-export --app kuteditor --delete' 2>/dev/null || true
  distrobox rm -f "$BOX" || true
  echo "Listo. El código ($SRC) y tus proyectos no se han tocado."
  exit 0
fi

echo "==> 1/5 Contenedor Fedora «$BOX»"
if ! distrobox list 2>/dev/null | awk -F'|' '{gsub(/ /,"",$2); print $2}' | grep -qx "$BOX"; then
  distrobox create --yes --name "$BOX" --image "$IMAGE"
fi

echo "==> 2/5 Dependencias (dnf dentro del contenedor)"
box 'sudo dnf install -y --setopt=install_weak_deps=False \
  git gcc-c++ cmake ninja-build pkgconf-pkg-config \
  qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel qt6-qtshadertools-devel \
  qt6-qtwayland qt6-qtsvg \
  pipewire-jack-audio-connection-kit-devel libsamplerate-devel taglib-devel \
  vulkan-headers vulkan-loader-devel glslang \
  pipewire-jack-audio-connection-kit pipewire-utils desktop-file-utils'

if [ "$WHISPER" = 1 ]; then
  echo "==> 3/5 whisper.cpp (transcripción local; solo CPU, para GPU ver docs)"
  box 'set -e
    if ! pkg-config --exists whisper 2>/dev/null && [ ! -f /usr/local/lib/pkgconfig/whisper.pc ] && [ ! -f /usr/local/lib64/pkgconfig/whisper.pc ]; then
      d=$(mktemp -d); git clone --depth 1 https://github.com/ggml-org/whisper.cpp "$d/w"
      cmake -S "$d/w" -B "$d/b" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
        -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_EXAMPLES=OFF -DWHISPER_BUILD_SERVER=OFF
      cmake --build "$d/b" -j"$(nproc)"
      sudo cmake --install "$d/b"
      printf "/usr/local/lib\n/usr/local/lib64\n" | sudo tee /etc/ld.so.conf.d/local.conf >/dev/null
      sudo ldconfig
      rm -rf "$d"
    fi'
else
  echo "==> 3/5 Sin Whisper"
fi

echo "==> 4/5 Código ($BRANCH) en $SRC"
if [ -d "$SRC/.git" ]; then
  git -C "$SRC" fetch origin "$BRANCH"
  git -C "$SRC" checkout "$BRANCH"
  git -C "$SRC" pull --ff-only origin "$BRANCH"
else
  git clone --branch "$BRANCH" "$REPO" "$SRC"
fi

echo "==> 5/5 Compilar e instalar (puede tardar varios minutos)"
JOBS=$(awk '/MemTotal/ {j=int($2/1024/1024/3); if (j<1) j=1; print j}' /proc/meminfo)
CORES=$(nproc); [ "$JOBS" -gt "$CORES" ] && JOBS=$CORES
box "set -e
  export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig:\${PKG_CONFIG_PATH:-}
  cd '$SRC/kuteditor_linux'
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DHAVE_WHISPER=$([ "$WHISPER" = 1 ] && echo ON || echo OFF) -DHAVE_KUTPOD=ON
  cmake --build build -j$JOBS
  sudo cmake --install build
  sudo ldconfig
  distrobox-export --app kuteditor"

cat <<MSG

✓ Listo. Busca «KUT Editor» en el menú de aplicaciones (o ejecuta: distrobox enter $BOX -- kuteditor).

  · Plugins de ejemplo (muletillas, sincronizar pistas…):
      mkdir -p ~/.local/share/Kut/KutEditor/plugins && cp -r $SRC/examples/plugins/* ~/.local/share/Kut/KutEditor/plugins/
    y actívalos en  Plugins → Gestor de plugins.
  · Transcripción: Herramientas → Transcripción local (Whisper)… y descarga un modelo (Small o mayor para español).
  · Actualizar: vuelve a ejecutar este script.
MSG
