# Plugins de KutEditor

KutEditor tiene un gestor de plugins (menú **Plugins → Gestor de plugins…**). Un plugin es una carpeta con un
`plugin.json` y, según el caso, un archivo QML o una biblioteca nativa.

## Dónde se instalan

Se buscan (en este orden; el primero que defina un `id` gana) en:

1. `$KUTEDITOR_PLUGIN_PATH` (carpetas separadas por `:`)
2. `~/.local/share/Kut/KutEditor/plugins/` ← aquí instala el gestor («Instalar desde carpeta…»)
3. `<carpeta del ejecutable>/plugins/`
4. `/usr/share/kuteditor/plugins/` y `/usr/lib/kuteditor/plugins/`

Todos los plugins empiezan **desactivados**; el usuario los activa en el gestor y el estado se recuerda.

## plugin.json

```json
{
  "id": "mi-plugin",              // minúsculas, números, - y _ (2-41 caracteres)
  "name": "Mi plugin",
  "version": "1.0.0",
  "apiVersion": 1,                // versión de la API de plugins que necesita
  "author": "Yo",
  "description": "Qué hace.",
  "requires": ["otro-plugin"],    // opcional: se activan antes y se desactivan después
  "qml": "Main.qml",              // plugin QML (no hay que compilar nada)
  "library": "libmi_plugin.so",   // plugin nativo (C++); se puede usar junto con "qml"
  "actions": [                    // opcional: entradas del menú Plugins
    { "id": "run", "title": "Hacer algo…", "shortcut": "" }
  ]
}
```

Las rutas de `qml` y `library` son relativas a la carpeta del plugin (no se admiten `..` ni rutas absolutas).
Si un plugin falla (QML con errores, biblioteca que no carga, dependencia ausente…) se muestra el motivo en el
gestor y el resto de plugins sigue funcionando.

## Plugin QML

`Main.qml` se instancia en el motor QML de la aplicación, así que ve todos los servicios globales
(`TrackModel`, `Transcription`, `UndoManager`, `ProjectIO`, `ChapterModel`, `ExportManager`, `AudioEngine`, `KutPod`…)
y recibe un objeto **`host`**:

| `host…` | |
|---|---|
| `pluginId`, `pluginDir`, `apiVersion` | datos del plugin |
| `registerAction(id, titulo, atajo?)` | añade una entrada al menú Plugins |
| `setting(clave, valorPorDefecto)` / `setSetting(clave, valor)` | ajustes persistentes del plugin |
| `service("TrackModel")` | un servicio del editor por nombre (`null` si no existe) |
| `log(texto)` | escribe en el registro de la aplicación |
| señal `actionTriggered(id)` | el usuario eligió una acción |

Funciones opcionales que llama el gestor: `onAction(id)` y `onShutdown()`.
Ejemplo completo: [`examples/plugins/hello-kut`](../examples/plugins/hello-kut).

## Transcripción palabra a palabra

El servicio `Transcription` guarda, además de las frases (`transcriptionSegments(pista)`), las **palabras con sus
tiempos** de cada pista, que es lo que necesitan los plugins de edición por texto:

| `Transcription…` | |
|---|---|
| `transcriptionWords(pista)` | lista de `{ startSec, endSec, text, prob }` (segundos de la línea de tiempo) |
| `hasWordTimestamps(pista)` | `false` en transcripciones hechas con versiones anteriores: hay que volver a transcribir |
| `initialPrompt` | texto de arranque que se le da a Whisper (por defecto, con «eeeh, mmm…» para que no los omita) |

La puntuación va pegada a la palabra («mundo,», «¿Qué»). Los tiempos salen de los tokens de Whisper (precisión
de unos ~100 ms) y se normalizan para que no haya solapes. Se guardan en el proyecto (`words` dentro de cada
transcripción).

## Edición por texto

*Herramientas → Editar por texto…* abre el texto unido de todas las pistas (cada hablante con su color).
Clic en una palabra mueve el cursor de reproducción; seleccionar y pulsar Supr (o «Marcar selección») la
marca; «Aplicar cortes» elimina lo marcado de la línea de tiempo en **un solo paso de Deshacer** y reajusta
las transcripciones. Si en ese momento habla otra pista, la palabra solo se silencia en su pista (no se
desincroniza); en caso contrario se corta de todas las pistas. Opciones: acortar pausas largas y recortar
el silencio inicial/final.

Los plugins disponen del servicio `TranscriptEditor` (también context property en QML):

| Miembro | Descripción |
|---|---|
| `refresh()` / `words()` | Lista unida `{index, track, trackName, start, end, text, marked}` |
| `markWhere(regex, on=true)` | Marca palabras cuyo texto (sin puntuación, minúsculas) encaja; devuelve cuántas |
| `markIndices(list, on)` / `setMarked(from, to, on)` / `clearMarks()` | Marcas manuales |
| `maxPauseSec`, `trimEdges`, `padSec` | Opciones del recorte |
| `preview()` | `{cuts, mutes, savedSec, deletedWords}` sin tocar nada |
| `apply()` | Aplica; devuelve los segundos ahorrados (−1 si no había nada) |

`requestOpen()` abre la ventana de edición por texto. Plugin real de ejemplo: [`examples/plugins/limpiar-habla`](../examples/plugins/limpiar-habla) (muletillas y pausas; «Marcar y revisar» o «Marcar y aplicar»).

Ejemplo mínimo: `TranscriptEditor.markWhere("^(?:e+h+m*|em+|h?m{2,}h*|um+)$")` y `apply()`.

## Plugin nativo (C++)

Una biblioteca compartida que implementa `KutPlugin` (`src/plugins/KutPluginApi.h`, solo cabeceras; no hay que
enlazar contra KutEditor):

```cpp
#include "plugins/KutPluginApi.h"

class MiPlugin : public QObject, public KutPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KutPlugin_iid)
    Q_INTERFACES(KutPlugin)
public:
    bool initialize(KutPluginHost *host, QString *error) override {
        host->registerAction("run", "Hacer algo…");
        return true;                       // false + *error → el gestor muestra el motivo
    }
    void actionTriggered(const QString &id) override { /* … */ }
    void shutdown() override {}
};
```

`KutPluginHost` ofrece lo mismo que `host` en QML (`service()`, `setting()`, `registerAction()`, `log()`…).
Se compila contra el mismo Qt 6 que KutEditor. Ver `tests/native_plugin/` para un plugin mínimo.

## Tests

```bash
cmake -S kuteditor_linux -B build -DKUT_BUILD_TESTS=ON
cmake --build build --target test_pluginmanager
./build/test_pluginmanager
```
