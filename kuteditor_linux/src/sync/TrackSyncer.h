#pragma once
// ============================================================================
// KutEditor · Sincronizar una pista con otra (servicio para plugins y QML)
// ----------------------------------------------------------------------------
// Compara la pista con la de referencia (TrackSync), y recoloca sus clips en la
// línea de tiempo: la mueve al desfase correcto y, si la referencia tuvo un fallo
// de grabación, corta el audio que sobra o deja el hueco (silencio) que la referencia
// tiene. Todo en un solo paso de «Deshacer»; el audio original no se toca.
// ============================================================================
#include <QObject>
#include <QVariantMap>

class TrackModel;
class UndoManager;
class AppBusy;

class TrackSyncer : public QObject
{
    Q_OBJECT
public:
    explicit TrackSyncer(QObject *parent = nullptr) : QObject(parent) {}
    void setModels(TrackModel *tracks, UndoManager *undo, AppBusy *busy = nullptr)
    { m_tracks = tracks; m_undo = undo; m_busy = busy; }

    /// Analiza sin tocar nada. { ok, message, reliable, offset, confidence, windows,
    ///   breaks:[{at,delta}], segments:[{start,end,offset}] }
    Q_INVOKABLE QVariantMap analyze(int refTrack, int track);
    /// Analiza y aplica. Si el pico de correlación no es fiable no toca nada salvo force=true.
    /// Devuelve lo mismo que analyze() más { applied }.
    Q_INVOKABLE QVariantMap apply(int refTrack, int track, bool force = false);

    /// Versión en segundo plano: no bloquea la interfaz y muestra el aviso «Sincronizando…».
    /// El resultado llega por la señal finished (mismo mapa que analyze/apply).
    Q_INVOKABLE void analyzeAsync(int refTrack, int track);
    Q_INVOKABLE void applyAsync(int refTrack, int track, bool force = false);
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    bool busy() const { return m_working; }

    struct Computed;   // resultado del análisis (interno)

signals:
    void finished(const QVariantMap &result);
    void busyChanged();

private:
    QVariantMap run(int refTrack, int track, bool doApply, bool force);
    void runAsync(int refTrack, int track, bool doApply, bool force);
    QVariantMap finish(const Computed &c, int ref, int track, bool doApply, bool force);
    bool m_working = false;
    AppBusy *m_busy = nullptr;
    TrackModel *m_tracks = nullptr;
    UndoManager *m_undo = nullptr;
};
