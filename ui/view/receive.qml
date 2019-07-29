import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import Beam.Wallet 1.0
import "controls"

ColumnLayout {
    id: thisView
    property bool regularMode: true
    property var defaultFocusItem: null
    property var currentView: null

    Component.onDestruction: currentView.saveAddress()
    Component.onCompleted:   createChild()
    onRegularModeChanged:    createChild()

    Item {
        Layout.fillWidth:    true
        Layout.topMargin:    75
        Layout.bottomMargin: 50

        SFText {
            x:                   parent.width / 2 - width / 2
            font.pixelSize:      18
            font.styleName:      "Bold"; font.weight: Font.Bold
            color:               Style.content_main
            //% "Receive" / "Create swap offer"
            text:                regularMode ? qsTrId("wallet-receive-title") : qsTrId("wallet-receive-swap-title")
        }

        CustomSwitch {
            id:   mode
            text: qsTrId("wallet-swap")
            x:    parent.width - width
        }

        Binding {
            target:   thisView
            property: "regularMode"
            value:    !mode.checked
        }
    }

    function createChild() {
        if (currentView) currentView.destroy();
        currentView       = Qt.createComponent(regularMode ? "receive_regular.qml" : "receive_swap.qml").createObject(thisView)
        defaultFocusItem  = currentView.defaultFocusItem
        currentView.defaultFocusItem.forceActiveFocus()
    }

    Item {
        Layout.fillHeight: true
    }
}