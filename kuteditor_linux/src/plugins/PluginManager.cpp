#include "PluginManager.h"
#include "PluginHost.h"
#include "KutPluginApi.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <functional>

namespace {

bool safeRelativePath(const QString &p)
{
    return !p.isEmpty() && !QDir::isAbsolutePath(p) && !p.contains(QStringLiteral(".."));
}

bool copyRecursively(const QString &src, const QString &dst)
{
    QDir sd(src);
    if (!QDir().mkpath(dst)) return false;
    for (const QFileInfo &fi : sd.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden)) {
        const QString to = dst + QLatin1Char('/') + fi.fileName();
        if (fi.isDir()) { if (!copyRecursively(fi.absoluteFilePath(), to)) return false; }
        else if (!QFile::copy(fi.absoluteFilePath(), to)) return false;
    }
    return true;
}

QString enabledKey(const QString &id) { return QStringLiteral("plugins/%1/enabled").arg(id); }

/// Llama a un método opcional del objeto QML solo si existe (evita avisos de Qt).
bool callIfExists(QObject *o, const char *normalizedSignature, const char *name, const QVariant &arg = QVariant())
{
    if (!o || o->metaObject()->indexOfMethod(normalizedSignature) < 0) return false;
    if (arg.isValid()) return QMetaObject::invokeMethod(o, name, Q_ARG(QVariant, arg));
    return QMetaObject::invokeMethod(o, name);
}

} // namespace

PluginManager::PluginManager(QObject *parent) : QObject(parent) {}

PluginManager::~PluginManager() { shutdown(); }

int PluginManager::apiVersion() const { return KUTEDITOR_PLUGIN_API_VERSION; }

void PluginManager::registerService(const QString &name, QObject *object)
{
    m_services.insert(name, object);
}

QObject *PluginManager::service(const QString &name) const
{
    return m_services.value(name, nullptr);
}

QString PluginManager::userPluginDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/plugins");
}

QStringList PluginManager::defaultPluginDirs()
{
    QStringList dirs;
    const QByteArray env = qgetenv("KUTEDITOR_PLUGIN_PATH");
    if (!env.isEmpty())
        dirs += QString::fromLocal8Bit(env).split(QDir::listSeparator(), Qt::SkipEmptyParts);
    dirs << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/plugins");
    dirs << QCoreApplication::applicationDirPath() + QStringLiteral("/plugins");
#ifdef Q_OS_MACOS
    dirs << QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/plugins");
#else
    dirs << QStringLiteral("/usr/share/kuteditor/plugins") << QStringLiteral("/usr/lib/kuteditor/plugins");
#endif
    dirs.removeDuplicates();
    return dirs;
}

// ── Manifiesto ──────────────────────────────────────────────────────────────

bool PluginManager::readManifest(const QString &dir, Info *out, QString *error) const
{
    QFile f(dir + QStringLiteral("/plugin.json"));
    if (!f.open(QIODevice::ReadOnly)) { *error = tr("No se puede leer plugin.json"); return false; }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = tr("plugin.json no es JSON válido: %1").arg(pe.errorString());
        return false;
    }
    const QJsonObject o = doc.object();
    static const QRegularExpression idRe(QStringLiteral("^[a-z0-9][a-z0-9_-]{1,40}$"));
    Info p;
    p.id = o.value(QStringLiteral("id")).toString();
    if (!idRe.match(p.id).hasMatch()) {
        *error = tr("«id» inválido (usa minúsculas, números, - y _; 2-41 caracteres)");
        return false;
    }
    p.name = o.value(QStringLiteral("name")).toString(p.id);
    p.version = o.value(QStringLiteral("version")).toString(QStringLiteral("0.0.0"));
    p.author = o.value(QStringLiteral("author")).toString();
    p.description = o.value(QStringLiteral("description")).toString();
    p.apiVersion = o.value(QStringLiteral("apiVersion")).toInt(1);
    p.qml = o.value(QStringLiteral("qml")).toString();
    p.library = o.value(QStringLiteral("library")).toString();
    p.dir = dir;
    for (const QJsonValue &v : o.value(QStringLiteral("requires")).toArray())
        if (v.isString()) p.requires << v.toString();
    p.manifestActions = o.value(QStringLiteral("actions")).toArray();

    if (p.qml.isEmpty() && p.library.isEmpty()) { *error = tr("Falta «qml» o «library»"); return false; }
    if ((!p.qml.isEmpty() && !safeRelativePath(p.qml)) || (!p.library.isEmpty() && !safeRelativePath(p.library))) {
        *error = tr("Rutas no permitidas en el manifiesto");
        return false;
    }
    if (p.apiVersion > KUTEDITOR_PLUGIN_API_VERSION) {
        *error = tr("Necesita la API de plugins v%1 (esta versión de KutEditor es la v%2)")
                     .arg(p.apiVersion).arg(KUTEDITOR_PLUGIN_API_VERSION);
        return false;
    }
    p.status = QStringLiteral("disabled");
    *out = p;
    return true;
}

