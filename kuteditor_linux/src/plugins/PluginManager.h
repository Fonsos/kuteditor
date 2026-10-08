#pragma once
#include <QObject>
#include <QHash>
#include <QJsonArray>
#include <QPointer>
#include <QStringList>
#include <QVariantList>

class QQmlEngine;
class QQmlContext;
class QPluginLoader;
class KutPlugin;
class PluginHost;

/**
 * Gestor de plugins de KutEditor.
 *
 * Un plugin es una carpeta con un `plugin.json`:
 *
 *   { "id": "mi-plugin", "name": "Mi plugin", "version": "1.0.0", "apiVersion": 1,
 *     "author": "…", "description": "…",
 *     "requires": ["otro-plugin"],
 *     "qml": "Main.qml",                     // plugin QML (sin compilar), opcional
 *     "library": "libmi_plugin.so",          // plugin nativo (KutPlugin), opcional
 *     "actions": [ { "id": "run", "title": "Hacer algo…" } ] }
 *
 * Carpetas de búsqueda (la primera que defina un id gana):
 *   $KUTEDITOR_PLUGIN_PATH · ~/.local/share/Kut/KutEditor/plugins · <app>/plugins · /usr/share|lib/kuteditor/plugins
 *
 * Se expone a QML como `PluginManager`.
 */
class PluginManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(QVariantList actions READ actions NOTIFY actionsChanged)
    Q_PROPERTY(QString userPluginDir READ userPluginDir CONSTANT)
    Q_PROPERTY(int apiVersion READ apiVersion CONSTANT)
public:
    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager() override;

    void setEngine(QQmlEngine *engine) { m_engine = engine; }
    /// Servicios que los plugins pueden pedir por nombre (TrackModel, Transcription…).
    void registerService(const QString &name, QObject *object);
    QObject *service(const QString &name) const;

    static QStringList defaultPluginDirs();
    QString userPluginDir() const;
    int apiVersion() const;

    QVariantList plugins() const;
    QVariantList actions() const;

    /// Busca plugins en disco (no activa nada). Devuelve cuántos encontró.
    Q_INVOKABLE int scan();
    /// Activa los plugins que el usuario dejó habilitados.
    Q_INVOKABLE void loadEnabled();
    /// Desactiva todo, vuelve a buscar en disco y activa lo habilitado.
    Q_INVOKABLE void reload();
    /// Habilita/deshabilita y persiste. Habilitar activa también lo que el plugin requiere;
    /// deshabilitar desactiva lo que depende de él. Devuelve false si falló.
    Q_INVOKABLE bool setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool isEnabled(const QString &id) const;
    Q_INVOKABLE bool isActive(const QString &id) const;
    Q_INVOKABLE QString statusOf(const QString &id) const;
    Q_INVOKABLE void triggerAction(const QString &pluginId, const QString &actionId);
    Q_INVOKABLE void openPluginDir() const;
    /// Instala un plugin copiando una carpeta (con plugin.json) a la carpeta de usuario.
    Q_INVOKABLE QString installFromFolder(const QString &folderPath);
    /// Desactiva todo (al cerrar).
    void shutdown();

    // Uso interno (PluginHost)
    void addAction(const QString &pluginId, const QString &id, const QString &title, const QString &shortcut);

signals:
    void pluginsChanged();
    void actionsChanged();
    void pluginMessage(const QString &pluginId, const QString &message);

private:
    struct Action { QString pluginId, id, title, shortcut; };
    struct Info {
        QString id, name, version, author, description, dir, qml, library;
        int apiVersion = 1;
        QStringList requires;
        QJsonArray manifestActions;
        // Estado en ejecución
        QString status;           // "disabled" | "active" | "error"
        QString error;
        QObject *qmlObject = nullptr;
        QQmlContext *qmlContext = nullptr;
        QPluginLoader *loader = nullptr;
        KutPlugin *instance = nullptr;
        PluginHost *host = nullptr;
    };

    bool readManifest(const QString &dir, Info *out, QString *error) const;
    bool activate(const QString &id, QStringList &stack);
    void deactivate(const QString &id);
    QStringList dependentsOf(const QString &id) const;
    void removeActionsOf(const QString &pluginId);
    void emitChanged();

    QQmlEngine *m_engine = nullptr;
    QHash<QString, QObject *> m_services;
    QHash<QString, Info> m_plugins;
    QStringList m_order;                 // ids en orden de descubrimiento
    QVector<Action> m_actions;
    QHash<QString, QString> m_scanErrors; // carpeta → error de manifiesto
};
