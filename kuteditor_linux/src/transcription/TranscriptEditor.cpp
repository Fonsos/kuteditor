#include "TranscriptEditor.h"
#include "TranscriptRemap.h"
#include "TranscriptionManager.h"
#include "ui/TrackModel.h"
#include "ui/UndoManager.h"
#include "ui/AppBusy.h"
#include <QPointer>
#include <QtConcurrent>

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

TranscriptEditor::TranscriptEditor(QObject *parent) : QObject(parent) {}

void TranscriptEditor::setModels(TrackModel *tracks, UndoManager *undo, TranscriptionManager *tr, AppBusy *busy)
{
    m_tracks = tracks; m_undo = undo; m_tr = tr; m_busy = busy;
    if (m_tr) {
        connect(m_tr, &TranscriptionManager::transcriptionFinished, this, [this](int) { refresh(); });
    }
}

int TranscriptEditor::markedCount() const
{
    int n = 0;
    for (const Entry &e : m_words) if (e.marked) ++n;
    return n;
}

void TranscriptEditor::setMaxPauseSec(double v) { if (v != m_options.maxPauseSec) { m_options.maxPauseSec = v; emit optionsChanged(); } }
void TranscriptEditor::setTrimEdges(bool v) { if (v != m_options.trimEdges) { m_options.trimEdges = v; emit optionsChanged(); } }
void TranscriptEditor::setPadSec(double v) { if (v != m_options.padSec) { m_options.padSec = v; emit optionsChanged(); } }

QString TranscriptEditor::trackName(int t) const
{
    if (!m_tracks) return QString();
    return m_tracks->getTrackData(t).value("name").toString();
}

void TranscriptEditor::refresh()
{
    m_words.clear();
    if (m_tr) {
        for (int t : m_tr->transcribedTracks()) {
            const auto words = m_tr->trackWords(t);
            for (int i = 0; i < words.size(); ++i) {
                Entry e;
                e.track = t; e.idx = i;
                e.start = words[i].startSec; e.end = words[i].endSec;
                e.text = words[i].text.trimmed();
                if (!e.text.isEmpty()) m_words.append(e);
            }
        }
        std::stable_sort(m_words.begin(), m_words.end(),
                         [](const Entry &a, const Entry &b) { return a.start < b.start; });
    }
    emit wordsChanged();
    emit marksChanged();
}

QVariantMap TranscriptEditor::wordAt(int i) const
{
    QVariantMap m;
    if (i < 0 || i >= m_words.size()) return m;
    const Entry &e = m_words[i];
    m["index"] = i; m["track"] = e.track; m["trackName"] = trackName(e.track);
    m["start"] = e.start; m["end"] = e.end; m["text"] = e.text; m["marked"] = e.marked;
    return m;
}

QVariantList TranscriptEditor::words() const
{
    QVariantList out;
    out.reserve(m_words.size());
    for (int i = 0; i < m_words.size(); ++i) out.append(wordAt(i));
    return out;
}

int TranscriptEditor::wordIndexAtTime(double sec) const
{
    int best = -1;
    for (int i = 0; i < m_words.size(); ++i) {
        if (m_words[i].start <= sec) best = i; else break;
    }
    return best;
}

void TranscriptEditor::setMarked(int from, int to, bool marked)
{
    if (from > to) std::swap(from, to);
    from = qMax(0, from); to = qMin(m_words.size() - 1, to);
    for (int i = from; i <= to; ++i) m_words[i].marked = marked;
    emit marksChanged();
}

void TranscriptEditor::markIndices(const QVariantList &indices, bool marked)
{
    for (const QVariant &v : indices) {
        const int i = v.toInt();
        if (i >= 0 && i < m_words.size()) m_words[i].marked = marked;
    }
    emit marksChanged();
}

int TranscriptEditor::markWhere(const QString &pattern, bool marked)
{
    const QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
    if (!re.isValid()) return 0;
    static const QRegularExpression punct(QStringLiteral("^[\\p{P}\\p{S}\\s]+|[\\p{P}\\p{S}\\s]+$"));
    int n = 0;
    for (Entry &e : m_words) {
        QString core = e.text; core.remove(punct);
        if (!core.isEmpty() && re.match(core.toLower()).hasMatch()) { e.marked = marked; ++n; }
    }
    emit marksChanged();
    return n;
}

void TranscriptEditor::clearMarks()
{
    for (Entry &e : m_words) e.marked = false;
    emit marksChanged();
}

bool TranscriptEditor::isMarked(int i) const { return i >= 0 && i < m_words.size() && m_words[i].marked; }

QMap<int, EnergyEnvelope> TranscriptEditor::envelopes() const
{
    QMap<int, EnergyEnvelope> out;
    if (!m_tracks) return out;
    QSet<int> tracks;
    for (const Entry &e : m_words) tracks.insert(e.track);
    // También las pistas sin transcripción cuentan para decidir silencios comunes.
    for (int t = 0; t < m_tracks->count(); ++t) tracks.insert(t);
    for (int t : std::as_const(tracks)) {
        int frames = 0, ch = 0, sr = 0;
        const float *d = m_tracks->trackSamplesData(t, &frames, &ch, &sr);
        if (!d || frames <= 0 || ch <= 0 || sr <= 0) continue;
        EnergyEnvelope env; env.stepSec = 0.01;
        const int step = qMax(1, int(sr * env.stepSec));
        env.db.reserve(frames / step + 1);
        for (qint64 f = 0; f < frames; f += step) {
            const qint64 end = qMin<qint64>(frames, f + step);
            double acc = 0;
            for (qint64 k = f; k < end; ++k)
                for (int c = 0; c < ch; ++c) { const double v = d[k * ch + c]; acc += v * v; }
            const double rms = std::sqrt(acc / double((end - f) * ch));
            env.db.append(float(20.0 * std::log10(rms + 1e-6)));
        }
        out.insert(t, env);
    }
    return out;
}