// ── Descubrimiento ──────────────────────────────────────────────────────────

int PluginManager::scan()
{
    shutdown();
    m_plugins.clear(); m_order.clear(); m_scanErrors.clear();
    for (const QString &base : defaultPluginDirs()) {
        const QDir bd(base);
        if (!bd.exists()) continue;
        for (const QFileInfo &fi : bd.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (!QFile::exists(fi.absoluteFilePath() + QStringLiteral("/plugin.json"))) continue;
            Info p; QString err;
            if (!readManifest(fi.absoluteFilePath(), &p, &err)) {
                m_scanErrors.insert(fi.absoluteFilePath(), err);
                qWarning().noquote() << "[plugins]" << fi.absoluteFilePath() << ":" << err;
                continue;
            }
            if (m_plugins.contains(p.id)) continue;      // la primera carpeta que lo defina gana
            m_order << p.id;
            m_plugins.insert(p.id, p);
        }
    }
    emitChanged();
    return m_plugins.size();
}

void PluginManager::reload()
{
    scan();
    loadEnabled();
}

void PluginManager::loadEnabled()
{
    QSettings s;
    for (const QString &id : std::as_const(m_order)) {
        if (!s.value(enabledKey(id), false).toBool()) continue;
        QStringList stack;
        activate(id, stack);
    }
    emitChanged();
}

// ── Activación ──────────────────────────────────────────────────────────────

bool PluginManager::activate(const QString &id, QStringList &stack)
{
    auto it = m_plugins.find(id);
    if (it == m_plugins.end()) return false;
    Info &p = it.value();
    if (p.status == QLatin1String("active")) return true;
    p.error.clear();

    if (stack.contains(id)) { p.status = QStringLiteral("error"); p.error = tr("Dependencia circular"); return false; }
    stack << id;
    for (const QString &dep : std::as_const(p.requires)) {
        if (!m_plugins.contains(dep)) {
            p.status = QStringLiteral("error"); p.error = tr("Falta el plugin requerido «%1»").arg(dep);
            stack.removeLast(); return false;
        }
        QSettings s;
        if (!s.value(enabledKey(dep), false).toBool()) {
            p.status = QStringLiteral("error"); p.error = tr("Requiere «%1», que está desactivado").arg(dep);
            stack.removeLast(); return false;
        }
        if (!activate(dep, stack)) {
            p.status = QStringLiteral("error"); p.error = tr("El plugin requerido «%1» no pudo activarse").arg(dep);
            stack.removeLast(); return false;
        }
    }
    stack.removeLast();

    p.host = new PluginHost(this, p.id, p.dir);
    for (const QJsonValue &v : std::as_const(p.manifestActions)) {
        const QJsonObject a = v.toObject();
        addAction(p.id, a.value(QStringLiteral("id")).toString(), a.value(QStringLiteral("title")).toString(),
                  a.value(QStringLiteral("shortcut")).toString());
    }

    auto fail = [&](const QString &msg) {
        p.error = msg; p.status = QStringLiteral("error");
        qWarning().noquote() << "[plugins]" << p.id << ":" << msg;
        if (p.instance) { p.instance->shutdown(); p.instance = nullptr; }
        if (p.loader) { p.loader->unload(); delete p.loader; p.loader = nullptr; }
        if (p.qmlObject) { delete p.qmlObject; p.qmlObject = nullptr; }
        if (p.qmlContext) { delete p.qmlContext; p.qmlContext = nullptr; }
        removeActionsOf(p.id);
        delete p.host; p.host = nullptr;
        return false;
    };

    if (!p.library.isEmpty()) {
        p.loader = new QPluginLoader(p.dir + QLatin1Char('/') + p.library, this);
        if (!p.loader->load()) return fail(tr("No se pudo cargar la biblioteca: %1").arg(p.loader->errorString()));
        p.instance = qobject_cast<KutPlugin *>(p.loader->instance());
        if (!p.instance) return fail(tr("La biblioteca no implementa la interfaz KutPlugin"));
        QString err;
        if (!p.instance->initialize(p.host, &err)) return fail(err.isEmpty() ? tr("initialize() devolvió false") : err);
    }

    if (!p.qml.isEmpty()) {
        if (!m_engine) return fail(tr("Motor QML no disponible"));
        QQmlComponent comp(m_engine, QUrl::fromLocalFile(p.dir + QLatin1Char('/') + p.qml));
        if (comp.isError()) return fail(comp.errorString().trimmed());
        p.qmlContext = new QQmlContext(m_engine->rootContext(), this);
        p.qmlContext->setContextProperty(QStringLiteral("host"), p.host);
        p.qmlObject = comp.create(p.qmlContext);
        if (!p.qmlObject) return fail(comp.errorString().trimmed());
        QQmlEngine::setObjectOwnership(p.qmlObject, QQmlEngine::CppOwnership);
    }

    p.status = QStringLiteral("active");
    return true;
}

