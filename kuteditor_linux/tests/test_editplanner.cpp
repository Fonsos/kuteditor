// Tests del planificador de cortes (sin audio real: envolventes sintéticas).
#include <QtTest>
#include "transcription/EditPlanner.h"

namespace {
EditWord w(int track, double s, double e, bool del = false) { EditWord x; x.track = track; x.start = s; x.end = e; x.deleted = del; return x; }
bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

/// Envolvente de `dur` s con -80 dB de fondo y `level` dB dentro de los tramos dados.
EnergyEnvelope env(double dur, std::initializer_list<TimeRange> loud, float level = -20.0f)
{
    EnergyEnvelope e; e.stepSec = 0.01; e.db.fill(-80.0f, int(dur / 0.01));
    for (const TimeRange &r : loud)
        for (int i = int(r.start / 0.01); i < int(r.end / 0.01) && i < e.db.size(); ++i) e.db[i] = level;
    return e;
}
}

class TestEditPlanner : public QObject
{
    Q_OBJECT
private slots:
    void rangeHelpers()
    {
        const auto m = EditPlanner::mergeRanges({{5, 6}, {0, 1}, {0.9, 2}, {2.001, 3}}, 0.01);
        QCOMPARE(m.size(), 2);
        QVERIFY(near(m[0].start, 0) && near(m[0].end, 3));
        const auto s = EditPlanner::subtractRanges({{0, 10}}, {{2, 3}, {5, 6}});
        QCOMPARE(s.size(), 3);
        QVERIFY(near(s[1].start, 3) && near(s[1].end, 5));
        QVERIFY(EditPlanner::subtractRanges({{1, 2}}, {{0, 5}}).isEmpty());
    }

    void deletedFillerInSilenceIsCutFromEveryTrack()
    {
        const auto p = EditPlanner::plan({w(0, 1.0, 1.4), w(0, 1.6, 2.1, true), w(0, 2.4, 3.0)}, {}, EditPlanOptions());
        QCOMPARE(p.cuts.size(), 1);
        QCOMPARE(p.mutes.size(), 0);
        QCOMPARE(p.deletedWords, 1);
        QVERIFY(p.cuts[0].start < 1.6 && p.cuts[0].end > 2.1);        // cubre la palabra con colchón…
        QVERIFY(p.cuts[0].start >= 1.4 && p.cuts[0].end <= 2.4);      // …sin invadir a los vecinos
    }

    void fillerOverlappingAnotherSpeakerIsOnlyMuted()
    {
        const auto p = EditPlanner::plan({w(0, 1.0, 2.0), w(0, 2.2, 2.8, true), w(0, 3.0, 4.0),
                                          w(1, 2.3, 2.7)}, {}, EditPlanOptions());
        QCOMPARE(p.cuts.size(), 0);                                    // el episodio no se acorta
        QCOMPARE(p.mutes.size(), 1);
        QVERIFY(p.mutes.contains(0));                                  // solo se silencia la pista del que dijo la muletilla
        QVERIFY(p.mutes[0][0].start < 2.3 && p.mutes[0][0].end > 2.7);
    }

    void consecutiveDeletedWordsFormOneRegion()
    {
        const auto p = EditPlanner::plan({w(0, 0.5, 0.9), w(0, 1.0, 1.3, true), w(0, 1.35, 1.8, true), w(0, 2.5, 3.0)}, {}, EditPlanOptions());
        QCOMPARE(p.cuts.size(), 1);
        QCOMPARE(p.deletedWords, 2);
        QVERIFY(p.cuts[0].start < 1.0 && p.cuts[0].end > 1.8);
    }

    void cutsNeverBiteKeptNeighbours()
    {
        EditPlanOptions o; o.padSec = 0.5;                              // colchón enorme
        const auto p = EditPlanner::plan({w(0, 1.0, 1.5), w(0, 1.55, 2.0, true), w(0, 2.05, 2.5)}, {}, o);
        QCOMPARE(p.cuts.size(), 1);
        QVERIFY(p.cuts[0].start >= 1.5 - 1e-9);                          // no pisa el final de la anterior
        QVERIFY(p.cuts[0].end <= 2.05 + 1e-9);                           // ni el inicio de la siguiente
    }

