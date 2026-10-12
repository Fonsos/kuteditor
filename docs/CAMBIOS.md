# Registro de cambios por fecha (fork de Fonsos)

Cambios hechos sobre los proyectos de Ernesto Acosta (`ernestoacostame/kuteditor` y `ernestoacostame/kutpod`), en los forks
`Fonsos/kuteditor` (rama `feature/plugin-manager`) y `Fonsos/kutpod` (rama `claude/fervent-rubin-wv8uey`).
Las fechas son las de los commits. Solo se sube a los forks; nunca a los repositorios de Ernesto.

**Reparto de papeles** (aclarado el 7 de octubre): KutEditor es el editor (donde va la edición del episodio, con un gestor de
plugins); KutPod es el servidor de publicación (solo necesita un contenedor Docker); los *shorts/reels* se generan en la web de
KutPod y quedan para el final del flujo.

---

## 2026-10-06 · KutPod

- **Plugin «Estudio de edición» (WIP)** `fba8637`: flujo simplificado de edición en la web: hasta 3 audios del episodio + música de
  entrada y de salida, sincronización, transcripción, corte editando el texto, filtro de muletillas y exportación.

## 2026-10-07 · KutPod

- **Instalador nativo para Bazzite y CLI de plugins** `3a3bda7`: instalación con Homebrew + servicios de usuario de systemd,
  paquete autoextraíble `.run`, alternativa en contenedor (Quadlet) y `cli/plugin.php` (listar/activar/desactivar plugins).
- **Sincronización por tramos para fallos de la referencia (P4)** `e2084f2`: la pista 2 (grabación propia del locutor) se alinea con la
  pista 3 (la misma voz grabada por la P4). Si la P4 pierde audio o mete silencio a mitad, se detecta el salto y se recorta o se
  deja el hueco para seguir la línea de tiempo de la referencia.

## 2026-10-08 · KutPod y KutEditor

- **KutPod · Estudio · Shorts y Reels** `5a9b4f0`: vídeo vertical de ~1 min (1080×1920) con onda y subtítulos karaoke, sugerencia de
  fragmentos, para YouTube Shorts e Instagram Reels.
- **KutPod · Plugins de edición web desactivados por defecto** `7f2d0a1`: todo lo hecho en la web se conserva pero apagado; los
  instaladores solo lo activan con `--studio`.
- **KutEditor · Gestor de plugins (Linux)** `f38f879`: *Plugins → Gestor de plugins…*; plugins QML (sin compilar) y nativos (C++),
  con `plugin.json`, ajustes persistentes, servicios del editor por nombre y acciones en el menú. Todos arrancan desactivados.
  Documentación en `docs/PLUGINS.md`, ejemplo `examples/plugins/hello-kut`.

## 2026-10-09 · KutEditor (y compose de KutPod)

- **Transcripción palabra a palabra** `7e406b6`: Whisper devuelve tiempos por palabra; texto de arranque configurable para que
  conserve «eeeh», «mmm»; las palabras se guardan en el proyecto.
- **Edición por texto** `ab97e22`: *Herramientas → Editar por texto…*. Texto unido de todas las pistas (cada hablante con su color);
  clic para ir a esa palabra; marcar y «Aplicar cortes» elimina de la línea de tiempo en **un solo Deshacer** y reajusta las
  transcripciones. Si otra persona habla encima, solo se silencia esa pista (no se desincroniza). Opciones: acortar pausas largas y
  recortar el silencio inicial/final. Servicio `TranscriptEditor` para plugins.
- **Plugin `limpiar-habla`** `a6f79d2`: marca muletillas («eh», «eeeh», «mmm», «hmm», «um»…) y palabras extra, opcionalmente acorta
  pausas; «Marcar y revisar» o «Marcar y aplicar».
- **Sincronización de pistas (P4)** `f0f12a7`: el mismo algoritmo de KutPod pasado a C++ (correlación de envolventes por FFT).
  Plugin `sincronizar-pistas`: «Analizar» (informa del desfase y de los saltos) y «Sincronizar» (recoloca los clips, un solo Deshacer;
  el audio original no se toca). Servicio `TrackSyncer`.
- **Arreglo del aplanado de audio largo** `f0f12a7`: `trackSamplesData` devolvía silencio para audio de más de 30 s.
- **KutPod · Docker Compose para servidor** `8cbac09`: `installer/docker` (Dockerfile con el código dentro, `docker-compose.yml`,
  `.env.example`, README); volúmenes de datos `storage`/`media`/`cache`; plugins de edición desactivados.

## 2026-10-10 · KutEditor

- **Merge con la versión 2.53 de Ernesto** `e7a6153`: sin conflictos. Trae: el cursor avanza durante la grabación y queda al final de
  la toma, descarte de buffers sobrantes, menú de sesión recuperable en la biblioteca. Se corrigió además un error suyo: `main.qml`
  llamaba a `hasRecoveryProject()` como función cuando ya era propiedad (el aviso de recuperar sesión nunca salía al arrancar).
  El `main` del fork quedó idéntico al de Ernesto.
