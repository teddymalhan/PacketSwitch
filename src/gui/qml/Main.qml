import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 1440
    height: 900
    minimumWidth: 1120
    minimumHeight: 720
    visible: true
    title: wirelab.topologyName === "" ? "WireLab" : "WireLab — " + wirelab.topologyName
    color: "#0b1220"

    property color panel: "#121c2e"
    property color panelRaised: "#18253a"
    property color border: "#293852"
    property color accent: "#42b8ff"
    property color good: "#45d49b"
    property color warning: "#ffbd59"
    property color danger: "#ff647c"
    property color textPrimary: "#edf5ff"
    property color textMuted: "#91a3ba"

    palette.window: root.color
    palette.windowText: textPrimary
    palette.base: panel
    palette.alternateBase: panelRaised
    palette.text: textPrimary
    palette.button: panelRaised
    palette.buttonText: textPrimary
    palette.highlight: accent
    palette.highlightedText: "#07111f"

    function nodePoint(id, area) {
        for (let i = 0; i < wirelab.topologyNodes.length; ++i) {
            const node = wirelab.topologyNodes[i]
            if (node.id === id)
                return Qt.point(node.x * area.width, node.y * area.height)
        }
        return Qt.point(0, 0)
    }

    function lastMetric(field) {
        if (wirelab.metricsHistory.length === 0)
            return 0
        return wirelab.metricsHistory[wirelab.metricsHistory.length - 1][field]
    }

    function selectGraphLink(mouseX, mouseY, area) {
        let bestDistance = 12
        let best = null
        for (let i = 0; i < wirelab.topologyLinks.length; ++i) {
            const link = wirelab.topologyLinks[i]
            const a = nodePoint(link.from, area)
            const b = nodePoint(link.to, area)
            const dx = b.x - a.x
            const dy = b.y - a.y
            const lengthSquared = dx * dx + dy * dy
            if (lengthSquared === 0)
                continue
            const t = Math.max(0, Math.min(1, ((mouseX - a.x) * dx + (mouseY - a.y) * dy) / lengthSquared))
            const px = a.x + t * dx
            const py = a.y + t * dy
            const distance = Math.sqrt((mouseX - px) * (mouseX - px) + (mouseY - py) * (mouseY - py))
            if (distance < bestDistance) {
                bestDistance = distance
                best = link
            }
        }
        if (best)
            wirelab.selectLink(best.from, best.to)
        else
            wirelab.clearSelection()
    }

    FileDialog {
        id: openDialog
        title: "Open topology"
        nameFilters: ["Topology YAML (*.yaml *.yml)"]
        onAccepted: wirelab.openTopology(selectedFile.toLocalFile())
    }

    FileDialog {
        id: saveDialog
        title: "Save topology"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "yaml"
        nameFilters: ["Topology YAML (*.yaml *.yml)"]
        onAccepted: wirelab.saveTopology(selectedFile.toLocalFile())
    }

    Timer {
        interval: 500
        repeat: true
        running: wirelab.trafficRunning
        onTriggered: wirelab.runTrafficStep()
    }

    header: ToolBar {
        height: 58
        background: Rectangle {
            color: root.panel
            border.color: root.border
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 12
            Label {
                text: "WIRELAB"
                color: root.accent
                font.pixelSize: 18
                font.bold: true
                font.letterSpacing: 2
            }
            ToolSeparator {}
            Button { text: "Open"; onClicked: openDialog.open() }
            Button {
                text: "Save"
                enabled: wirelab.hasTopology
                onClicked: saveDialog.open()
            }
            Label {
                text: wirelab.topologyName === "" ? "No topology loaded" : wirelab.topologyName
                color: root.textPrimary
                font.bold: true
                Layout.fillWidth: true
            }
            Rectangle {
                implicitWidth: backendLabel.implicitWidth + 24
                implicitHeight: 30
                radius: 15
                color: wirelab.trafficRunning ? "#153c35" : root.panelRaised
                border.color: wirelab.trafficRunning ? root.good : root.border
                Label {
                    id: backendLabel
                    anchors.centerIn: parent
                    text: wirelab.trafficRunning ? "● LIVE · " + wirelab.activeBackend : "○ IDLE"
                    color: wirelab.trafficRunning ? root.good : root.textMuted
                    font.bold: true
                }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 38
        color: root.panel
        border.color: root.border
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            Label {
                text: wirelab.statusMessage === "" ? "Open scenarios/security-lab.yaml to begin." : wirelab.statusMessage
                color: root.textMuted
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Label {
                text: wirelab.topologyNodes.length + " nodes · " + wirelab.topologyLinks.length + " links · "
                      + wirelab.activeFaults.length + " faults"
                color: root.textMuted
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: tabs
            Layout.fillWidth: true
            background: Rectangle { color: root.panel; border.color: root.border }
            TabButton { text: "Dashboard" }
            TabButton { text: "Topology" }
            TabButton { text: "Traffic" }
            TabButton { text: "Packets & Security" }
            TabButton { text: "Faults" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            Item {
                GridLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    columns: 4
                    rowSpacing: 12
                    columnSpacing: 12

                    Repeater {
                        model: [
                            { label: "THROUGHPUT", value: root.lastMetric("throughputMbps").toFixed(3) + " Mbps", color: root.accent },
                            { label: "AVG LATENCY", value: root.lastMetric("latencyMs").toFixed(2) + " ms", color: root.warning },
                            { label: "PACKET LOSS", value: root.lastMetric("lossPercent").toFixed(2) + "%", color: root.danger },
                            { label: "ACTIVE FAULTS", value: wirelab.activeFaults.length.toString(), color: root.good }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 100
                            radius: 8
                            color: root.panel
                            border.color: root.border
                            Column {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 8
                                Label { text: modelData.label; color: root.textMuted; font.pixelSize: 11; font.bold: true }
                                Label { text: modelData.value; color: modelData.color; font.pixelSize: 24; font.bold: true }
                            }
                        }
                    }

                    Rectangle {
                        Layout.columnSpan: 3
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 8
                        color: root.panel
                        border.color: root.border
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            Label { text: "LIVE TELEMETRY · LAST 30 SECONDS"; color: root.textMuted; font.bold: true }
                            Canvas {
                                id: dashboardChart
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.strokeStyle = root.border
                                    ctx.lineWidth = 1
                                    for (let grid = 1; grid < 5; ++grid) {
                                        const y = height * grid / 5
                                        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
                                    }
                                    const rows = wirelab.metricsHistory
                                    if (rows.length < 2)
                                        return
                                    function plot(field, color, maximum) {
                                        ctx.strokeStyle = color
                                        ctx.lineWidth = 2
                                        ctx.beginPath()
                                        for (let i = 0; i < rows.length; ++i) {
                                            const x = i * width / 59
                                            const y = height - Math.min(1, rows[i][field] / maximum) * (height - 12) - 6
                                            if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                                        }
                                        ctx.stroke()
                                    }
                                    let throughputMax = 1
                                    for (let i = 0; i < rows.length; ++i)
                                        throughputMax = Math.max(throughputMax, rows[i].throughputMbps)
                                    plot("throughputMbps", root.accent, throughputMax)
                                    plot("latencyMs", root.warning, 100)
                                    plot("lossPercent", root.danger, 100)
                                }
                                Connections {
                                    target: wirelab
                                    function onTelemetryChanged() { dashboardChart.requestPaint() }
                                }
                            }
                            RowLayout {
                                Label { text: "━ Throughput"; color: root.accent }
                                Label { text: "━ Latency (100 ms scale)"; color: root.warning }
                                Label { text: "━ Loss (100% scale)"; color: root.danger }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 8
                        color: root.panel
                        border.color: root.border
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            Label { text: "PORT STATE"; color: root.textMuted; font.bold: true }
                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: wirelab.portStates
                                delegate: Rectangle {
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 58
                                    color: index % 2 ? root.panelRaised : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Label { text: modelData.id; color: root.textPrimary; font.bold: true }
                                            Label { text: modelData.received + " rx · " + modelData.forwarded + " tx · " + modelData.dropped + " drop"; color: root.textMuted; font.pixelSize: 11 }
                                        }
                                        Label { text: modelData.state; color: root.good; font.bold: true }
                                    }
                                }
                                Label {
                                    anchors.centerIn: parent
                                    visible: parent.count === 0
                                    text: "Run traffic to populate port counters"
                                    color: root.textMuted
                                    wrapMode: Text.Wrap
                                    width: parent.width - 24
                                }
                            }
                        }
                    }
                }
            }

            Item {
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14
                    Rectangle {
                        id: graphPanel
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 8
                        color: root.panel
                        border.color: root.border

                        Item {
                            id: graphArea
                            anchors.fill: parent
                            anchors.margins: 20
                            Canvas {
                                id: topologyCanvas
                                anchors.fill: parent
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.lineWidth = 3
                                    for (let i = 0; i < wirelab.topologyLinks.length; ++i) {
                                        const link = wirelab.topologyLinks[i]
                                        const a = root.nodePoint(link.from, graphArea)
                                        const b = root.nodePoint(link.to, graphArea)
                                        const selected = wirelab.selectedType === "link"
                                                         && wirelab.selectedId.indexOf(link.from) >= 0
                                                         && wirelab.selectedId.indexOf(link.to) >= 0
                                        ctx.strokeStyle = selected ? root.accent : "#4d6482"
                                        ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke()
                                        const mx = (a.x + b.x) / 2
                                        const my = (a.y + b.y) / 2
                                        ctx.fillStyle = root.panel
                                        ctx.fillRect(mx - 20, my - 10, 40, 20)
                                        ctx.fillStyle = root.textMuted
                                        ctx.font = "11px sans-serif"
                                        ctx.textAlign = "center"
                                        ctx.fillText(link.latencyMs + " ms", mx, my + 4)
                                    }
                                }
                                Connections {
                                    target: wirelab
                                    function onTopologyChanged() { topologyCanvas.requestPaint() }
                                    function onSelectionChanged() { topologyCanvas.requestPaint() }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: function(mouse) { root.selectGraphLink(mouse.x, mouse.y, graphArea) }
                            }
                            Repeater {
                                model: wirelab.topologyNodes
                                delegate: Rectangle {
                                    required property var modelData
                                    width: modelData.type === "switch" ? 118 : 98
                                    height: 58
                                    x: modelData.x * graphArea.width - width / 2
                                    y: modelData.y * graphArea.height - height / 2
                                    radius: modelData.type === "switch" ? 8 : 29
                                    color: wirelab.selectedType === "node" && wirelab.selectedId === modelData.id ? "#17466a" : root.panelRaised
                                    border.width: 2
                                    border.color: wirelab.selectedType === "node" && wirelab.selectedId === modelData.id ? root.accent
                                                                                                                          : modelData.type === "switch" ? root.warning : root.good
                                    z: 2
                                    Column {
                                        anchors.centerIn: parent
                                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.id; color: root.textPrimary; font.bold: true }
                                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.type.toUpperCase(); color: root.textMuted; font.pixelSize: 10 }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: wirelab.selectNode(modelData.id)
                                    }
                                }
                            }
                            Label {
                                anchors.centerIn: parent
                                visible: !wirelab.hasTopology
                                text: "Open a topology YAML to render the network"
                                color: root.textMuted
                                font.pixelSize: 16
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 340
                        Layout.fillHeight: true
                        radius: 8
                        color: root.panel
                        border.color: root.border
                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 14
                            contentWidth: availableWidth
                            ColumnLayout {
                                width: parent.width
                                spacing: 12
                                Label { text: "INSPECTOR"; color: root.textMuted; font.bold: true }
                                Label {
                                    text: wirelab.selectedId === "" ? "Nothing selected" : wirelab.selectedId
                                    color: root.textPrimary
                                    font.pixelSize: 20
                                    font.bold: true
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                }
                                Label { text: wirelab.selectedSummary; color: root.textMuted; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                Button {
                                    text: "Remove selected"
                                    enabled: wirelab.selectedType !== ""
                                    Layout.fillWidth: true
                                    onClicked: wirelab.removeSelected()
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                                Label { text: "ADD NODE"; color: root.textMuted; font.bold: true }
                                TextField { id: nodeId; placeholderText: "Node ID"; Layout.fillWidth: true }
                                ComboBox { id: nodeType; model: ["host", "switch"]; Layout.fillWidth: true }
                                Button {
                                    text: "Add node"
                                    enabled: nodeId.text.trim().length > 0
                                    Layout.fillWidth: true
                                    onClicked: {
                                        wirelab.addNode(nodeId.text, nodeType.currentText)
                                        nodeId.clear()
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                                Label { text: "ADD LINK"; color: root.textMuted; font.bold: true }
                                ComboBox {
                                    id: linkFrom
                                    Layout.fillWidth: true
                                    model: wirelab.topologyNodes
                                    textRole: "id"
                                    valueRole: "id"
                                }
                                ComboBox {
                                    id: linkTo
                                    Layout.fillWidth: true
                                    model: wirelab.topologyNodes
                                    textRole: "id"
                                    valueRole: "id"
                                }
                                RowLayout {
                                    Label { text: "Latency" }
                                    SpinBox { id: linkLatency; from: 0; to: 60000; value: 1; Layout.fillWidth: true }
                                    Label { text: "ms"; color: root.textMuted }
                                }
                                Button {
                                    text: "Add link"
                                    enabled: linkFrom.currentValue !== undefined && linkTo.currentValue !== undefined
                                    Layout.fillWidth: true
                                    onClicked: wirelab.addLink(linkFrom.currentValue, linkTo.currentValue, linkLatency.value)
                                }
                            }
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 132
                        radius: 8
                        color: root.panel
                        border.color: root.border
                        GridLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            columns: 7
                            Label { text: "Scenario"; color: root.textMuted }
                            Label { text: "Packets / 500 ms"; color: root.textMuted }
                            Label { text: "Frame bytes"; color: root.textMuted }
                            Label { text: "Seed"; color: root.textMuted }
                            Label { text: "Analyzer"; color: root.textMuted }
                            Item { Layout.fillWidth: true }
                            Item {}
                            ComboBox { id: scenario; model: ["mixed-traffic", "known-unicast", "broadcast", "unknown-unicast"] }
                            SpinBox { id: packetRate; from: 1; to: 100000; value: 512; editable: true }
                            SpinBox { id: frameSize; from: 14; to: 1518; value: 128; editable: true }
                            SpinBox { id: trafficSeed; from: 1; to: 2147483647; value: 42; editable: true }
                            ComboBox {
                                id: backend
                                model: wirelab.cudaAvailable ? ["CPU", "CUDA"] : ["CPU"]
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: wirelab.trafficRunning ? "Stop" : "Start"
                                highlighted: !wirelab.trafficRunning
                                onClicked: {
                                    if (wirelab.trafficRunning)
                                        wirelab.stopTraffic()
                                    else
                                        wirelab.startTraffic(scenario.currentText, packetRate.value, frameSize.value,
                                                             trafficSeed.value, backend.currentText)
                                }
                            }
                            Label {
                                Layout.columnSpan: 7
                                Layout.fillWidth: true
                                text: wirelab.trafficResult
                                color: root.textPrimary
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 14
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 8
                            color: root.panel
                            border.color: root.border
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                Label { text: "FORWARDING / MAC TABLE"; color: root.textMuted; font.bold: true }
                                RowLayout {
                                    Label { text: "MAC ADDRESS"; color: root.textMuted; Layout.fillWidth: true }
                                    Label { text: "LEARNED PORT"; color: root.textMuted; Layout.preferredWidth: 160 }
                                }
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: wirelab.macTable
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: ListView.view.width
                                        height: 36
                                        color: index % 2 ? root.panelRaised : "transparent"
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 8
                                            anchors.rightMargin: 8
                                            Label { text: modelData.mac; font.family: "monospace"; Layout.fillWidth: true }
                                            Label { text: modelData.port; color: root.good; Layout.preferredWidth: 160 }
                                        }
                                    }
                                    Label { anchors.centerIn: parent; visible: parent.count === 0; text: "No learned addresses"; color: root.textMuted }
                                }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 8
                            color: root.panel
                            border.color: root.border
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                Label { text: "PORT COUNTERS"; color: root.textMuted; font.bold: true }
                                RowLayout {
                                    Label { text: "PORT"; color: root.textMuted; Layout.fillWidth: true }
                                    Label { text: "RX"; color: root.textMuted; Layout.preferredWidth: 90 }
                                    Label { text: "TX"; color: root.textMuted; Layout.preferredWidth: 90 }
                                    Label { text: "DROP"; color: root.textMuted; Layout.preferredWidth: 90 }
                                }
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: wirelab.portStates
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: ListView.view.width
                                        height: 36
                                        color: index % 2 ? root.panelRaised : "transparent"
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 8
                                            anchors.rightMargin: 8
                                            Label { text: modelData.id; Layout.fillWidth: true }
                                            Label { text: modelData.received; Layout.preferredWidth: 90 }
                                            Label { text: modelData.forwarded; color: root.good; Layout.preferredWidth: 90 }
                                            Label { text: modelData.dropped; color: root.danger; Layout.preferredWidth: 90 }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 8
                        color: root.panel
                        border.color: root.border
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            Label { text: "PACKET ANALYSIS · MOST RECENT BATCH"; color: root.textMuted; font.bold: true }
                            RowLayout {
                                Label { text: "INGRESS"; color: root.textMuted; Layout.preferredWidth: 100 }
                                Label { text: "SOURCE MAC"; color: root.textMuted; Layout.preferredWidth: 150 }
                                Label { text: "DESTINATION MAC"; color: root.textMuted; Layout.preferredWidth: 150 }
                                Label { text: "SOURCE IP"; color: root.textMuted; Layout.preferredWidth: 120 }
                                Label { text: "DESTINATION IP"; color: root.textMuted; Layout.preferredWidth: 120 }
                                Label { text: "CLASSIFICATION"; color: root.textMuted; Layout.fillWidth: true }
                                Label { text: "VALIDITY"; color: root.textMuted; Layout.preferredWidth: 160 }
                            }
                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: wirelab.packetRows
                                delegate: Rectangle {
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 34
                                    color: index % 2 ? root.panelRaised : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        anchors.rightMargin: 6
                                        Label { text: modelData.ingress; Layout.preferredWidth: 100 }
                                        Label { text: modelData.source; font.family: "monospace"; Layout.preferredWidth: 150 }
                                        Label { text: modelData.destination; font.family: "monospace"; Layout.preferredWidth: 150 }
                                        Label { text: modelData.sourceIp; Layout.preferredWidth: 120 }
                                        Label { text: modelData.destinationIp; Layout.preferredWidth: 120 }
                                        Label { text: modelData.classification; color: modelData.classification === "Malformed" ? root.danger : root.accent; Layout.fillWidth: true }
                                        Label { text: modelData.validity; Layout.preferredWidth: 160 }
                                    }
                                }
                                Label { anchors.centerIn: parent; visible: parent.count === 0; text: "Start traffic to inspect parsed packets"; color: root.textMuted }
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 210
                        radius: 8
                        color: root.panel
                        border.color: root.border
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            Label { text: "ACTIVE ANOMALIES"; color: root.textMuted; font.bold: true }
                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: wirelab.anomalyRows
                                delegate: Rectangle {
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 38
                                    color: "#321d2a"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        Label { text: modelData.type; color: root.danger; font.bold: true; Layout.preferredWidth: 220 }
                                        Label { text: modelData.source + " · " + modelData.sourceIp; Layout.fillWidth: true }
                                        Label { text: modelData.observed + " observed / " + modelData.threshold + " threshold"; color: root.warning }
                                    }
                                }
                                Label { anchors.centerIn: parent; visible: parent.count === 0; text: "No active anomaly in the latest analysis window"; color: root.good }
                            }
                        }
                    }
                }
            }

            Item {
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14
                    Rectangle {
                        Layout.preferredWidth: 390
                        Layout.fillHeight: true
                        radius: 8
                        color: root.panel
                        border.color: root.border
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            Label { text: "FAULT INJECTION"; color: root.textMuted; font.bold: true }
                            Label {
                                text: wirelab.selectedId === "" ? "Select a host or link on the Topology tab" : wirelab.selectedId
                                color: wirelab.selectedId === "" ? root.textMuted : root.accent
                                font.pixelSize: 18
                                font.bold: true
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }
                            Label { text: wirelab.selectedSummary; color: root.textMuted; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            RowLayout {
                                Label { text: "Extra latency"; Layout.fillWidth: true }
                                SpinBox { id: faultLatency; from: 0; to: 60000; value: 100; editable: true }
                                Label { text: "ms"; color: root.textMuted }
                            }
                            RowLayout {
                                Label { text: "Packet loss"; Layout.fillWidth: true }
                                SpinBox {
                                    id: faultLoss
                                    from: 0
                                    to: 10000
                                    value: 0
                                    editable: true
                                    textFromValue: function(value) { return (value / 100).toFixed(2) }
                                    valueFromText: function(text) { return Math.round(parseFloat(text) * 100) }
                                }
                                Label { text: "%"; color: root.textMuted }
                            }
                            CheckBox { id: faultBlackhole; text: "Blackhole all traffic" }
                            Button {
                                text: "Apply to selection"
                                highlighted: true
                                enabled: wirelab.selectedType !== ""
                                Layout.fillWidth: true
                                onClicked: wirelab.applySelectedFault(faultLatency.value, faultLoss.value / 100,
                                                                      faultBlackhole.checked)
                            }
                            Item { Layout.fillHeight: true }
                            Label {
                                text: "Fault decisions are evaluated for every generated frame before packet analysis. Link base latency remains active in addition to injected latency."
                                color: root.textMuted
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 8
                        color: root.panel
                        border.color: root.border
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            Label { text: "ACTIVE FAULTS"; color: root.textMuted; font.bold: true }
                            RowLayout {
                                Label { text: "TARGET"; color: root.textMuted; Layout.fillWidth: true }
                                Label { text: "TYPE"; color: root.textMuted; Layout.preferredWidth: 100 }
                                Label { text: "LATENCY"; color: root.textMuted; Layout.preferredWidth: 110 }
                                Label { text: "LOSS"; color: root.textMuted; Layout.preferredWidth: 100 }
                                Item { Layout.preferredWidth: 80 }
                            }
                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: wirelab.activeFaults
                                delegate: Rectangle {
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 48
                                    color: index % 2 ? root.panelRaised : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        Label { text: modelData.target; Layout.fillWidth: true; font.bold: true }
                                        Label { text: modelData.kind; Layout.preferredWidth: 100 }
                                        Label { text: modelData.latencyMs + " ms"; color: root.warning; Layout.preferredWidth: 110 }
                                        Label { text: modelData.blackhole ? "BLACKHOLE" : modelData.lossPercent.toFixed(2) + "%"; color: root.danger; Layout.preferredWidth: 100 }
                                        Button {
                                            text: "Clear"
                                            Layout.preferredWidth: 80
                                            onClicked: wirelab.clearFault(modelData.first, modelData.second)
                                        }
                                    }
                                }
                                Label { anchors.centerIn: parent; visible: parent.count === 0; text: "No active faults"; color: root.good }
                            }
                        }
                    }
                }
            }
        }
    }
}
