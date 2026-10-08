import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

/**
 * PluginsDialog: gestor de plugins. Lista los plugins encontrados, permite activarlos
 * o desactivarlos, instalar uno desde una carpeta y abrir la carpeta de plugins.
 * Usa el context property `PluginManager`.
 */
Window {
    id: pluginsDialog
    title: qsTr("Gestor de plugins")
    width: 640
    height: 560
    minimumWidth: 480
    minimumHeight: 360
    flags: Qt.Window
    color: "#22252a"

    function open() { visible = true; raise(); requestActivate() }
    function close() { visible = false }

    readonly property color textPri: "#ecf0f1"
    readonly property color textSec: "#95a5a6"
    readonly property color divider: "#3c4146"
    readonly property color card: "#2a2d31"
    readonly property color ok: "#2ecc71"
    readonly property color bad: "#e74c3c"

    property string installMessage: ""

    FolderDialog {
        id: installFolderDialog
        title: qsTr("Elige la carpeta del plugin (la que contiene plugin.json)")
        onAccepted: {
            const err = PluginManager.installFromFolder(selectedFolder.toString())
            pluginsDialog.installMessage = err === "" ? qsTr("Plugin instalado. Actívalo en la lista.") : err
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Plugins")
                font.pixelSize: 20; font.bold: true
                color: pluginsDialog.textPri
                Layout.fillWidth: true
            }
            Button { text: qsTr("Instalar desde carpeta…"); onClicked: installFolderDialog.open() }
            Button { text: qsTr("Abrir carpeta de plugins"); onClicked: PluginManager.openPluginDir() }
            Button { text: qsTr("Recargar"); onClicked: PluginManager.reload() }
        }

        Label {
            Layout.fillWidth: true
            visible: pluginsDialog.installMessage !== ""
            text: pluginsDialog.installMessage
            wrapMode: Text.WordWrap
            color: pluginsDialog.textSec
        }

        Label {
            Layout.fillWidth: true
            visible: listView.count === 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: pluginsDialog.textSec
            text: qsTr("No hay plugins instalados.\nColoca carpetas con un plugin.json en:\n%1").arg(PluginManager.userPluginDir)
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: PluginManager.plugins
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: row
                required property var modelData
                width: listView.width - 12
                height: info.implicitHeight + 24
                radius: 8
                color: pluginsDialog.card
                border.color: modelData.status === "error" ? pluginsDialog.bad : pluginsDialog.divider

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Switch {
                        enabled: !row.modelData.invalid
                        checked: row.modelData.enabled
                        onToggled: PluginManager.setEnabled(row.modelData.id, checked)
                        Layout.alignment: Qt.AlignTop
                    }

                    ColumnLayout {
                        id: info
                        Layout.fillWidth: true
                        spacing: 3

                        RowLayout {
                            spacing: 8
                            Label {
                                text: row.modelData.name
                                font.pixelSize: 15; font.bold: true
                                color: pluginsDialog.textPri
                            }
                            Label {
                                visible: row.modelData.version !== ""
                                text: "v" + row.modelData.version
                                color: pluginsDialog.textSec
                            }
                            Rectangle {
                                visible: row.modelData.active
                                radius: 8; color: Qt.rgba(0.18, 0.8, 0.44, 0.18)
                                implicitWidth: activeLbl.implicitWidth + 14; implicitHeight: 18
                                Label { id: activeLbl; anchors.centerIn: parent; text: qsTr("Activo"); font.pixelSize: 11; color: pluginsDialog.ok }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text !== ""
                            wrapMode: Text.WordWrap
                            text: row.modelData.description
                            color: pluginsDialog.textPri
                            opacity: 0.85
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: pluginsDialog.textSec
                            text: [
                                row.modelData.author !== "" ? qsTr("Autor: %1").arg(row.modelData.author) : "",
                                row.modelData.kind !== "" ? qsTr("Tipo: %1").arg(row.modelData.kind) : "",
                                row.modelData.requires.length > 0 ? qsTr("Requiere: %1").arg(row.modelData.requires.join(", ")) : ""
                            ].filter(function (s) { return s !== "" }).join("  ·  ")
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: row.modelData.error !== ""
                            wrapMode: Text.WordWrap
                            color: pluginsDialog.bad
                            text: row.modelData.error
                        }
                    }
                }
            }
        }
    }
}
