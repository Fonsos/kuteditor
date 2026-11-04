# KutStudio / KutEditor

KutEditor es el entorno nativo de escritorio para creadores de podcasts de la suite **KutStudio**. Diseñado desde cero para agilizar el flujo de trabajo, integra edición de audio multipista, gestión de metadatos (capítulos Podcasting 2.0) y publicación directa con KutPod.

## Plataformas

* **Linux**: [`kuteditor_linux/`](kuteditor_linux/)
* **macOS**: [`kuteditor_macos/`](kuteditor_macos/)
* **Windows**: [`kuteditor_windows/`](kuteditor_windows/)

## Instalación en Linux

Consulta [`kuteditor_linux/README.md`](kuteditor_linux/README.md) para instrucciones detalladas sobre:
* **Arch Linux / Manjaro**: mediante `PKGBUILD` o `installer.sh`.
* **Debian / Ubuntu**: compilación de paquete nativo `.deb` mediante `compiler.sh` o `installer.sh`.
* **Fedora / RPM**: empaquetado RPM con CPack o `installer.sh`.

## Licencia

Este proyecto está licenciado bajo la **GNU General Public License v3.0 (GPL-3.0-or-later)**. Consulta el archivo [LICENSE](LICENSE) para más información.

## Plugins (fork de Fonsos)

KutEditor incluye un gestor de plugins (**Plugins → Gestor de plugins…**): plugins QML sin compilar o nativos en C++.
Documentación y API en [`docs/PLUGINS.md`](docs/PLUGINS.md); ejemplos en [`examples/plugins/`](examples/plugins/)
(limpiar muletillas y pausas, sincronizar pistas con la P4…). Los cambios de este fork, ordenados por fecha, están en
[`docs/CAMBIOS.md`](docs/CAMBIOS.md).

### Instalación en Bazzite / Fedora Atomic

```bash
./kuteditor_linux/bazzite-install.sh      # compila en un contenedor Distrobox y crea el lanzador en el menú
```