EditPlan TranscriptEditor::buildPlan() const
{
    QVector<EditWord> ws;
    ws.reserve(m_words.size());
    for (const Entry &e : m_words) {
        EditWord w; w.track = e.track; w.start = e.start; w.end = e.end; w.deleted = e.marked;
        ws.append(w);
    }
    EditPlanOptions o = m_options;
    if (m_tracks && o.totalDuration <= 0) {
        for (int t = 0; t < m_tracks->count(); ++t) {
            int frames = 0, ch = 0, sr = 0;
            if (m_tracks->trackSamplesData(t, &frames, &ch, &sr) && sr > 0)
                o.totalDuration = qMax(o.totalDuration, double(frames) / sr);
        }
    }
    return EditPlanner::plan(ws, envelopes(), o);
}

static QVariantList rangesToVariant(const QVector<TimeRange> &r)
{
    QVariantList l;
    for (const TimeRange &t : r) l.append(QVariantMap{{"start", t.start}, {"end", t.end}});
    return l;
}

QVariantMap TranscriptEditor::preview()
{
    const EditPlan p = buildPlan();
    QVariantMap m;
    m["cuts"] = rangesToVariant(p.cuts);
    QVariantMap mu;
    int muteCount = 0;
    for (auto it = p.mutes.constBegin(); it != p.mutes.constEnd(); ++it) {
        mu.insert(QString::number(it.key()), rangesToVariant(it.value()));
        muteCount += it.value().size();
    }
    m["mutes"] = mu; m["muteCount"] = muteCount;
    m["savedSec"] = p.savedSec(); m["deletedWords"] = p.deletedWords;
    return m;
}

double TranscriptEditor::apply()
{
    if (!m_tracks || !m_undo || !m_tr) return -1;
    const EditPlan plan = buildPlan();
    if (plan.cuts.isEmpty() && plan.mutes.isEmpty()) return -1;
    applyTo(plan);
    const double saved = plan.savedSec();
    emit applied(saved);
    return saved;
}

void TranscriptEditor::applyAsync()
{
    if (!m_tracks || !m_undo || !m_tr || m_applying) { emit applied(-1); return; }
    m_applying = true;
    if (m_busy) m_busy->begin(tr("Aplicando cortes… (analizando el audio)"), true);
    QPointer<TranscriptEditor> weak(this);
    // buildPlan() solo lee: la interfaz está bloqueada por el aviso mientras tanto
    (void) QtConcurrent::run([weak]() {
        if (!weak) return;
        const EditPlan plan = weak->buildPlan();
        QMetaObject::invokeMethod(weak.data(), [weak, plan]() {
            if (!weak) return;
            double saved = -1;
            if (!plan.cuts.isEmpty() || !plan.mutes.isEmpty()) {
                weak->applyTo(plan);
                saved = plan.savedSec();
            }
            weak->m_applying = false;
            if (weak->m_busy) weak->m_busy->end();
            emit weak->applied(saved);
        }, Qt::QueuedConnection);
    });
}

void TranscriptEditor::applyTo(const EditPlan &plan)
{
    // Instantánea de la transcripción para poder deshacer / rehacer.
    const QJsonArray before = m_tr->transcriptionsToJson();

    // Palabras que se eliminan (por pista e índice) + nuevos tiempos de las que quedan.
    QMap<int, QSet<int>> dropped;
    for (const Entry &e : m_words) if (e.marked) dropped[e.track].insert(e.idx);

    QMap<int, QVector<TranscriptWord>> after;
    for (int t : m_tr->transcribedTracks()) {
        QVector<TranscriptWord> keep;
        const auto words = m_tr->trackWords(t);
        const auto &drop = dropped.value(t);
        for (int i = 0; i < words.size(); ++i) if (!drop.contains(i)) keep.append(words[i]);
        after.insert(t, TranscriptRemap::remapWords(keep, plan.cuts));
    }

    m_undo->beginMacro(tr("Editar por texto"));
    // Silencios por pista primero (los tiempos aún son los originales)…
    for (auto it = plan.mutes.constBegin(); it != plan.mutes.constEnd(); ++it)
        for (const TimeRange &r : it.value())
            m_undo->deleteRegion(r.start, r.end, false, QVariant::fromValue(QVariantList{it.key()}));
    // …y luego los cortes en todas las pistas, de atrás hacia delante.
    for (int i = plan.cuts.size() - 1; i >= 0; --i)
        m_undo->deleteRegion(plan.cuts[i].start, plan.cuts[i].end, true);

    auto setAll = [this](const QMap<int, QVector<TranscriptWord>> &m) {
        for (auto it = m.constBegin(); it != m.constEnd(); ++it)
            m_tr->setTrackTranscription(it.key(), {}, it.value());
    };
    auto redo = [this, after, setAll]() { setAll(after); refresh(); };
    auto undo = [this, before]() { m_tr->transcriptionsFromJson(before); refresh(); };
    setAll(after);
    m_undo->pushLambda(tr("Transcripción tras la edición"), redo, undo, /*alreadyDone*/ true);
    m_undo->endMacro();
    refresh();
}