void PluginManager::deactivate(const QString &id)
{
    auto it = m_plugins.find(id);
    if (it == m_plugins.end()) return;
    for (const QString &d : dependentsOf(id)) deactivate(d);
    Info &p = it.value();
    if (p.status != QLatin1String("active") && !p.host) { p.status = QStringLiteral("disabled"); return; }

    if (p.qmlObject) {
        callIfExists(p.qmlObject, "onShutdown()", "onShutdown");        // opcional
        delete p.qmlObject; p.qmlObject = nullptr;
    }
    if (p.qmlContext) { delete p.qmlContext; p.qmlContext = nullptr; }
    if (p.instance) { p.instance->shutdown(); p.instance = nullptr; }
    if (p.loader) { p.loader->unload(); delete p.loader; p.loader = nullptr; }
    removeActionsOf(id);
    delete p.host; p.host = nullptr;
    p.status = QStringLiteral("disabled");
    p.error.clear();
}

QStringList PluginManager::dependentsOf(const QString &id) const
{
    QStringList out;
    for (const QString &other : m_order)
        if (other != id && m_plugins.value(other).requires.contains(id)
            && m_plugins.value(other).status == QLatin1String("active"))
            out << other;
    return out;
}

void PluginManager::shutdown()
{
    // Primero los que dependen de otros
    for (const QString &id : std::as_const(m_order)) deactivate(id);
}

bool PluginManager::setEnabled(const QString &id, bool enabled)
{
    if (!m_plugins.contains(id)) return false;
    QSettings s;
    bool ok = true;
    if (enabled) {
        // Habilita también todo lo que requiere
        std::function<void(const QString &, QStringList &)> mark = [&](const QString &pid, QStringList &seen) {
            if (seen.contains(pid) || !m_plugins.contains(pid)) return;
            seen << pid;
            s.setValue(enabledKey(pid), true);
            for (const QString &dep : m_plugins.value(pid).requires) mark(dep, seen);
        };
        QStringList seen; mark(id, seen);
        QStringList stack;
        ok = activate(id, stack);
    } else {
        // Deshabilita también lo que depende de él
        std::function<void(const QString &)> off = [&](const QString &pid) {
            for (const QString &other : m_order)
                if (m_plugins.value(other).requires.contains(pid) && s.value(enabledKey(other), false).toBool())
                    { off(other); }
            s.setValue(enabledKey(pid), false);
            deactivate(pid);
        };
        off(id);
    }
    emitChanged();
    return ok;
}

bool PluginManager::isEnabled(const QString &id) const
{
    QSettings s;
    return s.value(enabledKey(id), false).toBool();
}

