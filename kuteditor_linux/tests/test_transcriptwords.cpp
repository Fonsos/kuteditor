// Tests de la conversión de tokens de Whisper en palabras con tiempos (sin whisper.cpp).
#include <QtTest>
#include "transcription/TranscriptWords.h"

namespace {
RawToken tok(const QString &text, double t0, double t1, float p = 0.9f, bool special = false)
{
    RawToken t; t.text = text; t.t0 = t0; t.t1 = t1; t.p = p; t.special = special; return t;
}
}

class TestTranscriptWords : public QObject
{
    Q_OBJECT
private slots:
    void joinsSubwordTokensAndAttachesPunctuation()
    {
        const auto w = TranscriptWords::fromTokens({
            tok(" Hola", 0.00, 0.40), tok(" mun", 0.50, 0.70), tok("do", 0.70, 0.90), tok(",", 0.90, 0.95),
            tok(" eeeh", 1.20, 1.80), tok(".", 1.80, 1.85)});
        QCOMPARE(w.size(), 3);
        QCOMPARE(w[0].text, QString("Hola"));
        QCOMPARE(w[1].text, QString("mundo,"));
        QCOMPARE(w[2].text, QString("eeeh."));
        QCOMPARE(w[1].startSec, 0.50);                 // inicio del primer token…
        QCOMPARE(w[1].endSec, 0.90);                   // …fin del último token de letras (la coma no cuenta)
        QCOMPARE(w[2].startSec, 1.20);
        QCOMPARE(w[2].endSec, 1.80);
    }

    void skipsSpecialTokens()
    {
        const auto w = TranscriptWords::fromTokens({
            tok("[_BEG_]", 0, 0, 0, true), tok(" Hola", 0.1, 0.5), tok("<|endoftext|>", 0.5, 0.5, 0, true),
            tok("<|0.50|>", 0.5, 0.5, 0, true)});
        QCOMPARE(w.size(), 1);
        QCOMPARE(w[0].text, QString("Hola"));
    }

    void spanishOpeningMarksAttachToNextWord()
    {
        const auto w = TranscriptWords::fromTokens({
            tok(" ¿", 0.0, 0.0), tok(" Qué", 0.1, 0.4), tok(" tal", 0.5, 0.8), tok("?", 0.8, 0.8),
            tok(" ¡", 1.0, 1.0), tok("Genial", 1.0, 1.5), tok("!", 1.5, 1.5)});
        QCOMPARE(w.size(), 3);
        QCOMPARE(w[0].text, QString("¿Qué"));
        QCOMPARE(w[1].text, QString("tal?"));
        QCOMPARE(w[2].text, QString("¡Genial!"));
        QCOMPARE(w[0].startSec, 0.1);                   // el signo no adelanta el inicio de la palabra
    }

    void leadingPunctuationAndEmptyInputAreSafe()
    {
        QVERIFY(TranscriptWords::fromTokens({}).isEmpty());
        QVERIFY(TranscriptWords::fromTokens({tok(",", 0, 0), tok(".", 0, 0)}).isEmpty());
        const auto w = TranscriptWords::fromTokens({tok(".", 0, 0), tok(" Hola", 0.2, 0.5)});
        QCOMPARE(w.size(), 1);
        QCOMPARE(w[0].text, QString("Hola"));
    }

    void keepsFillerSoundsAsWords()
    {
        const auto w = TranscriptWords::fromTokens({
            tok(" Pues", 0.0, 0.3), tok(" eee", 0.4, 0.9), tok("eh", 0.9, 1.1), tok(" mmm", 1.5, 2.0), tok(" vale", 2.3, 2.6)});
        QCOMPARE(w.size(), 4);
        QCOMPARE(w[1].text, QString("eeeeh"));
        QCOMPARE(w[2].text, QString("mmm"));
    }

    void averagesConfidence()
    {
        const auto w = TranscriptWords::fromTokens({tok(" mun", 0, 0.2, 0.5f), tok("do", 0.2, 0.4, 0.9f)});
        QCOMPARE(w.size(), 1);
        QVERIFY(qAbs(w[0].prob - 0.7f) < 1e-4f);
    }

    void sanitizeRemovesOverlapsAndEnforcesMinimumDuration()
    {
        QVector<TranscriptWord> w;
        w << TranscriptWord{0.0, 1.0, "a", 1} << TranscriptWord{0.8, 1.5, "b", 1} << TranscriptWord{1.5, 1.5, "c", 1};
        TranscriptWords::sanitize(w, 1.6);
        for (int i = 0; i < w.size(); ++i) {
            QVERIFY2(w[i].endSec - w[i].startSec >= 0.04 - 1e-9, qPrintable(QString::number(i)));
            if (i > 0) QVERIFY2(w[i].startSec >= w[i - 1].endSec - 1e-9, qPrintable(QString::number(i)));
        }
        QVERIFY(w.last().endSec <= 1.6 + 1e-9);
    }

    void sanitizeClampsToAudioEnd()
    {
        QVector<TranscriptWord> w;
        w << TranscriptWord{9.9, 12.0, "fin", 1};
        TranscriptWords::sanitize(w, 10.0);
        QVERIFY(w[0].endSec <= 10.0 + 1e-9);
        QVERIFY(w[0].startSec < w[0].endSec);
    }

    void jsonRoundTrip()
    {
        QVector<TranscriptWord> w;
        w << TranscriptWord{0.25, 0.75, "¿Qué?", 0.5f} << TranscriptWord{1.0, 1.5, "eeeh", 0.25f};
        const auto back = TranscriptWords::fromJson(TranscriptWords::toJson(w));
        QCOMPARE(back.size(), 2);
        QCOMPARE(back[0].text, QString("¿Qué?"));
        QCOMPARE(back[1].startSec, 1.0);
        QVERIFY(qAbs(back[1].prob - 0.25f) < 1e-6f);
        QVERIFY(TranscriptWords::fromJson(QJsonArray()).isEmpty());      // proyecto antiguo sin «words»
    }

    void variantListForQml()
    {
        QVector<TranscriptWord> w;
        w << TranscriptWord{0.5, 1.0, "hola", 0.9f};
        const QVariantList l = TranscriptWords::toVariantList(w);
        QCOMPARE(l.size(), 1);
        QCOMPARE(l[0].toMap()["text"].toString(), QString("hola"));
        QCOMPARE(l[0].toMap()["startSec"].toDouble(), 0.5);
    }
};

QTEST_GUILESS_MAIN(TestTranscriptWords)
#include "test_transcriptwords.moc"
