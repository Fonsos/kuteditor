#pragma once
#include <QObject>
#include "KutPluginApi.h"

class PluginManager;

/**
 * Objeto que ve cada plugin (una instancia por plugin). En QML se expone como `host`:
 *
 *   host.registerAction("run", "Hacer algo…")
 *   host.setting("clave", 42) / host.setSetting("clave", 43)
 *   host.service("TrackModel")
 *   Connections { target: host; function onActionTriggered(id) { ... } }
 */
class PluginHost : public QObject, public KutPluginHost
{
    Q_OBJECT
    Q_PROPERTY(QString pluginId READ pluginId CONSTANT)
    Q_PROPERTY(QString pluginDir READ pluginDir CONSTANT)
    Q_PROPERTY(int apiVersion READ apiVersion CONSTANT)
public:
    PluginHost(PluginManager *manager, const QString &id, const QString &dir);

    QString pluginId() const override { return m_id; }
    QString pluginDir() const override { return m_dir; }
    int apiVersion() const { return KUTEDITOR_PLUGIN_API_VERSION; }

    Q_INVOKABLE QObject *service(const QString &name) const override;
    Q_INVOKABLE void registerAction(const QString &id, const QString &title,
                                    const QString &shortcut = QString()) override;
    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &def = QVariant()) const override;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value) override;
    Q_INVOKABLE void log(const QString &message) override;

signals:
    /// El usuario eligió una de las acciones del plugin.
    void actionTriggered(const QString &id);

private:
    PluginManager *m_manager;
    QString m_id;
    QString m_dir;
};