- **Instalador para Bazzite (Distrobox)** `5d039a4`, `190e290`, `1a1d0f2`, `a403a71`, `4f8b761`: `kuteditor_linux/bazzite-install.sh`
  compila en un contenedor Fedora y crea su propio lanzador en el menú (`distrobox-export` exportaba el atajo del contenedor, no la
  app). Arreglos que fueron saliendo: enlazar `libjack` de pipewire-jack (`/usr/lib64/pipewire-0.3/jack`), instalar los plugins de ejemplo
  en `/usr/share/kuteditor/plugins`, carpeta temporal propia al compilar y aviso si queda poco espacio (en Bazzite `/tmp` es RAM).
- **Audio largo: guardar sin perder el audio** `31c04af`: el audio de más de 30 s vive en un mmap y el guardado solo leía el heap, así
  que escribía el proyecto sin el audio (pistas «planas» al reabrir; la copia de seguridad igual). Ahora se guarda y se reabre bien.
- **Botón «Analizar audio de las pistas»** `31c04af`: barra de herramientas y menú Herramientas; rehace la forma de onda y avisa de
  pistas sin audio.
- **Indicador global de «trabajando…»** `31c04af`: aviso que bloquea la edición mientras dura una tarea pesada (guardar/abrir,
  sincronizar, analizar, aplicar cortes). Sincronizar y aplicar cortes pasan a segundo plano. Servicio `AppBusy` para plugins.
- **Importar audio largo (1–2 h) sin caerse** `ad359aa`: los temporales grandes van a `~/.cache/Kut/KutEditor/scratch` (en disco; `/tmp`
  puede ser RAM) y el mmap se desvincula al crearse; el mipmap lee del mmap sin duplicarlo en RAM; un solo import pesado a la vez;
  ffmpeg hasta 1 h; los fallos de importación ya no son silenciosos (aviso con el motivo).
  Medido con 2 h estéreo 48 kHz: importar 35 s (WAV) / 61 s (mp3), guardar 20 s, pico de RAM 5,3 GB, sin archivos temporales residuales.

## 2026-10-11 · KutEditor

- **Registro local de sesión y cierres inesperados** `2a3e9c8`: `~/.local/share/Kut/KutEditor/logs/kuteditor-AAAAMMDD-HHMMSS.log`
  (últimos 10) con hora, versión, memoria y cada importación; si el programa muere por una señal anota cuál y la pila de llamadas; al
  arrancar tras un cierre mal avisa y dice qué registro mirar. *Ayuda → Abrir carpeta de registros*.
- El aviso «Importando…» ya no fuerza el repintado dentro del gesto de soltar un archivo (se sospechaba la causa del cierre inmediato;
  no se pudo reproducir). Tras actualizar, el programa dejó de cerrarse.

## 2026-10-12 · KutEditor

- **Reproducción de pistas mono** `5ba0449`: sonaban al doble de velocidad («voz de pito», metálico) porque `readMixSegment` escribía
  los canales de la fuente y el motor espera estéreo. Ahora la salida es siempre estéreo (mono → L=R). Fallo ya presente en el
  código de Ernesto; prueba muestra a muestra.
- **Iconos «cuadrados vacíos»**: eran emojis (🎙 ✕ 🖼 🌐) de la pantalla «Mis Podcasts» y
  del editor de capítulos; el contenedor Fedora no trae fuente de emojis. Sustituidos por iconos de la fuente incluida; el script de Bazzite
  instala además la fuente de emojis. Se tocaron `Library.qml`, `ChapterEditor.qml` y `OnlineLibrary.qml` (archivos de Ernesto).
- Diagnóstico de la fuente de iconos en el registro, con plan B (registrarla desde un archivo).

---

## Estado de las pruebas

- 8 grupos de tests automáticos (planificador, remapeo, sincronización, palabras, plugins, registro, integración con modelos reales,
  ventana QML); en integración se cubren cortes + Deshacer, plugins QML, sincronización con fallo de la P4, guardar/reabrir audio largo,
  importación fallida, temporales, análisis, reproducción mono/estéreo y el indicador de tareas.
- **Sin probar con audio real**: Whisper con un modelo real (calidad de las palabras y muletillas), sincronización con grabaciones
  de la P4, grabación en vivo con PipeWire dentro del contenedor y el `docker-compose` en el servidor.
- **No hecho**: port de los cambios al árbol de macOS (descartado: no hay dónde probarlo); automatización de publicación de reels
  (webhook → n8n → YouTube/Instagram).

## Cómo actualizar en Bazzite

```bash
~/KutEditor-src/kuteditor_linux/bazzite-install.sh      # hace git pull y recompila
```
