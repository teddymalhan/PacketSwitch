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
    color: root.windowBg

    // ---- macOS system palette, adaptive to light/dark appearance ----
    readonly property bool dark: Qt.styleHints.colorScheme === Qt.Dark
    readonly property color windowBg: dark ? "#1e1e1e" : "#ececec"
    readonly property color sidebarBg: dark ? "#282828" : "#e8e8e8"
    readonly property color cardBg: dark ? "#2d2d2d" : "#ffffff"
    readonly property color cardAltBg: dark ? "#3a3a3c" : "#f2f2f7"
    readonly property color separator: dark ? "#38383a" : "#c6c6c8"
    readonly property color textPrimary: dark ? "#ffffff" : "#000000"
    readonly property color textSecondary: dark ? "#98989d" : "#6e6e73"
    readonly property color accent: dark ? "#0a84ff" : "#007aff"
    readonly property color good: dark ? "#30d158" : "#34c759"
    readonly property color warning: dark ? "#ffd60a" : "#ff9f0a"
    readonly property color danger: dark ? "#ff453a" : "#ff3b30"
    readonly property color sidebarHoverBg: dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.05)
    readonly property color sidebarSelectedBg: dark ? "#48484a" : "#d1d1d6"

    function localFilePath(url) {
        const text = url.toString()
        return text.startsWith("file://") ? decodeURIComponent(text.slice(7)) : text
    }

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

    function microsText(nanoseconds) {
        return (nanoseconds / 1000).toFixed(1)
    }

    function provenanceValue(field) {
        const value = wirelab.reportProvenance[field]
        if (value === undefined)
            return "—"
        return Array.isArray(value) ? value.join(", ") : String(value)
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
        onAccepted: wirelab.openTopology(root.localFilePath(selectedFile))
    }

    FileDialog {
        id: saveDialog
        title: "Save topology"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "yaml"
        nameFilters: ["Topology YAML (*.yaml *.yml)"]
        onAccepted: wirelab.saveTopology(root.localFilePath(selectedFile))
    }

    FileDialog {
        id: exportDialog
        title: "Export benchmark report"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["JSON report (*.json)"]
        onAccepted: wirelab.exportReport(root.localFilePath(selectedFile))
    }

    Timer {
        interval: 500
        repeat: true
        running: wirelab.trafficRunning
        onTriggered: wirelab.runTrafficStep()
    }

    Timer {
        interval: 16
        repeat: true
        running: wirelab.reportRunning
        onTriggered: wirelab.runReportStep()
    }

    // System menu bar (renders natively on macOS)
    menuBar: MenuBar {
        Menu {
            title: "File"
            Action { text: "Open Topology…"; shortcut: "Cmd+O"; onTriggered: openDialog.open() }
            Action {
                text: "Save Topology…"
                shortcut: "Cmd+S"
                enabled: wirelab.hasTopology
                onTriggered: saveDialog.open()
            }
            MenuSeparator {}
            Action { text: "Quit WireLab"; shortcut: "Cmd+Q"; onTriggered: Qt.quit() }
        }
        Menu {
            title: "Edit"
            Action { text: "Cut"; shortcut: "Cmd+X"; enabled: false }
            Action { text: "Copy"; shortcut: "Cmd+C"; enabled: false }
            Action { text: "Paste"; shortcut: "Cmd+V"; enabled: false }
        }
    }

    // Unified toolbar
    header: ToolBar {
        height: 50
        background: Rectangle {
            color: root.windowBg
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: root.separator
            }
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 14
            spacing: 10
            Button { text: "Open"; onClicked: openDialog.open() }
            Button {
                text: "Save"
                enabled: wirelab.hasTopology
                onClicked: saveDialog.open()
            }
            Rectangle { width: 1; height: 22; color: root.separator }
            Label {
                text: wirelab.topologyName === "" ? "No topology loaded" : wirelab.topologyName
                color: root.textPrimary
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Rectangle {
                implicitWidth: livePillText.implicitWidth + 28
                implicitHeight: 26
                radius: 13
                color: wirelab.trafficRunning
                       ? Qt.rgba(root.good.r, root.good.g, root.good.b, dark ? 0.22 : 0.14)
                       : Qt.rgba(root.textSecondary.r, root.textSecondary.g, root.textSecondary.b, 0.12)
                Label {
                    id: livePillText
                    anchors.centerIn: parent
                    text: wirelab.trafficRunning ? "● LIVE · " + wirelab.activeBackend : "○ IDLE"
                    color: wirelab.trafficRunning ? root.good : root.textSecondary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 28
        color: root.windowBg
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: root.separator
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            Label {
                text: wirelab.statusMessage === "" ? "Open scenarios/security-lab.yaml to begin." : wirelab.statusMessage
                color: root.textSecondary
                font.pixelSize: 11
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Label {
                text: wirelab.topologyNodes.length + " nodes · " + wirelab.topologyLinks.length + " links · "
                      + wirelab.activeFaults.length + " faults"
                color: root.textSecondary
                font.pixelSize: 11
            }
        }
    }

    // ---- Sidebar vector icons ----
    component SidebarIcon: Canvas {
        property string kind: "dashboard"
        property color iconColor: root.textPrimary
        width: 16
        height: 16
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = iconColor
            ctx.fillStyle = iconColor
            ctx.lineWidth = 1.6
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            const dot = (x, y, r) => { ctx.beginPath(); ctx.arc(x, y, r, 0, Math.PI * 2); ctx.stroke() }
            switch (kind) {
            case "dashboard":
                ctx.beginPath(); ctx.arc(8, 9.5, 6, Math.PI, 0); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(8, 9.5); ctx.lineTo(11, 5.5); ctx.stroke()
                ctx.beginPath(); ctx.arc(8, 9.5, 1.5, 0, Math.PI * 2); ctx.fill()
                break
            case "topology":
                dot(4, 12, 2); dot(12, 12, 2); dot(8, 4, 2)
                ctx.beginPath(); ctx.moveTo(4, 12); ctx.lineTo(12, 12)
                ctx.moveTo(8, 4); ctx.lineTo(4, 12); ctx.moveTo(8, 4); ctx.lineTo(12, 12); ctx.stroke()
                break
            case "traffic":
                ctx.beginPath(); ctx.moveTo(2.5, 8); ctx.lineTo(13.5, 8)
                ctx.moveTo(13.5, 8); ctx.lineTo(10.5, 5); ctx.moveTo(13.5, 8); ctx.lineTo(10.5, 11); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(8, 2.5); ctx.lineTo(8, 13.5)
                ctx.moveTo(8, 2.5); ctx.lineTo(5, 5.5); ctx.moveTo(8, 2.5); ctx.lineTo(11, 5.5); ctx.stroke()
                break
            case "security":
                ctx.beginPath(); ctx.moveTo(8, 1.5); ctx.lineTo(14.5, 4)
                ctx.lineTo(14.5, 8); ctx.quadraticCurveTo(14.5, 12.5, 8, 14.5)
                ctx.quadraticCurveTo(1.5, 12.5, 1.5, 8); ctx.lineTo(1.5, 4); ctx.closePath(); ctx.stroke()
                break
            case "faults":
                ctx.beginPath(); ctx.moveTo(8, 2); ctx.lineTo(15, 14); ctx.lineTo(1, 14); ctx.closePath(); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(8, 6); ctx.lineTo(8, 10.5); ctx.stroke()
                ctx.beginPath(); ctx.arc(8, 12.5, 1, 0, Math.PI * 2); ctx.fill()
                break
            case "reports":
                ctx.beginPath(); ctx.moveTo(2, 14); ctx.lineTo(14.5, 14); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(4.5, 14); ctx.lineTo(4.5, 9)
                ctx.moveTo(8, 14); ctx.lineTo(8, 4.5)
                ctx.moveTo(11.5, 14); ctx.lineTo(11.5, 7); ctx.stroke()
                break
            }
        }
        onIconColorChanged: requestPaint()
        onKindChanged: requestPaint()
    }

    // ---- Content: sidebar + pages ----
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Finder-style sidebar
        Rectangle {
            id: sidebar
            Layout.preferredWidth: 216
            Layout.fillHeight: true
            color: root.sidebarBg
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: root.separator
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 12
                anchors.bottomMargin: 10
                spacing: 4
                Label {
                    text: "WIRELAB"
                    color: root.textSecondary
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.4
                    Layout.leftMargin: 18
                    Layout.bottomMargin: 4
                }
                ListView {
                    id: navList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: ListModel {
                        ListElement { label: "Dashboard"; page: 0; icon: "dashboard" }
                        ListElement { label: "Topology"; page: 1; icon: "topology" }
                        ListElement { label: "Traffic"; page: 2; icon: "traffic" }
                        ListElement { label: "Packets & Security"; page: 3; icon: "security" }
                        ListElement { label: "Faults"; page: 4; icon: "faults" }
                        ListElement { label: "Policies"; page: 5; icon: "security" }
                        ListElement { label: "Reports"; page: 6; icon: "reports" }
                    }
                    delegate: Item {
                        required property string label
                        required property int page
                        required property string icon
                        width: ListView.view.width
                        height: 30
                        Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            radius: 6
                            color: navList.currentIndex === page
                                   ? root.sidebarSelectedBg
                                   : (sidebarItemHover.hovered ? root.sidebarHoverBg : "transparent")
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 8
                                SidebarIcon { kind: icon; iconColor: navList.currentIndex === page ? root.textPrimary : root.textSecondary }
                                Label {
                                    text: label
                                    color: navList.currentIndex === page ? root.textPrimary : root.textSecondary
                                    font.pixelSize: 13
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        MouseArea {
                            id: sidebarItemHover
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                navList.currentIndex = page
                                stack.currentIndex = page
                            }
                        }
                    }
                }
                Label {
                    text: "WireLab 0.1.0"
                    color: root.textSecondary
                    font.pixelSize: 11
                    Layout.leftMargin: 18
                }
            }
        }

        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true

            // ============ DASHBOARD ============
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14
                    Label { text: "Dashboard"; color: root.textPrimary; font.pixelSize: 26; font.weight: Font.Bold }
                    GridLayout {
                        Layout.fillWidth: true
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
                                Layout.preferredHeight: 96
                                radius: 10
                                color: root.cardBg
                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 6
                                    Label { text: modelData.label; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                                    Label { text: modelData.value; color: modelData.color; font.pixelSize: 26; font.weight: Font.Bold }
                                }
                            }
                        }

                        Rectangle {
                            Layout.columnSpan: 3
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 10
                            color: root.cardBg
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                Label { text: "LIVE TELEMETRY · LAST 30 SECONDS"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                                Canvas {
                                    id: dashboardChart
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    onPaint: {
                                        const ctx = getContext("2d")
                                        ctx.reset()
                                        ctx.strokeStyle = root.separator
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
                                        plot("latencyMs", root.good, 100)
                                        plot("lossPercent", root.danger, 100)
                                    }
                                    Connections {
                                        target: wirelab
                                        function onTelemetryChanged() { dashboardChart.requestPaint() }
                                    }
                                }
                                RowLayout {
                                    spacing: 14
                                    Rectangle { width: 14; height: 3; radius: 1.5; color: root.accent }
                                    Label { text: "Throughput"; color: root.textSecondary; font.pixelSize: 11 }
                                    Rectangle { width: 14; height: 3; radius: 1.5; color: root.good }
                                    Label { text: "Latency (100 ms scale)"; color: root.textSecondary; font.pixelSize: 11 }
                                    Rectangle { width: 14; height: 3; radius: 1.5; color: root.danger }
                                    Label { text: "Loss (100% scale)"; color: root.textSecondary; font.pixelSize: 11 }
                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 10
                            color: root.cardBg
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                Label { text: "PORT STATE"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: wirelab.portStates
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: ListView.view.width
                                        height: 52
                                        color: index % 2 ? "transparent" : root.cardAltBg
                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 1
                                            color: root.separator
                                        }
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Label { text: modelData.id; color: root.textPrimary; font.weight: Font.DemiBold }
                                                Label { text: modelData.received + " rx · " + modelData.forwarded + " tx · " + modelData.dropped + " drop"; color: root.textSecondary; font.pixelSize: 11 }
                                            }
                                            Label { text: modelData.state; color: root.good; font.weight: Font.DemiBold; font.pixelSize: 12 }
                                        }
                                    }
                                    Label {
                                        anchors.centerIn: parent
                                        visible: parent.count === 0
                                        text: "Run traffic to populate port counters"
                                        color: root.textSecondary
                                        wrapMode: Text.Wrap
                                        width: parent.width - 24
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ============ TOPOLOGY ============
            Item {
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 14
                        Label { text: "Topology"; color: root.textPrimary; font.pixelSize: 26; font.weight: Font.Bold }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 10
                            color: root.cardBg

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
                                        ctx.lineWidth = 2.5
                                        for (let i = 0; i < wirelab.topologyLinks.length; ++i) {
                                            const link = wirelab.topologyLinks[i]
                                            const a = root.nodePoint(link.from, graphArea)
                                            const b = root.nodePoint(link.to, graphArea)
                                            const selected = wirelab.selectedType === "link"
                                                             && wirelab.selectedId.indexOf(link.from) >= 0
                                                             && wirelab.selectedId.indexOf(link.to) >= 0
                                            ctx.strokeStyle = selected ? root.accent : root.separator
                                            ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke()
                                            const mx = (a.x + b.x) / 2
                                            const my = (a.y + b.y) / 2
                                            ctx.fillStyle = root.cardBg
                                            ctx.fillRect(mx - 20, my - 10, 40, 20)
                                            ctx.fillStyle = root.textSecondary
                                            ctx.font = "11px system-ui, sans-serif"
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
                                        width: modelData.type === "switch" ? 116 : 96
                                        height: 56
                                        x: modelData.x * graphArea.width - width / 2
                                        y: modelData.y * graphArea.height - height / 2
                                        radius: modelData.type === "switch" ? 9 : 28
                                        color: wirelab.selectedType === "node" && wirelab.selectedId === modelData.id
                                               ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.18)
                                               : root.cardAltBg
                                        border.width: 1.5
                                        border.color: wirelab.selectedType === "node" && wirelab.selectedId === modelData.id ? root.accent
                                                                                                                          : modelData.type === "switch" ? root.warning : root.good
                                        z: 2
                                        Column {
                                            anchors.centerIn: parent
                                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.id; color: root.textPrimary; font.weight: Font.DemiBold }
                                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.type.toUpperCase(); color: root.textSecondary; font.pixelSize: 10 }
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
                                    color: root.textSecondary
                                    font.pixelSize: 15
                                }
                            }
                        }
                    }

                    // Inspector
                    Rectangle {
                        Layout.preferredWidth: 320
                        Layout.fillHeight: true
                        radius: 10
                        color: root.cardBg
                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 16
                            contentWidth: availableWidth
                            ColumnLayout {
                                width: parent.width
                                spacing: 12
                                Label { text: "INSPECTOR"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                                Label {
                                    text: wirelab.selectedId === "" ? "Nothing selected" : wirelab.selectedId
                                    color: root.textPrimary
                                    font.pixelSize: 20
                                    font.weight: Font.Bold
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                }
                                Label { text: wirelab.selectedSummary; color: root.textSecondary; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                Button {
                                    text: "Remove selected"
                                    enabled: wirelab.selectedType !== ""
                                    Layout.fillWidth: true
                                    onClicked: wirelab.removeSelected()
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: root.separator }
                                Label { text: "ADD NODE"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
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
                                Rectangle { Layout.fillWidth: true; height: 1; color: root.separator }
                                Label { text: "ADD LINK"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
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
                                    Label { text: "Latency"; color: root.textPrimary }
                                    SpinBox { id: linkLatency; from: 0; to: 60000; value: 1; Layout.fillWidth: true }
                                    Label { text: "ms"; color: root.textSecondary }
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

            // ============ TRAFFIC ============
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14
                    Label { text: "Traffic"; color: root.textPrimary; font.pixelSize: 26; font.weight: Font.Bold }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 124
                        radius: 10
                        color: root.cardBg
                        GridLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            columns: 7
                            Label { text: "Scenario"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            Label { text: "Packets / 500 ms"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            Label { text: "Frame bytes"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            Label { text: "Seed"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            Label { text: "Analyzer"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            Item { Layout.fillWidth: true }
                            Item {}
                            ComboBox { id: scenario; model: ["mixed-traffic", "known-unicast", "broadcast", "unknown-unicast"] }
                            SpinBox { id: packetRate; from: 1; to: 100000; value: 512; editable: true }
                            SpinBox { id: frameSize; from: 14; to: 1518; value: 128; editable: true }
                            SpinBox { id: trafficSeed; from: 1; to: 2147483647; value: 42; editable: true }
                            ComboBox {
                                id: backend
                                model: wirelab.availableBackends
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
                                font.pixelSize: 12
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
                            radius: 10
                            color: root.cardBg
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                Label { text: "FORWARDING / MAC TABLE"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                                RowLayout {
                                    Label { text: "MAC ADDRESS"; color: root.textSecondary; font.pixelSize: 11; Layout.fillWidth: true }
                                    Label { text: "LEARNED PORT"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 150 }
                                }
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: wirelab.macTable
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: ListView.view.width
                                        height: 34
                                        color: index % 2 ? "transparent" : root.cardAltBg
                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 1
                                            color: root.separator
                                        }
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 8
                                            anchors.rightMargin: 8
                                            Label { text: modelData.mac; font.family: "Menlo"; font.pixelSize: 12; Layout.fillWidth: true }
                                            Label { text: modelData.port; color: root.good; Layout.preferredWidth: 150 }
                                        }
                                    }
                                    Label { anchors.centerIn: parent; visible: parent.count === 0; text: "No learned addresses"; color: root.textSecondary }
                                }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 10
                            color: root.cardBg
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                Label { text: "PORT COUNTERS"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                                RowLayout {
                                    Label { text: "PORT"; color: root.textSecondary; font.pixelSize: 11; Layout.fillWidth: true }
                                    Label { text: "RX"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 84 }
                                    Label { text: "TX"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 84 }
                                    Label { text: "DROP"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 84 }
                                }
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: wirelab.portStates
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: ListView.view.width
                                        height: 34
                                        color: index % 2 ? "transparent" : root.cardAltBg
                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 1
                                            color: root.separator
                                        }
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 8
                                            anchors.rightMargin: 8
                                            Label { text: modelData.id; Layout.fillWidth: true }
                                            Label { text: modelData.received; Layout.preferredWidth: 84 }
                                            Label { text: modelData.forwarded; color: root.good; Layout.preferredWidth: 84 }
                                            Label { text: modelData.dropped; color: root.danger; Layout.preferredWidth: 84 }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ============ PACKETS & SECURITY ============
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14
                    Label { text: "Packets & Security"; color: root.textPrimary; font.pixelSize: 26; font.weight: Font.Bold }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 10
                        color: root.cardBg
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            Label { text: "PACKET ANALYSIS · MOST RECENT BATCH"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            RowLayout {
                                Label { text: "INGRESS"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 96 }
                                Label { text: "SOURCE MAC"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 150 }
                                Label { text: "DESTINATION MAC"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 150 }
                                Label { text: "SOURCE IP"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 116 }
                                Label { text: "DESTINATION IP"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 116 }
                                Label { text: "CLASSIFICATION"; color: root.textSecondary; font.pixelSize: 11; Layout.fillWidth: true }
                                Label { text: "VALIDITY"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 150 }
                            }
                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: wirelab.packetRows
                                delegate: Rectangle {
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 32
                                    color: index % 2 ? "transparent" : root.cardAltBg
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 1
                                        color: root.separator
                                    }
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        anchors.rightMargin: 6
                                        Label { text: modelData.ingress; Layout.preferredWidth: 96 }
                                        Label { text: modelData.source; font.family: "Menlo"; font.pixelSize: 11; Layout.preferredWidth: 150 }
                                        Label { text: modelData.destination; font.family: "Menlo"; font.pixelSize: 11; Layout.preferredWidth: 150 }
                                        Label { text: modelData.sourceIp; Layout.preferredWidth: 116 }
                                        Label { text: modelData.destinationIp; Layout.preferredWidth: 116 }
                                        Label { text: modelData.classification; color: modelData.classification === "Malformed" ? root.danger : root.accent; Layout.fillWidth: true }
                                        Label { text: modelData.validity; Layout.preferredWidth: 150 }
                                    }
                                }
                                Label { anchors.centerIn: parent; visible: parent.count === 0; text: "Start traffic to inspect parsed packets"; color: root.textSecondary }
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 190
                        radius: 10
                        color: root.cardBg
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            Label { text: "ACTIVE ANOMALIES"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: wirelab.anomalyRows
                                delegate: Rectangle {
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 38
                                    radius: 6
                                    color: Qt.rgba(root.danger.r, root.danger.g, root.danger.b, dark ? 0.16 : 0.08)
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        Label { text: modelData.type; color: root.danger; font.weight: Font.DemiBold; Layout.preferredWidth: 220 }
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

            // ============ FAULTS ============
            Item {
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 14
                        Label { text: "Faults"; color: root.textPrimary; font.pixelSize: 26; font.weight: Font.Bold }
                        Rectangle {
                            Layout.preferredWidth: 380
                            Layout.fillHeight: true
                            radius: 10
                            color: root.cardBg
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 18
                                spacing: 12
                                Label { text: "FAULT INJECTION"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                                Label {
                                    text: wirelab.selectedId === "" ? "Select a host or link on the Topology tab" : wirelab.selectedId
                                    color: wirelab.selectedId === "" ? root.textSecondary : root.accent
                                    font.pixelSize: 18
                                    font.weight: Font.Bold
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                }
                                Label { text: wirelab.selectedSummary; color: root.textSecondary; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                RowLayout {
                                    Label { text: "Extra latency"; Layout.fillWidth: true }
                                    SpinBox { id: faultLatency; from: 0; to: 60000; value: 100; editable: true }
                                    Label { text: "ms"; color: root.textSecondary }
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
                                    Label { text: "%"; color: root.textSecondary }
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
                                    color: root.textSecondary
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 10
                        color: root.cardBg
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            Label { text: "ACTIVE FAULTS"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            RowLayout {
                                Label { text: "TARGET"; color: root.textSecondary; font.pixelSize: 11; Layout.fillWidth: true }
                                Label { text: "TYPE"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 96 }
                                Label { text: "LATENCY"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 104 }
                                Label { text: "LOSS"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 96 }
                                Item { Layout.preferredWidth: 76 }
                            }
                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: wirelab.activeFaults
                                delegate: Rectangle {
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 46
                                    color: index % 2 ? "transparent" : root.cardAltBg
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 1
                                        color: root.separator
                                    }
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        Label { text: modelData.target; Layout.fillWidth: true; font.weight: Font.DemiBold }
                                        Label { text: modelData.kind; Layout.preferredWidth: 96 }
                                        Label { text: modelData.latencyMs + " ms"; color: root.warning; Layout.preferredWidth: 104 }
                                        Label { text: modelData.blackhole ? "BLACKHOLE" : modelData.lossPercent.toFixed(2) + "%"; color: root.danger; Layout.preferredWidth: 96 }
                                        Button {
                                            text: "Clear"
                                            Layout.preferredWidth: 76
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

            // ============ POLICY LAB ============
            Item {
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14
                    ColumnLayout {
                        Layout.preferredWidth: 380
                        Layout.fillHeight: true
                        spacing: 14
                        Label { text: "Policies"; color: root.textPrimary; font.pixelSize: 26; font.weight: Font.Bold }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 10
                            color: root.cardBg
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 18
                                spacing: 12
                                Label { text: "NEW RULE"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                                TextField {
                                    id: policyName
                                    Layout.fillWidth: true
                                    placeholderText: "Rule name"
                                }
                                RowLayout {
                                    Label { text: "When"; color: root.textSecondary; Layout.preferredWidth: 48 }
                                    ComboBox {
                                        id: policyAnomaly
                                        Layout.fillWidth: true
                                        model: wirelab.anomalyTypeNames
                                    }
                                }
                                RowLayout {
                                    Label { text: "Then"; color: root.textSecondary; Layout.preferredWidth: 48 }
                                    ComboBox {
                                        id: policyAction
                                        Layout.fillWidth: true
                                        model: wirelab.policyActionNames
                                    }
                                }
                                RowLayout {
                                    visible: policyAction.currentText === "Rate limit"
                                    Label { text: "Limit"; color: root.textSecondary; Layout.preferredWidth: 48 }
                                    SpinBox {
                                        id: policyRate
                                        from: 0
                                        to: 10000000
                                        stepSize: 1000
                                        value: 50000
                                        editable: true
                                        Layout.fillWidth: true
                                    }
                                    Label { text: "pps"; color: root.textSecondary }
                                }
                                Button {
                                    text: "Add policy"
                                    highlighted: true
                                    Layout.fillWidth: true
                                    enabled: policyName.text.trim() !== ""
                                    onClicked: {
                                        wirelab.addPolicy(policyName.text, policyAnomaly.currentText,
                                                          policyAction.currentText,
                                                          policyAction.currentText === "Rate limit" ? policyRate.value : 0)
                                        policyName.text = ""
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: root.separator }
                                Label { text: "RULES"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: wirelab.policyRules
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: ListView.view.width
                                        height: 62
                                        color: index % 2 ? "transparent" : root.cardAltBg
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 8
                                            anchors.rightMargin: 8
                                            spacing: 8
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Label {
                                                    text: modelData.name
                                                    color: modelData.enabled ? root.textPrimary : root.textSecondary
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }
                                                Label {
                                                    text: modelData.anomaly + " → " + modelData.action +
                                                          (modelData.rateLimit > 0 ? " (" + modelData.rateLimit + " pps)" : "")
                                                    color: root.textSecondary
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }
                                                Label {
                                                    text: modelData.hits + " hits"
                                                    color: modelData.hits > 0 ? root.warning : root.textSecondary
                                                    font.pixelSize: 11
                                                }
                                            }
                                            Switch {
                                                checked: modelData.enabled
                                                onToggled: wirelab.setPolicyEnabled(modelData.name, checked)
                                            }
                                            Button {
                                                text: "✕"
                                                Layout.preferredWidth: 34
                                                onClicked: wirelab.removePolicy(modelData.name)
                                            }
                                        }
                                    }
                                    Label {
                                        anchors.centerIn: parent
                                        visible: parent.count === 0
                                        text: "No policies defined"
                                        color: root.textSecondary
                                    }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 14
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 190
                            radius: 10
                            color: root.cardBg
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 6
                                Label { text: "ENFORCED PORTS"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: wirelab.enforcedPorts
                                    delegate: RowLayout {
                                        required property var modelData
                                        width: ListView.view.width
                                        spacing: 8
                                        Label { text: modelData.port; font.weight: Font.DemiBold; color: root.danger; Layout.preferredWidth: 120 }
                                        Label { text: modelData.summary; color: root.textPrimary; Layout.fillWidth: true; elide: Text.ElideRight }
                                        Label { text: modelData.rule; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 150; elide: Text.ElideRight }
                                        Button {
                                            text: "Release"
                                            Layout.preferredWidth: 88
                                            onClicked: wirelab.releaseEnforcement(modelData.port)
                                        }
                                    }
                                    Label {
                                        anchors.centerIn: parent
                                        visible: parent.count === 0
                                        text: "No port is under enforcement"
                                        color: root.good
                                    }
                                }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 10
                            color: root.cardBg
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 6
                                Label { text: "ENFORCEMENT LOG"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                                RowLayout {
                                    Label { text: "TICK"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                    Label { text: "RULE"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 150 }
                                    Label { text: "ANOMALY"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 150 }
                                    Label { text: "PORT"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 110 }
                                    Label { text: "OUTCOME"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 110 }
                                    Label { text: "DETAIL"; color: root.textSecondary; font.pixelSize: 11; Layout.fillWidth: true }
                                }
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: wirelab.policyActions
                                    verticalLayoutDirection: ListView.BottomToTop
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: ListView.view.width
                                        height: 30
                                        color: index % 2 ? "transparent" : root.cardAltBg
                                        RowLayout {
                                            anchors.fill: parent
                                            Label { text: modelData.sequence; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 56 }
                                            Label { text: modelData.rule; font.pixelSize: 11; Layout.preferredWidth: 150; elide: Text.ElideRight }
                                            Label { text: modelData.anomaly; font.pixelSize: 11; Layout.preferredWidth: 150; elide: Text.ElideRight }
                                            Label { text: modelData.port === "" ? "—" : modelData.port; font.pixelSize: 11; Layout.preferredWidth: 110 }
                                            Label {
                                                text: modelData.outcome
                                                font.pixelSize: 11
                                                Layout.preferredWidth: 110
                                                color: modelData.outcome === "applied" || modelData.outcome === "extended"
                                                       ? root.danger
                                                       : (modelData.outcome === "released" ? root.good : root.textSecondary)
                                            }
                                            Label { text: modelData.detail; color: root.textSecondary; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                        }
                                    }
                                    Label {
                                        anchors.centerIn: parent
                                        visible: parent.count === 0
                                        text: "Enforcement actions appear here while traffic runs"
                                        color: root.textSecondary
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ============ REPORTS ============
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14
                    Label { text: "Reports"; color: root.textPrimary; font.pixelSize: 26; font.weight: Font.Bold }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 150
                        radius: 10
                        color: root.cardBg
                        GridLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            columns: 6
                            Label { text: "Scenario"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            Label { text: "Packets"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            Label { text: "Batch size"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            Label { text: "Frame bytes"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            Label { text: "Seed"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            Item { Layout.fillWidth: true }
                            ComboBox { id: reportScenario; model: wirelab.reportScenarioNames }
                            SpinBox { id: reportPackets; from: 1; to: 10000000; stepSize: 1000; value: 50000; editable: true }
                            SpinBox { id: reportBatch; from: 1; to: 8192; stepSize: 32; value: 256; editable: true }
                            SpinBox { id: reportFrame; from: 14; to: 9000; value: 128; editable: true }
                            SpinBox { id: reportSeed; from: 0; to: 2147483647; value: 42; editable: true }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Button {
                                    text: "Run report"
                                    highlighted: true
                                    enabled: !wirelab.reportRunning
                                    onClicked: wirelab.runBenchmarkReport(reportScenario.currentText, reportPackets.value,
                                                                          reportBatch.value, reportFrame.value, reportSeed.value)
                                }
                                Button {
                                    text: "Export…"
                                    enabled: !wirelab.reportRunning && wirelab.reportRows.length > 0
                                    onClicked: exportDialog.open()
                                }
                                Item { Layout.fillWidth: true }
                            }
                            RowLayout {
                                Layout.columnSpan: 6
                                Layout.fillWidth: true
                                spacing: 12
                                ProgressBar {
                                    Layout.preferredWidth: 220
                                    from: 0
                                    to: 1
                                    value: wirelab.reportProgress
                                }
                                Label {
                                    text: wirelab.reportStage === "" ? "No report has been run yet" : wirelab.reportStage
                                    color: root.textPrimary
                                    font.pixelSize: 12
                                }
                                Item { Layout.fillWidth: true }
                                Label {
                                    text: wirelab.reportExportPath
                                    color: root.textSecondary
                                    font.pixelSize: 11
                                    elide: Text.ElideMiddle
                                    Layout.maximumWidth: 360
                                }
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
                            radius: 10
                            color: root.cardBg
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                Label { text: "CPU / GPU COMPARISON"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                                RowLayout {
                                    Label { text: "BACKEND"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 84 }
                                    Label { text: "MPKT/S"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 84 }
                                    Label { text: "GOODPUT Mbit/s"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 118 }
                                    Label { text: "P50 µs"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 78 }
                                    Label { text: "P95 µs"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 78 }
                                    Label { text: "P99 µs"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 78 }
                                    Label { text: "TRANSFER µs"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 96 }
                                    Label { text: "KERNEL µs"; color: root.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 88 }
                                    Label { text: "VS CPU"; color: root.textSecondary; font.pixelSize: 11; Layout.fillWidth: true }
                                }
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: wirelab.reportRows
                                    delegate: Rectangle {
                                        required property var modelData
                                        required property int index
                                        width: ListView.view.width
                                        height: 34
                                        color: index % 2 ? "transparent" : root.cardAltBg
                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 1
                                            color: root.separator
                                        }
                                        RowLayout {
                                            anchors.fill: parent
                                            Label { text: modelData.backend; font.weight: Font.DemiBold; Layout.preferredWidth: 84 }
                                            Label { text: (modelData.packetsPerSecond / 1000000).toFixed(2); font.family: "Menlo"; font.pixelSize: 12; Layout.preferredWidth: 84 }
                                            Label { text: (modelData.goodputBitsPerSecond / 1000000).toFixed(1); font.family: "Menlo"; font.pixelSize: 12; Layout.preferredWidth: 118 }
                                            Label { text: root.microsText(modelData.latencyP50Ns); font.family: "Menlo"; font.pixelSize: 12; Layout.preferredWidth: 78 }
                                            Label { text: root.microsText(modelData.latencyP95Ns); font.family: "Menlo"; font.pixelSize: 12; Layout.preferredWidth: 78 }
                                            Label { text: root.microsText(modelData.latencyP99Ns); font.family: "Menlo"; font.pixelSize: 12; Layout.preferredWidth: 78 }
                                            Label {
                                                text: root.microsText(modelData.hostToDeviceNs + modelData.deviceToHostNs)
                                                font.family: "Menlo"
                                                font.pixelSize: 12
                                                color: root.textSecondary
                                                Layout.preferredWidth: 96
                                            }
                                            Label {
                                                text: root.microsText(modelData.kernelNs)
                                                font.family: "Menlo"
                                                font.pixelSize: 12
                                                color: root.textSecondary
                                                Layout.preferredWidth: 88
                                            }
                                            Label {
                                                text: modelData.speedup.toFixed(2) + "×"
                                                font.weight: Font.DemiBold
                                                color: modelData.speedup >= 1 ? root.good : root.warning
                                                Layout.fillWidth: true
                                            }
                                        }
                                    }
                                    Label {
                                        anchors.centerIn: parent
                                        visible: parent.count === 0
                                        text: "Run a report to compare the analyzer backends"
                                        color: root.textSecondary
                                    }
                                }
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 340
                            Layout.fillHeight: true
                            radius: 10
                            color: root.cardBg
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 8
                                Label { text: "CONFIGURATION PROVENANCE"; color: root.textSecondary; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                                Repeater {
                                    model: [
                                        { label: "Scenario", field: "scenario" },
                                        { label: "Seed", field: "seed" },
                                        { label: "Packets", field: "packets" },
                                        { label: "Batch size", field: "batchSize" },
                                        { label: "Frame bytes", field: "frameSize" },
                                        { label: "Hosts", field: "hostCount" },
                                        { label: "Generator", field: "generator" },
                                        { label: "WireLab", field: "version" },
                                        { label: "Build", field: "buildType" },
                                        { label: "Compiled in", field: "backendsCompiledIn" },
                                        { label: "On this machine", field: "backendsPresent" },
                                        { label: "Generated", field: "generatedAt" }
                                    ]
                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Label {
                                            text: modelData.label
                                            color: root.textSecondary
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 118
                                        }
                                        Label {
                                            text: root.provenanceValue(modelData.field)
                                            color: root.textPrimary
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                                Item { Layout.fillHeight: true }
                            }
                        }
                    }
                }
            }
        }
    }
}
