#include "TrackSyncer.h"
#include "TrackSync.h"
#include "ui/TrackModel.h"
#include "ui/UndoManager.h"

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

QVariantMap TrackSyncer::analyze(int refTrack, int track) { return run(refTrack, track, false, false); }
QVariantMap TrackSyncer::apply(int refTrack, int track, bool force) { return run(refTrack, track, true, force); }

QVariantMap TrackSyncer::run(int ref, int track, bool doApply, bool force)
{
    QVariantMap out;
    out["ok"] = false; out["applied"] = false;
    auto fail = [&](const QString &m) { out["message"] = m; return out; };
    if (!m_tracks || !m_undo) return fail(tr("Editor no inicializado."));
    if (ref == track || ref < 0 || track < 0 || ref >= m_tracks->count() || track >= m_tracks->count())
        return fail(tr("Elige dos pistas distintas."));

    double refDur = 0, trkDur = 0;
    const QVector<float> envRef = trackEnvelope(m_tracks, ref, &refDur);
    const QVector<float> envTrk = trackEnvelope(m_tracks, track, &trkDur);
    if (envRef.size() < 3000 || envTrk.size() < 3000)
        return fail(tr("Hace falta al menos 30 s de audio en las dos pistas."));

    const TrackSync::SyncResult r = TrackSync::analyze(envRef, envTrk, trkDur);
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
