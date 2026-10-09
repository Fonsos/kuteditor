import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Limpiar habla: marca muletillas con TranscriptEditor.markWhere y, opcionalmente, acorta pausas.
// Necesita que las pistas estén transcritas (con tiempos por palabra).
Window {
    id: win
    width: 520
    height: 400
    visible: false
    title: qsTr("Limpiar muletillas y pausas")
    color: "#22252a"

    // Sonidos de duda: eh, eeeh, ehh, em, mm, mmm, hmm, um, uh…
    readonly property string soundPattern: "e+h+m*|e{2,}h*m*|em+|h?m{2,}h*|hm+|um+|uh+m*"
    property string message: ""

    // Expresión final: sonidos + palabras/frases extra (una sola palabra cada una)
    function buildPattern() {
        let parts = []
        if (soundsBox.checked) parts.push(win.soundPattern)
        const extra = extraField.text.split(",").map(s => s.trim().toLowerCase()).filter(s => s.length > 0)
        for (const w of extra) parts.push(w.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"))
        return parts.length ? "^(?:" + parts.join("|") + ")$" : ""
    }

    function run(applyNow) {
        TranscriptEditor.refresh()
        if (TranscriptEditor.wordCount === 0) {
            message = qsTr("No hay transcripción con tiempos por palabra. Transcribe las pistas primero.")
            return
        }
        const pat = buildPattern()
        const n = pat === "" ? 0 : TranscriptEditor.markWhere(pat, true)
        TranscriptEditor.maxPauseSec = pauseBox.checked ? pauseSpin.value / 10 : 0
        host.setSetting("extra", extraField.text)
        host.setSetting("pause", pauseBox.checked)
        host.setSetting("pauseTenths", pauseSpin.value)
        host.setSetting("sounds", soundsBox.checked)
        if (applyNow) {
            const saved = TranscriptEditor.apply()
            message = saved < 0 ? qsTr("Nada que quitar.")
                                : qsTr("Hecho: %1 muletillas quitadas, el episodio se acorta %2 s (Ctrl+Z lo deshace).").arg(n).arg(saved.toFixed(1))
        } else {
            message = qsTr("%1 muletillas marcadas. Revísalas en «Editar por texto» y aplica.").arg(n)
            TranscriptEditor.requestOpen()
        }
    }

    function onAction(id) { if (id === "open") { win.visible = true; win.raise() } }

    Component.onCompleted: {
        extraField.text = host.setting("extra", "")
        pauseBox.checked = host.setting("pause", false) === true
        pauseSpin.value = Number(host.setting("pauseTenths", 12))
        soundsBox.checked = host.setting("sounds", true) !== false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        CheckBox {
            id: soundsBox
            checked: true
            text: qsTr("Sonidos de duda: «eh», «eeeh», «mmm», «em», «hmm», «um»…")
            palette.windowText: "#ecf0f1"
        }
        Label {
            text: qsTr("Otras palabras a quitar (separadas por comas; solo palabras sueltas):")
            color: "#95a5a6"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        TextField {
            id: extraField
            Layout.fillWidth: true
            placeholderText: qsTr("ej.: pues, bueno, vale")
        }
        RowLayout {
            CheckBox {
                id: pauseBox
                text: qsTr("Acortar pausas de más de")
                palette.windowText: "#ecf0f1"
            }
            SpinBox {
                id: pauseSpin
                from: 5; to: 50; value: 12
                textFromValue: (v) => (v / 10).toFixed(1) + " s"
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#f5b041"
            text: win.message
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            Button { text: qsTr("Marcar y revisar…"); onClicked: win.run(false) }
            Button { text: qsTr("Marcar y aplicar"); highlighted: true; onClicked: win.run(true) }
        }
    }
}
