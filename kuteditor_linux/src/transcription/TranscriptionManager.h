#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>

#include "WhisperWorker.h"

class TrackModel;

/**
 * TranscriptionManager: gestiona el flujo completo de transcripción local.
 *
 * Responsabilidades:
 *  - Verificar si whisper.cpp está disponible (compilación condicional).
 *  - Localizar y gestionar modelos (.bin) en disco.
 *  - Preparar audio (resample a 16 kHz mono).
 *  - Lanzar WhisperWorker en hilo separado.
 *  - Almacenar segmentos de transcripción por pista.
 *  - Exponer datos a QML para la pista de subtítulos.
 *
 * Se inyecta como context property "Transcription" en main.cpp.
 */
class TranscriptionManager : public QObject
{
    Q_OBJECT

    /// ¿Se compiló con soporte Whisper?
    Q_PROPERTY(bool available READ isAvailable CONSTANT)
    Q_PROPERTY(QString gpuName READ gpuName CONSTANT)
    Q_PROPERTY(QString gpuBackend READ gpuBackend CONSTANT)
    Q_PROPERTY(bool hasGpu READ hasGpu CONSTANT)

    /// ¿Hay un modelo descargado y listo para usar?
    Q_PROPERTY(bool modelReady READ isModelReady NOTIFY modelStatusChanged)

    /// Nombre del modelo activo ("tiny", "base", "small", "medium")
    Q_PROPERTY(QString activeModel READ activeModel
               WRITE setActiveModel NOTIFY modelStatusChanged)

    /// Texto de arranque para Whisper (ayuda a transcribir «eeeh», «mmm»…)
    Q_PROPERTY(QString initialPrompt READ initialPrompt
               WRITE setInitialPrompt NOTIFY initialPromptChanged)

    /// Idioma de transcripción ("auto", "es", "en", ...)
    Q_PROPERTY(QString language READ language
               WRITE setLanguage NOTIFY languageChanged)

    /// Directorio donde se almacenan los modelos
    Q_PROPERTY(QString modelsDir READ modelsDir CONSTANT)

    /// Lista de modelos disponibles con su estado de descarga
    Q_PROPERTY(QVariantList modelsList READ modelsList NOTIFY modelStatusChanged)

public:
    explicit TranscriptionManager(QObject *parent = nullptr);

    void setTrackModel(TrackModel *m) { m_trackModel = m; }

    // -- Propiedades --

    bool isAvailable() const;

    /// Nombre de la GPU detectada (ej. "AMD Radeon 680M", "NVIDIA RTX 3060")
    QString gpuName() const { return m_gpuName; }

    /// Backend que whisper.cpp usará ("Vulkan", "CUDA", "CPU")
    QString gpuBackend() const { return m_gpuBackend; }

    /// True si hay una GPU usable detectada
    bool hasGpu() const { return m_gpuBackend != "CPU"; }
    bool isModelReady() const;

    QString activeModel() const { return m_activeModel; }
    void setActiveModel(const QString &model);

    QString initialPrompt() const { return m_initialPrompt; }
    void setInitialPrompt(const QString &prompt);

    QString language() const { return m_language; }
    void setLanguage(const QString &lang);

    QString modelsDir() const;

    QVariantList modelsList() const;

    // -- API desde QML --

    /// Inicia la transcripción de una pista. El audio se extrae, resamplea
    /// a 16 kHz mono y se envía al worker en background.
    Q_INVOKABLE void transcribeTrack(int trackIndex);

    /// Cancela la transcripción en curso de una pista (si hay alguna).
    Q_INVOKABLE void cancelTranscription(int trackIndex);

    /// ¿Está transcribiendo esta pista ahora?
    Q_INVOKABLE bool isTranscribing(int trackIndex) const;

    /// Progreso de transcripción [0..1] de una pista.
    Q_INVOKABLE float transcriptionProgress(int trackIndex) const;

    /// ¿Tiene transcripción completada esta pista?
    Q_INVOKABLE bool hasTranscription(int trackIndex) const;

    /// Devuelve los segmentos de transcripción como QVariantList de mapas.
    /// Cada mapa: { startSec, endSec, text }.
    Q_INVOKABLE QVariantList transcriptionSegments(int trackIndex) const;

    /// Devuelve las palabras con tiempos como QVariantList de mapas:
    /// { startSec, endSec, text, prob }. Vacío si la transcripción es anterior
    /// a la versión con palabras (hay que volver a transcribir).
    Q_INVOKABLE QVariantList transcriptionWords(int trackIndex) const;

    /// ¿La transcripción de la pista tiene tiempos por palabra?
    Q_INVOKABLE bool hasWordTimestamps(int trackIndex) const;

