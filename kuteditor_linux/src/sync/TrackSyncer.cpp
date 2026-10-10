#include "TrackSyncer.h"
#include "TrackSync.h"
#include "ui/TrackModel.h"
#include "ui/UndoManager.h"
#include "ui/AppBusy.h"
#include <QPointer>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace {
QVector<float> trackEnvelope(const TrackModel *tm, int i, double *durSec)
{
    int frames = 0, ch = 0, sr = 0;
    const float *d = tm->trackSamplesData(i, &frames, &ch, &sr);
    if (!d || frames <= 0 || sr <= 0) { *durSec = 0; return {}; }
    *durSec = double(frames) / sr;
    return TrackSync::envelopeDb(d, frames, ch, sr);
}
}

struct TrackSyncer::Computed {
    bool valid = false;
    QString error;
    TrackSync::SyncResult r;
    double trkDur = 0;
};

QVariantMap TrackSyncer::analyze(int refTrack, int track) { return run(refTrack, track, false, false); }
QVariantMap TrackSyncer::apply(int refTrack, int track, bool force) { return run(refTrack, track, true, force); }
void TrackSyncer::analyzeAsync(int refTrack, int track) { runAsync(refTrack, track, false, false); }
void TrackSyncer::applyAsync(int refTrack, int track, bool force) { runAsync(refTrack, track, true, force); }

static TrackSyncer::Computed computeSync(const TrackModel *tm, int ref, int track)
{
    TrackSyncer::Computed c;
    if (ref == track || ref < 0 || track < 0 || ref >= tm->count() || track >= tm->count()) {
        c.error = QObject::tr("Elige dos pistas distintas.");
        return c;
    }
    double refDur = 0;
    const QVector<float> envRef = trackEnvelope(tm, ref, &refDur);
    const QVector<float> envTrk = trackEnvelope(tm, track, &c.trkDur);
    if (envRef.size() < 3000 || envTrk.size() < 3000) {
        c.error = QObject::tr("Hace falta al menos 30 s de audio en las dos pistas.");
        return c;
    }
    c.r = TrackSync::analyze(envRef, envTrk, c.trkDur);
    c.valid = true;
    return c;
}

QVariantMap TrackSyncer::run(int ref, int track, bool doApply, bool force)
{
    if (!m_tracks || !m_undo) return QVariantMap{{"ok", false}, {"applied", false}, {"message", tr("Editor no inicializado.")}};
    return finish(computeSync(m_tracks, ref, track), ref, track, doApply, force);
}

void TrackSyncer::runAsync(int ref, int track, bool doApply, bool force)
{
    if (!m_tracks || !m_undo || m_working) {
        emit finished(QVariantMap{{"ok", false}, {"applied", false},
            {"message", m_working ? tr("Ya hay una sincronización en curso.") : tr("Editor no inicializado.")}});
        return;
    }
    m_working = true; emit busyChanged();
    if (m_busy) m_busy->begin(doApply ? tr("Sincronizando pistas… (puede tardar un poco con audio largo)")
                                       : tr("Analizando pistas…"), true);
    QPointer<TrackSyncer> weak(this);
    const TrackModel *tm = m_tracks;
    (void) QtConcurrent::run([weak, tm, ref, track, doApply, force]() {
        Computed c = computeSync(tm, ref, track);
        QMetaObject::invokeMethod(weak.data(), [weak, c, ref, track, doApply, force]() {
            if (!weak) return;
            const QVariantMap res = weak->finish(c, ref, track, doApply, force);
            weak->m_working = false;
            if (weak->m_busy) weak->m_busy->end();
            emit weak->busyChanged();
            emit weak->finished(res);
        }, Qt::QueuedConnection);
    });
}

QVariantMap TrackSyncer::finish(const Computed &c, int ref, int track, bool doApply, bool force)
{
    Q_UNUSED(ref);
    QVariantMap out;
    out["ok"] = false; out["applied"] = false;
    auto fail = [&](const QString &m) { out["message"] = m; return out; };
    if (!c.valid) return fail(c.error);
    const TrackSync::SyncResult &r = c.r;
    const double trkDur = c.trkDur; Q_UNUSED(trkDur);
    out["reliable"] = r.reliable; out["offset"] = r.globalOffset; out["confidence"] = r.confidence;
    out["windows"] = r.windows;
    QVariantList br, sg;
    for (const auto &b : r.breaks) br.append(QVariantMap{{"at", b.at}, {"delta", b.delta}});
    for (const auto &s : r.segments) sg.append(QVariantMap{{"start", s.s}, {"end", s.e}, {"offset", s.off}});
    out["breaks"] = br; out["segments"] = sg;
    out["ok"] = true;
    if (!doApply) { out["message"] = r.reliable ? tr("Las pistas se parecen lo bastante para sincronizarlas.")
                                                : tr("Las pistas no parecen contener el mismo audio (confianza baja)."); return out; }
    if (!r.reliable && !force) { out["message"] = tr("No lo aplico: las pistas no parecen contener el mismo audio."); return out; }

    // --- Recolocar los clips ---
    const QVariantList clips = m_tracks->clipsOf(track);
    if (clips.size() != 1) {
        out["ok"] = false;
        return fail(tr("La pista ya tiene %1 clips; sincroniza antes de cortarla (hace falta un único clip).").arg(clips.size()));
    }
    const QVariantMap orig = m_tracks->clipSnapshot(track, 0);
    const int sr = m_tracks->trackSampleRate(track);
    const double c0 = double(orig["timelineStart"].toLongLong()) / sr;
    const double c1 = c0 + double(orig["length"].toLongLong()) / sr;
    const qint64 srcOff = orig["sourceOffset"].toLongLong();
    const qint64 fade = qint64(0.005 * sr);

    QVariantList snaps;
    for (int k = 0; k < r.segments.size(); ++k) {
        double s = r.segments[k].s, e = r.segments[k].e;
        const double off = r.segments[k].off;
        // lo que cubre la pista original, en tiempo de la referencia
        s = std::max({s, 0.0, c0 + off}); e = std::min(e, c1 + off);
        if (e - s < 0.02) continue;
        QVariantMap sn = orig;
        sn["timelineStart"] = qint64(std::llround(s * sr));
        sn["sourceOffset"] = srcOff + qint64(std::llround((s - off - c0) * sr));
        sn["length"] = qint64(std::llround((e - s) * sr));
        sn["fadeInLen"] = k == 0 ? orig["fadeInLen"] : QVariant(fade);
        sn["fadeOutLen"] = k == r.segments.size() - 1 ? orig["fadeOutLen"] : QVariant(fade);
        snaps.append(sn);
    }
    if (snaps.isEmpty()) return fail(tr("No queda audio tras alinear."));

    auto replaceWith = [this, track](const QVariantList &list) {
        const QVariantList cur = m_tracks->clipsOf(track);
        QVariantList idx;
        for (int i = 0; i < cur.size(); ++i) idx.append(i);
        if (!idx.isEmpty()) m_tracks->removeClips(track, idx);
        m_tracks->insertClipSnapshots(track, list);
    };
    const QVariantList before{orig};
    replaceWith(snaps);
    m_undo->pushLambda(tr("Sincronizar pista"), [=]() { replaceWith(snaps); }, [=]() { replaceWith(before); }, true);
    out["applied"] = true;
    out["message"] = r.breaks.isEmpty()
        ? tr("Pista sincronizada (desfase %1 s).").arg(r.globalOffset, 0, 'f', 2)
        : tr("Pista sincronizada con %1 corte(s) por fallos de la referencia.").arg(r.breaks.size());
    return out;
}
