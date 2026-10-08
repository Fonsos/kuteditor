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
