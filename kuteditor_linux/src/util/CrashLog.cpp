#include "CrashLog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QUrl>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#endif

namespace {
int g_fd = -1;
QString g_dir, g_currentPath, g_previousPath, g_lockPath;
bool g_previousCrashed = false;
QMutex g_mutex;
QtMessageHandler g_prevHandler = nullptr;
CrashLog *g_instance = nullptr;
char g_signalPrefix[64];

void writeAll(int fd, const char *s, size_t n)
{
    while (n > 0) {
        const ssize_t w = ::write(fd, s, n);
        if (w <= 0) return;
        s += w; n -= size_t(w);
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const char *lvl = type == QtDebugMsg ? "DEBUG" : type == QtInfoMsg ? "INFO " : type == QtWarningMsg ? "WARN " : "CRIT ";
    const QString line = QStringLiteral("%1 [%2] %3\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")), QLatin1String(lvl), msg);
    {
        QMutexLocker lock(&g_mutex);
        if (g_fd >= 0) {
            const QByteArray b = line.toUtf8();
            writeAll(g_fd, b.constData(), size_t(b.size()));
        }
    }
    if (g_prevHandler) g_prevHandler(type, ctx, msg);
    else { const QByteArray b = line.toLocal8Bit(); fputs(b.constData(), stderr); }
    if (type == QtFatalMsg) abort();
}

const char *sigName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (acceso a memoria inválido)";
    case SIGBUS:  return "SIGBUS (acceso a un archivo/memoria mapeada que no se puede leer o escribir: disco o /tmp lleno)";
    case SIGABRT: return "SIGABRT (aborto: error interno o memoria agotada)";
    case SIGFPE:  return "SIGFPE (error aritmético)";
    case SIGILL:  return "SIGILL (instrucción ilegal)";
    default:      return "señal desconocida";
    }
}

void crashHandler(int sig)
{
    // Solo llamadas seguras en un manejador de señal (write, backtrace_symbols_fd).
    if (g_fd >= 0) {
        const char *head = "\n!!! CIERRE INESPERADO: ";
        writeAll(g_fd, head, strlen(head));
        const char *n = sigName(sig);
        writeAll(g_fd, n, strlen(n));
        writeAll(g_fd, "\nPila de llamadas:\n", 19);
#if defined(__linux__) || defined(__APPLE__)
        void *frames[64];
        const int c = backtrace(frames, 64);
        backtrace_symbols_fd(frames, c, g_fd);
#endif
        ::fsync(g_fd);
    }
    // Dejar que el sistema termine el proceso como siempre (con core dump si está activado).
    signal(sig, SIG_DFL);
    raise(sig);
}

QString defaultDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
}

bool pidAlive(qint64 pid)
{
    if (pid <= 0) return false;
    QFile f(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) return false;
    // «pid (nombre) E …»: un proceso zombi (E = Z) ya está muerto aunque su entrada siga en /proc
    const QByteArray st = f.readAll();
    const int rp = st.lastIndexOf(')');
    return !(rp >= 0 && rp + 2 < st.size() && st[rp + 2] == 'Z');
}
} // namespace

CrashLog *CrashLog::instance()
{
    if (!g_instance) g_instance = new CrashLog(QCoreApplication::instance());
    return g_instance;
}

bool CrashLog::start(const QString &dirIn)
{
    g_dir = dirIn.isEmpty() ? defaultDir() : dirIn;
    QDir().mkpath(g_dir);
    g_lockPath = g_dir + QStringLiteral("/session.lock");

    // ¿La sesión anterior terminó mal? (marca de sesión que sobrevive y cuyo proceso ya no existe)
    g_previousCrashed = false; g_previousPath.clear();
    QFile lock(g_lockPath);
    if (lock.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> parts = lock.readAll().split('\n');
        lock.close();
        const qint64 pid = parts.value(0).trimmed().toLongLong();
        if (pid != QCoreApplication::applicationPid() && !pidAlive(pid)) {
            g_previousCrashed = true;
            g_previousPath = QString::fromUtf8(parts.value(1).trimmed());
        }
    }

    // Conservar los 10 registros más recientes
    const QStringList old = QDir(g_dir).entryList({QStringLiteral("kuteditor-*.log")}, QDir::Files, QDir::Name);
    for (int i = 0; i < old.size() - 9; ++i) QFile::remove(g_dir + QLatin1Char('/') + old[i]);

    g_currentPath = g_dir + QStringLiteral("/kuteditor-%1.log")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    g_fd = ::open(g_currentPath.toUtf8().constData(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (g_fd < 0) return false;

    if (lock.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        lock.write(QByteArray::number(QCoreApplication::applicationPid()) + "\n" + g_currentPath.toUtf8() + "\n");
        lock.close();
    }

    {   // nunca encadenarse consigo mismo si start() se llama dos veces
        const QtMessageHandler old = qInstallMessageHandler(messageHandler);
        if (old != messageHandler) g_prevHandler = old;
    }

#if defined(__linux__) || defined(__APPLE__)
    void *warm[4]; backtrace(warm, 4);       // precarga libgcc para poder usar backtrace dentro del manejador
#endif
    for (int s : {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL}) signal(s, crashHandler);

    qInfo().noquote() << "=== KutEditor" << QCoreApplication::applicationVersion()
                      << "| Qt" << qVersion() << "|" << QSysInfo::prettyProductName()
                      << "| pid" << QCoreApplication::applicationPid();
    qInfo().noquote() << memoryLine();
    if (g_previousCrashed)
        qWarning().noquote() << "La sesión anterior terminó de forma inesperada. Registro:" << g_previousPath;
    return true;
}

void CrashLog::finish()
{
    qInfo() << "Cierre normal.";
    QFile::remove(g_lockPath);
    for (int s : {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL}) signal(s, SIG_DFL);
    qInstallMessageHandler(g_prevHandler);          // vuelve el manejador original
    QMutexLocker lock(&g_mutex);
    if (g_fd >= 0) { ::close(g_fd); g_fd = -1; }
}

bool CrashLog::previousCrashed() const { return g_previousCrashed; }
QString CrashLog::previousLogPath() const { return g_previousPath; }
QString CrashLog::currentLogPath() const { return g_currentPath; }
QString CrashLog::logDir() const { return g_dir; }

QString CrashLog::memoryLine()
{
    auto kb = [](const QByteArray &file, const char *key) -> qint64 {
        QFile f(QString::fromLatin1(file)); if (!f.open(QIODevice::ReadOnly)) return -1;
        for (const QByteArray &l : f.readAll().split('\n'))
            if (l.startsWith(key)) return l.mid(int(strlen(key)) + 1).trimmed().split(' ')[0].toLongLong();
        return -1;
    };
    return QStringLiteral("Memoria: proceso %1 MB (pico %2 MB) · sistema disponible %3 MB de %4 MB")
        .arg(kb("/proc/self/status", "VmRSS") / 1024).arg(kb("/proc/self/status", "VmHWM") / 1024)
        .arg(kb("/proc/meminfo", "MemAvailable") / 1024).arg(kb("/proc/meminfo", "MemTotal") / 1024);
}

void CrashLog::note(const QString &text) { qInfo().noquote() << text; }

void CrashLog::openLogDir() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(g_dir));
}