    void cutsSnapToQuietPointsOfTheAudio()
    {
        // Habla alta de 1.0 a 1.6 (palabra conservada) y 1.8–2.3 (la eliminada); silencio entre medias.
        const auto e = env(4.0, {{1.0, 1.6}, {1.8, 2.3}, {2.9, 3.5}});
        EditPlanOptions o; o.padSec = 0.1;
        const auto p = EditPlanner::plan({w(0, 1.0, 1.6), w(0, 1.8, 2.3, true), w(0, 2.9, 3.5)}, {{0, e}}, o);
        QCOMPARE(p.cuts.size(), 1);
        QVERIFY(e.at(p.cuts[0].start) < -70.0f);                         // empieza y acaba en silencio real
        QVERIFY(e.at(p.cuts[0].end) < -70.0f);
        QVERIFY(p.cuts[0].start <= 1.8 && p.cuts[0].end >= 2.3);         // sin dejar trozo de la palabra
    }

    void longPausesAreShortened()
    {
        EditPlanOptions o; o.maxPauseSec = 1.2;
        const auto p = EditPlanner::plan({w(0, 0.5, 1.0), w(0, 4.0, 4.5)}, {}, o);   // pausa de 3 s
        QCOMPARE(p.cuts.size(), 1);
        QVERIFY(near(p.savedSec(), 3.0 - 1.2, 0.01));                    // quedan 1,2 s de pausa
    }

    void shortPausesAreUntouched()
    {
        EditPlanOptions o; o.maxPauseSec = 1.2;
        QVERIFY(EditPlanner::plan({w(0, 0.5, 1.0), w(0, 1.8, 2.4)}, {}, o).cuts.isEmpty());
    }

    void pauseCutsOnlyWhereEveryTrackIsQuiet()
    {
        // Pausa de 1.0 a 5.0 según las palabras, pero hay un ruido fuerte (risa, tos…) de 2.8 a 3.2 en la otra pista.
        const auto quiet = env(6.0, {});
        const auto noisy = env(6.0, {{2.8, 3.2}});
        EditPlanOptions o; o.maxPauseSec = 1.0;
        const auto p = EditPlanner::plan({w(0, 0.2, 1.0), w(0, 5.0, 5.8)}, {{0, quiet}, {1, noisy}}, o);
        for (const TimeRange &c : p.cuts) QVERIFY2(c.end <= 2.8 + 1e-9 || c.start >= 3.2 - 1e-9, "no debe cortar el ruido");
        QVERIFY(p.savedSec() > 1.0);
    }

    void trimsLeadingAndTrailingSilence()
    {
        EditPlanOptions o; o.trimEdges = true; o.totalDuration = 20.0;
        const auto p = EditPlanner::plan({w(0, 5.0, 6.0), w(0, 8.0, 9.0)}, {}, o);
        QCOMPARE(p.cuts.size(), 2);
        QVERIFY(near(p.cuts[0].start, 0.0) && near(p.cuts[0].end, 4.75, 1e-6));    // deja 0,25 s antes de hablar
        QVERIFY(near(p.cuts[1].start, 9.6, 1e-6) && near(p.cuts[1].end, 20.0));    // y 0,6 s tras la última palabra
    }

    void mutesInsideCutsAreDropped()
    {
        // La pista 0 elimina una palabra mientras la 1 habla (→ silenciar), pero esa zona queda dentro de una pausa larga recortada.
        EditPlanOptions o; o.maxPauseSec = 0.5;
        const auto p = EditPlanner::plan({w(0, 1.0, 1.2), w(0, 3.0, 3.5, true), w(0, 8.0, 8.5), w(1, 3.1, 3.4)}, {}, o);
        // Con la pista 1 hablando en 3.1–3.4 no hay pausa ahí: el silenciado se mantiene.
        QVERIFY(p.mutes.contains(0));
    }

    void bothTracksDeletedAtTheSameTimeIsAFullCut()
    {
        const auto p = EditPlanner::plan({w(0, 1.0, 1.5), w(0, 2.0, 2.6, true), w(0, 3.2, 3.8),
                                          w(1, 2.1, 2.5, true), w(1, 4.0, 4.5)}, {}, EditPlanOptions());
        QCOMPARE(p.mutes.size(), 0);                                     // nadie habla encima: corte limpio
        QCOMPARE(p.cuts.size(), 1);
        QCOMPARE(p.deletedWords, 2);
    }

    void nothingDeletedMeansNothingToDo()
    {
        const auto p = EditPlanner::plan({w(0, 1, 2), w(1, 2, 3)}, {}, EditPlanOptions());
        QVERIFY(p.cuts.isEmpty() && p.mutes.isEmpty());
        QCOMPARE(p.deletedWords, 0);
    }
};

QTEST_GUILESS_MAIN(TestEditPlanner)
#include "test_editplanner.moc"
