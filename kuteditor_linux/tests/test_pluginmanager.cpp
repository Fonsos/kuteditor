// Tests del gestor de plugins (sin interfaz gráfica): QTEST con QQmlEngine.
#include <QtTest>
#include <QQmlEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "plugins/PluginManager.h"

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(data);
}

/// Crea un plugin QML en <base>/<id>/ con el QML dado y campos extra en el manifiesto.
void makePlugin(const QString &base, const QString &id, const QByteArray &qml,
                const QString &extraJson = QString(), const QString &apiVersion = QStringLiteral("1"))
{
    const QString dir = base + "/" + id;
    writeFile(dir + "/plugin.json",
              QStringLiteral("{ \"id\": \"%1\", \"name\": \"Plugin %1\", \"version\": \"1.2.3\", \"apiVersion\": %2, "
                             "\"qml\": \"Main.qml\"%3 }")
                  .arg(id, apiVersion, extraJson.isEmpty() ? QString() : (", " + extraJson)).toUtf8());
    writeFile(dir + "/Main.qml", qml);
}

const QByteArray kBasicQml = R"(
import QtQml
QtObject {
    Component.onCompleted: { host.registerAction("run", "Hacer algo"); host.setSetting("loaded", true) }
    function onAction(id) { host.setSetting("last", id) }
    function onShutdown() { host.setSetting("shutdown", true) }
}
)";

/// Servicio de prueba con una propiedad visible desde QML.
class Svc : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int value READ value CONSTANT)
public:
    int value() const { return 42; }
};

} // namespace

