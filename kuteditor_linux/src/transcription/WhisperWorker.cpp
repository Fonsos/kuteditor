#include "WhisperWorker.h"

#include <QDebug>
#include <QFile>
#include <QElapsedTimer>
#include <thread>

#ifdef HAVE_WHISPER
#include "whisper.h"
#endif

WhisperWorker::WhisperWorker(const QString &modelPath,
                             const QVector<float> &samples,
                             int trackIndex,
                             const QString &language,
                             const QString &initialPrompt,
                             QObject *parent)
    : QObject(parent)
    , m_modelPath(modelPath)
    , m_samples(samples)
    , m_trackIndex(trackIndex)
    , m_language(language)
    , m_initialPrompt(initialPrompt)
{
    setAutoDelete(true);
}

void WhisperWorker::run()
{
#ifdef HAVE_WHISPER
    // ---- Validar modelo ----
    if (!QFile::exists(m_modelPath)) {
        emit failed(m_trackIndex,
                    tr("Modelo Whisper no encontrado: %1").arg(m_modelPath));
        return;
    }

    if (m_samples.isEmpty()) {
        emit failed(m_trackIndex, tr("No hay audio para transcribir."));
        return;
    }

    QElapsedTimer timer;
    timer.start();

    // ---- Inicializar contexto Whisper ----
    struct whisper_context_params cparams = whisper_context_default_params();
    // Usar GPU si está disponible (NO-OP si se compiló sin soporte)
    cparams.use_gpu = true;

    struct whisper_context *ctx = whisper_init_from_file_with_params(
        m_modelPath.toUtf8().constData(), cparams);

    if (!ctx) {
        emit failed(m_trackIndex,
                    tr("Error al cargar el modelo Whisper: %1").arg(m_modelPath));
        return;
    }

    emit progressChanged(m_trackIndex, 0.1f);

    // ---- Configurar parámetros de inferencia ----
    struct whisper_full_params wparams =
        whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    // Idioma: el elegido por el usuario. Con «auto» se mantiene el español de siempre
    // (el autodetectado fallaba con los modelos pequeños).
    const QByteArray lang = (m_language.isEmpty() || m_language == QLatin1String("auto"))
                                ? QByteArrayLiteral("es") : m_language.toUtf8();
    wparams.language = lang.constData();
    wparams.detect_language = false;

    // Tiempos por palabra (a partir de los tokens). No cambia la segmentación en frases.
    wparams.token_timestamps = true;

    // Con un texto de arranque con «eeeh, mmm…» Whisper tiende a escribir esos sonidos
    // en lugar de limpiarlos, y no se arrastra el contexto de la frase anterior.
    const QByteArray prompt = m_initialPrompt.toUtf8();
    if (!prompt.isEmpty()) {
        wparams.initial_prompt = prompt.constData();
        wparams.carry_initial_prompt = true;
    }

    // Usaremos los parámetros estrictamente por defecto (salvo el idioma y el progreso)
    // para descartar que alguna optimización rompa la salida en tu versión 1.8.4.
    wparams.n_threads = std::min(4, (int)std::thread::hardware_concurrency());

    // Callback de progreso (whisper.cpp lo llama periódicamente)
    struct ProgressData {
        WhisperWorker *self;
        int trackIndex;
    };
    ProgressData pd { this, m_trackIndex };

    wparams.progress_callback = [](struct whisper_context * /*ctx*/,
                                   struct whisper_state   * /*state*/,
                                   int progress, void *user_data) {
        auto *data = static_cast<ProgressData*>(user_data);
        // progress va de 0 a 100 en whisper.cpp
        float p = 0.1f + 0.85f * (progress / 100.0f);
        emit data->self->progressChanged(data->trackIndex, p);
    };
    wparams.progress_callback_user_data = &pd;

    emit progressChanged(m_trackIndex, 0.15f);

    // ---- Ejecutar inferencia ----
    int ret = whisper_full(ctx, wparams,
                           m_samples.constData(), m_samples.size());

    if (ret != 0) {
        whisper_free(ctx);
        emit failed(m_trackIndex,
                    tr("Error de inferencia Whisper (código %1)").arg(ret));
        return;
    }

    emit progressChanged(m_trackIndex, 0.95f);

    // ---- Extraer segmentos y palabras ----
    const int nSegments = whisper_full_n_segments(ctx);
    const whisper_token eot = whisper_token_eot(ctx);       // ids >= eot son marcas especiales
    QVector<TranscriptSegment> segments;
    QVector<TranscriptWord> words;
    segments.reserve(nSegments);

    for (int i = 0; i < nSegments; ++i) {
        TranscriptSegment seg;
        seg.startSec = whisper_full_get_segment_t0(ctx, i) / 100.0;
        seg.endSec   = whisper_full_get_segment_t1(ctx, i) / 100.0;
        seg.text     = QString::fromUtf8(whisper_full_get_segment_text(ctx, i)).trimmed();

        // Ignorar segmentos vacíos o de solo ruido
        if (seg.text.isEmpty() || seg.endSec <= seg.startSec) continue;
        segments.append(seg);

        QVector<RawToken> tokens;
        const int nTok = whisper_full_n_tokens(ctx, i);
        tokens.reserve(nTok);
        for (int t = 0; t < nTok; ++t) {
            const whisper_token_data td = whisper_full_get_token_data(ctx, i, t);
            RawToken rt;
            rt.text    = QString::fromUtf8(whisper_full_get_token_text(ctx, i, t));
            rt.t0      = td.t0 / 100.0;
            rt.t1      = td.t1 / 100.0;
            rt.p       = td.p;
            rt.special = td.id >= eot;
            tokens.append(rt);
        }
        words += TranscriptWords::fromTokens(tokens);
    }
    TranscriptWords::sanitize(words, m_samples.size() / 16000.0);

    whisper_free(ctx);

    qDebug() << "[WhisperWorker] Transcripción completada:"
             << segments.size() << "segmentos," << words.size() << "palabras en"
             << timer.elapsed() << "ms";

    emit progressChanged(m_trackIndex, 1.0f);
    emit finished(m_trackIndex, segments, words);

#else
    // whisper.cpp no compilado — no debería llegar aquí, pero por seguridad
    emit failed(m_trackIndex,
                tr("Kut se compiló sin soporte de transcripción (whisper.cpp)."));
#endif
}
