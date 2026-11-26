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
  rm -f "$HOME/.local/share/applications/kuteditor-app.desktop" "$HOME/.local/share/icons/kuteditor.svg"
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
  pipewire-jack-audio-connection-kit pipewire-utils desktop-file-utils curl xz'

# Fuentes de emojis y símbolos (el contenedor no las trae; algunos textos de la interfaz las usan: ⚠ ✓ 💡…).
# Aparte y tolerante: si el nombre del paquete no existe en esta versión de Fedora no debe abortar la instalación.
box 'sudo dnf install -y --setopt=install_weak_deps=False google-noto-color-emoji-fonts dejavu-sans-fonts 2>/dev/null \
  || sudo dnf install -y --setopt=install_weak_deps=False google-noto-emoji-color-fonts dejavu-sans-fonts 2>/dev/null \
  || echo "(aviso) no se pudo instalar la fuente de emojis; los iconos principales no dependen de ella"; fc-cache -f >/dev/null 2>&1 || true'

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
# Compilar necesita unos GB temporales (TrackModel.cpp genera un ensamblador enorme) y /tmp puede tener cuota:
# se usa una carpeta propia en la carpeta personal. Aviso si queda poco espacio.
BUILD_TMP="$HOME/.cache/kuteditor-build-tmp"; mkdir -p "$BUILD_TMP"
FREE_MB=$(df -Pm "$HOME" | awk 'NR==2 {print $4}')
if [ "${FREE_MB:-0}" -lt 4096 ]; then
  echo "⚠ Solo quedan ${FREE_MB} MB libres en tu carpeta personal; compilar necesita unos 4 GB. Libera espacio y repite." >&2
  exit 1
fi
JOBS=$(awk '/MemTotal/ {j=int($2/1024/1024/3); if (j<1) j=1; print j}' /proc/meminfo)
CORES=$(nproc); [ "$JOBS" -gt "$CORES" ] && JOBS=$CORES
box "set -e
  export TMPDIR='$BUILD_TMP'
  export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig:\${PKG_CONFIG_PATH:-}
  # libjack de pipewire-jack no está en el path del enlazador en Fedora: añadir su carpeta
  JACKDIR=\$(pkg-config --variable=libdir jack 2>/dev/null || true)
  [ -n \"\$JACKDIR\" ] && export LIBRARY_PATH=\"\$JACKDIR:\${LIBRARY_PATH:-}\"
  cd '$SRC/kuteditor_linux'
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DHAVE_WHISPER=$([ "$WHISPER" = 1 ] && echo ON || echo OFF) -DHAVE_KUTPOD=ON
  cmake --build build -j$JOBS
  sudo cmake --install build
  sudo ldconfig"

# Lanzador en el menú del sistema (distrobox-export no resultó fiable aquí: exportaba el atajo del contenedor)
mkdir -p "$HOME/.local/share/applications" "$HOME/.local/share/icons"
cp "$SRC/kuteditor_linux/resources/kuteditor.svg" "$HOME/.local/share/icons/kuteditor.svg"
cat > "$HOME/.local/share/applications/kuteditor-app.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=KUT Editor
Comment=Editor de podcasts
Exec=distrobox enter $BOX -- kuteditor %F
Icon=$HOME/.local/share/icons/kuteditor.svg
Terminal=false
Categories=AudioVideo;Audio;AudioVideoEditing;
MimeType=application/x-podcastproj;
StartupWMClass=kuteditor
DESK
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true

# ffmpeg incluido: el editor corre DENTRO del contenedor y no ve el ffmpeg del sistema, y el ffmpeg-free de
# Fedora no trae el codificador MP3. Se descarga una compilación estática (BtbN/FFmpeg-Builds, GPL, con MP3,
# AAC/M4A, OGG y FLAC), se comprueba su suma SHA-256 y se deja en /usr/lib/kuteditor/, donde el editor la busca
# antes que la del sistema. Si falla la descarga, el editor sigue usando el ffmpeg que haya en el contenedor.
echo "==> ffmpeg incluido (MP3, M4A, OGG, FLAC)"
FF_BASE=https://github.com/BtbN/FFmpeg-Builds/releases/download/latest
FF_CACHE="$HOME/.cache/kuteditor-ffmpeg"; mkdir -p "$FF_CACHE"
if curl -fsSL "$FF_BASE/checksums.sha256" -o "$FF_CACHE/checksums.sha256"; then
  # Última versión estable (ffmpeg-nX.Y-latest-linux64-gpl-X.Y.tar.xz); si no hay, la de desarrollo.
  FF_FILE=$(grep -oE 'ffmpeg-n[0-9.]+-latest-linux64-gpl-[0-9.]+\.tar\.xz' "$FF_CACHE/checksums.sha256" | sort -V | tail -1)
  [ -n "$FF_FILE" ] || FF_FILE=ffmpeg-master-latest-linux64-gpl.tar.xz
  FF_SUM=$(awk -v f="$FF_FILE" '$2==f || $2=="*"f {print $1}' "$FF_CACHE/checksums.sha256")
  if [ -f "$FF_CACHE/$FF_FILE" ] && echo "$FF_SUM  $FF_CACHE/$FF_FILE" | sha256sum -c --status; then
    echo "   ya descargado: $FF_FILE"
  else
    rm -f "$FF_CACHE"/ffmpeg-*.tar.xz
    curl -fL --progress-bar "$FF_BASE/$FF_FILE" -o "$FF_CACHE/$FF_FILE" || true
  fi
  if [ -n "$FF_SUM" ] && echo "$FF_SUM  $FF_CACHE/$FF_FILE" | sha256sum -c --status 2>/dev/null; then
    rm -rf "$FF_CACHE/x"; mkdir -p "$FF_CACHE/x"
    tar -xJf "$FF_CACHE/$FF_FILE" -C "$FF_CACHE/x" --strip-components=2 --wildcards '*/bin/ffmpeg' '*/bin/ffprobe'
    box "sudo install -Dm755 '$FF_CACHE/x/ffmpeg' /usr/lib/kuteditor/ffmpeg && sudo install -Dm755 '$FF_CACHE/x/ffprobe' /usr/lib/kuteditor/ffprobe"
    rm -rf "$FF_CACHE/x"
    if box '/usr/lib/kuteditor/ffmpeg -hide_banner -encoders 2>/dev/null | grep -q libmp3lame'; then
      echo "   ✓ ffmpeg incluido con MP3 ($FF_FILE)"
    else
      echo "(aviso) el ffmpeg incluido no arranca o no trae MP3" >&2
    fi
  else
    echo "(aviso) no se pudo descargar o verificar ffmpeg; se usará el del contenedor si lo hay" >&2
  fi
else
  echo "(aviso) sin acceso a GitHub para descargar ffmpeg; se usará el del contenedor si lo hay" >&2
fi

cat <<MSG

✓ Listo. Busca «KUT Editor» en el menú de aplicaciones (o ejecuta: distrobox enter $BOX -- kuteditor).

  · Plugins de ejemplo (muletillas, sincronizar pistas…):
      mkdir -p ~/.local/share/Kut/KutEditor/plugins && cp -r $SRC/examples/plugins/* ~/.local/share/Kut/KutEditor/plugins/
    y actívalos en  Plugins → Gestor de plugins.
  · Transcripción: Herramientas → Transcripción local (Whisper)… y descarga un modelo (Small o mayor para español).
  · Actualizar: vuelve a ejecutar este script.
MSG
