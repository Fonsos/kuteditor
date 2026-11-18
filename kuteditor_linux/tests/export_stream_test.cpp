// Prueba aislada (sin Qt) del núcleo de export por bloques: compara cada pieza con
// una copia literal del algoritmo clásico de ExportManager.cpp.
//   g++ -std=c++17 -O2 -I../src/io export_stream_test.cpp -o export_stream_test
#include "ExportStream.h"
#include <cstdio>
#include <random>
#include <sys/resource.h>
using namespace kutxs;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FALLO: " __VA_ARGS__); std::printf("\n"); } } while (0)

// ---------------- copias literales del algoritmo clásico ----------------
struct RawTrack { const float* data; int frames; int ch; int sr; float gain; float pan; bool active; };
static TrackIn toIn(const RawTrack& r)
{
    TrackIn t; t.frames = r.frames; t.gain = r.gain; t.pan = r.pan; t.active = r.active; t.hasFx = false;
    const float* d = r.data; const int ch = r.ch;
    t.read = [d, ch](int64_t start, int n, float* dst) {          // equivale a readMixSegment (salida estéreo)
        for (int i = 0; i < n; ++i) { dst[i*2] = d[(start+i)*ch]; dst[i*2+1] = d[(start+i)*ch + (ch == 1 ? 0 : 1)]; }
    };
    return t;
}
static std::vector<float> legacyMix(const std::vector<RawTrack>& tr, int channels, int sampleRate, int64_t maxFrames)
{
    std::vector<float> out(size_t(maxFrames) * channels, 0.0f);
    for (auto& t : tr) {
        if (!t.active) continue;
        const float* src = t.data; int f = t.frames, ch = t.ch, sr = t.sr;
        if (!src || f <= 0 || ch <= 0) continue;
        const int64_t copyFrames = std::min<int64_t>(f, maxFrames);
        const float angle = (t.pan + 1.0f) * 0.25f * float(M_PI);
        const float panL = std::cos(angle) * t.gain;
        const float panR = std::sin(angle) * t.gain;
        if (ch == channels && channels == 2 && sr == sampleRate) {
            for (int64_t i = 0; i < copyFrames; ++i) { out[i*2] += src[i*ch] * panL; out[i*2+1] += src[i*ch+1] * panR; }
        } else if (ch >= 1 && channels == 2) {
            for (int64_t i = 0; i < copyFrames; ++i) { const float v = src[i*ch]; out[i*2] += v*panL; out[i*2+1] += v*panR; }
        }
    }
    for (auto& v : out) v = std::max(-1.0f, std::min(1.0f, v));
    return out;
}
static void legacyResampleLinear(const std::vector<float>& in, int srcRate, std::vector<float>& out, int dstRate, int channels)
{
    const int64_t srcFrames = in.size() / channels;
    const double ratio = double(dstRate) / double(srcRate);
    const int64_t dstFrames = int64_t(srcFrames * ratio);
    out.resize(dstFrames * channels);
    for (int64_t i = 0; i < dstFrames; ++i) {
        const double srcPos = double(i) / ratio;
        const int64_t i0 = int64_t(srcPos);
        const int64_t i1 = std::min(srcFrames - 1, i0 + 1);
        const float frac = float(srcPos - i0);
        for (int c = 0; c < channels; ++c) {
            const float s0 = in[i0*channels+c], s1 = in[i1*channels+c];
            out[i*channels+c] = s0 + (s1 - s0) * frac;
        }
    }
}
struct LBiquad { double b0,b1,b2,a1,a2,z1=0,z2=0; double process(double x){ double y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return y; } };
static LBiquad lPre(int sr){ const double f0=1681.974450955533,G=3.999843853973347,Q=0.7071752369554196; const double K=std::tan(M_PI*f0/sr),Vh=std::pow(10.0,G/20.0),Vb=std::pow(Vh,0.4996667741545416),a0=1.0+K/Q+K*K; LBiquad f; f.b0=(Vh+Vb*K/Q+K*K)/a0; f.b1=2.0*(K*K-Vh)/a0; f.b2=(Vh-Vb*K/Q+K*K)/a0; f.a1=2.0*(K*K-1.0)/a0; f.a2=(1.0-K/Q+K*K)/a0; return f; }
static LBiquad lRlb(int sr){ const double f0=38.13547087602444,Q=0.5003270373238773; const double K=std::tan(M_PI*f0/sr),a0=1.0+K/Q+K*K; LBiquad f; f.b0=1.0/a0; f.b1=-2.0/a0; f.b2=1.0/a0; f.a1=2.0*(K*K-1.0)/a0; f.a2=(1.0-K/Q+K*K)/a0; return f; }
static double legacyLUFS(const std::vector<float>& samples, int sampleRate, int channels)
{
    if (samples.empty()) return -100.0;
    const int64_t frames = samples.size() / channels;
    if (frames < sampleRate) return -100.0;
    std::vector<LBiquad> k(channels, lPre(sampleRate)), r(channels, lRlb(sampleRate));
    const int64_t blockFrames = int64_t(0.400 * sampleRate), hopFrames = int64_t(0.100 * sampleRate);
    if (frames < blockFrames) return -100.0;
    std::vector<double> sq(frames * channels, 0.0);
    for (int64_t i = 0; i < frames; ++i) for (int c = 0; c < channels; ++c) { double x = samples[i*channels+c]; x = k[c].process(x); x = r[c].process(x); sq[i*channels+c] = x*x; }
    std::vector<double> blockMS;
    for (int64_t start = 0; start + blockFrames <= frames; start += hopFrames) { double sum = 0; for (int64_t i = 0; i < blockFrames; ++i) for (int c = 0; c < channels; ++c) sum += sq[(start+i)*channels+c]; blockMS.push_back(sum / double(blockFrames)); }
    if (blockMS.empty()) return -100.0;
    auto m2l = [](double ms){ return ms > 0.0 ? (-0.691 + 10.0*std::log10(ms)) : -100.0; };
    std::vector<double> ag; for (double ms : blockMS) if (m2l(ms) >= -70.0) ag.push_back(ms);
    if (ag.empty()) return -100.0;
    double mean = 0; for (double v : ag) mean += v; mean /= ag.size();
    const double rel = m2l(mean) - 10.0; double sf = 0; int cf = 0;
    for (double ms : ag) if (m2l(ms) >= rel) { sf += ms; cf++; }
    if (!cf) return -100.0;
    return m2l(sf / cf);
}

