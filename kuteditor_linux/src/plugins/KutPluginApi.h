#pragma once
// ============================================================================
// KutEditor · API pública para plugins nativos (C++ / QPluginLoader)
// ----------------------------------------------------------------------------
// Un plugin nativo es una biblioteca compartida que implementa `KutPlugin`
// y se declara con Q_PLUGIN_METADATA(IID KutPlugin_iid) (ver doc/PLUGINS.md).
// Solo usa interfaces puras (sin símbolos exportados por el ejecutable), de modo
// que no hace falta enlazar contra KutEditor.
// ============================================================================
#include <QObject>
#include <QString>
#include <QVariant>

#define KUTEDITOR_PLUGIN_API_VERSION 1

class KutPluginHost
{
public:
    virtual ~KutPluginHost() = default;

    /// Identificador del plugin (el "id" de su plugin.json).
    virtual QString pluginId() const = 0;
    /// Carpeta del plugin (para leer sus recursos).
    virtual QString pluginDir() const = 0;

    /// Servicios del editor por nombre: "TrackModel", "Transcription", "UndoManager",
    /// "ProjectIO", "ChapterModel", "ExportManager", "AudioEngine"… (nullptr si no existe).
    virtual QObject *service(const QString &name) const = 0;

    /// Añade una entrada al menú «Plugins». Al pulsarla se llama a KutPlugin::actionTriggered.
    virtual void registerAction(const QString &id, const QString &title,
                                const QString &shortcut = QString()) = 0;

    /// Ajustes persistentes propios del plugin.
    virtual QVariant setting(const QString &key, const QVariant &def = QVariant()) const = 0;
    virtual void setSetting(const QString &key, const QVariant &value) = 0;

    virtual void log(const QString &message) = 0;
};

class KutPlugin
{
public:
    virtual ~KutPlugin() = default;

    /// Se llama al activar el plugin. Devuelve false (y rellena `error`) si no puede arrancar.
    virtual bool initialize(KutPluginHost *host, QString *error) = 0;
    /// Se llama al desactivar el plugin o al cerrar la aplicación.
    virtual void shutdown() {}
    /// El usuario eligió una de las acciones registradas.
    virtual void actionTriggered(const QString &actionId) { Q_UNUSED(actionId); }
};

#define KutPlugin_iid "org.kutstudio.kuteditor.Plugin/1"
QT_BEGIN_NAMESPACE
Q_DECLARE_INTERFACE(KutPlugin, KutPlugin_iid)
QT_END_NAMESPACE
