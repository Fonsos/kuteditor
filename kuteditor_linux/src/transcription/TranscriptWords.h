#pragma once
// ============================================================================
// KutEditor · Transcripción palabra a palabra
// ----------------------------------------------------------------------------
// Utilidades puras (solo Qt Core, sin whisper.cpp) para convertir los tokens que
// devuelve Whisper en palabras con tiempos, y para guardarlas/leerlas en JSON.
// ============================================================================
#include <QJsonArray>
#include <QString>
#include <QVariantList>
#include <QVector>

/// Una palabra transcrita con su posición en la línea de tiempo (segundos).
struct TranscriptWord {
    double startSec = 0.0;
    double endSec   = 0.0;
    QString text;          // incluye la puntuación pegada («mundo,» «¿Qué»)
    float prob      = 0.0f; // confianza media de los tokens (0..1)
};

/// Un token de Whisper ya convertido a segundos.
struct RawToken {
    QString text;           // tal cual lo devuelve Whisper (una palabra nueva empieza con espacio)
    double t0 = 0.0;
    double t1 = 0.0;
    float p = 0.0f;
    bool special = false;   // marcas de Whisper ([_BEG_], <|endoftext|>, marcas de tiempo…)
};

namespace TranscriptWords {

/// Agrupa los tokens de UN segmento en palabras. La puntuación se pega a la palabra
/// que acompaña; los signos de apertura (¿ ¡ « “ ( ") se pegan a la siguiente.
QVector<TranscriptWord> fromTokens(const QVector<RawToken> &tokens);

/// Deja los tiempos coherentes: sin solapes, orden creciente y duración mínima.
/// `maxEnd` (si > 0) acota el final de la última palabra (duración del audio).
void sanitize(QVector<TranscriptWord> &words, double maxEnd = -1.0);

QJsonArray toJson(const QVector<TranscriptWord> &words);
QVector<TranscriptWord> fromJson(const QJsonArray &arr);
QVariantList toVariantList(const QVector<TranscriptWord> &words);

} // namespace TranscriptWords