// ---------------- utilidades ----------------
static std::vector<float> noise(int64_t frames, int ch, unsigned seed, float amp, bool bursts)
{
    std::mt19937 g(seed); std::uniform_real_distribution<float> d(-1.f, 1.f);
    std::vector<float> v(size_t(frames) * ch);
    for (int64_t i = 0; i < frames; ++i) {
        float a = amp;
        if (bursts) { const int64_t sec = i / 24000; a = (sec % 7 < 3) ? amp : (sec % 7 == 3 ? 0.0f : amp * 0.05f); }
        for (int c = 0; c < ch; ++c) v[i*ch+c] = d(g) * a;
    }
    return v;
}
static double maxDiff(const std::vector<float>& a, const std::vector<float>& b) { double m = 0; for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) m = std::max(m, double(std::fabs(a[i]-b[i]))); return m; }
static long rssKB() { struct rusage u; getrusage(RUSAGE_SELF, &u); return u.ru_maxrss; }

int main()
{
    // ---- T1: mezcla por bloques == mezcla clásica ----
    {
        auto a = noise(150000, 2, 1, 0.6f, false), b = noise(90000, 1, 2, 0.8f, false), c = noise(200001, 2, 3, 0.5f, false);
        std::vector<RawTrack> raw = {
            {a.data(), 150000, 2, 48000, 1.0f, -0.3f, true},
            {b.data(), 90000, 1, 48000, 0.9f, 0.5f, true},
            {c.data(), 200001, 2, 48000, 0.7f, 0.0f, true},
            {c.data(), 100000, 2, 44100, 1.0f, 1.0f, false}};   // muteada
        std::vector<TrackIn> tr; for (auto& r : raw) tr.push_back(toIn(r));
        const int64_t maxF = 200001;
        auto ref = legacyMix(raw, 2, 48000, maxF);
        for (int bs : {4096, 65536, 1024, 99999}) {
            std::vector<float> got; std::vector<float> scratch;
            runPipeline(maxF, 48000, 48000, 1.0f,
                [&](int64_t s, int n, float* o){ mixBlock(tr, maxF, s, n, o, scratch, nullptr, nullptr); for (int i=0;i<n*2;++i) o[i]=std::max(-1.f,std::min(1.f,o[i])); },
                [&](const float* s, int64_t f){ got.insert(got.end(), s, s + f*2); return true; }, nullptr, bs);
            CHECK(got.size() == ref.size(), "mezcla: tamaño bs=%d (%zu vs %zu)", bs, got.size(), ref.size());
            CHECK(maxDiff(got, ref) < 1e-6, "mezcla: diferencia %g bs=%d", maxDiff(got, ref), bs);
        }
        std::printf("T1 mezcla por bloques OK\n");
    }
    // ---- T2: remuestreo lineal en streaming == clásico ----
    {
        auto in = noise(300001, 2, 4, 0.5f, false);
        for (int dst : {44100, 48000, 32000}) {
            std::vector<float> ref; if (dst != 48000) legacyResampleLinear(in, 48000, ref, dst, 2); else ref = in;
            for (int bs : {1000, 65536, 4097}) {
                StreamResampler rs(48000, dst, 2, true); std::vector<float> got;
                for (int64_t s = 0; s < 300001; s += bs) { int n = int(std::min<int64_t>(bs, 300001 - s)); rs.process(in.data() + s*2, n, s + n >= 300001, got); }
                CHECK(got.size() == ref.size(), "resample %d bs=%d: tamaño %zu vs %zu", dst, bs, got.size(), ref.size());
                CHECK(maxDiff(got, ref) == 0.0, "resample %d bs=%d: dif %g", dst, bs, maxDiff(got, ref));
            }
        }
        std::printf("T2 resample (lineal) OK\n");
    }
#ifdef HAVE_SAMPLERATE
    // ---- T2b: remuestreo SINC en streaming vs src_simple de una pasada (como el clásico) ----
    {
        auto in = noise(500000, 2, 8, 0.5f, false);
        for (int i = 0; i < 500000; ++i) { float s = 0.3f * std::sin(2*M_PI*440.0*i/48000.0); in[i*2] += s; in[i*2+1] += s; }
        const double ratio = 44100.0 / 48000.0;
        std::vector<float> ref(size_t(500000 * ratio + 16) * 2);
        SRC_DATA d{}; d.data_in = in.data(); d.input_frames = 500000; d.data_out = ref.data();
        d.output_frames = long(ref.size() / 2); d.src_ratio = ratio; d.end_of_input = 1;
        CHECK(src_simple(&d, SRC_SINC_MEDIUM_QUALITY, 2) == 0, "src_simple");
        ref.resize(size_t(d.output_frames_gen) * 2);
        for (int bs : {65536, 1000, 4097}) {
            StreamResampler rs(48000, 44100, 2); std::vector<float> got;
            for (int64_t s = 0; s < 500000; s += bs) { int n = int(std::min<int64_t>(bs, 500000 - s)); rs.process(in.data() + s*2, n, s + n >= 500000, got); }
            const double df = std::abs(double(got.size()) - double(ref.size())) / 2;
            CHECK(df <= 64, "SINC bs=%d: frames %zu vs %zu", bs, got.size()/2, ref.size()/2);
            // misma señal (alineada): comparar la parte común
            double md = 0; for (size_t i = 0; i + 2 < std::min(got.size(), ref.size()) - 256; ++i) md = std::max(md, double(std::fabs(got[i]-ref[i])));
            CHECK(md < 1e-4, "SINC bs=%d: diferencia máx %g", bs, md);
            std::printf("   SINC bs=%d: %zu vs %zu frames, dif máx %.2e\n", bs, got.size()/2, ref.size()/2, md);
        }
        std::printf("T2b resample SINC OK\n");
    }
#endif
    // ---- T3: LUFS en streaming == clásico (+ pico, + ganancia con guarda de clip) ----
    {
        for (int sr : {48000, 44100}) {
            auto s = noise(int64_t(sr) * 40, 2, 5, 0.4f, true);
            const double ref = legacyLUFS(s, sr, 2);
            LufsMeter m(sr, 2);
            for (int64_t p = 0; p < int64_t(s.size()/2); p += 12345) { int64_t n = std::min<int64_t>(12345, s.size()/2 - p); m.feed(s.data() + p*2, n); }
            CHECK(std::fabs(m.integrated() - ref) < 1e-6, "LUFS %d: %f vs %f", sr, m.integrated(), ref);
            float mx = 0; for (float v : s) mx = std::max(mx, std::fabs(v));
            CHECK(m.peak() == mx, "pico %d", sr);
            float g = 1.0f; CHECK(m.gainFor(-16.0, g), "gainFor");
            std::printf("   %d Hz: LUFS %.4f (clásico %.4f), ganancia %.3f dB\n", sr, m.integrated(), ref, 20*std::log10(g));
        }
        LufsMeter shortM(48000, 2); auto sh = noise(20000, 2, 6, 0.5f, false); shortM.feed(sh.data(), 20000);
        CHECK(shortM.integrated() == -100.0, "audio < 1 s no medible");
        std::printf("T3 LUFS OK\n");
    }
    // ---- T4: conversión a PCM idéntica a writeWavFile ----
    {
        auto s = noise(5000, 2, 7, 1.4f, false);  // con recorte
        for (int bits : {16, 24, 32}) {
            std::vector<uint8_t> got; floatToPcm(s.data(), s.size(), bits, got);
            std::vector<uint8_t> ref;
            for (float x : s) {
                const float c = std::max(-1.0f, std::min(1.0f, x));
                if (bits == 32) { auto p = reinterpret_cast<const uint8_t*>(&x); ref.insert(ref.end(), p, p+4); }
                else if (bits == 24) { int32_t v = int32_t(c * 8388607.0f); ref.push_back(v&0xFF); ref.push_back((v>>8)&0xFF); ref.push_back((v>>16)&0xFF); }
                else { int16_t v = int16_t(c * 32767.0f); ref.push_back(v&0xFF); ref.push_back((v>>8)&0xFF); }
            }
            CHECK(got == ref, "PCM %d bits", bits);
        }
        auto h = wavHeader(48000, 2, 24, 1000); CHECK(h.size() == 44, "cabecera 44 bytes");
        std::printf("T4 PCM/WAV OK\n");
    }
    // ---- T5: memoria constante en un episodio largo ----
    {
        const int64_t frames = int64_t(48000) * 60 * 30;       // 30 min
        std::vector<float> trackData(size_t(48000) * 10 * 2, 0.1f);  // 10 s que se repiten (mismo buffer)
        // Pista "virtual": 30 min leyendo del mismo buffer vía bucle de bloques propio.
        std::vector<float> scratch; std::vector<TrackIn> tr(1);
        tr[0] = toIn({trackData.data(), int(trackData.size()/2), 2, 48000, 1.0f, 0.0f, true});
        const long before = rssKB();
        int64_t written = 0; LufsMeter meter(44100, 2);
        bool ok = runPipeline(frames, 48000, 44100, 1.0f,
            [&](int64_t s, int n, float* o){ std::fill(o, o+n*2, 0.0f); if (s < tr[0].frames) { mixBlock(tr, frames, s, std::min<int64_t>(n, tr[0].frames - s), o, scratch, nullptr, nullptr);} },
            [&](const float* s, int64_t f){ meter.feed(s, f); written += f; return true; }, nullptr);
        const long after = rssKB();
        CHECK(ok, "pipeline largo");
        std::printf("T5 30 min estéreo (%.1f GB en float si se mezclara entero): pico extra de RAM %ld MB, %lld frames escritos\n",
                    frames*8/1e9, (after-before)/1024, (long long)written);
        CHECK((after - before) / 1024 < 100, "RAM extra < 100 MB");
    }
    std::printf(fails ? "\n%d FALLOS\n" : "\nTODO OK\n", fails);
    return fails ? 1 : 0;
}
