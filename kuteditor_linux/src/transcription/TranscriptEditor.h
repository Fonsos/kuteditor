#pragma once
// ============================================================================
// KutEditor · Edición por texto
// ----------------------------------------------------------------------------
// Une las palabras transcritas de todas las pistas en un único texto (ordenado
// en el tiempo), permite marcar las que sobran y aplica el recorte en la línea
// de tiempo como UN solo paso de «Deshacer», reajustando también la transcripción.
// Los plugins pueden marcar palabras (muletillas, pausas…) con markWhere/markIndices.
// ============================================================================
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "EditPlanner.h"
#include "TranscriptWords.h"

class TrackModel;
class UndoManager;
class TranscriptionManager;
class AppBusy;

class TranscriptEditor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int wordCount READ wordCount NOTIFY wordsChanged)
    Q_PROPERTY(int markedCount READ markedCount NOTIFY marksChanged)
    Q_PROPERTY(double maxPauseSec READ maxPauseSec WRITE setMaxPauseSec NOTIFY optionsChanged)
    Q_PROPERTY(bool trimEdges READ trimEdges WRITE setTrimEdges NOTIFY optionsChanged)
    Q_PROPERTY(double padSec READ padSec WRITE setPadSec NOTIFY optionsChanged)
public:
    explicit TranscriptEditor(QObject *parent = nullptr);

    void setModels(TrackModel *tracks, UndoManager *undo, TranscriptionManager *tr, AppBusy *busy = nullptr);

    int wordCount() const { return m_words.size(); }
    int markedCount() const;
    double maxPauseSec() const { return m_options.maxPauseSec; }
    void setMaxPauseSec(double v);
    bool trimEdges() const { return m_options.trimEdges; }
    void setTrimEdges(bool v);
    double padSec() const { return m_options.padSec; }
    void setPadSec(double v);

    /// Reconstruye la lista de palabras desde las transcripciones (borra las marcas).
    Q_INVOKABLE void refresh();

    /// Palabras unidas de todas las pistas: { index, track, trackName, start, end, text, marked }.
    Q_INVOKABLE QVariantList words() const;
    Q_INVOKABLE QVariantMap wordAt(int index) const;
    /// Índice de la palabra que suena en `sec` (o la anterior más cercana); -1 si no hay.
    Q_INVOKABLE int wordIndexAtTime(double sec) const;

    // ---- Marcas ----
    Q_INVOKABLE void setMarked(int from, int to, bool marked);   // rango inclusivo
    Q_INVOKABLE void markIndices(const QVariantList &indices, bool marked);
    /// Marca las palabras cuyo texto (sin puntuación, minúsculas) encaja con la expresión regular.
    /// Devuelve cuántas marcó.
    Q_INVOKABLE int markWhere(const QString &regexPattern, bool marked = true);
    Q_INVOKABLE void clearMarks();
    Q_INVOKABLE bool isMarked(int index) const;

    // ---- Plan y aplicación ----
    /// Calcula el plan sin tocar nada: { cuts, mutes, savedSec, deletedWords }.
    Q_INVOKABLE QVariantMap preview();
    /// Aplica el plan (un solo Deshacer). Devuelve los segundos que acorta el episodio,
    /// o -1 si no había nada que hacer.
    Q_INVOKABLE double apply();

    /// Igual que apply() pero el cálculo pesado (analizar el audio) va en segundo plano y se
    /// muestra el aviso «Aplicando cortes…». Al terminar emite applied(savedSec) (−1 si no había nada).
    Q_INVOKABLE void applyAsync();

    /// Pide a la interfaz que abra la ventana «Editar por texto» (la usan los plugins).
    Q_INVOKABLE void requestOpen() { emit openRequested(); }

    /// Calcula el plan a partir de palabras ya preparadas (para pruebas y plugins).
    EditPlan buildPlan() const;

signals:
    void wordsChanged();
    void marksChanged();
    void optionsChanged();
    void applied(double savedSec);
    void openRequested();

private:
    struct Entry { int track = 0; int idx = 0; double start = 0, end = 0; QString text; bool marked = false; };
    QMap<int, EnergyEnvelope> envelopes() const;
    AppBusy *m_busy = nullptr;
    bool m_applying = false;
    void applyTo(const EditPlan &plan);
    QString trackName(int t) const;

    TrackModel *m_tracks = nullptr;
    UndoManager *m_undo = nullptr;
    TranscriptionManager *m_tr = nullptr;
    QVector<Entry> m_words;
    EditPlanOptions m_options;
};
