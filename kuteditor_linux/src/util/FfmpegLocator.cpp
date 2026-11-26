#include "FfmpegLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace {
bool isRunnable(const QString &path)
{
    const QFileInfo fi(path);
    return fi.isFile() && fi.isExecutable();
}
}

QString FfmpegLocator::find()
{
#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("ffmpeg.exe");
#else
    const QString exe = QStringLiteral("ffmpeg");
#endif

    const QString fromEnv = qEnvironmentVariable("KUTEDITOR_FFMPEG");
    if (!fromEnv.isEmpty() && isRunnable(fromEnv))
        return fromEnv;

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList bundled = {
        appDir + QLatin1Char('/') + exe,
        QDir::cleanPath(appDir + QStringLiteral("/../lib/kuteditor/") + exe),
    };
    for (const QString &p : bundled)
        if (isRunnable(p))
            return p;

#ifdef Q_OS_MAC
    const QStringList macPaths = {
        QStringLiteral("/opt/homebrew/bin/ffmpeg"),
        QStringLiteral("/usr/local/bin/ffmpeg"),
        QStringLiteral("/opt/local/bin/ffmpeg"),
    };
    for (const QString &p : macPaths)
        if (isRunnable(p))
            return p;
#endif

    return QStandardPaths::findExecutable(exe);
}
