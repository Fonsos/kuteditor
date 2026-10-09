#include "TrackSync.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace TrackSync {
namespace {

using cd = std::complex<double>;

void fft(std::vector<cd> &a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * M_PI / double(len) * (inverse ? -1 : 1);
        const cd wl(std::cos(ang), -std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cd w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) for (auto &x : a) x /= double(n);
}

size_t pow2Above(size_t n) { size_t p = 1; while (p < n) p <<= 1; return p; }

/// Reduce la envolvente a 100/step Hz y la deja con media 0 y desviación 1.
std::vector<double> envFeature(const QVector<float> &env, int step)
{
    std::vector<double> out;
    const int n = env.size();
    for (int i = 0; i + step <= n; i += step) {
        double s = 0;
        for (int k = 0; k < step; ++k) s += std::max(-70.0f, std::min(-10.0f, env[i + k]));
        out.push_back(s / step);
    }
    if (out.empty()) return out;
    double m = 0; for (double x : out) m += x; m /= out.size();
    double v = 0; for (double x : out) v += (x - m) * (x - m);
    double sd = std::sqrt(v / out.size()); if (sd <= 0) sd = 1;
    for (double &x : out) x = (x - m) / sd;
    return out;
}

double refine(const QVector<float> &A, const QVector<float> &B, double offset)
{
    const int o0 = int(std::lround(offset * 100));
    const int na = A.size(), nb = B.size();
    double best = -1e300; int bo = o0;
    for (int o = o0 - 8; o <= o0 + 8; ++o) {
        int from = std::max(0, -o), to = std::min(nb, na - o);
        if (to - from < 200) continue;
        if (to - from > 60000) { const int mid = (from + to) / 2; from = mid - 30000; to = mid + 30000; }
        double sa = 0, sb = 0, sab = 0, saa = 0, sbb = 0; const double m = to - from;
        for (int t = from; t < to; ++t) {
            const double x = A[t + o], y = B[t];
            sa += x; sb += y; sab += x * y; saa += x * x; sbb += y * y;
        }
        const double den = std::sqrt(std::max(1e-9, (saa - sa * sa / m) * (sbb - sb * sb / m)));
        const double c = (sab - sa * sb / m) / den;
        if (c > best) { best = c; bo = o; }
    }
    return bo / 100.0;
}

/// Pearson entre la referencia en [t0,t1] y la pista desplazada `off` (100 Hz).
double ncc100(const QVector<float> &A, const QVector<float> &B, double t0, double t1, double off)
{
    const int shift = int(std::lround(off * 100));
    const int i0 = std::max({int(std::lround(t0 * 100)), shift, 0});
    const int i1 = std::min({int(std::lround(t1 * 100)), int(A.size()), int(B.size()) + shift});
    const int n = i1 - i0;
    if (n < 100) return 0.0;
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    for (int i = i0; i < i1; ++i) {
        const double x = A[i], y = B[i - shift];
        sa += x; sb += y; saa += x * x; sbb += y * y; sab += x * y;
    }
    const double va = saa - sa * sa / n, vb = sbb - sb * sb / n;
    if (va < 1e-6 || vb < 1e-6) return 0.0;
    return (sab - sa * sb / n) / std::sqrt(va * vb);
}

double refineRange(const QVector<float> &A, const QVector<float> &B, double off, double t0, double t1)
{
    const double mid = (t0 + t1) / 2, half = std::min((t1 - t0) / 2, 30.0);
    double best = -1e300, bo = off;
    for (int k = -10; k <= 10; ++k) {
        const double o = off + k * 0.01;
        const double c = ncc100(A, B, mid - half, mid + half, o);
        if (c > best) { best = c; bo = o; }
    }
    return std::round(bo * 1000) / 1000;
}

double median(std::vector<double> l) { std::sort(l.begin(), l.end()); return l[l.size() / 2]; }

} // namespace

QVector<float> envelopeDb(const float *d, qint64 frames, int ch, int sr)
{
    QVector<float> out;
    if (!d || frames <= 0 || ch <= 0 || sr <= 0) return out;
    const qint64 step = std::max<qint64>(1, qint64(sr / kEnvRate));
    out.reserve(int(frames / step) + 1);
    for (qint64 f = 0; f + step <= frames; f += step) {
        double acc = 0;
        for (qint64 k = f; k < f + step; ++k)
            for (int c = 0; c < ch; ++c) { const double v = d[k * ch + c]; acc += v * v; }
        const double rms = std::sqrt(acc / double(step * ch));
        out.append(float(20.0 * std::log10(rms + 1e-6)));
    }
    return out;
}

