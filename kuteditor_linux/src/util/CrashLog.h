#pragma once
// ============================================================================
// KutEditor · Registro local de la sesión y de cierres inesperados
// ----------------------------------------------------------------------------
//  · Todo lo que el programa escribe con qDebug/qWarning va (con hora) a
//    ~/.local/share/Kut/KutEditor/logs/kuteditor-AAAAMMDD-HHMMSS.log  (se conservan los últimos 10).
//  · Si el programa muere por una señal (SIGSEGV, SIGBUS, SIGABRT…) se anota la señal y la pila de
//    llamadas en el mismo registro.
//  · Una marca «session.lock» distingue un cierre normal de uno inesperado: al arrancar, si la marca
//    quedó de una sesión que ya no existe, se avisa y se indica qué registro mirar.
// ============================================================================
#include <QObject>
#include <QString>

class CrashLog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool previousCrashed READ previousCrashed CONSTANT)
    Q_PROPERTY(QString previousLogPath READ previousLogPath CONSTANT)
    Q_PROPERTY(QString currentLogPath READ currentLogPath CONSTANT)
    Q_PROPERTY(QString logDir READ logDir CONSTANT)
public:
    explicit CrashLog(QObject *parent = nullptr) : QObject(parent) {}

    /// Arranca el registro. `dir` vacío = carpeta por defecto. Devuelve false si no se pudo abrir el archivo.
    static bool start(const QString &dir = QString());
    /// Cierre normal: borra la marca de sesión y cierra el archivo.
    static void finish();
    static CrashLog *instance();

    bool previousCrashed() const;
    QString previousLogPath() const;
    QString currentLogPath() const;
    QString logDir() const;

    /// Línea con la memoria del proceso y del sistema (para anotar en importaciones largas).
    static QString memoryLine();
    /// Escribe una línea en el registro (además de qInfo).
    static void note(const QString &text);

    Q_INVOKABLE void openLogDir() const;
};
