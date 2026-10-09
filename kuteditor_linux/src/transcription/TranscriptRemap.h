#pragma once
// ============================================================================
// KutEditor · Reasignar tiempos de la transcripción tras cortar audio
// ----------------------------------------------------------------------------
// Tras un corte ripple las palabras posteriores se adelantan. Estas funciones
// puras calculan los nuevos tiempos y reconstruyen los segmentos (frases).
// ============================================================================
#include "TranscriptWords.h"
#include "EditPlanner.h"
#include <QString>
#include <QVector>

struct TranscriptSegment;

namespace TranscriptRemap {

/// Instante tras eliminar `cuts` (ordenados y sin solapes). Un instante dentro de un corte
/// cae en el inicio del corte.
double mapTime(double t, const QVector<TimeRange> &cuts);

/// Desplaza las palabras; descarta las que quedan (por su punto medio) dentro de un corte.
QVector<TranscriptWord> remapWords(const QVector<TranscriptWord> &words,
                                   const QVector<TimeRange> &cuts);

/// Agrupa palabras en frases: corta tras . ? ! … o tras una pausa mayor que `gapSec`.
struct Sentence { double startSec; double endSec; QString text; };
QVector<Sentence> sentencesFromWords(const QVector<TranscriptWord> &words, double gapSec = 1.2);

} // namespace TranscriptRemap
