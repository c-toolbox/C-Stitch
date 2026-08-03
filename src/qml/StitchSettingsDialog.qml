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

import org.kde.kirigami as Kirigami
import org.ctoolbox.cstitch

Dialog {
    id: root

    property var controller
    readonly property var rotations: [0, 90, 180, 270]
    readonly property var stereoEyeValues: ["both", "left", "right"]

    title: qsTr("C-Stitch Preferences")
    modal: true
    width: Math.min(parent ? parent.width - Kirigami.Units.gridUnit * 8 : 600, 600)
    height: Math.min(parent ? parent.height - Kirigami.Units.gridUnit * 8 : 760, 760)
    anchors.centerIn: parent

    function rotationIndex(value) {
        return Math.max(0, rotations.indexOf(value))
    }

    function stereoEyeIndex(value) {
        return Math.max(0, stereoEyeValues.indexOf(value))
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

    function loadFields() {
        leftFaceField.text = StitchSettings.leftFace
        rightFaceField.text = StitchSettings.rightFace
        topFaceField.text = StitchSettings.topFace
        bottomFaceField.text = StitchSettings.bottomFace
        rightLeftFaceField.text = StitchSettings.rightLeftFace
        rightRightFaceField.text = StitchSettings.rightRightFace
        rightTopFaceField.text = StitchSettings.rightTopFace
        rightBottomFaceField.text = StitchSettings.rightBottomFace
        outputDirectoryField.text = StitchSettings.outputDirectory
        monoSubDirField.text = StitchSettings.monoSubDir
        leftSubDirField.text = StitchSettings.leftSubDir
        rightSubDirField.text = StitchSettings.rightSubDir
        outputNameField.text = StitchSettings.outputName
        stereoCheck.checked = StitchSettings.stereo
        stereoEyeCombo.currentIndex = stereoEyeIndex(StitchSettings.stereoEye)
        alphaCheck.checked = StitchSettings.alpha
        fxaaCheck.checked = StitchSettings.fxaa
        cubicCheck.checked = StitchSettings.cubic
        autoDetectCheck.checked = StitchSettings.autoDetectCubemapFaces
        fisheyeFovSpin.value = Math.round(StitchSettings.fisheyeFov)
        domeDiameterSpin.value = Math.round(StitchSettings.domeDiameter * 100)
        eyeSeparationSpin.value = Math.round(StitchSettings.eyeSeparation * 1000)
        resolutionCombo.currentIndex = Math.max(0, resolutionCombo.model.indexOf(StitchSettings.resolution))
        cubemapResolutionCombo.currentIndex = Math.max(0, cubemapResolutionCombo.model.indexOf(StitchSettings.cubemapResolution))
        msaaCombo.currentIndex = Math.max(0, msaaCombo.model.indexOf(StitchSettings.msaa))
        formatCombo.currentIndex = Math.max(0, formatCombo.model.indexOf(StitchSettings.format))
        compressionSpin.value = StitchSettings.compression
        qualitySpin.value = StitchSettings.quality
        loadingThreadsSpin.value = StitchSettings.loadingThreads
        encodingThreadsSpin.value = StitchSettings.encodingThreads
        startIndexSpin.value = StitchSettings.startIndex
        stopIndexSpin.value = StitchSettings.stopIndex
        outputStartIndexSpin.value = StitchSettings.outputStartIndex
        stepsSpin.value = StitchSettings.steps
        customFrameListField.text = StitchSettings.customFrameList
        loadAttemptsSpin.value = StitchSettings.loadTextureAttemptsMax
        loadSleepSpin.value = StitchSettings.loadTextureAttemptsSleep
        leftRotationCombo.currentIndex = rotationIndex(StitchSettings.leftRotation)
        rightRotationCombo.currentIndex = rotationIndex(StitchSettings.rightRotation)
        topRotationCombo.currentIndex = rotationIndex(StitchSettings.topRotation)
        bottomRotationCombo.currentIndex = rotationIndex(StitchSettings.bottomRotation)
    }

    function loadRuntimeFields() {
        if (!root.controller) {
            return
        }
        leftFaceField.text = root.controller.leftFace
        rightFaceField.text = root.controller.rightFace
        topFaceField.text = root.controller.topFace
        bottomFaceField.text = root.controller.bottomFace
        rightLeftFaceField.text = root.controller.rightLeftFace
        rightRightFaceField.text = root.controller.rightRightFace
        rightTopFaceField.text = root.controller.rightTopFace
        rightBottomFaceField.text = root.controller.rightBottomFace
        outputDirectoryField.text = root.controller.outputDirectory
        monoSubDirField.text = StitchSettings.monoSubDir
        leftSubDirField.text = StitchSettings.leftSubDir
        rightSubDirField.text = StitchSettings.rightSubDir
        outputNameField.text = root.controller.outputName
        stereoCheck.checked = root.controller.stereo
        stereoEyeCombo.currentIndex = stereoEyeIndex(root.controller.stereoEye)
        alphaCheck.checked = root.controller.alpha
        fxaaCheck.checked = root.controller.fxaa
        cubicCheck.checked = root.controller.cubic
        autoDetectCheck.checked = root.controller.autoDetectCubemapFaces
        fisheyeFovSpin.value = Math.round(root.controller.fisheyeFov)
        domeDiameterSpin.value = Math.round(root.controller.domeDiameter * 100)
        eyeSeparationSpin.value = Math.round(root.controller.eyeSeparation * 1000)
        resolutionCombo.currentIndex = Math.max(0, resolutionCombo.model.indexOf(root.controller.resolution))
        cubemapResolutionCombo.currentIndex = Math.max(0, cubemapResolutionCombo.model.indexOf(root.controller.cubemapResolution))
        msaaCombo.currentIndex = Math.max(0, msaaCombo.model.indexOf(root.controller.msaa))
        formatCombo.currentIndex = Math.max(0, formatCombo.model.indexOf(root.controller.format))
        compressionSpin.value = root.controller.compression
        qualitySpin.value = root.controller.quality
        loadingThreadsSpin.value = root.controller.loadingThreads
        encodingThreadsSpin.value = root.controller.encodingThreads
        startIndexSpin.value = root.controller.startIndex
        stopIndexSpin.value = root.controller.stopIndex
        outputStartIndexSpin.value = root.controller.outputStartIndex
        stepsSpin.value = root.controller.steps
        customFrameListField.text = root.controller.customFrameList
        loadAttemptsSpin.value = root.controller.loadTextureAttemptsMax
        loadSleepSpin.value = root.controller.loadTextureAttemptsSleep
        leftRotationCombo.currentIndex = rotationIndex(root.controller.leftRotation)
        rightRotationCombo.currentIndex = rotationIndex(root.controller.rightRotation)
        topRotationCombo.currentIndex = rotationIndex(root.controller.topRotation)
        bottomRotationCombo.currentIndex = rotationIndex(root.controller.bottomRotation)
    }

    function storeFields() {
        StitchSettings.leftFace = leftFaceField.text
        StitchSettings.rightFace = rightFaceField.text
        StitchSettings.topFace = topFaceField.text
        StitchSettings.bottomFace = bottomFaceField.text
        StitchSettings.rightLeftFace = rightLeftFaceField.text
        StitchSettings.rightRightFace = rightRightFaceField.text
        StitchSettings.rightTopFace = rightTopFaceField.text
        StitchSettings.rightBottomFace = rightBottomFaceField.text
        StitchSettings.outputDirectory = outputDirectoryField.text
        // Right output directory is no longer used - paths are now OutputDirectory/OutputName/SubDir/Filename
        StitchSettings.outputName = outputNameField.text
        StitchSettings.monoSubDir = monoSubDirField.text
        StitchSettings.leftSubDir = leftSubDirField.text
        StitchSettings.rightSubDir = rightSubDirField.text
        StitchSettings.stereo = stereoCheck.checked
        StitchSettings.stereoEye = stereoEyeCombo.currentValue
        StitchSettings.alpha = alphaCheck.checked
        StitchSettings.fxaa = fxaaCheck.checked
        StitchSettings.cubic = cubicCheck.checked
        StitchSettings.autoDetectCubemapFaces = autoDetectCheck.checked
        StitchSettings.fisheyeFov = fisheyeFovSpin.value
        StitchSettings.domeDiameter = domeDiameterSpin.value / 100
        StitchSettings.eyeSeparation = eyeSeparationSpin.value / 1000
        StitchSettings.resolution = resolutionCombo.currentValue
        StitchSettings.cubemapResolution = cubemapResolutionCombo.currentValue
        StitchSettings.msaa = msaaCombo.currentValue
        StitchSettings.format = formatCombo.currentText
        StitchSettings.compression = compressionSpin.value
        StitchSettings.quality = qualitySpin.value
        StitchSettings.loadingThreads = loadingThreadsSpin.value
        StitchSettings.encodingThreads = encodingThreadsSpin.value
        StitchSettings.startIndex = startIndexSpin.value
        StitchSettings.stopIndex = stopIndexSpin.value
        StitchSettings.outputStartIndex = outputStartIndexSpin.value
        StitchSettings.steps = stepsSpin.value
        StitchSettings.customFrameList = customFrameListField.text
        StitchSettings.loadTextureAttemptsMax = loadAttemptsSpin.value
        StitchSettings.loadTextureAttemptsSleep = loadSleepSpin.value
        StitchSettings.leftRotation = leftRotationCombo.currentValue
        StitchSettings.rightRotation = rightRotationCombo.currentValue
        StitchSettings.topRotation = topRotationCombo.currentValue
        StitchSettings.bottomRotation = bottomRotationCombo.currentValue
    }

    function setDefaults() {
        leftFaceField.text = ""
        rightFaceField.text = ""
        topFaceField.text = ""
        bottomFaceField.text = ""
        rightLeftFaceField.text = ""
        rightRightFaceField.text = ""
        rightTopFaceField.text = ""
        rightBottomFaceField.text = ""
        outputDirectoryField.text = ""
        monoSubDirField.text = StitchSettings.defaultMonoSubDirValue
        leftSubDirField.text = StitchSettings.defaultLeftSubDirValue
        rightSubDirField.text = StitchSettings.defaultRightSubDirValue
        outputNameField.text = StitchSettings.defaultOutputNameValue
        stereoCheck.checked = StitchSettings.defaultStereoValue
        stereoEyeCombo.currentIndex = stereoEyeIndex(StitchSettings.defaultStereoEyeValue)
        alphaCheck.checked = StitchSettings.defaultAlphaValue
        fxaaCheck.checked = StitchSettings.defaultFxaaValue
        cubicCheck.checked = StitchSettings.defaultCubicValue
        autoDetectCheck.checked = StitchSettings.defaultAutoDetectCubemapFacesValue
        fisheyeFovSpin.value = Math.round(StitchSettings.defaultFisheyeFovValue)
        domeDiameterSpin.value = Math.round(StitchSettings.defaultDomeDiameterValue * 100)
        eyeSeparationSpin.value = Math.round(StitchSettings.defaultEyeSeparationValue * 1000)
        resolutionCombo.currentIndex = Math.max(0, resolutionCombo.model.indexOf(StitchSettings.defaultResolutionValue))
        cubemapResolutionCombo.currentIndex = Math.max(0, cubemapResolutionCombo.model.indexOf(StitchSettings.defaultCubemapResolutionValue))
        msaaCombo.currentIndex = Math.max(0, msaaCombo.model.indexOf(StitchSettings.defaultMsaaValue))
        formatCombo.currentIndex = Math.max(0, formatCombo.model.indexOf(StitchSettings.defaultFormatValue))
        compressionSpin.value = StitchSettings.defaultCompressionValue
        qualitySpin.value = StitchSettings.defaultQualityValue
        loadingThreadsSpin.value = StitchSettings.defaultLoadingThreadsValue
        encodingThreadsSpin.value = StitchSettings.defaultEncodingThreadsValue
        startIndexSpin.value = StitchSettings.defaultStartIndexValue
        stopIndexSpin.value = StitchSettings.defaultStopIndexValue
        outputStartIndexSpin.value = StitchSettings.defaultOutputStartIndexValue
        stepsSpin.value = StitchSettings.defaultStepsValue
        customFrameListField.text = StitchSettings.defaultCustomFrameListValue
        loadAttemptsSpin.value = StitchSettings.defaultLoadTextureAttemptsMaxValue
        loadSleepSpin.value = StitchSettings.defaultLoadTextureAttemptsSleepValue
        leftRotationCombo.currentIndex = rotationIndex(StitchSettings.defaultLeftRotationValue)
        rightRotationCombo.currentIndex = rotationIndex(StitchSettings.defaultRightRotationValue)
        topRotationCombo.currentIndex = rotationIndex(StitchSettings.defaultTopRotationValue)
        bottomRotationCombo.currentIndex = rotationIndex(StitchSettings.defaultBottomRotationValue)
    }

    FileDialog {
        id: faceDialog
        property var targetField
        parentWindow: root.Window.window
        title: qsTr("Choose cubemap face")
        nameFilters: [ qsTr("Images (*.png *.jpg *.jpeg *.tga *.bmp)"), qsTr("All files (*)") ]
        onAccepted: targetField.text = app.urlToPath(selectedFile)
    }

    FolderDialog {
        id: directoryDialog
        property var targetField
        parentWindow: root.Window.window
        title: qsTr("Choose output directory")
        onAccepted: targetField.text = app.urlToPath(selectedFolder)
    }

    function pickFile(field) {
        faceDialog.targetField = field
        faceDialog.currentFolder = field.text
        faceDialog.open()
    }

    function pickDirectory(field) {
        directoryDialog.targetField = field
        directoryDialog.currentFolder = field.text
        directoryDialog.open()
    }

    onOpened: loadFields()

    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: Math.max(0, parent.width - Kirigami.Units.largeSpacing * 2)
            x: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Information
                icon.source: "dialog-information"
                text: qsTr("Startup defaults are stored with KDE KConfig and applied when C-Stitch opens.")
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true

                TextField { id: leftFaceField; Kirigami.FormData.label: qsTr("Left face:"); Layout.fillWidth: true }
                Button { text: qsTr("Browse left..."); icon.name: "document-open"; icon.color: Kirigami.Theme.textColor; onClicked: pickFile(leftFaceField) }
                TextField { id: rightFaceField; Kirigami.FormData.label: qsTr("Right face:"); Layout.fillWidth: true }
                Button { text: qsTr("Browse right..."); icon.name: "document-open"; icon.color: Kirigami.Theme.textColor; onClicked: pickFile(rightFaceField) }
                TextField { id: topFaceField; Kirigami.FormData.label: qsTr("Top face:"); Layout.fillWidth: true }
                Button { text: qsTr("Browse top..."); icon.name: "document-open"; icon.color: Kirigami.Theme.textColor; onClicked: pickFile(topFaceField) }
                TextField { id: bottomFaceField; Kirigami.FormData.label: qsTr("Bottom face:"); Layout.fillWidth: true }
                Button { text: qsTr("Browse bottom..."); icon.name: "document-open"; icon.color: Kirigami.Theme.textColor; onClicked: pickFile(bottomFaceField) }

                TextField { id: rightLeftFaceField; Kirigami.FormData.label: qsTr("Right-eye left:"); Layout.fillWidth: true }
                Button { text: qsTr("Browse right-eye left..."); icon.name: "document-open"; icon.color: Kirigami.Theme.textColor; onClicked: pickFile(rightLeftFaceField) }
                TextField { id: rightRightFaceField; Kirigami.FormData.label: qsTr("Right-eye right:"); Layout.fillWidth: true }
                Button { text: qsTr("Browse right-eye right..."); icon.name: "document-open"; icon.color: Kirigami.Theme.textColor; onClicked: pickFile(rightRightFaceField) }
                TextField { id: rightTopFaceField; Kirigami.FormData.label: qsTr("Right-eye top:"); Layout.fillWidth: true }
                Button { text: qsTr("Browse right-eye top..."); icon.name: "document-open"; icon.color: Kirigami.Theme.textColor; onClicked: pickFile(rightTopFaceField) }
                TextField { id: rightBottomFaceField; Kirigami.FormData.label: qsTr("Right-eye bottom:"); Layout.fillWidth: true }
                Button { text: qsTr("Browse right-eye bottom..."); icon.name: "document-open"; icon.color: Kirigami.Theme.textColor; onClicked: pickFile(rightBottomFaceField) }

                // New output path structure: OutputDirectory/OutputName/SubDir/Filename
                // Example paths:
                // C:/output/myproject/mono/stitched
                // C:/output/myproject/left/stitched_L
                // C:/output/myproject/right/stitched_R
                
                TextField { id: outputDirectoryField; Kirigami.FormData.label: qsTr("Output folder:"); Layout.fillWidth: true }
                Button { text: qsTr("Browse output..."); icon.name: "folder-open"; icon.color: Kirigami.Theme.textColor; onClicked: pickDirectory(outputDirectoryField) }
                
                // Show output paths that will be created
                Item {
                    Layout.preferredHeight: Kirigami.Units.smallSpacing
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Output paths:")
                    font.bold: true
                }
                Label {
                    text: qsTr("  • {0}/{1}/").arg(outputDirectoryField.text).arg(outputNameField.text)
                    opacity: 0.85
                }
                Label {
                    text: qsTr("  • {0}/{1}_L/").arg(outputDirectoryField.text).arg(outputNameField.text)
                    opacity: 0.85
                }
                Label {
                    text: qsTr("  • {0}/{1}_R/").arg(outputDirectoryField.text).arg(outputNameField.text)
                    opacity: 0.85
                }

                // Editable subdirectory settings
                Item {
                    Layout.preferredHeight: Kirigami.Units.largeSpacing
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Subdirectories:")
                    font.bold: true
                }
                TextField { id: monoSubDirField; Kirigami.FormData.label: qsTr("Mono subfolder:"); Layout.fillWidth: true; text: StitchSettings.monoSubDir }
                TextField { id: leftSubDirField; Kirigami.FormData.label: qsTr("Left subfolder:"); Layout.fillWidth: true; text: StitchSettings.leftSubDir }
                TextField { id: rightSubDirField; Kirigami.FormData.label: qsTr("Right subfolder:"); Layout.fillWidth: true; text: StitchSettings.rightSubDir }

                TextField { id: outputNameField; Kirigami.FormData.label: qsTr("Filename stem:"); Layout.fillWidth: true }

                CheckBox { id: stereoCheck; Kirigami.FormData.label: qsTr("Stereo:"); text: qsTr("Use off-axis eye separation") }
                ComboBox {
                    id: stereoEyeCombo
                    Kirigami.FormData.label: qsTr("Stereo eye:")
                    enabled: stereoCheck.checked
                    model: root.stereoEyeValues
                    displayText: root.stereoEyeLabel(currentValue)
                    delegate: ItemDelegate { text: root.stereoEyeLabel(modelData); width: ListView.view.width }
                }
                CheckBox { id: alphaCheck; Kirigami.FormData.label: qsTr("Alpha:"); text: qsTr("Preserve alpha") }
                CheckBox { id: fxaaCheck; Kirigami.FormData.label: qsTr("FXAA:"); text: qsTr("Enable FXAA") }
                CheckBox { id: cubicCheck; Kirigami.FormData.label: qsTr("Filtering:"); text: qsTr("Use cubic filtering") }
                CheckBox { id: autoDetectCheck; Kirigami.FormData.label: qsTr("Detection:"); text: qsTr("Automatic face detection") }

                SpinBox { id: fisheyeFovSpin; Kirigami.FormData.label: qsTr("Fisheye FOV (deg):"); from: 1; to: 360; editable: true }
                SpinBox { id: domeDiameterSpin; Kirigami.FormData.label: qsTr("Dome diameter (m):"); from: 1; to: 10000; editable: true; textFromValue: function(value, locale) { return Number(value / 100).toLocaleString(locale, "f", 2) }; valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 100) } }
                SpinBox { id: eyeSeparationSpin; Kirigami.FormData.label: qsTr("Eye separation (m):"); from: 0; to: 500; editable: true; textFromValue: function(value, locale) { return Number(value / 1000).toLocaleString(locale, "f", 3) }; valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 1000) } }
                ComboBox { id: resolutionCombo; Kirigami.FormData.label: qsTr("Resolution (px):"); model: [256, 512, 1024, 2048, 3072, 4096, 8192]; displayText: currentValue + "x" + currentValue + " px"; delegate: ItemDelegate { text: modelData + "x" + modelData + " px"; width: ListView.view.width } }
                ComboBox { id: cubemapResolutionCombo; Kirigami.FormData.label: qsTr("Cubemap (px):"); model: [256, 512, 1024, 2048, 3072, 4096, 8192]; displayText: currentValue + "x" + currentValue + " px"; delegate: ItemDelegate { text: modelData + "x" + modelData + " px"; width: ListView.view.width } }
                ComboBox { id: msaaCombo; Kirigami.FormData.label: qsTr("MSAA (samples):"); model: [1, 2, 4, 8, 16, 32, 64] }

                ComboBox { id: formatCombo; Kirigami.FormData.label: qsTr("Format:"); model: ["png", "jpeg", "tga"] }
                SpinBox { id: compressionSpin; Kirigami.FormData.label: qsTr("Compression:"); from: 0; to: 9; editable: true }
                SpinBox { id: qualitySpin; Kirigami.FormData.label: qsTr("Quality:"); from: 1; to: 100; editable: true }
                SpinBox { id: loadingThreadsSpin; Kirigami.FormData.label: qsTr("Loading threads / eye:"); from: 4; to: 128; stepSize: 4; editable: true }
                SpinBox { id: encodingThreadsSpin; Kirigami.FormData.label: qsTr("Encoding threads / eye:"); from: 1; to: 128; editable: true }

                SpinBox { id: startIndexSpin; Kirigami.FormData.label: qsTr("Input start:"); from: -999999; to: 999999; editable: true }
                SpinBox { id: stopIndexSpin; Kirigami.FormData.label: qsTr("Input stop:"); from: -999999; to: 999999; editable: true }
                SpinBox { id: outputStartIndexSpin; Kirigami.FormData.label: qsTr("Output start:"); from: -999999; to: 999999; editable: true }
                SpinBox { id: stepsSpin; Kirigami.FormData.label: qsTr("Steps:"); from: 1; to: 10000; editable: true }
                TextField { id: customFrameListField; Kirigami.FormData.label: qsTr("Custom list:"); Layout.fillWidth: true; placeholderText: qsTr("12, 18, 25..30") }
                SpinBox { id: loadAttemptsSpin; Kirigami.FormData.label: qsTr("Attempts:"); from: 1; to: 1000; editable: true }
                SpinBox { id: loadSleepSpin; Kirigami.FormData.label: qsTr("Sleep (s):"); from: 0; to: 3600; editable: true }

                ComboBox { id: leftRotationCombo; Kirigami.FormData.label: qsTr("Left rotation:"); model: rotations }
                ComboBox { id: rightRotationCombo; Kirigami.FormData.label: qsTr("Right rotation:"); model: rotations }
                ComboBox { id: topRotationCombo; Kirigami.FormData.label: qsTr("Top rotation:"); model: rotations }
                ComboBox { id: bottomRotationCombo; Kirigami.FormData.label: qsTr("Bottom rotation:"); model: rotations }
            }
        }
    }

    footer: Pane {
        Kirigami.Theme.colorSet: Kirigami.Theme.Header
        Kirigami.Theme.inherit: false
        implicitHeight: footerLayout.implicitHeight + topPadding + bottomPadding

        RowLayout {
            id: footerLayout
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            Button {
                text: qsTr("Load Startup Values")
                icon.name: "document-open-recent"
                icon.color: Kirigami.Theme.textColor
                onClicked: {
                    StitchSettings.load()
                    root.loadFields()
                }
            }
            Button {
                text: qsTr("Use Current Values")
                icon.name: "view-refresh"
                icon.color: Kirigami.Theme.textColor
                enabled: root.controller !== null
                onClicked: root.loadRuntimeFields()
            }
            Button {
                text: qsTr("Load Default Values")
                icon.name: "edit-undo"
                icon.color: Kirigami.Theme.textColor
                onClicked: root.setDefaults()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Save Startup Values")
                icon.name: "document-save"
                icon.color: Kirigami.Theme.textColor
                onClicked: {
                    root.storeFields()
                    StitchSettings.save()
                    if (root.controller) {
                        root.controller.applyApplicationSettings()
                    }
                }
            }
            Button {
                text: qsTr("Close")
                icon.name: "dialog-close"
                icon.color: Kirigami.Theme.textColor
                onClicked: root.close()
            }
        }
    }
}