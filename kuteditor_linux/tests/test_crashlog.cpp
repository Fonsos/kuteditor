#include <QtTest>
#include <QTemporaryDir>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include "util/CrashLog.h"

static QString readAll(const QString &p) { QFile f(p); f.open(QIODevice::ReadOnly); return QString::fromUtf8(f.readAll()); }

class TestCrashLog : public QObject {
    Q_OBJECT
private slots:
    void logsMessagesAndCleanShutdown() {
        QTemporaryDir d;
        QVERIFY(CrashLog::start(d.path()));
        qInfo() << "hola desde el test";
        qWarning() << "aviso de prueba";
        const QString path = CrashLog::instance()->currentLogPath();
        QVERIFY(QFile::exists(d.path() + "/session.lock"));
        CrashLog::finish();
        QVERIFY(!QFile::exists(d.path() + "/session.lock"));       // cierre normal: sin marca
        const QString txt = readAll(path);
        QVERIFY(txt.contains("hola desde el test"));
        QVERIFY(txt.contains("[WARN ] aviso de prueba") || txt.contains("[WARN] aviso de prueba") || txt.contains("aviso de prueba"));
        QVERIFY(txt.contains("Cierre normal."));
        QVERIFY(txt.contains("Memoria: proceso"));
        QVERIFY(!CrashLog::instance()->previousCrashed());
    }
    void staleLockFromDeadSessionIsDetected() {
        QTemporaryDir d;
        { QFile f(d.path() + "/session.lock"); QVERIFY(f.open(QIODevice::WriteOnly));
          f.write("999999\n" + d.path().toUtf8() + "/kuteditor-viejo.log\n"); }
        QVERIFY(CrashLog::start(d.path()));
        QVERIFY(CrashLog::instance()->previousCrashed());
        QVERIFY(CrashLog::instance()->previousLogPath().endsWith("kuteditor-viejo.log"));
        QVERIFY(readAll(CrashLog::instance()->currentLogPath()).contains("terminó de forma inesperada"));
        CrashLog::finish();
    }
    void oldLogsAreRotated() {
        QTemporaryDir d;
        for (int i = 0; i < 14; ++i) { QFile f(QString("%1/kuteditor-2020010%2-%3.log").arg(d.path()).arg(i / 10).arg(i % 10, 6, 10, QChar('0'))); f.open(QIODevice::WriteOnly); }
        QVERIFY(CrashLog::start(d.path()));
        CrashLog::finish();
        QVERIFY(QDir(d.path()).entryList({"kuteditor-*.log"}, QDir::Files).size() <= 11);
    }
    void realCrashWritesSignalAndBacktrace() {
        QTemporaryDir d;
        const pid_t pid = fork();
        if (pid == 0) {                      // hijo: arranca el registro y muere con SIGSEGV
            CrashLog::start(d.path());
            qInfo() << "antes del fallo";
            raise(SIGSEGV);
            _exit(0);
        }
        int st = 0; waitpid(pid, &st, 0);
        QVERIFY(WIFSIGNALED(st));
        QCOMPARE(WTERMSIG(st), int(SIGSEGV));
        const QStringList logs = QDir(d.path()).entryList({"kuteditor-*.log"}, QDir::Files);
        QCOMPARE(logs.size(), 1);
        const QString txt = readAll(d.path() + "/" + logs.first());
        QVERIFY2(txt.contains("antes del fallo"), qPrintable(txt));
        QVERIFY2(txt.contains("CIERRE INESPERADO: SIGSEGV"), qPrintable(txt));
        QVERIFY2(txt.contains("Pila de llamadas"), qPrintable(txt));
        QVERIFY(QFile::exists(d.path() + "/session.lock"));         // la marca queda: el próximo arranque lo sabrá
        // …y el siguiente arranque lo detecta
        QVERIFY(CrashLog::start(d.path()));
        QVERIFY(CrashLog::instance()->previousCrashed());
        CrashLog::finish();
    }
};
QTEST_MAIN(TestCrashLog)
#include "test_crashlog.moc"
