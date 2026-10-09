// Prueba de integración: TranscriptEditor + TrackModel + UndoManager + TranscriptionManager reales.
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>

#include "ui/TrackModel.h"
#include "ui/UndoManager.h"
#include "transcription/TranscriptionManager.h"
#include "transcription/TranscriptEditor.h"

namespace {
const int SR = 48000;

// WAV 16-bit estéreo con "habla" (ruido tonal) en los tramos dados.
QString makeWav(const QString &path, double seconds, const QVector<QPair<double,double>> &bursts)
{
    const int frames = int(seconds * SR);
    QByteArray pcm(frames * 4, 0);
    auto *p = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < frames; ++i) {
        const double t = double(i) / SR;
        double v = 0;
        for (const auto &b : bursts)
            if (t >= b.first && t < b.second) v = 0.4 * std::sin(2 * M_PI * 220 * t) + 0.2 * std::sin(2 * M_PI * 1330 * t);
        p[2 * i] = p[2 * i + 1] = qint16(v * 32767);
    }
    QFile f(path); f.open(QIODevice::WriteOnly);
    QDataStream ds(&f); ds.setByteOrder(QDataStream::LittleEndian);
    f.write("RIFF"); ds << quint32(36 + pcm.size()); f.write("WAVEfmt ");
    ds << quint32(16) << quint16(1) << quint16(2) << quint32(SR) << quint32(SR * 4) << quint16(4) << quint16(16);
    f.write("data"); ds << quint32(pcm.size()); f.write(pcm);
    return path;
}
TranscriptWord W(double a, double b, const char *t) { TranscriptWord w; w.startSec = a; w.endSec = b; w.text = QString::fromUtf8(t); w.prob = 0.9f; return w; }
}

class TestEditIntegration : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    TrackModel *tm = nullptr;
    UndoManager *um = nullptr;
    TranscriptionManager *trans = nullptr;
    TranscriptEditor *ed = nullptr;

    double trackLen(int t) {
        int f = 0, c = 0, s = 0;
        return tm->trackSamplesData(t, &f, &c, &s) ? double(f) / s : 0;
    }
    void setup(const QVector<QPair<double,double>> &a, const QVector<QPair<double,double>> &b) {
        delete ed; delete trans; delete um; delete tm;
        tm = new TrackModel(this); um = new UndoManager(this); trans = new TranscriptionManager(this);
        ed = new TranscriptEditor(this);
        um->setTrackModel(tm); trans->setTrackModel(tm); ed->setModels(tm, um, trans);
        tm->addTrack("A"); tm->addTrack("B");
        QVERIFY(tm->importAudioFile(0, makeWav(dir.filePath("a.wav"), 8, a), 0));
        QVERIFY(tm->importAudioFile(1, makeWav(dir.filePath("b.wav"), 8, b), 0));
        QTRY_VERIFY_WITH_TIMEOUT(!tm->clipsOf(0).isEmpty() && !tm->clipsOf(1).isEmpty(), 10000);
        QTest::qWait(200);
    }
private slots:
    void cutsAllTracksAndRemapsTranscript() {
        setup({{0, 1.0}, {1.2, 1.6}, {1.8, 3.0}}, {{5.0, 6.0}});
        trans->setTrackTranscription(0, {}, {W(0,1,"hola"), W(1.2,1.6,"eh"), W(1.8,3.0,"mundo")});
        trans->setTrackTranscription(1, {}, {W(5,6,"vale")});
        ed->refresh();
        QCOMPARE(ed->wordCount(), 4);
        const double lenBefore = trackLen(0);
        QCOMPARE(ed->markWhere("^(eh|ehh+|mm+)$"), 1);
        QCOMPARE(ed->markedCount(), 1);

        const double saved = ed->apply();
        QVERIFY2(saved > 0.35 && saved < 0.5, qPrintable(QString::number(saved)));
        QVERIFY(qAbs(trackLen(0) - (lenBefore - saved)) < 0.02);
        QVERIFY(qAbs(trackLen(1) - (lenBefore - saved)) < 0.02);
        // Transcripción: «eh» desaparece y «mundo»/«vale» se adelantan lo cortado
        auto wa = trans->trackWords(0);
        QCOMPARE(wa.size(), 2);
        QCOMPARE(wa[1].text, QString("mundo"));
        QVERIFY(qAbs(wa[1].startSec - (1.8 - saved)) < 0.03);
        QVERIFY(qAbs(trans->trackWords(1)[0].startSec - (5.0 - saved)) < 0.03);
        QCOMPARE(ed->wordCount(), 3);

        // Un solo Deshacer lo revierte todo
        um->undo();
        QVERIFY(qAbs(trackLen(0) - lenBefore) < 0.02);
        QCOMPARE(trans->trackWords(0).size(), 3);
        QVERIFY(qAbs(trans->trackWords(0)[2].startSec - 1.8) < 1e-6);
        QCOMPARE(ed->wordCount(), 4);
        // …y Rehacer lo aplica otra vez
        um->redo();
        QVERIFY(qAbs(trackLen(0) - (lenBefore - saved)) < 0.02);
        QCOMPARE(trans->trackWords(0).size(), 2);
    }
    void overlappingSpeechIsMutedNotCut() {
        // A dice «um» (4.2-4.5) mientras B habla (4.0-5.0): se silencia en A, el episodio no se acorta.
        setup({{0, 1.0}, {4.2, 4.5}}, {{4.0, 5.0}});
        trans->setTrackTranscription(0, {}, {W(0,1,"hola"), W(4.2,4.5,"um")});
        trans->setTrackTranscription(1, {}, {W(4,5,"claro")});
        ed->refresh();
        const double lenBefore = trackLen(0);
        QCOMPARE(ed->markWhere("^um+$"), 1);
        const double saved = ed->apply();
        QVERIFY(saved >= 0 && saved < 0.01);
        QVERIFY(qAbs(trackLen(0) - lenBefore) < 0.02);
        QVERIFY(qAbs(trackLen(1) - lenBefore) < 0.02);
        // A está en silencio donde estaba «um»
        int f = 0, c = 0, s = 0;
        const float *d = tm->trackSamplesData(0, &f, &c, &s);
        double e = 0;
        for (int i = int(4.25 * s); i < int(4.45 * s); ++i) e += std::abs(d[i * c]);
        QVERIFY2(e < 1.0, qPrintable(QString::number(e)));
        um->undo();
        d = tm->trackSamplesData(0, &f, &c, &s);
        e = 0;
        for (int i = int(4.25 * s); i < int(4.45 * s); ++i) e += std::abs(d[i * c]);
        QVERIFY(e > 100.0);
    }
};
QTEST_MAIN(TestEditIntegration)
#include "test_editintegration.moc"
