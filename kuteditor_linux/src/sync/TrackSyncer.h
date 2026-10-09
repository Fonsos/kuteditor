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

class TrackSyncer : public QObject
{
    Q_OBJECT
public:
    explicit TrackSyncer(QObject *parent = nullptr) : QObject(parent) {}
    void setModels(TrackModel *tracks, UndoManager *undo) { m_tracks = tracks; m_undo = undo; }

    /// Analiza sin tocar nada. { ok, message, reliable, offset, confidence, windows,
    ///   breaks:[{at,delta}], segments:[{start,end,offset}] }
    Q_INVOKABLE QVariantMap analyze(int refTrack, int track);
    /// Analiza y aplica. Si el pico de correlación no es fiable no toca nada salvo force=true.
    /// Devuelve lo mismo que analyze() más { applied }.
    Q_INVOKABLE QVariantMap apply(int refTrack, int track, bool force = false);

private:
    QVariantMap run(int refTrack, int track, bool doApply, bool force);
    TrackModel *m_tracks = nullptr;
    UndoManager *m_undo = nullptr;
};