class TestPluginManager : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;          // carpeta de plugins y de ajustes
    QQmlEngine m_engine;

    QString plugins() const { return m_dir.path() + "/plugins"; }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("KutTest");
        QCoreApplication::setApplicationName("PluginManagerTest");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path() + "/settings");
        qputenv("KUTEDITOR_PLUGIN_PATH", plugins().toUtf8());
    }

    void init()
    {
        QDir(plugins()).removeRecursively();
        QSettings().clear();
    }

    void scanFindsValidAndReportsInvalid()
    {
        makePlugin(plugins(), "good", kBasicQml);
        writeFile(plugins() + "/broken/plugin.json", "{ esto no es json");
        writeFile(plugins() + "/Bad_ID/plugin.json", R"({"id":"Bad ID!","qml":"Main.qml"})");
        makePlugin(plugins(), "future", kBasicQml, QString(), "99");
        writeFile(plugins() + "/nothing/plugin.json", R"({"id":"nothing"})");

        PluginManager pm; pm.setEngine(&m_engine);
        QCOMPARE(pm.scan(), 1);
        const QVariantList list = pm.plugins();
        QCOMPARE(list.size(), 5);                       // 1 válido + 4 inválidos listados con su error
        int invalid = 0;
        for (const QVariant &v : list) {
            const QVariantMap m = v.toMap();
            if (m["invalid"].toBool()) { invalid++; QVERIFY(!m["error"].toString().isEmpty()); }
        }
        QCOMPARE(invalid, 4);
        QCOMPARE(pm.statusOf("good"), QString("disabled"));
    }

    void pathTraversalInManifestIsRejected()
    {
        writeFile(plugins() + "/evil/plugin.json", R"({"id":"evil","qml":"../../etc/x.qml"})");
        PluginManager pm; pm.setEngine(&m_engine);
        QCOMPARE(pm.scan(), 0);
    }

    void enableActivatesQmlPluginAndRunsActions()
    {
        makePlugin(plugins(), "demo", kBasicQml, R"("actions": [{"id":"manifest","title":"Desde manifiesto"}])");
        PluginManager pm; pm.setEngine(&m_engine); pm.scan();
        QVERIFY(!pm.isActive("demo"));
        QVERIFY(pm.setEnabled("demo", true));
        QVERIFY(pm.isActive("demo"));
        QCOMPARE(QSettings().value("plugins/demo/data/loaded").toBool(), true);   // Component.onCompleted corrió
        QCOMPARE(pm.actions().size(), 2);                                         // manifiesto + registerAction

        pm.triggerAction("demo", "run");
        QCOMPARE(QSettings().value("plugins/demo/data/last").toString(), QString("run"));

        QVERIFY(pm.setEnabled("demo", false));
        QVERIFY(!pm.isActive("demo"));
        QCOMPARE(pm.actions().size(), 0);                                         // acciones retiradas
        QCOMPARE(QSettings().value("plugins/demo/data/shutdown").toBool(), true); // onShutdown corrió
    }

    void enabledStatePersistsAcrossInstances()
    {
        makePlugin(plugins(), "demo", kBasicQml);
        { PluginManager pm; pm.setEngine(&m_engine); pm.scan(); pm.setEnabled("demo", true); }
        PluginManager again; again.setEngine(&m_engine);
        again.scan();
        QVERIFY(!again.isActive("demo"));
        QVERIFY(again.isEnabled("demo"));
        again.loadEnabled();
        QVERIFY(again.isActive("demo"));
    }

    void dependenciesEnableAndDisableTogether()
    {
        makePlugin(plugins(), "base", kBasicQml);
        makePlugin(plugins(), "extra", kBasicQml, R"("requires": ["base"])");
        PluginManager pm; pm.setEngine(&m_engine); pm.scan();

        QVERIFY(pm.setEnabled("extra", true));           // arrastra a "base"
        QVERIFY(pm.isActive("base"));
        QVERIFY(pm.isActive("extra"));

        QVERIFY(pm.setEnabled("base", false));           // desactiva también "extra"
        QVERIFY(!pm.isActive("base"));
        QVERIFY(!pm.isActive("extra"));
        QVERIFY(!pm.isEnabled("extra"));
    }

    void missingAndCircularDependenciesGiveErrors()
    {
        makePlugin(plugins(), "orphan", kBasicQml, R"("requires": ["ghost"])");
        makePlugin(plugins(), "ping", kBasicQml, R"("requires": ["pong"])");
        makePlugin(plugins(), "pong", kBasicQml, R"("requires": ["ping"])");
        PluginManager pm; pm.setEngine(&m_engine); pm.scan();

        QVERIFY(!pm.setEnabled("orphan", true));
        QCOMPARE(pm.statusOf("orphan"), QString("error"));
        bool found = false;
        for (const QVariant &v : pm.plugins())
            if (v.toMap()["id"] == "orphan") { QVERIFY(v.toMap()["error"].toString().contains("ghost")); found = true; }
        QVERIFY(found);

        QVERIFY(!pm.setEnabled("ping", true));           // ciclo: no se cuelga y da error
        QVERIFY(!pm.isActive("ping"));
    }

    void qmlSyntaxErrorIsReportedNotFatal()
    {
        makePlugin(plugins(), "oops", "import QtQml\nQtObject { esto no compila }\n");
        makePlugin(plugins(), "fine", kBasicQml);
        PluginManager pm; pm.setEngine(&m_engine); pm.scan();
        QVERIFY(!pm.setEnabled("oops", true));
        QCOMPARE(pm.statusOf("oops"), QString("error"));
        QVERIFY(pm.setEnabled("fine", true));            // un plugin roto no afecta a los demás
        QVERIFY(pm.isActive("fine"));
    }

    void pluginsSeeEditorServices()
    {
        Svc svc;
        makePlugin(plugins(), "svc", R"(
import QtQml
QtObject {
    Component.onCompleted: {
        var s = host.service("Svc")
        host.setSetting("value", s ? s.value : -1)
        host.setSetting("missing", host.service("Nope") === null ? "null" : "other")
    }
}
)");
        PluginManager pm; pm.setEngine(&m_engine); pm.registerService("Svc", &svc); pm.scan();
        QVERIFY(pm.setEnabled("svc", true));
        QCOMPARE(QSettings().value("plugins/svc/data/value").toInt(), 42);
        QCOMPARE(QSettings().value("plugins/svc/data/missing").toString(), QString("null"));
    }

    void nativeLibraryPluginLoadsRunsAndUnloads()
    {
        const QString dir = plugins() + "/native";
        QDir().mkpath(dir);
        const QString lib = "libnative.so";
        QVERIFY(QFile::copy(NATIVE_TEST_PLUGIN_PATH, dir + "/" + lib));
        writeFile(dir + "/plugin.json", QStringLiteral(
            R"({"id":"native","name":"Nativo","version":"1.0.0","apiVersion":1,"library":"%1"})").arg(lib).toUtf8());
        Svc svc;
        PluginManager pm; pm.setEngine(&m_engine); pm.registerService("Svc", &svc); pm.scan();
        QVERIFY(pm.setEnabled("native", true));
        QVERIFY(pm.isActive("native"));
        QCOMPARE(QSettings().value("plugins/native/data/init").toBool(), true);
        QCOMPARE(QSettings().value("plugins/native/data/sawService").toBool(), true);   // service() llega al plugin nativo
        QCOMPARE(pm.actions().size(), 1);
        pm.triggerAction("native", "hello");
        QCOMPARE(QSettings().value("plugins/native/data/last").toString(), QString("hello"));
        QVERIFY(pm.setEnabled("native", false));
        QCOMPARE(QSettings().value("plugins/native/data/shutdown").toBool(), true);
        QCOMPARE(pm.actions().size(), 0);
    }

    void nativeLibraryInitializeFailureIsReported()
    {
        const QString dir = plugins() + "/nativebad";
        QDir().mkpath(dir);
        QVERIFY(QFile::copy(NATIVE_TEST_PLUGIN_PATH, dir + "/libnative.so"));
        writeFile(dir + "/plugin.json", R"({"id":"nativebad","name":"Malo","library":"libnative.so"})");
        QSettings().setValue("plugins/nativebad/data/fail", true);       // el plugin se niega a arrancar
        PluginManager pm; pm.setEngine(&m_engine); pm.scan();
        QVERIFY(!pm.setEnabled("nativebad", true));
        QCOMPARE(pm.statusOf("nativebad"), QString("error"));
        QCOMPARE(pm.actions().size(), 0);
    }

    void missingLibraryIsReported()
    {
        writeFile(plugins() + "/nolib/plugin.json", R"({"id":"nolib","library":"libnada.so"})");
        PluginManager pm; pm.setEngine(&m_engine); pm.scan();
        QVERIFY(!pm.setEnabled("nolib", true));
        QCOMPARE(pm.statusOf("nolib"), QString("error"));
    }

    void installFromFolderCopiesAndActivatesNothing()
    {
        QTemporaryDir src;
        makePlugin(src.path(), "newone", kBasicQml);
        qputenv("KUTEDITOR_PLUGIN_PATH", plugins().toUtf8());
        PluginManager pm; pm.setEngine(&m_engine);
        // La carpeta de usuario es AppDataLocation/plugins (modo test) → la añadimos a la ruta
        const QString userDir = pm.userPluginDir();
        QDir(userDir).removeRecursively();
        qputenv("KUTEDITOR_PLUGIN_PATH", (plugins() + QDir::listSeparator() + userDir).toUtf8());
        QCOMPARE(pm.installFromFolder(src.path() + "/newone"), QString());
        QVERIFY(QFile::exists(userDir + "/newone/plugin.json"));
        QVERIFY(!pm.isActive("newone"));                 // instalar no activa
        QVERIFY(!pm.installFromFolder(src.path() + "/newone").isEmpty());   // ya instalado → error
        QDir(userDir).removeRecursively();
        qputenv("KUTEDITOR_PLUGIN_PATH", plugins().toUtf8());
    }
};

QTEST_GUILESS_MAIN(TestPluginManager)
#include "test_pluginmanager.moc"
