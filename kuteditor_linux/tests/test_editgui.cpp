// Prueba de la ventana «Editar por texto» (QML) con los objetos reales.
#include <QtTest>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>

#include "audio/AudioEngine.h"
#include "ui/TrackModel.h"
#include "ui/UndoManager.h"
#include "transcription/TranscriptionManager.h"
#include "transcription/TranscriptEditor.h"

static TranscriptWord W(double a, double b, const char *t) { TranscriptWord w; w.startSec = a; w.endSec = b; w.text = QString::fromUtf8(t); return w; }

class TestEditGui : public QObject {
    Q_OBJECT
private slots:
    void windowShowsAndMarksWords() {
        TrackModel tm; UndoManager um; TranscriptionManager trans; TranscriptEditor ed; AudioEngine ae;
        um.setTrackModel(&tm); trans.setTrackModel(&tm); ed.setModels(&tm, &um, &trans); ae.setTrackModel(&tm);
        tm.addTrack("Ana"); tm.addTrack("Luis");
        trans.setTrackTranscription(0, {}, {W(0,.5,"Hola"), W(.6,1,"eeeh,"), W(1.2,2,"mundo.")});
        trans.setTrackTranscription(1, {}, {W(3,3.5,"Vale")});
        ed.refresh();

        QQmlEngine engine;
        engine.rootContext()->setContextProperty("TranscriptEditor", &ed);
        engine.rootContext()->setContextProperty("AudioEngine", &ae);
        QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(SRC_DIR "/src/ui/components/TextEditorWindow.qml")));
        QObject *obj = comp.create();
        QVERIFY2(obj, qPrintable(comp.errorString()));
        QVERIFY(QMetaObject::invokeMethod(obj, "open"));
        auto *w = qobject_cast<QQuickWindow *>(obj);
        QVERIFY(w);
        QTest::qWait(300);

        QCOMPARE(ed.wordCount(), 4);
        // Los desplazamientos de palabras se han calculado
        const QVariantList offs = obj->property("offsets").toList();
        QCOMPARE(offs.size(), 4);
        QVERIFY(offs[1].toInt() > offs[0].toInt());
        QVariant r;
        QVERIFY(QMetaObject::invokeMethod(obj, "wordAtOffset", Q_RETURN_ARG(QVariant, r), Q_ARG(QVariant, offs[2].toInt() + 1)));
        QCOMPARE(r.toInt(), 2);

        QCOMPARE(ed.markWhere("^e+h+$"), 1);
        QTest::qWait(100);
        QCOMPARE(obj->property("offsets").toList().size(), 4);   // se reconstruyó tras marcar

        const QString shot = qEnvironmentVariable("KUT_SHOT");
        if (!shot.isEmpty()) w->grabWindow().save(shot);
        delete obj;
    }
};
QTEST_MAIN(TestEditGui)
#include "test_editgui.moc"
