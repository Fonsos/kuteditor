#pragma once
#include <QString>

// Localiza el ffmpeg que usa KutEditor para importar y exportar.
// Orden: variable KUTEDITOR_FFMPEG, el ffmpeg incluido con el programa
// (junto al ejecutable o en <prefijo>/lib/kuteditor/ffmpeg, donde lo deja bazzite-install.sh),
// rutas típicas de macOS y, por último, el PATH del sistema.
namespace FfmpegLocator {
QString find();
}
