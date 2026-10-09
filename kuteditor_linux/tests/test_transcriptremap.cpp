#include <QtTest>
#include "transcription/TranscriptRemap.h"

class TestRemap : public QObject {
    Q_OBJECT
    static TranscriptWord W(double a, double b, const char *t) { TranscriptWord w; w.startSec=a; w.endSec=b; w.text=QString::fromUtf8(t); return w; }
private slots:
    void mapTimeShifts() {
        QVector<TimeRange> c{{1,2},{5,6}};
        QCOMPARE(TranscriptRemap::mapTime(0.5, c), 0.5);
        QCOMPARE(TranscriptRemap::mapTime(3, c), 2.0);
        QCOMPARE(TranscriptRemap::mapTime(7, c), 5.0);
        QCOMPARE(TranscriptRemap::mapTime(1.5, c), 1.0);
    }
    void remapDropsAndShifts() {
        QVector<TranscriptWord> w{W(0,.5,"a"),W(1.1,1.9,"b"),W(2.5,3,"c")};
        auto r = TranscriptRemap::remapWords(w, {{1,2}});
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[1].text, QString("c"));
        QCOMPARE(r[1].startSec, 1.5);
        QCOMPARE(r[1].endSec, 2.0);
    }
    void sentences() {
        QVector<TranscriptWord> w{W(0,.5,"Hola"),W(.5,1,"mundo."),W(1.1,1.5,"Otra"),W(5,5.5,"cosa")};
        auto s = TranscriptRemap::sentencesFromWords(w);
        QCOMPARE(s.size(), 3);
        QCOMPARE(s[0].text, QString("Hola mundo."));
        QCOMPARE(s[1].text, QString("Otra"));
        QCOMPARE(s[2].startSec, 5.0);
    }
};
QTEST_APPLESS_MAIN(TestRemap)
#include "test_transcriptremap.moc"