XcorrResult xcorr(const QVector<float> &envA, const QVector<float> &envB)
{
    const int step = 4;
    const auto a = envFeature(envA, step), b = envFeature(envB, step);
    if (a.size() < 50 || b.size() < 50) return {};
    const int na = int(a.size()), nb = int(b.size());
    const size_t n = pow2Above(size_t(na + nb));
    std::vector<cd> fa(n), fb(n);
    for (int i = 0; i < na; ++i) fa[i] = a[i];
    for (int i = 0; i < nb; ++i) fb[i] = b[i];
    fft(fa, false); fft(fb, false);
    for (size_t i = 0; i < n; ++i) fa[i] = fa[i] * std::conj(fb[i]);
    fft(fa, true);
    double best = -1e300, sum = 0, sum2 = 0; int bk = 0, cnt = 0;
    const int minOverlap = std::max(100, int(0.3 * std::min(na, nb)));
    for (int k = -(nb - 1); k <= na - 1; ++k) {
        const size_t idx = k >= 0 ? size_t(k) : n - size_t(-k);
        const int overlap = std::min(na, nb + k) - std::max(0, k);
        if (overlap < minOverlap) continue;
        const double c = fa[idx].real() / overlap;
        sum += c; sum2 += c * c; ++cnt;
        if (c > best) { best = c; bk = k; }
    }
    if (cnt == 0) return {};
    const double mean = sum / cnt;
    const double sd = std::sqrt(std::max(1e-12, sum2 / cnt - mean * mean));
    XcorrResult r;
    r.confidence = std::round((best - mean) / sd * 10) / 10;
    r.offset = refine(envA, envB, bk * step / 100.0);
    return r;
}

QVector<WindowOffset> windowOffsets(const QVector<float> &envA, const QVector<float> &envB, double o0,
                                    double maxDrift, double winSec, double hopSec)
{
    const double dt = 0.04;
    const auto fa = envFeature(envA, 4), fb = envFeature(envB, 4);
    const int n = int(std::lround(winSec / dt)), hop = int(std::lround(hopSec / dt));
    const int kc = int(std::lround(o0 / dt)), D = int(std::lround(maxDrift / dt));
    const int kmax = kc + D;
    const int A = int(fa.size()), B = int(fb.size());
    const int m = n + 2 * D;
    const size_t N = pow2Above(size_t(m + n));
    QVector<WindowOffset> out;
    for (int a0 = 0; a0 + n <= A; a0 += hop) {
        std::vector<double> x(fa.begin() + a0, fa.begin() + a0 + n);
        double mx = 0; for (double v : x) mx += v; mx /= n;
        double vx = 0; for (double v : x) vx += (v - mx) * (v - mx);
        const double sx = std::sqrt(vx / n);
        if (sx < 0.35) continue;                       // ventana casi en silencio: no informa
        for (double &v : x) v = (v - mx) / sx;
        const int y0 = a0 - kmax;
        std::vector<double> y(m);
        for (int i = 0; i < m; ++i) { const int j = y0 + i; y[i] = (j >= 0 && j < B) ? fb[j] : 0.0; }
        const int sLo = std::max(0, -y0), sHi = std::min(2 * D, B - y0 - n);
        if (sHi - sLo < D / 2) continue;
        std::vector<cd> X(N), Y(N);
        for (int i = 0; i < n; ++i) X[i] = x[i];
        for (int i = 0; i < m; ++i) Y[i] = y[i];
        fft(X, false); fft(Y, false);
        for (size_t i = 0; i < N; ++i) X[i] = std::conj(X[i]) * Y[i];
        fft(X, true);
        std::vector<double> p1(m + 1, 0), p2(m + 1, 0);
        for (int i = 0; i < m; ++i) { p1[i + 1] = p1[i] + y[i]; p2[i + 1] = p2[i] + y[i] * y[i]; }
        double best = -1e300, sum = 0, sum2 = 0; int bs = 0, cnt = 0;
        for (int s = sLo; s <= sHi; ++s) {
            const double my = (p1[s + n] - p1[s]) / n;
            const double vy = (p2[s + n] - p2[s]) / n - my * my;
            const double c = vy > 1e-6 ? X[s].real() / (n * std::sqrt(vy)) : 0.0;
            sum += c; sum2 += c * c; ++cnt;
            if (c > best) { best = c; bs = s; }
        }
        if (cnt == 0) continue;
        const double mean = sum / cnt, sd = std::sqrt(std::max(1e-12, sum2 / cnt - mean * mean));
        out.append({a0 * dt, (kmax - bs) * dt, best, (best - mean) / sd});
    }
    return out;
}

