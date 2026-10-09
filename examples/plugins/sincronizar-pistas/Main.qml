import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Sincronizar pistas: usa el servicio global TrackSyncer (correlación de envolventes).
//  · Referencia: la pista que manda (p. ej. la pista 3 de la P4, o la mezcla del episodio).
//  · Pista a sincronizar: la grabación propia del locutor (p. ej. la pista 2).
Window {
    id: win
    width: 560
    height: 460
    visible: false
    title: qsTr("Sincronizar pistas")
    color: "#22252a"

    property int refIdx: 0
    property int trkIdx: 1
    property string report: ""
    property var names: []

    function refreshNames() {
        const l = []
        for (let i = 0; i < TrackModel.count; ++i) l.push((i + 1) + " · " + TrackModel.getTrackData(i).name)
        names = l
        refIdx = Math.min(refIdx, Math.max(0, l.length - 1))
        trkIdx = Math.min(trkIdx, Math.max(0, l.length - 1))
    }

    function fmt(r) {
        if (!r.ok) return r.message
        let t = ""
        t += qsTr("Desfase: %1 s · confianza %2 (%3)\n").arg(r.offset.toFixed(2)).arg(r.confidence)
              .arg(r.reliable ? qsTr("fiable") : qsTr("baja: no parecen el mismo audio"))
        if (r.breaks.length === 0) {
            t += qsTr("Sin fallos de grabación en la referencia: basta un único desplazamiento.\n")
        } else {
            t += qsTr("%1 salto(s) en la referencia:\n").arg(r.breaks.length)
            for (const b of r.breaks) {
                const m = Math.floor(b.at / 60), s = (b.at - m * 60).toFixed(1)
                t += b.delta < 0
                    ? qsTr("  · en %1:%2 la referencia pierde %3 s → se recorta la pista\n").arg(m).arg(s.padStart(4, "0")).arg((-b.delta).toFixed(2))
                    : qsTr("  · en %1:%2 la referencia mete %3 s de silencio → se deja el hueco\n").arg(m).arg(s.padStart(4, "0")).arg(b.delta.toFixed(2))
            }
        }
        if (r.message) t += "\n" + r.message
        return t
    }

    function analyze() { report = fmt(TrackSyncer.analyze(refIdx, trkIdx)) }
    function sync(force) {
        const r = TrackSyncer.apply(refIdx, trkIdx, force === true)
        report = fmt(r)
        host.log(r.message)
    }

    function onAction(id) { if (id === "open") { refreshNames(); win.visible = true; win.raise() } }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#95a5a6"
            text: qsTr("Elige la pista de referencia (la que manda: p. ej. la pista 3 de la P4) y la grabación propia que hay que alinear con ella.")
        }
        GridLayout {
            columns: 2; columnSpacing: 10; rowSpacing: 8
            Label { text: qsTr("Referencia"); color: "#ecf0f1" }
            ComboBox { Layout.fillWidth: true; model: win.names; currentIndex: win.refIdx; onActivated: (i) => win.refIdx = i }
            Label { text: qsTr("Pista a sincronizar"); color: "#ecf0f1" }
            ComboBox { Layout.fillWidth: true; model: win.names; currentIndex: win.trkIdx; onActivated: (i) => win.trkIdx = i }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            TextArea {
                readOnly: true; wrapMode: Text.Wrap; text: win.report; color: "#ecf0f1"
                background: Rectangle { color: "#2a2d31"; border.color: "#3c4146" }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            Button { text: qsTr("Analizar"); onClicked: win.analyze() }
            Button { text: qsTr("Sincronizar"); highlighted: true; onClicked: win.sync(false) }
        }
    }
}