    /// Elimina la transcripción de una pista.
    Q_INVOKABLE void clearTranscription(int trackIndex);

    /// Devuelve el texto del segmento activo en un momento dado (para
    /// mostrar en la barra de estado o bajo el playhead).
    Q_INVOKABLE QString textAtTime(int trackIndex, double timeSec) const;

    // -- Acceso desde C++ (edición por texto) --
    QVector<TranscriptWord> trackWords(int trackIndex) const;
    QVector<TranscriptSegment> trackSegments(int trackIndex) const;
    QList<int> transcribedTracks() const;
    /// Sustituye la transcripción de una pista (p. ej. tras un corte). Si `segments`
    /// está vacío se regeneran a partir de las palabras.
    void setTrackTranscription(int trackIndex, const QVector<TranscriptSegment> &segments,
                               const QVector<TranscriptWord> &words);

    /// Exporta la transcripción como archivo SRT.
    Q_INVOKABLE bool exportSRT(int trackIndex, const QString &filePath) const;

    /// Exporta como texto plano.
    Q_INVOKABLE bool exportText(int trackIndex, const QString &filePath) const;

    /// Descarga un modelo desde Hugging Face. Usa QProcess con curl/wget.
    Q_INVOKABLE void downloadModel(const QString &modelName);

    /// ¿Está descargando un modelo ahora?
    Q_INVOKABLE bool isDownloading() const { return m_downloading; }

    /// Progreso de descarga [0..1].
    Q_INVOKABLE float downloadProgress() const { return m_downloadProgress; }

    /// Elimina un modelo descargado.
    Q_INVOKABLE void deleteModel(const QString &modelName);

    /// Verifica si los modelos descargados están actualizados comparando
    /// el tamaño local con el remoto (HEAD request). Emite modelCheckResult.
    Q_INVOKABLE void checkModelUpdates();

    /// Copia texto al portapapeles del sistema.
    Q_INVOKABLE void copyToClipboard(const QString &text) const;

    /// Serializa todas las transcripciones a JSON para guardar en proyecto.
    Q_INVOKABLE QJsonArray transcriptionsToJson() const;

    /// Restaura transcripciones desde JSON al abrir un proyecto.
    Q_INVOKABLE void transcriptionsFromJson(const QJsonArray &arr);

signals:
    void modelStatusChanged();
    void languageChanged();
    void initialPromptChanged();
    /// Resultado del chequeo de actualizaciones (mensaje descriptivo)
    void modelCheckResult(const QString &message, bool hasUpdates);

    /// Emitida cuando cambia el estado de transcripción de una pista.
    void transcriptionStateChanged(int trackIndex);

    /// Emitida cuando una transcripción se completa con éxito.
    void transcriptionFinished(int trackIndex);

    /// Emitida en caso de error.
    void transcriptionError(int trackIndex, QString message);

    /// Progreso actualizado.
    void transcriptionProgressChanged(int trackIndex, float progress);

    /// Estado de descarga de modelo.
    void downloadStarted(QString modelName);
    void downloadProgressChanged(float progress);
    void downloadFinished(QString modelName);
    void downloadError(QString message);

private slots:
    void onWorkerFinished(int trackIndex, QVector<TranscriptSegment> segments,
                          QVector<TranscriptWord> words);
    void onWorkerFailed(int trackIndex, QString error);
    void onWorkerProgress(int trackIndex, float progress);

private:
    TrackModel *m_trackModel = nullptr;
    QString m_activeModel = "base";
    QString m_language = "auto";
    QString m_initialPrompt;

    // Datos de transcripción por pista
    struct TrackTranscription {
        QVector<TranscriptSegment> segments;
        QVector<TranscriptWord> words;
        float progress = 0.0f;
        bool transcribing = false;
    };
    QHash<int, TrackTranscription> m_transcriptions;

    // Estado de descarga
    bool m_downloading = false;
    float m_downloadProgress = 0.0f;

    // Helpers
    QString modelFilePath(const QString &modelName) const;
    QVector<float> extractAndResample(int trackIndex) const;

    // URLs de descarga de modelos (Hugging Face / ggerganov)
    static QString modelUrl(const QString &modelName);

    // Info de modelos disponibles
    struct ModelInfo {
        QString name;
        QString displayName;
        int sizeMB;           // tamaño aprox. en MB
        QString description;
    };
    static QVector<ModelInfo> availableModels();

    // GPU detection (runtime)
    void detectGpu();
    QString m_gpuName    = "No detectada";
    QString m_gpuBackend = "CPU";
};
