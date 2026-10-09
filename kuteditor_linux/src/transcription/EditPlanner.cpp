#include "EditPlanner.h"

#include <algorithm>
#include <cmath>

// ── Envolvente ──────────────────────────────────────────────────────────────

float EnergyEnvelope::at(double sec) const
{
    const int i = int(std::floor(sec / stepSec));
    return (i >= 0 && i < db.size()) ? db[i] : -120.0f;
}

double EnergyEnvelope::quietestNear(double t, double back, double fwd) const
{
    if (db.isEmpty()) return t;
    const int c = int(std::round(t / stepSec));
    const int i0 = std::max(0, int(std::round((t - back) / stepSec)));
    const int i1 = std::min(int(db.size()) - 1, int(std::round((t + fwd) / stepSec)));
    if (i1 < i0) return t;
    float best = 1e9f; int bi = std::clamp(c, i0, i1);
    for (int i = i0; i <= i1; ++i) {
        const float v = db[i] + 0.002f * float(std::abs(i - c));   // desempata hacia lo más cercano
        if (v < best) { best = v; bi = i; }
    }
    return bi * stepSec + stepSec / 2;
}

namespace EditPlanner {

QVector<TimeRange> mergeRanges(QVector<TimeRange> r, double gap)
{
    std::sort(r.begin(), r.end(), [](const TimeRange &a, const TimeRange &b) { return a.start < b.start; });
    QVector<TimeRange> out;
    for (const TimeRange &x : std::as_const(r)) {
        if (x.end <= x.start) continue;
        if (!out.isEmpty() && x.start <= out.last().end + gap) out.last().end = std::max(out.last().end, x.end);
        else out.append(x);
    }
    return out;
}

QVector<TimeRange> subtractRanges(const QVector<TimeRange> &a, const QVector<TimeRange> &b)
{
    QVector<TimeRange> out;
    for (const TimeRange &r : a) {
        double cur = r.start;
        for (const TimeRange &s : b) {
            if (s.end <= cur || s.start >= r.end) continue;
            if (s.start > cur) out.append({cur, s.start});
            cur = std::max(cur, s.end);
        }
        if (cur < r.end) out.append({cur, r.end});
    }
    return out;
}

namespace {

/// ¿Algún intervalo de `sorted` (ordenado por inicio) toca [a, b)?
bool overlaps(const QVector<TimeRange> &sorted, double a, double b)
{
    int lo = 0, hi = sorted.size();
    while (lo < hi) { const int m = (lo + hi) / 2; if (sorted[m].end > a) hi = m; else lo = m + 1; }
    for (int i = std::max(0, lo - 3); i < sorted.size() && sorted[i].start < b; ++i)
        if (sorted[i].end > a && sorted[i].start < b) return true;
    return false;
}

} // namespace

EditPlan plan(const QVector<EditWord> &allWords,
              const QMap<int, EnergyEnvelope> &envelopes,
              const EditPlanOptions &opt)
{
    EditPlan out;

    // Palabras por pista (ordenadas) y tramos de habla conservada por pista
    QMap<int, QVector<EditWord>> byTrack;
    for (const EditWord &w : allWords) byTrack[w.track].append(w);
    QMap<int, QVector<TimeRange>> kept;
    for (auto it = byTrack.begin(); it != byTrack.end(); ++it) {
        std::sort(it->begin(), it->end(), [](const EditWord &a, const EditWord &b) { return a.start < b.start; });
        for (const EditWord &w : std::as_const(*it)) if (!w.deleted) kept[it.key()].append({w.start, w.end});
    }

    // 1) Regiones a eliminar: rachas de palabras eliminadas consecutivas en la misma pista
    QVector<TimeRange> cuts;
    QMap<int, QVector<TimeRange>> mutes;
    for (auto it = byTrack.constBegin(); it != byTrack.constEnd(); ++it) {
        const int track = it.key();
        const QVector<EditWord> &list = it.value();
        const EnergyEnvelope env = envelopes.value(track);
        const int n = list.size();
        for (int i = 0; i < n; ++i) {
            if (!list[i].deleted) continue;
            int j = i;
            while (j + 1 < n && list[j + 1].deleted) ++j;
            out.deletedWords += j - i + 1;

            const double prevEnd = i > 0 ? list[i - 1].end : -1e18;      // vecinos conservados: límites duros
            const double nextStart = j + 1 < n ? list[j + 1].start : 1e18;
            double rs = list[i].start - opt.padSec;
            double re = list[j].end + opt.padSec;
            if (!env.isEmpty()) {                                         // ajusta a un punto silencioso
                rs = env.quietestNear(rs, 0.06, 0.03);
                re = env.quietestNear(re, 0.03, 0.06);
            }
            rs = std::max(rs, prevEnd + 0.005);
            re = std::min(re, nextStart - 0.005);
            if (re - rs >= 0.02) {
                // ¿Habla otra pista a la vez? → solo silenciar esta
                bool over = false;
                for (auto k = kept.constBegin(); k != kept.constEnd(); ++k)
                    if (k.key() != track && overlaps(k.value(), rs - 0.03, re + 0.03)) { over = true; break; }
                if (over) mutes[track].append({rs, re}); else cuts.append({rs, re});
            }
            i = j;
        }
    }

    // 2) Pausas largas y bordes: solo donde TODAS las pistas están en silencio real
    QVector<TimeRange> activity;
    for (auto k = kept.constBegin(); k != kept.constEnd(); ++k) activity += k.value();
    activity = mergeRanges(activity);

    if (!activity.isEmpty() && (opt.maxPauseSec > 0 || opt.trimEdges)) {
        QVector<TimeRange> cands;
        if (opt.maxPauseSec > 0)
            for (int i = 0; i + 1 < activity.size(); ++i) {
                const double g0 = activity[i].end, g1 = activity[i + 1].start;
                if (g1 - g0 > opt.maxPauseSec + 0.1) cands.append({g0 + opt.maxPauseSec / 2, g1 - opt.maxPauseSec / 2});
            }
        if (opt.trimEdges) {
            if (activity.first().start > 0.35) cands.append({0.0, activity.first().start - 0.25});
            if (opt.totalDuration > 0 && opt.totalDuration - activity.last().end > 0.9)
                cands.append({activity.last().end + 0.6, opt.totalDuration});
        }

        // Umbral de silencio por pista: percentil 10 de su energía + 8 dB, acotado
        QMap<int, double> thr;
        for (auto e = envelopes.constBegin(); e != envelopes.constEnd(); ++e) {
            if (e->isEmpty()) continue;
            QVector<float> s = e->db;
            std::nth_element(s.begin(), s.begin() + int(s.size() * 0.10), s.end());
            thr[e.key()] = std::clamp(double(s[int(s.size() * 0.10)]) + 8.0, -60.0, -32.0);
        }
        auto quietRuns = [&](double a, double b) {
            QVector<TimeRange> runs;
            if (thr.isEmpty()) return QVector<TimeRange>{{a, b}};            // sin audio: se fía de las palabras
            double start = -1;
            for (double t = a; t <= b; t += 0.01) {
                bool q = true;
                for (auto e = envelopes.constBegin(); e != envelopes.constEnd() && q; ++e)
                    if (thr.contains(e.key()) && e->at(t) > thr[e.key()]) q = false;
                if (q && start < 0) start = t;
                if (!q && start >= 0) { if (t - start >= 0.15) runs.append({start + 0.03, t - 0.03}); start = -1; }
            }
            if (start >= 0 && b - start >= 0.15) runs.append({start + 0.03, b});
            return runs;
        };
        for (const TimeRange &c : std::as_const(cands)) cuts += quietRuns(c.start, c.end);
    }

    out.cuts = mergeRanges(cuts, 0.005);
    // Silenciar algo que ya se recorta no tiene sentido
    for (auto it = mutes.begin(); it != mutes.end(); ++it) {
        const QVector<TimeRange> rest = subtractRanges(mergeRanges(it.value()), out.cuts);
        if (!rest.isEmpty()) out.mutes[it.key()] = rest;
    }
    return out;
}

} // namespace EditPlanner