bool PluginManager::isActive(const QString &id) const
{
    return m_plugins.value(id).status == QLatin1String("active");
}

QString PluginManager::statusOf(const QString &id) const
{
    return m_plugins.contains(id) ? m_plugins.value(id).status : QString();
}

// ── Acciones ────────────────────────────────────────────────────────────────

void PluginManager::addAction(const QString &pluginId, const QString &id, const QString &title, const QString &shortcut)
{
    if (id.isEmpty() || title.isEmpty()) return;
    for (const Action &a : std::as_const(m_actions))
        if (a.pluginId == pluginId && a.id == id) return;
    m_actions.append({pluginId, id, title, shortcut});
    emit actionsChanged();
}

void PluginManager::removeActionsOf(const QString &pluginId)
{
    const int before = m_actions.size();
    m_actions.erase(std::remove_if(m_actions.begin(), m_actions.end(),
                                   [&](const Action &a) { return a.pluginId == pluginId; }), m_actions.end());
    if (m_actions.size() != before) emit actionsChanged();
}

void PluginManager::triggerAction(const QString &pluginId, const QString &actionId)
{
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end() || it->status != QLatin1String("active")) return;
    if (it->instance) it->instance->actionTriggered(actionId);
    if (it->qmlObject)
        callIfExists(it->qmlObject, "onAction(QVariant)", "onAction", actionId);  // opcional
    if (it->host) emit it->host->actionTriggered(actionId);
}

// ── Utilidades ──────────────────────────────────────────────────────────────

void PluginManager::openPluginDir() const
{
    QDir().mkpath(userPluginDir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(userPluginDir()));
}

QString PluginManager::installFromFolder(const QString &folderPath)
{
    QString path = folderPath;
    if (path.startsWith(QStringLiteral("file://"))) path = QUrl(path).toLocalFile();
    Info p; QString err;
    if (!readManifest(path, &p, &err)) return err;
    const QString dest = userPluginDir() + QLatin1Char('/') + p.id;
    if (QDir(dest).exists()) return tr("Ya hay un plugin «%1» instalado").arg(p.id);
    if (!copyRecursively(path, dest)) return tr("No se pudo copiar el plugin a %1").arg(dest);
    scan();
    loadEnabled();
    return QString();
}

QVariantList PluginManager::plugins() const
{
    QVariantList out;
    QStringList ids = m_order;
    std::sort(ids.begin(), ids.end(), [this](const QString &a, const QString &b) {
        return m_plugins.value(a).name.compare(m_plugins.value(b).name, Qt::CaseInsensitive) < 0;
    });
    for (const QString &id : ids) {
        const Info &p = m_plugins.value(id);
        QVariantMap m;
        m["id"] = p.id; m["name"] = p.name; m["version"] = p.version; m["author"] = p.author;
        m["description"] = p.description; m["requires"] = p.requires; m["dir"] = p.dir;
        m["enabled"] = isEnabled(id);
        m["active"] = p.status == QLatin1String("active");
        m["status"] = p.status; m["error"] = p.error;
        m["kind"] = !p.library.isEmpty() ? (!p.qml.isEmpty() ? "qml+native" : "native") : "qml";
        m["invalid"] = false;
        out << m;
    }
    for (auto it = m_scanErrors.constBegin(); it != m_scanErrors.constEnd(); ++it) {
        QVariantMap m;
        const QString name = QFileInfo(it.key()).fileName();
        m["id"] = name; m["name"] = name; m["version"] = ""; m["author"] = ""; m["description"] = "";
        m["requires"] = QStringList(); m["dir"] = it.key(); m["enabled"] = false; m["active"] = false;
        m["status"] = "error"; m["error"] = it.value(); m["kind"] = ""; m["invalid"] = true;
        out << m;
    }
    return out;
}

QVariantList PluginManager::actions() const
{
    QVariantList out;
    for (const Action &a : m_actions) {
        QVariantMap m;
        m["pluginId"] = a.pluginId; m["id"] = a.id; m["title"] = a.title; m["shortcut"] = a.shortcut;
        out << m;
    }
    return out;
}

void PluginManager::emitChanged()
{
    emit pluginsChanged();
}
