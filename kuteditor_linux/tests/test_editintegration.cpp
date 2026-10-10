// Prueba de integración: TranscriptEditor + TrackModel + UndoManager + TranscriptionManager reales.
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>

#include "ui/TrackModel.h"
#include "ui/UndoManager.h"
#include "transcription/TranscriptionManager.h"
#include "transcription/TranscriptEditor.h"
#include "plugins/PluginManager.h"
#include "sync/TrackSyncer.h"
#include "ui/ChapterModel.h"
#include "io/ProjectIO.h"
#include "ui/AppBusy.h"
#include "audio/KutScratch.h"
#include "sync/TrackSync.h"
#include <QQmlEngine>
#include <QQmlContext>
#include <QSettings>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QWindow>

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
// WAV mono 8 kHz cuyo volumen sigue una envolvente (dB a 100 Hz).
QString makeWavFromEnv(const QString &path, const QVector<float> &envDb)
{
    const int sr = 8000, n = envDb.size() * 80;
    QByteArray pcm(n * 2, 0);
    auto *p = reinterpret_cast<qint16 *>(pcm.data());
    quint32 g = 99;
    for (int i = 0; i < n; ++i) {
        g = g * 1664525u + 1013904223u;
        const double noise = ((g >> 8) / double(1 << 24)) * 2 - 1;
        p[i] = qint16(noise * std::pow(10.0, envDb[i / 80] / 20.0) * 1.7 * 32767);
    }
    QFile f(path); f.open(QIODevice::WriteOnly);
    QDataStream ds(&f); ds.setByteOrder(QDataStream::LittleEndian);
    f.write("RIFF"); ds << quint32(36 + pcm.size()); f.write("WAVEfmt ");
    ds << quint32(16) << quint16(1) << quint16(1) << quint32(sr) << quint32(sr * 2) << quint16(2) << quint16(16);
    f.write("data"); ds << quint32(pcm.size()); f.write(pcm);
    return path;
}
quint32 sg = 777;
double srnd() { sg = sg * 1664525u + 1013904223u; return (sg >> 8) / double(1 << 24); }
QVector<float> speechEnv(double seconds)
{
    QVector<float> v; bool talking = true; int left = 0; double level = -25;
    for (int i = 0; i < int(seconds * 100); ++i) {
        if (left <= 0) {
            talking = !talking || srnd() < 0.3;
            left = int((talking ? 0.3 + 2.5 * srnd() : 0.2 + 1.5 * srnd()) * 100);
            level = talking ? -22 - 14 * srnd() : -64;
        }
        --left; v.append(float(level + (srnd() - 0.5) * (talking ? 6 : 3)));
    }
    return v;
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
    void fillerPluginMarksAndApplies() {
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        qputenv("KUTEDITOR_PLUGIN_PATH", SRC_DIR "/../examples/plugins");
        setup({{0, 1.0}, {1.2, 1.6}, {1.8, 3.0}, {3.4, 3.8}}, {{5.0, 6.0}});
        trans->setTrackTranscription(0, {}, {W(0,1,"hola"), W(1.2,1.6,"eeeh,"), W(1.8,3.0,"mundo"), W(3.4,3.8,"pues")});
        trans->setTrackTranscription(1, {}, {W(5,6,"vale")});
        ed->refresh();
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("TranscriptEditor", ed);
        PluginManager pm; pm.setEngine(&engine);
        pm.registerService("TranscriptEditor", ed);
        QVERIFY(pm.scan() >= 1);
        QVERIFY(pm.setEnabled("limpiar-habla", true));
        QCOMPARE(pm.statusOf("limpiar-habla"), QString("active"));
        QWindow *win = nullptr;
        for (QWindow *w : QGuiApplication::allWindows())
            if (w->title() == "Limpiar muletillas y pausas") win = w;
        QVERIFY(win);
        const double lenBefore = trackLen(0);
        QVERIFY(QMetaObject::invokeMethod(win, "run", Q_ARG(QVariant, true)));
        // (el recorte va en segundo plano) «eeeh,» fuera; «pues» se queda (no estaba en la lista extra)
        QTRY_COMPARE_WITH_TIMEOUT(trans->trackWords(0).size(), 3, 10000);
        QCOMPARE(trans->trackWords(0)[0].text, QString("hola"));
        QVERIFY(trackLen(0) < lenBefore - 0.3);
        um->undo();
        QCOMPARE(trans->trackWords(0).size(), 4);
        QVERIFY(qAbs(trackLen(0) - lenBefore) < 0.02);
        pm.shutdown();
    }
    void syncCutsTrackFollowingReferenceGlitch() {
        // Ref = S retrasada 5 s y con 4 s perdidos en u=125 (fallo de la P4). Pista = S completa.
        const auto S = speechEnv(300);
        QVector<float> ref;
        for (int i = 0; i < 500; ++i) ref.append(-66.f);
        for (int i = 0; i < S.size(); ++i) { const int u = i + 500; if (u >= 12500 && u < 12900) continue; ref.append(S[i]); }
        delete ed; delete trans; delete um; delete tm;
        tm = new TrackModel(this); um = new UndoManager(this); trans = new TranscriptionManager(this);
        ed = new TranscriptEditor(this);
        um->setTrackModel(tm); trans->setTrackModel(tm);
        tm->addTrack("P4"); tm->addTrack("Local");
        QVERIFY(tm->importAudioFile(0, makeWavFromEnv(dir.filePath("ref.wav"), ref), 0));
        QVERIFY(tm->importAudioFile(1, makeWavFromEnv(dir.filePath("loc.wav"), S), 0));
        QTRY_VERIFY_WITH_TIMEOUT(!tm->clipsOf(0).isEmpty() && !tm->clipsOf(1).isEmpty(), 20000);
        QTest::qWait(300);
        TrackSyncer syncer; syncer.setModels(tm, um);
        // la importación es asíncrona: esperar a que haya audio real en las dos pistas
        auto ready = [&]() {
            // readMixSegment no usa la caché aplanada: sirve para saber si ya hay audio cargado
            for (int t = 0; t < 2; ++t) {
                QVector<float> buf(2 * 4800);
                tm->readMixSegment(t, qint64(48000) * 100, 4800, buf.data());
                double e = 0;
                for (float v : buf) e += std::abs(v);
                if (e == 0) return false;
            }
            return true;
        };
        QTRY_VERIFY_WITH_TIMEOUT(ready(), 60000);

        const auto a = syncer.analyze(0, 1);
        QVERIFY2(a["ok"].toBool() && a["reliable"].toBool(), qPrintable(a["message"].toString() + " conf=" + a["confidence"].toString() + " off=" + a["offset"].toString()));
        QCOMPARE(a["breaks"].toList().size(), 1);
        // …y lo mismo desde el plugin QML (ventana «Sincronizar pistas»)
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        qputenv("KUTEDITOR_PLUGIN_PATH", SRC_DIR "/../examples/plugins");
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("TrackSyncer", &syncer);
        engine.rootContext()->setContextProperty("TrackModel", tm);
        PluginManager pm; pm.setEngine(&engine);
        QVERIFY(pm.scan() >= 1);
        QVERIFY(pm.setEnabled("sincronizar-pistas", true));
        QCOMPARE(pm.statusOf("sincronizar-pistas"), QString("active"));
        pm.triggerAction("sincronizar-pistas", "open");
        QWindow *win = nullptr;
        for (QWindow *w : QGuiApplication::allWindows())
            if (w->title() == "Sincronizar pistas") win = w;
        QVERIFY(win);
        win->setProperty("refIdx", 0); win->setProperty("trkIdx", 1);
        QVERIFY(QMetaObject::invokeMethod(win, "analyze"));
        QTRY_VERIFY_WITH_TIMEOUT(win->property("report").toString().contains("1 salto"), 30000);
        QVERIFY(QMetaObject::invokeMethod(win, "sync", Q_ARG(QVariant, false)));
        QTRY_COMPARE_WITH_TIMEOUT(tm->clipsOf(1).size(), 2, 30000);
        QVERIFY(!syncer.busy());
        pm.shutdown();
        const auto res = a;   // análisis previo (sin aplicar)
        Q_UNUSED(res);
        // Tras alinear, las dos pistas casan en todo el tramo (sin saltos y con desfase ~0)
        double d1 = 0, d2 = 0; int f, c, s1, s2;
        double dummy; Q_UNUSED(dummy);
        const float *p1 = tm->trackSamplesData(0, &f, &c, &s1);
        const auto e1 = TrackSync::envelopeDb(p1, f, c, s1);
        const float *p2 = tm->trackSamplesData(1, &f, &c, &s2);
        const auto e2 = TrackSync::envelopeDb(p2, f, c, s2);
        Q_UNUSED(d1); Q_UNUSED(d2);
        const auto again = TrackSync::analyze(e1, e2, double(e2.size()) / 100);
        QCOMPARE(again.breaks.size(), 0);
        QVERIFY2(qAbs(again.globalOffset) < 0.03, qPrintable(QString::number(again.globalOffset)));
        // Deshacer devuelve el clip original
        um->undo();
        QCOMPARE(tm->clipsOf(1).size(), 1);
        QVERIFY(qAbs(tm->clipSnapshot(1, 0)["startSec"].toDouble()) < 1e-6);
        um->redo();
        QCOMPARE(tm->clipsOf(1).size(), 2);
    }
    void longTrackSurvivesSaveAndReopen() {
        // Audio largo (>30 s) va a un mmap: el guardado no debe perderlo (pistas «planas» al reabrir).
        const auto S = speechEnv(120);
        TrackModel t1; ProjectIO p1; p1.setTrackModel(&t1);
        t1.addTrack("Larga");
        QVERIFY(t1.importAudioFile(0, makeWavFromEnv(dir.filePath("long.wav"), S), 0));
        auto energyAt = [](TrackModel &tm, double sec) {
            QVector<float> buf(2 * 4800);
            tm.readMixSegment(0, qint64(sec * 48000), 4800, buf.data());
            double e = 0; for (float v : buf) e += std::abs(v);
            return e;
        };
        QTRY_VERIFY_WITH_TIMEOUT(!t1.clipsOf(0).isEmpty() && energyAt(t1, 30) > 0, 30000);
        QTest::qWait(300);
        const QVector<float> src = t1.trackSourceSamples(0, 0, nullptr, nullptr);
        QVERIFY2(src.size() > 48000 * 100, qPrintable(QString::number(src.size())));

        const QString proj = dir.filePath("long.kutproj");
        QVERIFY2(p1.save(proj), qPrintable(p1.lastError()));

        TrackModel t2; ProjectIO p2; p2.setTrackModel(&t2);
        QVERIFY2(p2.open(proj), qPrintable(p2.lastError()));
        QCOMPARE(t2.count(), 1);
        QVERIFY(!t2.clipsOf(0).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(energyAt(t2, 30) > 0, 10000);
        QVERIFY(qAbs(energyAt(t2, 30) - energyAt(t1, 30)) < 0.01 * energyAt(t1, 30) + 1e-3);
    }
    void importFailureIsReportedNotSilent() {
        TrackModel t; t.addTrack("X");
        const QString bad = dir.filePath("roto.wav");
        { QFile f(bad); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("esto no es audio"); }
        QSignalSpy started(&t, &TrackModel::importStarted), fin(&t, &TrackModel::importFinished);
        QVERIFY(t.importAudioFile(0, bad, 0));
        QCOMPARE(started.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(fin.count(), 1, 30000);
        QVERIFY(!fin.first().at(1).toBool());
        QVERIFY2(!fin.first().at(2).toString().isEmpty(), "falta el motivo del fallo");
        QVERIFY(t.clipsOf(0).isEmpty());
    }
    void bigAudioUsesScratchDirAndLeavesNoFiles() {
        QTemporaryDir scratch;
        qputenv("KUTEDITOR_SCRATCH", scratch.path().toUtf8());
        {
            TrackModel t; t.addTrack("Larga");
            QSignalSpy fin(&t, &TrackModel::importFinished);
            QVERIFY(t.importAudioFile(0, makeWavFromEnv(dir.filePath("s.wav"), speechEnv(60)), 0));
            QTRY_COMPARE_WITH_TIMEOUT(fin.count(), 1, 30000);
            QVERIFY(fin.first().at(1).toBool());
            QTest::qWait(300);
            // el audio está en un mmap (>30 s) pero su archivo ya no tiene nombre: nada que se quede si el programa muere
            QVERIFY2(QDir(scratch.path()).entryList(QDir::Files).isEmpty(),
                     qPrintable(QDir(scratch.path()).entryList(QDir::Files).join(",")));
            QVector<float> buf(2 * 4800);
            t.readMixSegment(0, qint64(20 * 48000), 4800, buf.data());
            double e = 0; for (float v : buf) e += std::abs(v);
            QVERIFY(e > 0);
        }
        // restos de un proceso muerto se limpian; los de un proceso vivo no
        QFile dead(scratch.path() + "/kut_mmap_999999_1.raw"); QVERIFY(dead.open(QIODevice::WriteOnly)); dead.close();
        const QString mine = QString("%1/kut_mmap_%2_5.raw").arg(scratch.path()).arg(QCoreApplication::applicationPid());
        QFile alive(mine); QVERIFY(alive.open(QIODevice::WriteOnly)); alive.close();
        QCOMPARE(KutScratch::cleanupStale(), 1);
        QVERIFY(!QFile::exists(dead.fileName()));
        QVERIFY(QFile::exists(mine));
        qunsetenv("KUTEDITOR_SCRATCH");
    }
    void bigFileBenchmark() {
        // Manual: KUT_BIG_FILE=/ruta/episodio.(wav|mp3|flac) ./test_editintegration bigFileBenchmark
        const QString f = qEnvironmentVariable("KUT_BIG_FILE");
        if (f.isEmpty()) QSKIP("define KUT_BIG_FILE para medir un archivo largo");
        auto hwmMB = []() { QFile st("/proc/self/status"); st.open(QIODevice::ReadOnly);
            for (const QByteArray &l : st.readAll().split('\n')) if (l.startsWith("VmHWM")) return l.split(':')[1].trimmed().split(' ')[0].toLongLong() / 1024;
            return qint64(0); };
        TrackModel t; ProjectIO p; p.setTrackModel(&t);
        t.addTrack("Episodio");
        QSignalSpy fin(&t, &TrackModel::importFinished);
        QElapsedTimer tm; tm.start();
        QVERIFY(t.importAudioFile(0, f, 0));
        QTRY_COMPARE_WITH_TIMEOUT(fin.count(), 1, 3600000);
        const bool ok = fin.first().at(1).toBool();
        qInfo() << "BENCH importado ok=" << ok << fin.first().at(2).toString() << "en" << tm.elapsed() / 1000.0 << "s, pico RAM" << hwmMB() << "MB";
        QVERIFY(ok);
        QTest::qWait(3000);
        qInfo() << "BENCH tras mipmap: pico RAM" << hwmMB() << "MB, scratch libre" << KutScratch::freeBytes() / (1 << 20) << "MB";
        tm.restart();
        QVERIFY2(p.save(QDir::tempPath() + "/bench_big.kutproj"), qPrintable(p.lastError()));
        qInfo() << "BENCH guardado en" << tm.elapsed() / 1000.0 << "s, pico RAM" << hwmMB() << "MB";
        QFile::remove(QDir::tempPath() + "/bench_big.kutproj");
    }
    void appBusyCountsNestedTasks() {
        AppBusy b;
        QSignalSpy spy(&b, &AppBusy::changed);
        QVERIFY(!b.active());
        b.begin("uno"); b.begin("dos");
        QVERIFY(b.active()); QCOMPARE(b.text(), QString("dos"));
        b.end(); QVERIFY(b.active()); QCOMPARE(b.text(), QString("uno"));
        b.end(); QVERIFY(!b.active()); QVERIFY(b.text().isEmpty());
        b.end();                       // de más: no falla
        QVERIFY(!b.active());
        QCOMPARE(spy.count(), 5);
    }
    void asyncSyncShowsBusyAndFinishes() {
        const auto S = speechEnv(150);
        QVector<float> ref;
        for (int i = 0; i < 300; ++i) ref.append(-66.f);
        ref += S;
        TrackModel t; UndoManager u; AppBusy busy; TrackSyncer sy;
        u.setTrackModel(&t); sy.setModels(&t, &u, &busy);
        t.addTrack("P4"); t.addTrack("Local");
        QVERIFY(t.importAudioFile(0, makeWavFromEnv(dir.filePath("ra.wav"), ref), 0));
        QVERIFY(t.importAudioFile(1, makeWavFromEnv(dir.filePath("la.wav"), S), 0));
        auto ready = [&]() {
            for (int k = 0; k < 2; ++k) {
                QVector<float> buf(2 * 4800);
                t.readMixSegment(k, qint64(48000) * 60, 4800, buf.data());
                double e = 0; for (float v : buf) e += std::abs(v);
                if (e == 0) return false;
            }
            return true;
        };
        QTRY_VERIFY_WITH_TIMEOUT(ready(), 30000);
        QTest::qWait(300);
        QSignalSpy done(&sy, &TrackSyncer::finished);
        bool sawBusy = false;
        connect(&busy, &AppBusy::changed, this, [&] { if (busy.active()) sawBusy = true; });
        sy.applyAsync(0, 1);
        QVERIFY(busy.active());                    // el aviso sale al instante
        QVERIFY(sy.busy());
        sy.applyAsync(0, 1);                       // una segunda petición mientras trabaja se rechaza
        QCOMPARE(done.count(), 1);
        QVERIFY(!done.first().first().toMap().value("ok").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 30000);
        QVERIFY(sawBusy);
        QVERIFY(!busy.active());
        QVERIFY(!sy.busy());
        const auto res = done.last().first().toMap();
        QVERIFY2(res.value("applied").toBool(), qPrintable(res.value("message").toString()));
        QVERIFY(qAbs(t.clipSnapshot(1, 0)["startSec"].toDouble() - 3.0) < 0.05);   // movida 3 s hacia delante
    }
    void reanalyzeAudioRebuildsPeaksAndReportsSilence() {
        TrackModel tm1;
        tm1.addTrack("Voz"); tm1.addTrack("Muda");
        QVector<float> quiet(120 * 100, -120.0f);              // 120 s de silencio absoluto
        QVERIFY(tm1.importAudioFile(0, makeWavFromEnv(dir.filePath("v.wav"), speechEnv(120)), 0));
        QVERIFY(tm1.importAudioFile(1, makeWavFromEnv(dir.filePath("m.wav"), quiet), 0));
        QTRY_VERIFY_WITH_TIMEOUT(!tm1.clipsOf(0).isEmpty() && !tm1.clipsOf(1).isEmpty(), 30000);
        QTest::qWait(500);
        QSignalSpy spy(&tm1, &TrackModel::audioReanalyzed);
        tm1.reanalyzeAudio();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 30000);
        QCOMPARE(spy.first().at(0).toInt(), 2);                  // analizadas
        QCOMPARE(spy.first().at(1).toInt(), 1);                  // una es todo silencio
        QCOMPARE(spy.first().at(2).toInt(), 0);                  // ninguna sin audio
        // La forma de onda vuelve a tener picos
        const QVariantList pk = tm1.clipPeaks(0, 0, 400);
        QVERIFY(!pk.isEmpty());
        // Pista con clips pero sin audio: se avisa
        tm1.addTrack("Vacía");
        spy.clear();
        tm1.reanalyzeAudio(2);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
        QCOMPARE(spy.first().at(0).toInt(), 0);
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
