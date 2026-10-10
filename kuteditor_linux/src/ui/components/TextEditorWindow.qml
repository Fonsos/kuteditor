import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * TextEditorWindow: edición por texto. Muestra la transcripción de todas las pistas
 * como un único texto; al marcar palabras y pulsar «Aplicar» se cortan de la línea de
 * tiempo (un solo Deshacer). Usa los context properties `TranscriptEditor`, `AudioEngine`.
 */
Window {
    id: win
    title: qsTr("Editar por texto")
    width: 820
    height: 640
    minimumWidth: 520
    minimumHeight: 380
    flags: Qt.Window
    color: "#22252a"

    function open() { visible = true; raise(); requestActivate(); TranscriptEditor.refresh() }
    function close() { visible = false }

    readonly property color textPri: "#ecf0f1"
    readonly property color textSec: "#95a5a6"
    readonly property color divider: "#3c4146"
    readonly property color card: "#2a2d31"
    readonly property color accent: "#e74c3c"
    readonly property var trackColors: ["#5dade2", "#f5b041", "#58d68d", "#bb8fce", "#ec7063", "#48c9b0"]

    property var offsets: []        // inicio (en el texto plano) de cada palabra
    property var ends: []
    property int playingWord: -1
    property string resultMessage: ""

    function rebuild() {
        const ws = TranscriptEditor.words()
        let html = ""
        let lastTrack = -2
        for (let i = 0; i < ws.length; ++i) {
            const w = ws[i]
            if (w.track !== lastTrack) {
                if (lastTrack !== -2) html += "<br><br>"
                const c = trackColors[w.track % trackColors.length]
                html += "<span style='color:" + c + ";font-weight:bold'>" + escapeHtml(w.trackName || ("Pista " + (w.track + 1))) + ":</span> "
                lastTrack = w.track
            }
            const t = escapeHtml(w.text)
            html += w.marked
                ? "<span style='color:#e74c3c;text-decoration:line-through'>" + t + "</span> "
                : "<span style='color:#ecf0f1'>" + t + "</span> "
        }
        const sel = view.selectionStart, selE = view.selectionEnd
        view.text = html
        // Offsets de cada palabra dentro del texto plano
        const plain = view.getText(0, view.length)
        let pos = 0
        const o = [], e = []
        for (let i = 0; i < ws.length; ++i) {
            const k = plain.indexOf(ws[i].text, pos)
            const at = k < 0 ? pos : k
            o.push(at); e.push(at + ws[i].text.length)
            pos = e[i]
        }
        offsets = o; ends = e
    }

    function escapeHtml(s) {
        return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    // Palabra que contiene la posición del cursor (o la más cercana por delante)
    function wordAtOffset(p) {
        let lo = 0, hi = offsets.length - 1, best = -1
        while (lo <= hi) {
            const mid = (lo + hi) >> 1
            if (offsets[mid] <= p) { best = mid; lo = mid + 1 } else hi = mid - 1
        }
        return best
    }

    function selectionRange() {
        const a = view.selectionStart, b = view.selectionEnd
        if (a === b) return null
        let first = wordAtOffset(a)
        if (first >= 0 && ends[first] <= a) first++
        const last = wordAtOffset(b - 1)
        if (first < 0 || last < first || first >= offsets.length) return null
        return [first, last]
    }

    function markSelection(on) {
        const r = selectionRange()
        if (!r) return
        TranscriptEditor.setMarked(r[0], r[1], on)
        view.deselect()
    }

    function doApply() { TranscriptEditor.applyAsync() }

    Connections {
        target: TranscriptEditor
        function onApplied(saved) {
            win.resultMessage = saved < 0 ? qsTr("No hay nada que aplicar.")
                                          : qsTr("Hecho: el episodio se acorta %1 s. Puedes deshacerlo con Ctrl+Z.").arg(saved.toFixed(1))
        }
        function onWordsChanged() { win.rebuild() }
        function onMarksChanged() { win.rebuild() }
    }
    Connections {
        target: AudioEngine
        function onTimeChanged() {
            if (!win.visible) return
            win.playingWord = TranscriptEditor.wordIndexAtTime(AudioEngine.currentTime)
        }
    }
    Component.onCompleted: rebuild()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: win.textSec
            text: TranscriptEditor.wordCount === 0
                  ? qsTr("No hay transcripción con tiempos por palabra. Transcribe las pistas (Herramientas → Transcribir) y vuelve aquí.")
                  : qsTr("Haz clic en una palabra para situar el cursor de reproducción. Selecciona texto y pulsa Supr (o «Marcar») para quitarlo; después «Aplicar».")
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: win.card
            border.color: win.divider
            radius: 4

            ScrollView {
                id: scroll
                anchors.fill: parent
                anchors.margins: 6
                clip: true

                TextEdit {
                    id: view
                    width: scroll.availableWidth
                    readOnly: true
                    selectByMouse: true
                    textFormat: TextEdit.RichText
                    wrapMode: TextEdit.Wrap
                    font.pixelSize: 15
                    color: win.textPri
                    selectionColor: "#8e44ad"
                    persistentSelection: true

                    // Resaltado de la palabra que suena
                    Rectangle {
                        id: hl
                        visible: win.playingWord >= 0 && win.playingWord < win.offsets.length
                        z: -1
                        color: "#3b4a63"
                        radius: 2
                        property rect r: visible ? view.positionToRectangle(win.offsets[win.playingWord]) : Qt.rect(0, 0, 0, 0)
                        property rect r2: visible ? view.positionToRectangle(win.ends[win.playingWord]) : Qt.rect(0, 0, 0, 0)
                        x: r.x; y: r.y
                        width: r2.y === r.y ? Math.max(4, r2.x - r.x) : 40
                        height: r.height
                    }

                    TapHandler {
                        onTapped: (pt) => {
                            if (view.selectionStart !== view.selectionEnd) return
                            const i = win.wordAtOffset(view.positionAt(pt.position.x, pt.position.y))
                            if (i >= 0) AudioEngine.seekTime(TranscriptEditor.wordAt(i).start)
                        }
                    }
                    Keys.onDeletePressed: win.markSelection(true)
                    Keys.onPressed: (ev) => {
                        if (ev.key === Qt.Key_Backspace) { win.markSelection(true); ev.accepted = true }
                    }
                }
            }
        }

        RowLayout {
            spacing: 8
            Button { text: qsTr("Marcar selección"); onClicked: win.markSelection(true) }
            Button { text: qsTr("Desmarcar selección"); onClicked: win.markSelection(false) }
            Button { text: qsTr("Quitar marcas"); onClicked: TranscriptEditor.clearMarks() }
            Item { Layout.fillWidth: true }
            Label {
                color: win.textSec
                text: qsTr("%1 palabras marcadas").arg(TranscriptEditor.markedCount)
            }
        }

        RowLayout {
            spacing: 12
            CheckBox {
                text: qsTr("Acortar pausas de más de")
                checked: TranscriptEditor.maxPauseSec > 0
                onToggled: TranscriptEditor.maxPauseSec = checked ? pauseBox.value / 10 : 0
            }
            SpinBox {
                id: pauseBox
                from: 5; to: 50; value: 12; stepSize: 1
                textFromValue: (v) => (v / 10).toFixed(1) + " s"
                onValueModified: if (TranscriptEditor.maxPauseSec > 0) TranscriptEditor.maxPauseSec = value / 10
            }
            CheckBox {
                text: qsTr("Recortar silencio inicial y final")
                checked: TranscriptEditor.trimEdges
                onToggled: TranscriptEditor.trimEdges = checked
            }
        }

        RowLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: win.textSec
                text: win.resultMessage
            }
            Button {
                text: qsTr("Aplicar cortes")
                highlighted: true
                enabled: TranscriptEditor.markedCount > 0 || TranscriptEditor.maxPauseSec > 0 || TranscriptEditor.trimEdges
                onClicked: win.doApply()
            }
        }
    }
}
