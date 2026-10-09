#include <QtTest>
#include <cmath>
#include "sync/TrackSync.h"

namespace {
quint32 g = 12345;
double rnd() { g = g * 1664525u + 1013904223u; return (g >> 8) / double(1 << 24); }

// Envolvente tipo conversación: tramos de habla y silencio con niveles variables (100 Hz, dB).
QVector<float> speech(double seconds)
{
    QVector<float> v; const int n = int(seconds * 100);
    bool talking = true; int left = 0; double level = -25;
    for (int i = 0; i < n; ++i) {
        if (left <= 0) {
            talking = !talking || rnd() < 0.3;
            left = int((talking ? 0.3 + 2.5 * rnd() : 0.2 + 1.5 * rnd()) * 100);
            level = talking ? -22 - 14 * rnd() : -64;
        }
        --left;
        v.append(float(level + (rnd() - 0.5) * (talking ? 6 : 3)));
    }
    return v;
}
// Copia con otro ruido de medida (dos micros no dan exactamente la misma envolvente)
QVector<float> noisy(const QVector<float> &s) { QVector<float> o = s; for (auto &x : o) x += float((rnd() - 0.5) * 3); return o; }
// out[t] = s[t + shift] (relleno con ruido de fondo)
QVector<float> shifted(const QVector<float> &s, double shiftSec, double outSec)
{
    QVector<float> o; const int n = int(outSec * 100), sh = int(std::lround(shiftSec * 100));
    for (int i = 0; i < n; ++i) { const int j = i + sh; o.append(j >= 0 && j < s.size() ? s[j] : float(-66 + (rnd() - 0.5) * 3)); }
    return o;
}
}

class TestTrackSync : public QObject {
    Q_OBJECT
private slots:
    void globalOffsetPositive() {
        const auto S = speech(240);
        const auto ref = noisy(shifted(S, -5.27, 250));      // ref[u] = S[u-5.27]
        const auto trk = noisy(shifted(S, 0, 240));
        const auto r = TrackSync::xcorr(ref, trk);
        QVERIFY2(qAbs(r.offset - 5.27) < 0.03, qPrintable(QString::number(r.offset)));
        QVERIFY(r.confidence > 4);
    }
    void globalOffsetNegative() {
        const auto S = speech(240);
        const auto ref = noisy(shifted(S, 3.4, 230));        // ref[u] = S[u+3.4]
        const auto trk = noisy(shifted(S, 0, 240));
        const auto r = TrackSync::xcorr(ref, trk);
        QVERIFY2(qAbs(r.offset + 3.4) < 0.03, qPrintable(QString::number(r.offset)));
    }
    void noBreaksGivesOneSegment() {
        const auto S = speech(300);
        const auto ref = noisy(shifted(S, -4.0, 310));
        const auto trk = noisy(shifted(S, 0, 300));
        const auto r = TrackSync::analyze(ref, trk, 300);
        QVERIFY(r.reliable);
        QCOMPARE(r.breaks.size(), 0);
        QCOMPARE(r.segments.size(), 1);
        QVERIFY(qAbs(r.segments[0].off - 4.0) < 0.03);
    }
    void referenceLostAudio() {
        // La referencia pierde 4 s de golpe en u=125 (la pista conserva ese audio): hay que recortar la pista.
        const auto S = speech(300);
        const auto base = shifted(S, -5.0, 310);              // ref[u]=S[u-5]
        QVector<float> ref;
        for (int i = 0; i < base.size(); ++i) { if (i >= 12500 && i < 12900) continue; ref.append(base[i]); }
        ref = noisy(ref);
        const auto trk = noisy(shifted(S, 0, 300));
        const auto r = TrackSync::analyze(ref, trk, 300);
        QCOMPARE(r.breaks.size(), 1);
        QVERIFY2(qAbs(r.breaks[0].at - 125.0) < 0.6, qPrintable(QString::number(r.breaks[0].at)));
        QVERIFY2(qAbs(r.breaks[0].delta + 4.0) < 0.05, qPrintable(QString::number(r.breaks[0].delta)));
        QCOMPARE(r.segments.size(), 2);
        QVERIFY(qAbs(r.segments[0].off - 5.0) < 0.03);
        QVERIFY(qAbs(r.segments[1].off - 1.0) < 0.03);
    }
    void referenceInsertedSilence() {
        // La referencia mete 3 s de silencio en u=125: hay que insertar silencio en la pista.
        const auto S = speech(300);
        const auto base = shifted(S, -5.0, 310);
        QVector<float> ref;
        for (int i = 0; i < base.size(); ++i) {
            if (i == 12500) for (int k = 0; k < 300; ++k) ref.append(float(-66 + (rnd() - 0.5) * 3));
            ref.append(base[i]);
        }
        ref = noisy(ref);
        const auto trk = noisy(shifted(S, 0, 300));
        const auto r = TrackSync::analyze(ref, trk, 300);
        QCOMPARE(r.breaks.size(), 1);
        QVERIFY2(qAbs(r.breaks[0].delta - 3.0) < 0.05, qPrintable(QString::number(r.breaks[0].delta)));
        QCOMPARE(r.segments.size(), 2);
        QVERIFY(qAbs(r.segments[1].s - (r.breaks[0].at + 3.0)) < 0.01);   // la pista reanuda tras el silencio
    }
    void unrelatedTracksAreNotReliable() {
        const auto a = speech(200); const auto b = speech(200);
        const auto r = TrackSync::analyze(a, b, 200);
        QVERIFY2(!r.reliable, qPrintable(QString::number(r.confidence)));
    }
    void envelopeFromAudio() {
        QVector<float> pcm(48000 * 2 * 2);                    // 2 s estéreo: 1 s de tono, 1 s de silencio
        for (int i = 0; i < 48000; ++i) { const float v = 0.5f * std::sin(i * 0.05f); pcm[2 * i] = pcm[2 * i + 1] = v; }
        const auto e = TrackSync::envelopeDb(pcm.constData(), 96000, 2, 48000);
        QCOMPARE(e.size(), 200);
        QVERIFY(e[50] > -12 && e[50] < -8);
        QVERIFY(e[150] < -100);
    }
};
QTEST_APPLESS_MAIN(TestTrackSync)
#include "test_tracksync.moc"
