#pragma once
// ============================================================================
// KutEditor · Planificador de cortes para la edición por texto
// ----------------------------------------------------------------------------
// Dado el texto transcrito de cada pista y las palabras que el usuario ha marcado
// para eliminar, calcula qué tramos hay que:
//   · CORTAR de todas las pistas a la vez (el episodio se acorta), o
//   · SILENCIAR solo en una pista, cuando en ese momento habla otra persona
//     (así no se desincroniza ni se recorta al que habla por encima).
// Opcionalmente acorta pausas largas y recorta el silencio inicial/final.
//
// Solo usa Qt Core: se puede probar sin interfaz ni audio real.
// ============================================================================
#include <QMap>
#include <QVector>

struct EditWord {
    int track = 0;
    double start = 0.0;
    double end = 0.0;
    bool deleted = false;     // marcada para eliminar
};

/// Energía del audio de una pista (dB, una muestra cada `stepSec`), para cortar en silencios.
struct EnergyEnvelope {
    double stepSec = 0.01;
    QVector<float> db;
    bool isEmpty() const { return db.isEmpty(); }
    float at(double sec) const;                       // dB en ese instante (-120 si está fuera)
    double quietestNear(double t, double back, double fwd) const;
};

struct TimeRange {
    double start = 0.0;
    double end = 0.0;
    double length() const { return end - start; }
};

struct EditPlanOptions {
    double padSec = 0.015;        // colchón alrededor de cada palabra eliminada
    double maxPauseSec = 0.0;     // >0: acorta las pausas más largas que esto (0 = no tocar)
    bool trimEdges = false;       // recorta el silencio inicial y final
    double totalDuration = 0.0;   // duración del episodio (para el recorte final)
};

struct EditPlan {
    QVector<TimeRange> cuts;                  // se eliminan de TODAS las pistas (ripple), ordenados
    QMap<int, QVector<TimeRange>> mutes;      // por pista: se silencian sin acortar
    int deletedWords = 0;
    double savedSec() const { double s = 0; for (const auto &c : cuts) s += c.length(); return s; }
};

namespace EditPlanner {

EditPlan plan(const QVector<EditWord> &words,
              const QMap<int, EnergyEnvelope> &envelopes,
              const EditPlanOptions &options);

/// Unión de intervalos solapados o contiguos (separados menos de `gap`).
QVector<TimeRange> mergeRanges(QVector<TimeRange> ranges, double gap = 0.0);
/// a menos b.
QVector<TimeRange> subtractRanges(const QVector<TimeRange> &a, const QVector<TimeRange> &b);

} // namespace EditPlanner
