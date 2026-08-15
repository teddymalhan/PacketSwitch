import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    width: 1160; height: 760; visible: true
    title: "WireLab"
    FileDialog { id: topologyDialog; nameFilters: ["Topology YAML (*.yaml *.yml)"]; onAccepted: wirelab.openTopology(selectedFile.toLocalFile()) }
    header: ToolBar { RowLayout { anchors.fill: parent; ToolButton { text: "Open topology"; onClicked: topologyDialog.open() } Label { text: wirelab.topologyName === "" ? "No topology loaded" : wirelab.topologyName; Layout.fillWidth: true; font.bold: true } Label { text: wirelab.faultSummary } } }
    footer: Label { text: wirelab.statusMessage; padding: 10; width: parent.width; wrapMode: Text.Wrap }
    TabBar { id: tabs; width: parent.width; TabButton { text: "Topology" }; TabButton { text: "Traffic Lab" }; TabButton { text: "Fault Lab" } }
    StackLayout { anchors.top: tabs.bottom; anchors.bottom: parent.footer.top; width: parent.width; currentIndex: tabs.currentIndex
        Item { RowLayout { anchors.fill: parent; anchors.margins: 20
            GroupBox { title: "Nodes"; Layout.fillWidth: true; Layout.fillHeight: true; ListView { anchors.fill: parent; model: wirelab.topologyNodes; delegate: Label { width: parent.width; padding: 10; text: modelData.id + "  —  " + modelData.type } } }
            GroupBox { title: "Links"; Layout.fillWidth: true; Layout.fillHeight: true; ListView { anchors.fill: parent; model: wirelab.topologyLinks; delegate: Label { width: parent.width; padding: 10; text: modelData.from + " ↔ " + modelData.to + "  (" + modelData.latencyMs + " ms)" } } }
        } }
        Item { ColumnLayout { anchors.fill: parent; anchors.margins: 20; spacing: 12
            Label { text: "Deterministic CPU traffic preview"; font.pixelSize: 20 }
            RowLayout { Label { text: "Scenario" }; ComboBox { id: scenario; model: ["mixed-traffic", "known-unicast", "broadcast", "unknown-unicast"] } Label { text: "Packets" }; SpinBox { id: packets; from: 1; to: 1000000; value: 10000 } Label { text: "Batch" }; SpinBox { id: batch; from: 1; to: 8192; value: 128 } }
            RowLayout { Label { text: "Frame bytes" }; SpinBox { id: size; from: 14; to: 1518; value: 64 } Label { text: "Seed" }; SpinBox { id: seed; from: 1; to: 2147483647; value: 42 } Button { text: "Run preview"; onClicked: wirelab.runTrafficPreview(scenario.currentText, packets.value, batch.value, size.value, seed.value) } }
            Label { text: wirelab.trafficResult; wrapMode: Text.Wrap; Layout.fillWidth: true }
        } }
        Item { ColumnLayout { anchors.fill: parent; anchors.margins: 20; spacing: 12
            Label { text: "Port fault"; font.pixelSize: 20 }
            RowLayout { Label { text: "Port" }; TextField { id: port; placeholderText: "host node id" } Label { text: "Latency (ms)" }; SpinBox { id: latency; from: 0; to: 60000 } Label { text: "Loss (%)" }; SpinBox { id: loss; from: 0; to: 10000; stepSize: 1; textFromValue: function(v) { return (v / 100).toFixed(2) } } CheckBox { id: blackhole; text: "Blackhole" } }
            RowLayout { Button { text: "Apply"; enabled: port.text.length > 0; onClicked: wirelab.applyPortFault(port.text, latency.value, loss.value / 100, blackhole.checked) } Button { text: "Clear"; enabled: port.text.length > 0; onClicked: wirelab.clearPortFault(port.text) } }
        } }
    }
}
