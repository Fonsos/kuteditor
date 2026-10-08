#include "PluginHost.h"
#include "PluginManager.h"

#include <QDebug>
#include <QSettings>

PluginHost::PluginHost(PluginManager *manager, const QString &id, const QString &dir)
    : QObject(manager), m_manager(manager), m_id(id), m_dir(dir)
{
}

QObject *PluginHost::service(const QString &name) const
{
    return m_manager ? m_manager->service(name) : nullptr;
}

void PluginHost::registerAction(const QString &id, const QString &title, const QString &shortcut)
{
    if (m_manager) m_manager->addAction(m_id, id, title, shortcut);
}

QVariant PluginHost::setting(const QString &key, const QVariant &def) const
{
    QSettings s;
    return s.value(QStringLiteral("plugins/%1/data/%2").arg(m_id, key), def);
}

void PluginHost::setSetting(const QString &key, const QVariant &value)
{
    QSettings s;
    s.setValue(QStringLiteral("plugins/%1/data/%2").arg(m_id, key), value);
}

void PluginHost::log(const QString &message)
{
    qInfo().noquote() << QStringLiteral("[plugin:%1]").arg(m_id) << message;
}
