// Plugin nativo mínimo para probar la carga con QPluginLoader.
#include <QObject>
#include <QtPlugin>
#include "plugins/KutPluginApi.h"

class NativeTestPlugin : public QObject, public KutPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KutPlugin_iid)
    Q_INTERFACES(KutPlugin)
public:
    bool initialize(KutPluginHost *host, QString *error) override
    {
        if (host->setting("fail").toBool()) { *error = QStringLiteral("fallo pedido por el test"); return false; }
        m_host = host;
        host->registerAction("hello", QStringLiteral("Hola nativo"));
        host->setSetting("init", true);
        host->setSetting("sawService", host->service("Svc") != nullptr);
        return true;
    }
    void shutdown() override { if (m_host) m_host->setSetting("shutdown", true); m_host = nullptr; }
    void actionTriggered(const QString &id) override { if (m_host) m_host->setSetting("last", id); }
private:
    KutPluginHost *m_host = nullptr;
};

#include "NativeTestPlugin.moc"