QVector<Group> groupOffsets(const QVector<WindowOffset> &w, double winSec, double tol)
{
    QVector<WindowOffset> good;
    for (const auto &x : w) if (x.ncc >= 0.5 && x.conf >= 3.5) good.append(x);
    if (good.size() < 3) return {};
    QVector<QVector<WindowOffset>> groups;
    QVector<WindowOffset> cur{good[0]};
    for (int i = 1; i < good.size(); ++i) {
        std::vector<double> recent;
        for (int k = std::max(0, int(cur.size()) - 5); k < cur.size(); ++k) recent.push_back(cur[k].off);
        if (std::abs(good[i].off - median(recent)) <= tol) { cur.append(good[i]); continue; }
        // ¿cambio sostenido (la siguiente ventana lo confirma) o valor aislado (se descarta)?
        if (i + 1 < good.size() && std::abs(good[i + 1].off - good[i].off) <= tol) { groups.append(cur); cur = {good[i]}; }
    }
    groups.append(cur);
    QVector<Group> out;
    for (const auto &g : groups) {
        if (g.size() < 2 && groups.size() > 1) continue;      // grupo diminuto: ruido
        std::vector<double> offs; for (const auto &x : g) offs.push_back(x.off);
        const double off = median(offs);
        if (!out.isEmpty() && std::abs(out.last().off - off) <= tol) {
            out.last().t1 = g.last().t + winSec; out.last().n += g.size();
            continue;
        }
        out.append({off, g.first().t, g.last().t + winSec, int(g.size())});
    }
    return out;
}

SyncResult segmentSync(const QVector<float> &envRef, const QVector<float> &envTrack, double o0, double trackDur)
{
    const double winSec = 24.0, W = 5.0;
    SyncResult r; r.globalOffset = o0;
    const auto wins = windowOffsets(envRef, envTrack, o0, 12.0, winSec, 6.0);
    r.windows = int(wins.size());
    auto groups = groupOffsets(wins, winSec);
    if (groups.isEmpty()) { r.segments.append({o0, o0 + trackDur, o0}); return r; }
    for (auto &g : groups) g.off = refineRange(envRef, envTrack, g.off, g.t0, g.t1);
    for (int k = 0; k + 1 < groups.size(); ++k) {
        const double old = groups[k].off, nw = groups[k + 1].off, delta = nw - old;
        const double lo = groups[k].t1 - winSec / 2, hi = groups[k + 1].t0 + winSec / 2;
        double best = -1e300, bb = (lo + hi) / 2;
        for (double b = lo; b <= hi; b += 0.05) {
            const double ns = b + std::max(0.0, delta);
            const double sc = ncc100(envRef, envTrack, b - W, b, old) + ncc100(envRef, envTrack, ns, ns + W, nw);
            if (sc > best + 1e-9) { best = sc; bb = b; }
        }
        r.breaks.append({std::round(bb * 100) / 100, std::round(delta * 1000) / 1000});
    }
    const int last = int(groups.size()) - 1;
    for (int k = 0; k <= last; ++k) {
        const double s = k == 0 ? groups[k].off : r.breaks[k - 1].at + std::max(0.0, r.breaks[k - 1].delta);
        const double e = k == last ? groups[k].off + trackDur : r.breaks[k].at;
        if (e - s > 0.05) r.segments.append({std::round(s * 1000) / 1000, std::round(e * 1000) / 1000, groups[k].off});
    }
    return r;
}

SyncResult analyze(const QVector<float> &envRef, const QVector<float> &envTrack, double trackDur)
{
    const XcorrResult g = xcorr(envRef, envTrack);
    SyncResult r = segmentSync(envRef, envTrack, g.offset, trackDur);
    r.globalOffset = g.offset;
    r.confidence = g.confidence;
    r.reliable = g.confidence >= 4.0;
    return r;
}

} // namespace TrackSync
