/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import QtCore

import org.kde.kirigami as Kirigami
import org.ctoolbox.cstitch

Kirigami.ApplicationWindow {
    id: window

    readonly property var controller: app.controller
    readonly property var rotations: [0, 90, 180, 270]
    readonly property var stereoEyeValues: ["both", "left", "right"]
    readonly property bool leftInputActive: !controller.stereo || controller.stereoEye !== "right"
    readonly property bool rightInputActive: controller.stereo && controller.stereoEye !== "left"

    title: qsTr("C-Stitch %1").arg(app.version())
    visible: true
    color: Kirigami.Theme.alternateBackgroundColor
    minimumWidth: 1120
    minimumHeight: 720
    width: 1360
    height: 1050

    Kirigami.Theme.inherit: false
    Kirigami.Theme.colorSet: Kirigami.Theme.Window

    function selectRotationIndex(value) {
        const index = rotations.indexOf(value)
        return Math.max(0, index)
    }

    function stereoEyeLabel(value) {
        switch (value) {
        case "left":
            return qsTr("Left only")
        case "right":
            return qsTr("Right only")
        default:
            return qsTr("Both eyes")
        }
    }

    FileDialog {
        id: faceDialog
        property string target
        parentWindow: window
        title: qsTr("Choose cubemap face")
        nameFilters: [ qsTr("Images (*.png *.jpg *.jpeg *.tga *.bmp)"), qsTr("All files (*)") ]
        onAccepted: controller[target] = app.urlToPath(selectedFile)
    }

    FolderDialog {
        id: outputDirectoryDialog
        property string target
        parentWindow: window
        title: qsTr("Choose output directory")
        onAccepted: controller[target] = app.urlToPath(selectedFolder)
    }

    function chooseFacePath(target, currentPath) {
        faceDialog.target = target
        faceDialog.currentFolder = currentPath
        faceDialog.open()
    }

    function chooseOutputDirectory(target, currentPath) {
        outputDirectoryDialog.target = target
        outputDirectoryDialog.currentFolder = currentPath
        outputDirectoryDialog.open()
    }

    FileDialog {
        id: leftDialog
        parentWindow: window
        title: qsTr("Choose left/input media")
        nameFilters: [ qsTr("Images (*.png *.jpg *.jpeg *.tga)"), qsTr("All files (*)") ]
    }

    FileDialog {
        id: saveQueueDialog
        parentWindow: window
        title: qsTr("Save internal queue")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [ qsTr("Queue files (*.json)"), qsTr("All files (*)") ]
        onAccepted: controller.saveInternalQueue(app.urlToPath(selectedFile))
    }

    FileDialog {
        id: loadQueueDialog
        parentWindow: window
        title: qsTr("Load internal queue")
        fileMode: FileDialog.OpenFile
        nameFilters: [ qsTr("Queue files (*.json)"), qsTr("All files (*)") ]
        onAccepted: {
            if (controller.loadInternalQueue(app.urlToPath(selectedFile))) {
                internalQueueModel.selectedIndex = -1
                queueListView.currentIndex = -1
            }
        }
    }

    function refreshSequenceFields() {
        inputStartSpin.value = controller.startIndex
        inputStopSpin.value = controller.stopIndex
        outputStartSpin.value = controller.outputStartIndex
        stepsSpin.value = controller.steps
        customFrameListField.text = controller.customFrameList
        loadAttemptsSpin.value = controller.loadTextureAttemptsMax
        loadSleepSpin.value = controller.loadTextureAttemptsSleep
    }

    component FormLabel: Label {
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        horizontalAlignment: Text.AlignRight
        opacity: enabled ? 0.86 : 0.45
    }

    component PathField: RowLayout {
        id: pathField

        property alias text: input.text
        property string label
        property string target
        property bool active: true

        enabled: active
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        FormLabel {
            text: pathField.label
            Layout.preferredWidth: 68
        }
        TextField {
            id: input
            Layout.fillWidth: true
            selectByMouse: true
            onEditingFinished: controller[pathField.target] = text
        }
        Button {
            icon.name: "document-open"
            display: AbstractButton.IconOnly
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Choose image")
            onClicked: chooseFacePath(pathField.target, input.text)
        }
        Connections {
            target: controller
            function onFaceInputsChanged() {
                input.text = controller[pathField.target]
            }
        }
    }

    component SectionPane: Pane {
        id: section

        property string title
        property string iconName
        property string trailingText
        default property alias content: sectionContent.data

        Layout.fillWidth: true
        padding: Kirigami.Units.largeSpacing
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false

        background: Rectangle {
            color: Kirigami.Theme.backgroundColor
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.12)
            radius: Kirigami.Units.smallSpacing
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Icon {
                    source: section.iconName
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: Kirigami.Units.iconSizes.smallMedium
                    color: Kirigami.Theme.textColor
                }
                Kirigami.Heading {
                    text: section.title
                    level: 2
                    Layout.fillWidth: true
                }
                Label {
                    visible: section.trailingText.length > 0
                    text: section.trailingText
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    opacity: 0.65
                    Layout.maximumWidth: section.width * 0.45
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                }
            }
            GridLayout {
                id: sectionContent
                Layout.fillWidth: true
                columns: 1
                rowSpacing: Kirigami.Units.smallSpacing
            }
        }
    }

    Connections {
        target: controller
        function onSequenceChanged() {
            refreshSequenceFields()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Heading {
                text: qsTr("C-Stitch")
                level: 1
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("Preferences")
                icon.name: "configure"
                onClicked: stitchSettingsDialog.open()
            }
            Button {
                text: qsTr("Verify")
                icon.name: "document-preview"
                enabled: !controller.running
                onClicked: controller.verifyInputs()
            }
             Button {
                  text: controller.editingInternalQueueIndex >= 0 ? qsTr("Save Queue Job") : qsTr("Queue Job")
                  icon.name: controller.editingInternalQueueIndex >= 0 ? "document-save" : "list-add"
                  enabled: !controller.running && !controller.sequenceIndexing
                      && (controller.editingInternalQueueIndex < 0 || !controller.cjobEnabled)
                  onClicked: {
                      if (controller.editingInternalQueueIndex >= 0) {
                          controller.saveEditedInternalQueueJob()
                      } else {
                          controller.queueJob()
                      }
                  }
             }
             CheckBox {
                 id: cjobSubmitCheck
                 text: qsTr("Submit to C-Job")
                 visible: true
                 checked: controller.cjobEnabled
                 onCheckedChanged: controller.setCJobEnabled(checked)
             }
             Button {
                 text: controller.running ? qsTr("Abort") : qsTr("Stitch")
                 icon.name: controller.running ? "process-stop" : "media-playback-start"
                 highlighted: !controller.running
                 onClicked: controller.running ? controller.abortStitch() : controller.launchStitch()
             }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: Math.max(window.width - Kirigami.Units.largeSpacing * 4, 1080)
                spacing: Kirigami.Units.largeSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    // Left column
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.largeSpacing

                        SectionPane {
                            id: queueSection
                            title: qsTr("Queue")
                            iconName: "kt-queue-manager"
                            Layout.fillWidth: true

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                 RowLayout {
                                     Layout.fillWidth: true

                                     Label {
                                         text: controller.editingInternalQueueIndex >= 0
                                             ? qsTr("Editing job %1").arg(controller.editingInternalQueueIndex + 1)
                                             : qsTr("%1 job(s) in queue").arg(controller.internalQueueSize)
                                         opacity: 0.76
                                         Layout.fillWidth: true
                                     }

                                     Button {
                                         text: qsTr("Cancel Edit")
                                         icon.name: "dialog-cancel"
                                         visible: controller.editingInternalQueueIndex >= 0
                                         enabled: !controller.running && !controller.cjobEnabled
                                         onClicked: controller.cancelEditingInternalQueueJob()
                                     }
                                 }

                                 ScrollView {
                                     Layout.fillWidth: true
                                     Layout.maximumHeight: 200
                                     Layout.minimumHeight: 20
                                     clip: true

                                     ListView {
                                         id: queueListView
                                         width: parent ? parent.width : 300
                                         model: internalQueueModel
                                         
                                         delegate: internalQueueItemDelegate
                                         spacing: Kirigami.Units.smallSpacing
                                         
                                         // Catch mouse release outside items to clean up stuck drags
                                         MouseArea {
                                             anchors.fill: parent
                                             hoverEnabled: true
                                             acceptedButtons: Qt.NoButton
                                             onContainsMouseChanged: {
                                                 if (!containsMouse) {
                                                     // Mouse left the ListView area - force cleanup of any active drag
                                                     for (var i = 0; i < queueListView.count; i++) {
                                                         var item = queueListView.itemForIndex(i);
                                                         if (item && item.dragging) {
                                                             item.dragging = false;
                                                             item.z = 0;
                                                             item.opacity = 1.0;
                                                             if (item.insertionIndicator) {
                                                                 item.insertionIndicator.destroy();
                                                                 item.insertionIndicator = null;
                                                             }
                                                             item._dragActive = false;
                                                         }
                                                     }
                                                 }
                                             }
                                         }
                                         
                                         // Catch Escape key to cancel active drags
                                         Keys.onPressed: {
                                             if (event.key === Qt.Key_Escape) {
                                                 for (var i = 0; i < queueListView.count; i++) {
                                                     var item = queueListView.itemForIndex(i);
                                                     if (item && item.dragging) {
                                                         event.accepted = true;
                                                         item.dragging = false;
                                                         item.z = 0;
                                                         item.opacity = 1.0;
                                                         if (item.insertionIndicator) {
                                                             item.insertionIndicator.destroy();
                                                             item.insertionIndicator = null;
                                                         }
                                                         item._dragActive = false;
                                                         item.y = item.originalY;
                                                     }
                                                 }
                                             }
                                         }
                                     }
                                 }

    Component {
        id: internalQueueItemDelegate
        
        ItemDelegate {
            id: itemRoot
            width: parent ? parent.width : 300
            height: 30
            
            property bool dragging: false
            property real originalY: 0
            property bool _dragActive: false
            property real _pressPosX: 0
            property real _pressPosY: 0
            // transient insertion indicator instance (created on drag start)
            property var insertionIndicator: null
                                        
                                        contentItem: RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: Kirigami.Units.smallSpacing
                                            spacing: Kirigami.Units.smallSpacing
                                            
                                             Label {
                                                 text: "%1.".arg(index + 1)
                                                 color: Kirigami.Theme.neutralTextColor
                                                 opacity: 0.65
                                                 Layout.preferredWidth: 28
                                                 horizontalAlignment: Text.RightAlign
                                             }
                                             Label {
                                                 text: model.jobName || ""
                                                 Layout.fillWidth: true
                                                 elide: Text.ElideMiddle
                                             }
                                            
                                             Label {
                                                 text: (controller.running && controller.internalQueueSize > 0 && index === controller.runningJobIndex ? "Running" : "Pending")
                                                 color: (controller.running && controller.internalQueueSize > 0 && index === controller.runningJobIndex ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.neutralTextColor)
                                                 opacity: 0.85
                                             }

                                             ToolButton {
                                                 id: editButton
                                                 Layout.preferredWidth: 24
                                                 Layout.preferredHeight: 24
                                                 icon.name: "document-edit"
                                                 visible: !controller.running && !controller.cjobEnabled
                                                 enabled: controller.editingInternalQueueIndex < 0
                                                 opacity: hovered ? 1.0 : 0.5
                                                 ToolTip.visible: hovered
                                                 ToolTip.text: qsTr("Edit job")
                                             }
                                            
                                            ToolButton {
                                                id: removeButton
                                                Layout.preferredWidth: 24
                                                Layout.preferredHeight: 24
                                                icon.name: "list-remove"
                                                visible: !controller.running && !controller.cjobEnabled
                                                opacity: hovered ? 1.0 : 0.5
                                            }
                                        }

                                         Rectangle {
                                             id: editArea
                                             anchors.right: deleteArea.left
                                             anchors.top: parent.top
                                             anchors.bottom: parent.bottom
                                             width: 32
                                             color: "transparent"
                                             enabled: !controller.running && !controller.cjobEnabled && controller.editingInternalQueueIndex < 0

                                             MouseArea {
                                                 anchors.fill: parent
                                                 acceptedButtons: Qt.LeftButton
                                                 hoverEnabled: true
                                                 onClicked: {
                                                     mouse.accepted = true
                                                     if (controller.editInternalQueueJob(index)) {
                                                         queueListView.currentIndex = index
                                                         internalQueueModel.selectedIndex = index
                                                     }
                                                 }
                                             }
                                         }
                                        
                                        background: Rectangle {
                                            anchors.fill: parent
                                            color: {
                                                if (itemRoot.dragging) return Qt.alpha(Kirigami.Theme.highlightColor, 0.4);
                                                if (index === internalQueueModel.selectedIndex) return Qt.alpha(0x00ff00, 0.25);
                                                if (hovered && !itemRoot.dragging) return Qt.alpha(Kirigami.Theme.hoverColor, 0.4);
                                                return Kirigami.Theme.backgroundColor;
                                            }
                                        }
                                        
                                        // Delete button area - placed at top level to intercept clicks before drag
                                        Rectangle {
                                            id: deleteArea
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            width: 32
                                            color: "transparent"
                                            
                                            // Only active when visible (not running, not C-Job)
                                            enabled: !controller.running && !controller.cjobEnabled
                                            
                                            MouseArea {
                                                anchors.fill: parent
                                                acceptedButtons: Qt.LeftButton
                                                hoverEnabled: true
                                                onClicked: {
                                                    mouse.accepted = true
                                                    controller.removeJobFromInternalQueue(index)
                                                }
                                            }
                                        }
                                        
                                        // Template for transient insertion indicator
                                        Component {
                                            id: insertionLineComp
                                            Rectangle {
                                                color: Kirigami.Theme.highlightColor
                                                height: 4
                                                width: parent ? parent.width : queueListView.width
                                                radius: 2
                                                opacity: 0.95
                                                z: 10000
                                            }
                                        }
                                        
                                         // Drag area covering the rest of the item (excluding queue action buttons)
                                         MouseArea {
                                              anchors.right: editArea.left
                                             anchors.top: parent.top
                                             anchors.bottom: parent.bottom
                                             anchors.left: parent.left
                                             hoverEnabled: true
                                             acceptedButtons: Qt.LeftButton
                                             drag.target: itemRoot
                                             drag.axis: Drag.YAxis
                                              enabled: controller.editingInternalQueueIndex < 0
                                             
                                             onPressed: {
                                                 itemRoot._dragActive = false;
                                                 itemRoot._pressPosX = mouse.x;
                                                 itemRoot._pressPosY = mouse.y;
                                                 itemRoot.originalY = itemRoot.y;
                                                 queueListView.currentIndex = index;
                                                 internalQueueModel.selectedIndex = index;
                                             }
                                             
                                             onPositionChanged: {
                                                 // Only start drag when mouse moves beyond system drag distance threshold (3 pixels)
                                                 if (!itemRoot._dragActive) {
                                                     var dx = Math.abs(mouse.x - itemRoot._pressPosX);
                                                     var dy = Math.abs(mouse.y - itemRoot._pressPosY);
                                                     if (dx <= 3 && dy <= 3) {
                                                         return;
                                                     }
                                                     // Drag has actually started now
                                                     itemRoot._dragActive = true;
                                                     itemRoot.dragging = true;
                                                     itemRoot.z = 9999;
                                                     itemRoot.opacity = 0.85;
                                                     drag.minimumY = 0;
                                                     drag.maximumY = Math.max(0, queueListView.contentHeight);
                                                     
                                                     // Create insertion indicator only when drag actually starts
                                                     if (queueListView && queueListView.contentItem && !itemRoot.insertionIndicator) {
                                                         itemRoot.insertionIndicator = insertionLineComp.createObject(queueListView.contentItem, { x: 0, y: itemRoot.y + itemRoot.height/2 - 2, width: queueListView.width });
                                                     }
                                                 }
                                                 
                                                 if (!itemRoot._dragActive) return;
                                                 
                                                 var edgeThreshold = 20;
                                                 if (itemRoot.y < queueListView.contentY + edgeThreshold && queueListView.contentY > 0) {
                                                     queueListView.contentY = Math.max(0, queueListView.contentY - 8);
                                                 }
                                                 var bottomEdge = queueListView.contentHeight - edgeThreshold - itemRoot.height;
                                                 if (itemRoot.y > bottomEdge && (queueListView.contentY < queueListView.contentHeight - queueListView.height)) {
                                                     queueListView.contentY = Math.min(queueListView.contentHeight - queueListView.height, queueListView.contentY + 8);
                                                 }

                                                 // update insertion indicator position
                                                if (itemRoot.insertionIndicator) {
                                                    var centerY = itemRoot.y + itemRoot.height / 2;
                                                    var destIndex = Math.floor(centerY / itemRoot.height);
                                                    if (destIndex < 0) destIndex = 0;
                                                    if (destIndex > queueListView.count - 1) destIndex = queueListView.count - 1;
                                                    
                                                    var halfIndicator = itemRoot.insertionIndicator.height / 2;
                                                    var indicatorY;
                                                    
                                                    // When dragging down, show indicator at BOTTOM of target item
                                                    // When dragging up, show it at TOP of target item
                                                    if (itemRoot.y > itemRoot.originalY) {
                                                        indicatorY = (destIndex + 1) * itemRoot.height - halfIndicator;
                                                    } else {
                                                        indicatorY = destIndex * itemRoot.height - halfIndicator;
                                                    }
                                                    
                                                    // clamp indicator to content area
                                                    if (indicatorY < 0) indicatorY = 0;
                                                    var maxIndicatorY = queueListView.contentHeight - itemRoot.insertionIndicator.height;
                                                    if (indicatorY > maxIndicatorY) indicatorY = maxIndicatorY;
                                                    
                                                    itemRoot.insertionIndicator.y = indicatorY;
                                                    itemRoot.insertionIndicator.width = queueListView.width;
                                                }
                                            }
                                            
                                             onCanceled: {
                                                 // cleanup transient indicator
                                                 if (itemRoot.insertionIndicator) {
                                                     itemRoot.insertionIndicator.destroy();
                                                     itemRoot.insertionIndicator = null;
                                                 }
                                                 itemRoot._dragActive = false;
                                                 itemRoot.dragging = false;
                                                 itemRoot.z = 0;
                                                 itemRoot.opacity = 1.0;
                                                 // restore original position
                                                 itemRoot.y = itemRoot.originalY;
                                             }
                                            
                                             onReleased: {
                                                 if (!itemRoot._dragActive) return;
                                                 
                                                 itemRoot._dragActive = false;
                                                 itemRoot.dragging = false;
                                                 itemRoot.z = 0;
                                                 itemRoot.opacity = 1.0;
                                                
                                                // Compute destination index based on delegate center and drag direction
                                                var centerY = itemRoot.y + itemRoot.height / 2;
                                                var destIndex = Math.floor(centerY / itemRoot.height);
                                                if (destIndex < 0) destIndex = 0;
                                                if (destIndex > queueListView.count - 1) destIndex = queueListView.count - 1;
                                                
                                                // If dragged down, insert AFTER destIndex; if up, insert BEFORE destIndex
                                                var finalDest = (itemRoot.y > itemRoot.originalY) ? destIndex : destIndex;
                                                
                                                // clamp finalDest to valid range
                                                if (finalDest < 0) finalDest = 0;
                                                if (finalDest > queueListView.count - 1) finalDest = queueListView.count - 1;
                                                
                                                if (index !== finalDest) {
                                                    internalQueueModel.moveRow(index, finalDest);
                                                }
                                                
                                                // remove transient indicator after move
                                                if (itemRoot.insertionIndicator) {
                                                    itemRoot.insertionIndicator.destroy();
                                                    itemRoot.insertionIndicator = null;
                                                }
                                            }
                                        }
                                    }
                                }

                                 RowLayout {
                                     Label { text: qsTr("Queue Running:"); opacity: 0.76 }
                                     Label {
                                         text: controller.internalQueueRunning ? qsTr("Yes") : qsTr("No")
                                         color: controller.internalQueueRunning ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                                         opacity: 1.0
                                     }
                                 }

                                 RowLayout {
                                     Layout.fillWidth: true
                                     spacing: Kirigami.Units.smallSpacing

                                     Button {
                                         text: controller.internalQueueRunning ? qsTr("Pause Queue") : qsTr("Run Queue")
                                         icon.name: controller.internalQueueRunning ? "media-playlist-shuffle" : "media-playback-start"
                                         flat: false
                                         enabled: !controller.cjobEnabled && (controller.internalQueueSize > 0 || true)
                                         onClicked: {
                                             if (controller.internalQueueRunning) {
                                                 controller.internalQueueRunning = false
                                             } else {
                                                 controller.internalQueueRunning = true
                                             }
                                         }
                                     }

                                     Button {
                                         text: qsTr("Start Selected Job")
                                         icon.name: "media-playback-start"
                                         onClicked: controller.launchJobAtQueueIndex(internalQueueModel.selectedIndex)
                                         enabled: internalQueueModel.selectedIndex >= 0 && 
                                                 internalQueueModel.selectedIndex < controller.internalQueueSize &&
                                                 !controller.running && 
                                                 !controller.cjobEnabled
                                     }

                                      Button {
                                          text: qsTr("Save Queue")
                                          icon.name: "document-save"
                                          enabled: !controller.running && !controller.cjobEnabled
                                              && controller.editingInternalQueueIndex < 0
                                          onClicked: saveQueueDialog.open()
                                      }

                                      Button {
                                          text: qsTr("Load Queue")
                                          icon.name: "document-open"
                                          enabled: !controller.running && !controller.cjobEnabled
                                              && controller.editingInternalQueueIndex < 0
                                          onClicked: loadQueueDialog.open()
                                      }

                                     Item { Layout.fillWidth: true }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing
                            
                                    Label {
                                        visible: controller.cjobEnabled
                                        text: qsTr("(Disabled - C-Job enabled)")
                                        opacity: 0.5
                                        font.italic: true
                                    }
                                }
                            }
                        }

                        SectionPane {
                            id: inputSection
                            title: controller.stereo ? qsTr("Left Eye Input") : qsTr("Input")
                            iconName: "image-x-generic"
                            opacity: window.leftInputActive ? 1.0 : 0.45
                            Layout.fillWidth: true

                            PathField { label: qsTr("Left"); target: "leftFace"; text: controller.leftFace; active: window.leftInputActive }
                            PathField { label: qsTr("Right"); target: "rightFace"; text: controller.rightFace; active: window.leftInputActive }
                            PathField { label: qsTr("Top"); target: "topFace"; text: controller.topFace; active: window.leftInputActive }
                            PathField { label: qsTr("Bottom"); target: "bottomFace"; text: controller.bottomFace; active: window.leftInputActive }
                            CheckBox { checked: controller.autoDetectCubemapFaces; text: qsTr("Automatic face detection"); onToggled: controller.autoDetectCubemapFaces = checked }
                        }

                        SectionPane {
                            title: qsTr("Right Eye Input")
                            iconName: "image-x-generic"
                            enabled: window.rightInputActive
                            opacity: enabled ? 1.0 : 0.45
                            Layout.fillWidth: true

                            PathField { label: qsTr("Left"); target: "rightLeftFace"; text: controller.rightLeftFace; active: window.rightInputActive }
                            PathField { label: qsTr("Right"); target: "rightRightFace"; text: controller.rightRightFace; active: window.rightInputActive }
                            PathField { label: qsTr("Top"); target: "rightTopFace"; text: controller.rightTopFace; active: window.rightInputActive }
                            PathField { label: qsTr("Bottom"); target: "rightBottomFace"; text: controller.rightBottomFace; active: window.rightInputActive }
                        }

                        SectionPane {
                            title: qsTr("Sequence")
                            iconName: "view-list-details"
                            Layout.fillWidth: true

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 4
                                rowSpacing: Kirigami.Units.smallSpacing
                                columnSpacing: Kirigami.Units.smallSpacing

                                FormLabel { text: qsTr("Input start") }
                                SpinBox { id: inputStartSpin; from: -999999; to: 999999; value: controller.startIndex; editable: true; onValueModified: controller.startIndex = value; Layout.fillWidth: true }
                                FormLabel { text: qsTr("Input stop") }
                                SpinBox { id: inputStopSpin; from: -999999; to: 999999; value: controller.stopIndex; editable: true; onValueModified: controller.stopIndex = value; Layout.fillWidth: true }
                                FormLabel { text: qsTr("Output start") }
                                SpinBox { id: outputStartSpin; from: -999999; to: 999999; value: controller.outputStartIndex; editable: true; onValueModified: controller.outputStartIndex = value; Layout.fillWidth: true }
                                FormLabel { text: qsTr("Steps") }
                                SpinBox { id: stepsSpin; from: 1; to: 10000; value: controller.steps; editable: true; onValueModified: controller.steps = value; Layout.fillWidth: true }
                                FormLabel { text: qsTr("Attempts") }
                                SpinBox { id: loadAttemptsSpin; from: 1; to: 1000; value: controller.loadTextureAttemptsMax; editable: true; onValueModified: controller.loadTextureAttemptsMax = value; Layout.fillWidth: true }
                                FormLabel { text: qsTr("Sleep (s)") }
                                SpinBox { id: loadSleepSpin; from: 0; to: 3600; value: controller.loadTextureAttemptsSleep; editable: true; onValueModified: controller.loadTextureAttemptsSleep = value; Layout.fillWidth: true }
                                FormLabel { text: qsTr("Custom list") }
                                TextField {
                                    id: customFrameListField
                                    text: controller.customFrameList
                                    placeholderText: qsTr("12, 18, 25..30")
                                    selectByMouse: true
                                    onEditingFinished: controller.customFrameList = text
                                    Layout.columnSpan: 3
                                    Layout.fillWidth: true
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: controller.sequenceStatus; Layout.fillWidth: true; elide: Text.ElideRight; opacity: 0.76 }
                                Button {
                                    text: qsTr("Index")
                                    icon.name: "view-refresh"
                                    enabled: !controller.sequenceIndexing
                                    onClicked: controller.indexSequence(true)
                                }
                            }
                        }

                        Item { Layout.fillWidth: true; Layout.fillHeight: true }
                    }

                    // Right column
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.largeSpacing

                        SectionPane {
                            title: qsTr("Progress")
                            iconName: "view-statistics"
                            trailingText: controller.elapsedTime.length > 0 ? qsTr("Elapsed %1\nRemaining %2").arg(controller.elapsedTime).arg(controller.remainingTime) : ""
                            enabled: controller.running || controller.progressTotal > 0
                            Layout.fillWidth: true
                            Layout.preferredWidth: parent.width * 0.353
                            Layout.preferredHeight: queueSection.height

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Label {
                                    Layout.fillWidth: true
                                    text: controller.progressStatus.length > 0 ? controller.progressStatus : qsTr("Images are buffered and processed on the virtual dome before capture/encoding.")
                                    wrapMode: Text.Wrap
                                    opacity: 0.78
                                }

                                ProgressBar {
                                    id: renderProgressBar
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: SectionPane.width / 3.5
                                    Layout.preferredHeight: Kirigami.Units.gridUnit * 0.8
                                    implicitHeight: Kirigami.Units.gridUnit * 0.8
                                    padding: 1
                                    from: 0
                                    to: 100
                                    value: controller.progress * 100
                                    background: Rectangle {
                                        implicitHeight: renderProgressBar.implicitHeight
                                        radius: height / 2
                                        color: Kirigami.Theme.alternateBackgroundColor
                                        opacity: 0.9
                                    }
                                    contentItem: Item {
                                        implicitHeight: renderProgressBar.implicitHeight
                                        clip: true

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            width: parent.width * Math.max(0, Math.min(1, renderProgressBar.visualPosition))
                                            radius: height / 2
                                            color: "lightgreen"
                                        }
                                    }
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    rowSpacing: Kirigami.Units.smallSpacing
                                    columnSpacing: Kirigami.Units.smallSpacing

                                    FormLabel { text: controller.running ? qsTr("Processing") : qsTr("Processed") }
                                    Label {
                                        text: qsTr("%1/%2 (%3%)")
                                            .arg(controller.progressCompleted)
                                            .arg(controller.progressTotal)
                                            .arg(Math.round(controller.progress * 100))
                                        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                    }

                                    FormLabel { text: controller.running ? qsTr("Loaded") : qsTr("Loaded") }
                                    Label {
                                        text: qsTr("%1/%2 (%3%)")
                                            .arg(controller.progressCompleted)
                                            .arg(controller.progressTotal)
                                            .arg(Math.round(controller.progress * 100))
                                        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                    }
                                }

                                Kirigami.InlineMessage {
                                    Layout.fillWidth: true
                                    type: Kirigami.MessageType.Error
                                    icon.source: "dialog-error"
                                    visible: false
                                    text: qsTr("An error occurred. See the Log for details.")
                                    showCloseButton: false
                                }
                            }
                        }

                        SectionPane {
                            title: qsTr("Output")
                            iconName: "document-save"
                            Layout.fillWidth: true
                            
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 4
                                rowSpacing: Kirigami.Units.smallSpacing
                                columnSpacing: Kirigami.Units.smallSpacing

                                FormLabel { text: qsTr("Output folder") }
                                RowLayout {
                                    Layout.columnSpan: 3
                                    Layout.fillWidth: true
                                    TextField { text: controller.outputDirectory; Layout.fillWidth: true; selectByMouse: true; onEditingFinished: controller.outputDirectory = text }
                                    Button { icon.name: "folder-open"; display: AbstractButton.IconOnly; onClicked: chooseOutputDirectory("outputDirectory", controller.outputDirectory); ToolTip.visible: hovered; ToolTip.text: qsTr("Choose output directory") }
                                }
                                FormLabel { text: qsTr("Filename stem") }
                                TextField { text: controller.outputName; Layout.fillWidth: true; selectByMouse: true; onEditingFinished: controller.outputName = text }                               
                            }
                        }

                        SectionPane {
                            title: qsTr("Encoding")
                            iconName: "document-save"
                            Layout.fillWidth: true

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 4
                                rowSpacing: Kirigami.Units.smallSpacing
                                columnSpacing: Kirigami.Units.smallSpacing

                                FormLabel { text: qsTr("Format") }
                                ComboBox { model: ["png", "jpeg", "tga"]; currentIndex: Math.max(0, model.indexOf(controller.format)); onActivated: controller.format = model[index]; Layout.fillWidth: true }

                                FormLabel { text: qsTr("Loading threads / eye") }
                                SpinBox { from: 4; to: 128; stepSize: 4; value: controller.loadingThreads; editable: true; onValueModified: controller.loadingThreads = value; Layout.fillWidth: true }

                                FormLabel { text: qsTr("Quality") }
                                SpinBox { from: 1; to: 100; value: controller.quality; editable: true; onValueModified: controller.quality = value; Layout.fillWidth: true }

                                FormLabel { text: qsTr("Encoding threads / eye") }
                                SpinBox { from: 1; to: 128; value: controller.encodingThreads; editable: true; onValueModified: controller.encodingThreads = value; Layout.fillWidth: true }
                                                                FormLabel { text: qsTr("Compression") }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Slider { from: 0; to: 9; stepSize: 1; value: controller.compression; snapMode: Slider.SnapAlways; onMoved: controller.compression = Math.round(value); Layout.fillWidth: true }
                                    SpinBox { from: 0; to: 9; value: controller.compression; editable: true; onValueModified: controller.compression = value; Layout.preferredWidth: 72 }
                                }
                            }
                        }

                        SectionPane {
                            title: qsTr("Projection")
                            iconName: "transform-scale"
                            Layout.fillWidth: true

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    rowSpacing: Kirigami.Units.smallSpacing
                                    columnSpacing: Kirigami.Units.smallSpacing

                                    FormLabel { text: qsTr("Cubemap (px)") }
                                    ComboBox {
                                        model: [256, 512, 1024, 2048, 3072, 4096, 8192]
                                        currentIndex: Math.max(0, model.indexOf(controller.cubemapResolution))
                                        displayText: currentValue + "x" + currentValue + " px"
                                        delegate: ItemDelegate { text: modelData + "x" + modelData + " px" }
                                        onActivated: controller.cubemapResolution = model[index]
                                        Layout.fillWidth: true
                                    }

                                    FormLabel { text: qsTr("Resolution (px)") }
                                    ComboBox {
                                        model: [256, 512, 1024, 2048, 3072, 4096, 8192]
                                        currentIndex: Math.max(0, model.indexOf(controller.resolution))
                                        textRole: ""
                                        displayText: currentValue + "x" + currentValue + " px"
                                        delegate: ItemDelegate { text: modelData + "x" + modelData + " px" }
                                        onActivated: controller.resolution = model[index]
                                        Layout.fillWidth: true
                                    }

                                    FormLabel { text: qsTr("Stereo") }
                                    CheckBox { checked: controller.stereo; onToggled: controller.stereo = checked }

                                    FormLabel { text: qsTr("Stereo eye") }
                                    ComboBox {
                                        enabled: controller.stereo
                                        model: window.stereoEyeValues
                                        currentIndex: Math.max(0, model.indexOf(controller.stereoEye))
                                        displayText: window.stereoEyeLabel(currentValue)
                                        delegate: ItemDelegate { text: window.stereoEyeLabel(modelData) }
                                        onActivated: controller.stereoEye = model[index]
                                        Layout.fillWidth: true
                                    }

                                    FormLabel { text: qsTr("Eye separation (m)") }
                                    SpinBox {
                                        from: 0; to: 500; value: Math.round(controller.eyeSeparation * 1000)
                                        editable: true
                                        textFromValue: function(value) { return Number(value / 1000).toLocaleString(locale, "f", 3) }
                                        valueFromText: function(text) { return Math.round(Number.fromLocaleString(locale, text) * 1000) }
                                        onValueModified: controller.eyeSeparation = value / 1000
                                        Layout.fillWidth: true
                                    }

                                    Item { Layout.fillWidth: true; Layout.fillHeight: true }
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    rowSpacing: Kirigami.Units.smallSpacing
                                    columnSpacing: Kirigami.Units.smallSpacing

                                    FormLabel { text: qsTr("Fisheye FOV (deg)") }
                                    SpinBox {
                                        from: 1; to: 360; value: Math.round(controller.fisheyeFov)
                                        editable: true
                                        onValueModified: controller.fisheyeFov = value
                                        Layout.fillWidth: true
                                    }

                                    FormLabel { text: qsTr("Dome diameter (m)") }
                                    SpinBox {
                                        from: 1; to: 10000; value: Math.round(controller.domeDiameter * 100)
                                        editable: true
                                        textFromValue: function(value) { return Number(value / 100).toLocaleString(locale, "f", 2) }
                                        valueFromText: function(text) { return Math.round(Number.fromLocaleString(locale, text) * 100) }
                                        onValueModified: controller.domeDiameter = value / 100
                                        Layout.fillWidth: true
                                    }

                                    FormLabel { text: qsTr("MSAA (samples)") }
                                    ComboBox {
                                        model: [1, 2, 4, 8, 16, 32, 64]
                                        currentIndex: Math.max(0, model.indexOf(controller.msaa))
                                        onActivated: controller.msaa = model[index]
                                        Layout.fillWidth: true
                                    }

                                    FormLabel { text: qsTr("FXAA") }
                                    CheckBox { checked: controller.fxaa; onToggled: controller.fxaa = checked }

                                    FormLabel { text: qsTr("Preserve alpha") }
                                    CheckBox { checked: controller.alpha; onToggled: controller.alpha = checked }

                                    FormLabel { text: qsTr("Cubic interpolation") }
                                    CheckBox { checked: controller.cubic; onToggled: controller.cubic = checked }

                                    Item { Layout.fillWidth: true; Layout.fillHeight: true }
                                }
                            }
                        }

                        SectionPane {
                            title: qsTr("Face Rotation")
                            iconName: "object-rotate-right"
                            Layout.fillWidth: true

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 4
                                rowSpacing: Kirigami.Units.smallSpacing
                                columnSpacing: Kirigami.Units.smallSpacing

                                FormLabel { text: qsTr("Left") }
                                ComboBox { model: rotations; currentIndex: selectRotationIndex(controller.leftRotation); onActivated: controller.leftRotation = model[index]; Layout.fillWidth: true }
                                FormLabel { text: qsTr("Right") }
                                ComboBox { model: rotations; currentIndex: selectRotationIndex(controller.rightRotation); onActivated: controller.rightRotation = model[index]; Layout.fillWidth: true }
                                FormLabel { text: qsTr("Top") }
                                ComboBox { model: rotations; currentIndex: selectRotationIndex(controller.topRotation); onActivated: controller.topRotation = model[index]; Layout.fillWidth: true }
                                FormLabel { text: qsTr("Bottom") }
                                ComboBox { model: rotations; currentIndex: selectRotationIndex(controller.bottomRotation); onActivated: controller.bottomRotation = model[index]; Layout.fillWidth: true }
                            }
                        }

                        Item { Layout.fillWidth: true; Layout.fillHeight: true }
                    }
                }

                SectionPane {
                    title: qsTr("Command")
                    iconName: "utilities-terminal"
                    Layout.fillWidth: true

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                        contentWidth: availableWidth
                        clip: true

                        TextArea {
                            text: controller.commandLinePreview
                            readOnly: true
                            wrapMode: Text.WrapAnywhere
                            Layout.fillWidth: true
                            Layout.preferredHeight: 110
                        }
                    }
                }

                SectionPane {
                    title: qsTr("Log")
                    iconName: "view-pim-notes"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 180

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: -30
                        Item { Layout.fillWidth: true }
                        Button {
                            onClicked: controller.clearLog()
                            text: qsTr("Clear Log")
                            icon.color: Kirigami.Theme.textColor
                            icon.name: "edit-clear-history"
                            display: AbstractButton.TextBesideIcon
                        }
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true

                        TextArea {
                            text: controller.logText
                            readOnly: true
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.preferredHeight: 180
                        }
                    }
                }
            }
        }
    }

    StitchSettingsDialog {
        id: stitchSettingsDialog
        controller: window.controller
    }
}