#pragma once
// ============================================================================
// KutEditor · Sincronización de pistas por correlación de envolventes
// ----------------------------------------------------------------------------
// Alinea una pista con una referencia comparando cómo suben y bajan sus volúmenes
// (envolvente RMS a 100 Hz, en dB). Sirve para:
//   · el desfase global (p. ej. la grabación propia del locutor frente a la que
//     entra por la grabadora), y
//   · fallos de grabación de la referencia: si pierde audio o mete silencio a mitad,
//     el desfase cambia de golpe; se detecta por ventanas y se localiza el salto.
//
// Convención: pista[t] ≈ referencia[t + offset]  →  t_ref = t_pista + offset.
// Solo Qt Core / STL: se prueba sin audio real.
// ============================================================================
#include <QVector>

namespace TrackSync {

constexpr double kEnvRate = 100.0;   // muestras de envolvente por segundo

/// Envolvente RMS en dB (una muestra cada 10 ms) de audio entrelazado.
QVector<float> envelopeDb(const float *interleaved, qint64 frames, int channels, int sampleRate);

struct XcorrResult { double offset = 0.0; double confidence = 0.0; };
/// Dónde empieza `b` en la línea de tiempo de `a` (desfase global).
XcorrResult xcorr(const QVector<float> &envA, const QVector<float> &envB);

struct WindowOffset { double t = 0, off = 0, ncc = 0, conf = 0; };
QVector<WindowOffset> windowOffsets(const QVector<float> &envA, const QVector<float> &envB, double o0,
                                    double maxDrift = 20.0, double winSec = 30.0, double hopSec = 10.0);

struct Group { double off = 0, t0 = 0, t1 = 0; int n = 0; };
QVector<Group> groupOffsets(const QVector<WindowOffset> &w, double winSec, double tol = 0.12);

struct Segment { double s = 0, e = 0, off = 0; };   // tramo en tiempo de la referencia
struct Break { double at = 0, delta = 0; };         // delta<0: sobra audio en la pista · delta>0: la referencia metió silencio
struct SyncResult {
    QVector<Segment> segments;
    QVector<Break> breaks;
    int windows = 0;
    double globalOffset = 0.0;
    double confidence = 0.0;
    bool reliable = false;      // el pico global destaca lo bastante como para fiarse
};

/// Análisis completo: desfase global + saltos. `trackDur` en segundos.
SyncResult analyze(const QVector<float> &envRef, const QVector<float> &envTrack, double trackDur);

/// Variante con el desfase global ya conocido.
SyncResult segmentSync(const QVector<float> &envRef, const QVector<float> &envTrack, double o0, double trackDur);

} // namespace TrackSync
