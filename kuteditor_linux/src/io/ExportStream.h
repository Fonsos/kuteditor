#pragma once
/**
 * ExportStream.h — núcleo de la exportación "por bloques" (modo en prueba).
 *
 * El export clásico (ExportManager::mixDown) mezcla TODO el episodio en un único
 * QVector<float>, y encima hace copias para remuestrear, medir LUFS (QVector<double>
 * de 2x el tamaño) y convertir a 16/24 bits. En episodios largos el pico supera los
 * 5 GB y el sistema mata el proceso.
 *
 * Aquí se hace lo mismo pero por bloques de ~1,5 s: mezcla → remuestreo → ganancia →
 * escritura, con memoria constante. Es C++ estándar puro (sin Qt) para poder probarlo
 * de forma aislada (tests/export_stream_test.cpp) comparándolo con el algoritmo clásico.
 *
 * Solo estéreo (2 canales), que es lo que usa AudioEngine::CHANNELS. Con otro número de
 * canales ExportManager recurre al camino clásico.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#ifdef HAVE_SAMPLERATE
#include <samplerate.h>
#endif

namespace kutxs {

/// Frames por bloque. Múltiplo de 1024 (tamaño de bloque de los FX) para que el
/// procesado de efectos con estado quede alineado igual que en el export clásico.
constexpr int kBlockFrames = 65536;
constexpr int kFxBlock     = 1024;

// ============================================================================
//  Mezcla por bloques
// ============================================================================

/// Lector de un tramo de la pista: escribe `n` frames ESTÉREO intercalados (mono se
/// duplica en L y R) con clips, fades, envolventes y denoiser ya aplicados. En la
/// aplicación es TrackModel::readMixSegment, la misma ruta de la reproducción y sin
/// el "aplanado" de la pista en RAM que hacía el export clásico.
using SegmentReader = std::function<void(int64_t start, int n, float *dst)>;

struct TrackIn {
    int64_t frames = 0;            ///< longitud de la pista en frames
    SegmentReader read;
    float gain = 1.0f;
    float pan = 0.0f;              ///< -1 .. +1
    bool active = false;           ///< válida, sin mute y respetando solos
    bool hasFx = false;            ///< tiene efectos activos (los aplica el hook)
};

/// Contribución de una pista (ganancia + pan, ley de potencia constante) a
/// [start, start+n). Escribe n frames estéreo en `dst` (los que caen fuera de la pista
/// quedan a 0). Devuelve cuántos frames del bloque caen dentro de la pista.
inline int trackContribution(const TrackIn &t, int64_t maxFrames,
                             int64_t start, int n, float *dst)
{
    std::fill(dst, dst + size_t(n) * 2, 0.0f);
    const int64_t copyFrames = std::min<int64_t>(t.frames, maxFrames);
    const int64_t valid64 = std::min<int64_t>(n, copyFrames - start);
    if (valid64 <= 0 || !t.read) return 0;
    const int valid = int(valid64);

    t.read(start, valid, dst);

    const float angle = (t.pan + 1.0f) * 0.25f * float(M_PI);
    const float panL = std::cos(angle) * t.gain;
    const float panR = std::sin(angle) * t.gain;
    for (int i = 0; i < valid; ++i) {
        dst[i * 2 + 0] *= panL;
        dst[i * 2 + 1] *= panR;
    }
    return valid;
}

/// Hook por pista con FX: procesa `contrib` (estéreo, n frames, los primeros `valid`
/// son audio de la pista) en sitio. `start` es múltiplo de kFxBlock.
using TrackHook = std::function<void(int trackIndex, float *contrib, int64_t start,
                                     int n, int valid)>;
/// Aviso de la señal ya procesada de cada pista (la necesita el AutoDuck master).
using ProcessedHook = std::function<void(int trackIndex, const float *contrib,
                                         int64_t start, int n, int valid)>;

/// Mezcla el bloque [start, start+n) de todas las pistas en `out` (estéreo, se
/// sobrescribe). No aplica la cadena master ni el clamp (lo hace quien llama).
inline void mixBlock(const std::vector<TrackIn> &tracks,
                     int64_t maxFrames, int64_t start, int n, float *out,
                     std::vector<float> &scratch, const TrackHook &fxHook,
                     const ProcessedHook &processed)
{
    std::fill(out, out + size_t(n) * 2, 0.0f);
    scratch.resize(size_t(n) * 2);
    for (size_t t = 0; t < tracks.size(); ++t) {
        const TrackIn &tr = tracks[t];
        if (!tr.active) continue;
        const bool useFx = tr.hasFx && bool(fxHook);
        const int valid = trackContribution(tr, maxFrames, start, n, scratch.data());
        if (valid <= 0) continue;
        if (useFx) fxHook(int(t), scratch.data(), start, n, valid);
        if (processed) processed(int(t), scratch.data(), start, n, valid);
        for (int i = 0; i < valid * 2; ++i) out[i] += scratch[i];
    }
}

// ============================================================================
//  Remuestreo en streaming
// ============================================================================

class StreamResampler {
public:
    /// forceLinear: no usar libsamplerate (para pruebas del camino lineal).
    StreamResampler(int srcRate, int dstRate, int channels, bool forceLinear = false)
        : m_src(srcRate), m_dst(dstRate), m_ch(channels),
          m_ratio(double(dstRate) / double(srcRate))
    {
#ifdef HAVE_SAMPLERATE
        if (forceLinear) {
            m_useLinear = true;
        } else if (m_src != m_dst) {
            int err = 0;
            m_state = src_new(SRC_SINC_MEDIUM_QUALITY, m_ch, &err);
            if (!m_state) m_useLinear = true;
        }
#else
        (void)forceLinear;
        m_useLinear = true;
#endif
    }
    ~StreamResampler() {
#ifdef HAVE_SAMPLERATE
        if (m_state) src_delete(m_state);
#endif
    }
    StreamResampler(const StreamResampler &) = delete;
    StreamResampler &operator=(const StreamResampler &) = delete;

    bool passthrough() const { return m_src == m_dst; }

    /// Procesa `n` frames de entrada y AÑADE la salida a `out`. `last` = fin de entrada.
    void process(const float *in, int64_t n, bool last, std::vector<float> &out)
    {
        if (passthrough()) {
            out.insert(out.end(), in, in + size_t(n) * m_ch);
            return;
        }
#ifdef HAVE_SAMPLERATE
        if (m_state && !m_useLinear) { processSinc(in, n, last, out); return; }
#endif
        processLinear(in, n, last, out);
    }

private:
    // --- Lineal: misma fórmula que ExportManager::resampleLinear, en streaming ---
    void processLinear(const float *in, int64_t n, bool last, std::vector<float> &out)
    {
        m_buf.insert(m_buf.end(), in, in + size_t(n) * m_ch);
        m_consumed += n;
        const int64_t dstTotal = last ? int64_t(double(m_consumed) * m_ratio) : INT64_MAX;
        while (m_nextOut < dstTotal) {
            const double srcPos = double(m_nextOut) / m_ratio;
            const int64_t i0 = int64_t(srcPos);
            if (!last && i0 + 1 >= m_consumed) break;
            const int64_t i1 = std::min<int64_t>(m_consumed - 1, i0 + 1);
            const float frac = float(srcPos - double(i0));
            const size_t b0 = size_t(i0 - m_base) * m_ch;
            const size_t b1 = size_t(i1 - m_base) * m_ch;
            for (int c = 0; c < m_ch; ++c) {
                const float s0 = m_buf[b0 + c];
                const float s1 = m_buf[b1 + c];
                out.push_back(s0 + (s1 - s0) * frac);
            }
            ++m_nextOut;
        }
        // Descartar la entrada que ya no hace falta.
        const int64_t keepFrom = std::min<int64_t>(int64_t(double(m_nextOut) / m_ratio), m_consumed);
        if (keepFrom > m_base) {
            const size_t drop = size_t(keepFrom - m_base) * m_ch;
            m_buf.erase(m_buf.begin(), m_buf.begin() + drop);
            m_base = keepFrom;
        }
    }

#ifdef HAVE_SAMPLERATE
    void processSinc(const float *in, int64_t n, bool last, std::vector<float> &out)
    {
        std::vector<float> tmp(size_t(8192) * m_ch);
        const float *p = in;
        int64_t left = n;
        for (;;) {
            SRC_DATA d{};
            d.data_in = p;
            d.input_frames = long(left);
            d.data_out = tmp.data();
            d.output_frames = long(tmp.size() / m_ch);
            d.src_ratio = m_ratio;
            d.end_of_input = last ? 1 : 0;
            if (src_process(m_state, &d) != 0) break;
            out.insert(out.end(), tmp.begin(), tmp.begin() + size_t(d.output_frames_gen) * m_ch);
            p += size_t(d.input_frames_used) * m_ch;
            left -= d.input_frames_used;
            // Seguir mientras queden entradas o la salida llenara el búfer.
            if (left <= 0 && d.output_frames_gen < long(tmp.size() / m_ch)) break;
            if (d.input_frames_used == 0 && d.output_frames_gen == 0) break;
        }
    }
    SRC_STATE *m_state = nullptr;
#endif

    int m_src, m_dst, m_ch;
    double m_ratio;
    bool m_useLinear = false;
    std::vector<float> m_buf;
    int64_t m_base = 0;       ///< índice de entrada del primer frame de m_buf
    int64_t m_consumed = 0;   ///< frames de entrada recibidos
    int64_t m_nextOut = 0;    ///< siguiente frame de salida a generar
};

// ============================================================================
//  LUFS integrado (BS.1770-4) en streaming, + pico
// ============================================================================
//  Misma matemática que ExportManager::measureLUFS, sin guardar el audio: solo
//  acumula la energía de cada hop de 100 ms (4 hops = un bloque de 400 ms).

class LufsMeter {
public:
    LufsMeter(int sampleRate, int channels)
        : m_sr(sampleRate), m_ch(channels)
    {
        m_k.assign(channels, preFilter(sampleRate));
        m_r.assign(channels, rlbHighpass(sampleRate));
        m_block = int64_t(0.400 * sampleRate);
        m_hop   = int64_t(0.100 * sampleRate);
        m_exact = (m_hop > 0 && m_block == 4 * m_hop);
    }

    void feed(const float *s, int64_t frames)
    {
        for (int64_t i = 0; i < frames; ++i) {
            for (int c = 0; c < m_ch; ++c) {
                const float v = s[i * m_ch + c];
                const float a = std::fabs(v);
                if (a > m_peak) m_peak = a;
                double x = v;
                x = m_k[c].process(x);
                x = m_r[c].process(x);
                m_acc += x * x;
            }
            ++m_total;
            if (++m_inHop == m_hop) {
                m_seg.push_back(m_acc);
                m_acc = 0.0; m_inHop = 0;
            }
        }
    }

    float peak() const { return m_peak; }
    int64_t frames() const { return m_total; }

    /// LUFS integrado, o -100 si no es medible (igual que el clásico).
    double integrated() const
    {
        if (!m_exact) return -100.0;
        if (m_total < m_sr) return -100.0;
        if (m_total < m_block) return -100.0;

        std::vector<double> blockMS;
        for (size_t b = 0; b + 4 <= m_seg.size(); ++b) {
            const double sum = m_seg[b] + m_seg[b + 1] + m_seg[b + 2] + m_seg[b + 3];
            blockMS.push_back(sum / double(m_block));
        }
        if (blockMS.empty()) return -100.0;

        auto msToLufs = [](double ms) { return ms > 0.0 ? (-0.691 + 10.0 * std::log10(ms)) : -100.0; };
        std::vector<double> absGated;
        for (double ms : blockMS) if (msToLufs(ms) >= -70.0) absGated.push_back(ms);
        if (absGated.empty()) return -100.0;

        double meanMS = 0.0;
        for (double v : absGated) meanMS += v;
        meanMS /= double(absGated.size());
        const double relThresh = msToLufs(meanMS) - 10.0;

        double sumFinal = 0.0; int countFinal = 0;
        for (double ms : absGated) {
            if (msToLufs(ms) >= relThresh) { sumFinal += ms; ++countFinal; }
        }
        if (countFinal == 0) return -100.0;
        return msToLufs(sumFinal / countFinal);
    }

    /// Ganancia lineal para llegar a targetLUFS sin pasar de 0,99 de pico (como el
    /// clásico). Devuelve false si no se puede medir (no se normaliza).
    bool gainFor(double targetLUFS, float &gainOut, double *currentOut = nullptr) const
    {
        const double current = integrated();
        if (currentOut) *currentOut = current;
        if (current <= -99.0) return false;
        const double deltaDB = targetLUFS - current;
        const float gain = float(std::pow(10.0, deltaDB / 20.0));
        float finalGain = gain;
        if (m_peak * gain > 0.99f) finalGain = 0.99f / std::max(0.0001f, m_peak);
        gainOut = finalGain;
        return true;
    }

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline double process(double x) {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    static Biquad preFilter(int sr) {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(M_PI * f0 / sr);
        const double Vh = std::pow(10.0, G / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad f;
        f.b0 = (Vh + Vb * K / Q + K * K) / a0;
        f.b1 = 2.0 * (K * K - Vh) / a0;
        f.b2 = (Vh - Vb * K / Q + K * K) / a0;
        f.a1 = 2.0 * (K * K - 1.0) / a0;
        f.a2 = (1.0 - K / Q + K * K) / a0;
        return f;
    }
    static Biquad rlbHighpass(int sr) {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(M_PI * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad f;
        f.b0 = 1.0 / a0; f.b1 = -2.0 / a0; f.b2 = 1.0 / a0;
        f.a1 = 2.0 * (K * K - 1.0) / a0;
        f.a2 = (1.0 - K / Q + K * K) / a0;
        return f;
    }

    int m_sr, m_ch;
    int64_t m_block = 0, m_hop = 0;
    bool m_exact = false;
    std::vector<Biquad> m_k, m_r;
    std::vector<double> m_seg;
    double m_acc = 0.0;
    int64_t m_inHop = 0, m_total = 0;
    float m_peak = 0.0f;
};

// ============================================================================
//  WAV: cabecera y conversión de bloques
// ============================================================================

/// Cabecera WAV de 44 bytes. dataBytes se satura a 4 GiB (límite del WAV clásico).
inline std::vector<uint8_t> wavHeader(int sampleRate, int channels, int bitDepth,
                                      uint64_t dataBytes)
{
    const uint32_t data = uint32_t(std::min<uint64_t>(dataBytes, 0xFFFFFFFFull));
    const uint32_t riff = uint32_t(std::min<uint64_t>(36ull + dataBytes, 0xFFFFFFFFull));
    const uint16_t fmt = (bitDepth == 32) ? 3 : 1;
    const uint16_t bits = uint16_t(bitDepth);
    const uint32_t byteRate = uint32_t(sampleRate * channels * (bits / 8));
    const uint16_t align = uint16_t(channels * (bits / 8));
    std::vector<uint8_t> h;
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) h.push_back(uint8_t(v >> (8 * i))); };
    auto put16 = [&](uint16_t v) { for (int i = 0; i < 2; ++i) h.push_back(uint8_t(v >> (8 * i))); };
    auto tag = [&](const char *t) { for (int i = 0; i < 4; ++i) h.push_back(uint8_t(t[i])); };
    tag("RIFF"); put32(riff); tag("WAVE");
    tag("fmt "); put32(16); put16(fmt); put16(uint16_t(channels));
    put32(uint32_t(sampleRate)); put32(byteRate); put16(align); put16(bits);
    tag("data"); put32(data);
    return h;
}

/// Convierte `count` samples float a PCM (16/24 bits) o float32 crudo, añadiéndolos a `out`.
/// Misma conversión (recorte y truncado) que ExportManager::writeWavFile.
inline void floatToPcm(const float *s, size_t count, int bitDepth, std::vector<uint8_t> &out)
{
    if (bitDepth == 32) {
        const uint8_t *p = reinterpret_cast<const uint8_t *>(s);
        out.insert(out.end(), p, p + count * sizeof(float));
    } else if (bitDepth == 24) {
        for (size_t i = 0; i < count; ++i) {
            const float c = std::max(-1.0f, std::min(1.0f, s[i]));
            const int32_t v = int32_t(c * 8388607.0f);
            out.push_back(uint8_t(v & 0xFF));
            out.push_back(uint8_t((v >> 8) & 0xFF));
            out.push_back(uint8_t((v >> 16) & 0xFF));
        }
    } else {
        for (size_t i = 0; i < count; ++i) {
            const float c = std::max(-1.0f, std::min(1.0f, s[i]));
            const int16_t v = int16_t(c * 32767.0f);
            out.push_back(uint8_t(v & 0xFF));
            out.push_back(uint8_t((v >> 8) & 0xFF));
        }
    }
}

// ============================================================================
//  Tubería completa: mezcla → remuestreo → ganancia → destino
// ============================================================================

/// Mezcla ya terminada (master + clamp) de [start, start+n); n*2 floats en `out`.
using MixFn = std::function<void(int64_t start, int n, float *out)>;
/// Recibe audio estéreo ya remuestreado. Devuelve false para abortar.
using SinkFn = std::function<bool(const float *samples, int64_t frames)>;
/// Progreso 0..1 tras cada bloque. Devuelve false para abortar.
using ProgressFn = std::function<bool(double fraction)>;

inline bool runPipeline(int64_t maxFrames, int srcRate, int dstRate, float gain,
                        const MixFn &mix, const SinkFn &sink, const ProgressFn &progress,
                        int blockFrames = kBlockFrames)
{
    StreamResampler rs(srcRate, dstRate, 2);
    std::vector<float> blk(size_t(blockFrames) * 2);
    std::vector<float> res;
    for (int64_t start = 0; start < maxFrames; start += blockFrames) {
        const int n = int(std::min<int64_t>(blockFrames, maxFrames - start));
        mix(start, n, blk.data());
        const bool last = (start + n >= maxFrames);
        res.clear();
        rs.process(blk.data(), n, last, res);
        if (gain != 1.0f) for (float &v : res) v *= gain;
        if (!res.empty() && !sink(res.data(), int64_t(res.size() / 2))) return false;
        if (progress && !progress(double(start + n) / double(maxFrames))) return false;
    }
    return true;
}

} // namespace kutxs
