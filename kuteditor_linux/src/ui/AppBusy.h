#pragma once
// ============================================================================
// KutEditor · Indicador global de «trabajando…»
// ----------------------------------------------------------------------------
// Las tareas largas (sincronizar, analizar audio, aplicar cortes, guardar…) llaman a
// begin("texto") al empezar y a end() al terminar. La interfaz muestra un aviso que
// bloquea la edición mientras haya alguna en curso. Solo desde el hilo de la interfaz.
// Los plugins lo tienen como servicio «AppBusy» (y como context property en QML).
// ============================================================================
#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QString>
#include <QStringList>

class AppBusy : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
public:
    explicit AppBusy(QObject *parent = nullptr) : QObject(parent) {}

    bool active() const { return !m_stack.isEmpty(); }
    QString text() const { return m_stack.isEmpty() ? QString() : m_stack.last(); }

    /// Anuncia una tarea. Con `paintNow` deja que la interfaz pinte el aviso antes de que
    /// empiece un trabajo que bloqueará el hilo (guardar, abrir…).
    Q_INVOKABLE void begin(const QString &text, bool paintNow = false)
    {
        m_stack.append(text);
        emit changed();
        if (paintNow) QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
    }
    Q_INVOKABLE void end()
    {
        if (!m_stack.isEmpty()) m_stack.removeLast();
        emit changed();
    }

signals:
    void changed();

private:
    QStringList m_stack;
};
