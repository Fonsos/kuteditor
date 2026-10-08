import QtQuick
import QtQuick.Controls

// Plugin QML de ejemplo. El objeto `host` lo inyecta KutEditor (ver docs/PLUGINS.md):
//   host.registerAction(id, titulo)  host.setting(k, def)  host.setSetting(k, v)
//   host.service("TrackModel")       host.log(texto)
// Las funciones opcionales onAction(id) y onShutdown() las llama el gestor.
Window {
    id: win
    width: 360
    height: 140
    visible: false
    title: qsTr("Hola KutEditor")
    color: "#2a2d31"

    property string message: ""

    function onAction(id) {
        if (id === "count") {
            var tm = host.service("TrackModel")
            var n = tm ? tm.rowCount() : 0
            host.setSetting("lastCount", n)
            win.message = qsTr("El proyecto tiene %1 pista(s).").arg(n)
            host.log(win.message)
            win.visible = true
            win.raise()
        }
    }

    Label {
        anchors.centerIn: parent
        text: win.message
        color: "#ecf0f1"
        font.pixelSize: 16
    }
}
