#!/usr/bin/env python3

# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.


import argparse
import re
from collections import OrderedDict

defaultValues = {}
structs = {}

def extractFields(classDefinition):
    # Bring all declarations on a single line
    classDefinition = re.sub(r",\s+", ",", classDefinition)
    # Make sure there are no spaces in templates
    classDefinition = classDefinition.replace("< ", "<").replace(" >", ">")
    # Remove function definitions (aka: lines that contain a parenthesis)
    classDefinition = re.sub(r".*\(.*", "", classDefinition)
    # Remove enum declarations
    classDefinition = re.sub(r"enum\s*\w+\s*\{.*?\};", "", classDefinition, flags=re.DOTALL)
    # Remove "unsigned" type specifier
    classDefinition = classDefinition.replace("unsigned ", "")
    # Remove "mutable" keyword
    classDefinition = classDefinition.replace("mutable ", "")
    # Remove comments
    classDefinition = re.sub(r"//.*", "", classDefinition)
    # Remove default value assignments
    classDefinition = re.sub(r"\s*=[^;]+;", ";", classDefinition)
    # remove semicolons
    classDefinition = classDefinition.replace(";", "")

    fields = OrderedDict()
    for currentLine in classDefinition.split("\n"):
        currentLine = currentLine.strip()

        if currentLine.startswith("//") or currentLine in ["private:", "public:"]:
            continue

        words = currentLine.split()

        if len(words) == 0:
            continue

        if words[0] in ["enum", "typedef", "struct"] or "static" in words:
            continue

        if len(words) > 2:
            # Comma-separated lists
            names = "".join(words[1:])
            words = [words[0], names]

        assert len(words) == 2, "Expected remaining lines to be of form <type> <name(s)>"

        for currentFieldName in words[1].split(","):
            currentFieldName = currentFieldName.strip()

            if not currentFieldName:
                continue

            fields[currentFieldName] = words[0]

    return fields


def extractClassDefinition(className, contents, classIdentifier = "class"):
    definition = contents[contents.find(classIdentifier + " " + className) :]
    curlyBrackets = 0
    endIndex = 0
    for c in definition:
        if c == '{':
            curlyBrackets += 1
        elif c == '}':
            curlyBrackets -= 1

            if curlyBrackets == 0:
                break
        
        endIndex += 1

    assert curlyBrackets == 0

    definition = definition[0 : endIndex]

    return definition


def getTemplateArguments(templateDef):
    relevantContent = templateDef[templateDef.find("<") + 1 : templateDef.rfind(">")]

    # Nested templates not yet supported
    assert not "<" in relevantContent

    args = relevantContent.split(",")
    args = [x.strip() for x in args]

    return args


def getDefaultValueForType(dataType):
    if dataType in ["int", "short", "long", "float", "double", "qreal"] or dataType.startswith("qint") or dataType.startswith("quint") or \
        dataType.startswith("uint"):
        return "42"
    elif match := re.search(r"optional<(.*)>", dataType):
        return getDefaultValueForType(match.group(1))
    elif dataType in ["bool"]:
        return "true"
    elif dataType in ["QString", "std::string"]:
        return "\"My String\""
    elif dataType in ["QByteArray"]:
        return "QByteArray::fromStdString(\"My ByteArray\")"
    elif dataType in ["QPoint"]:
        return "{ 4, 2 }"
    elif dataType in ["QVariant"]:
        return "QVariant(15)"
    elif dataType in ["QStringList"]:
        return "QStringList({ QStringLiteral(\"Another string\") })"
    elif dataType in ["QColor"]:
        return "QColor(QLatin1String(\"indigo\"))"
    elif dataType in ["QFont"]:
        return "QFont(QLatin1String(\"Helvetica\"))"
    elif dataType in ["QRect", "QRectF"]:
        return "{ 3, 5, 10, 7 }"
    elif dataType in ["QSize", "QSizeF"]:
        return "{ 8, 12 }"
    elif dataType in ["AudioTransmit"]:
        return "Settings::PushToTalk"
    elif dataType in ["VADSource"]:
        return "Settings::SignalToNoise"
    elif dataType in ["LoopMode"]:
        return "Settings::Server"
    elif dataType in ["ChannelExpand"]:
        return "Settings::AllChannels"
    elif dataType in ["ChannelDrag"]:
        return "Settings::DoNothing"
    elif dataType in ["ServerShow"]:
        return "Settings::ShowPopulated"
    elif dataType in ["IdleAction"]:
        return "Settings::Deafen"
    elif dataType in ["NoiseCancel"]:
        return "Settings::NoiseCancelOff"
    elif dataType in ["SpeechCleanupBackend"]:
        return "Settings::DeepFilterNetBackend"
    elif dataType in ["RemoteSpeechCleanupPreset"]:
        return "Settings::Aggressive"
    elif dataType in ["EchoCancelOptionID"]:
        return "EchoCancelOptionID::SPEEX_MULTICHANNEL"
    elif dataType in ["QuitBehavior"]:
        return "QuitBehavior::ALWAYS_QUIT"
    elif dataType in ["OverlayShow"]:
        return "OverlaySettings::HomeChannel"
    elif dataType in ["OverlayShow"]:
        return "OverlaySettings::HomeChannel"
    elif dataType in ["OverlaySort"]:
        return "OverlaySettings::LastStateChange"
    elif dataType in ["OverlayExclusionMode"]:
        return "OverlaySettings::BlacklistExclusionMode"
    elif dataType in ["Qt::Alignment"]:
        return "Qt::AlignJustify | Qt::AlignBaseline"
    elif dataType in ["WindowLayout"]:
        return "Settings::LayoutStacked"
    elif dataType in ["ModernLayoutPolicy"]:
        return "Settings::ModernLayoutFollowLegacy"
    elif dataType in ["AlwaysOnTopBehaviour"]:
        return "Settings::OnTopAlways"
    elif dataType in ["Search::SearchDialog::UserAction"]:
        return "Search::SearchDialog::UserAction::NONE"
    elif dataType in ["Search::SearchDialog::ChannelAction"]:
        return "Search::SearchDialog::ChannelAction::NONE"
    elif dataType in ["ProxyType"]:
        return "Settings::Socks5Proxy"
    elif dataType in ["RecordingMode"]:
        return "Settings::RecordingMultichannel"
    elif dataType in ["StyleType"]:
        return "StyleType::Dark"
    elif dataType.startswith("QMap") or dataType.startswith("QHash"):
        types = getTemplateArguments(dataType)

        assert len(types) == 2

        return "{ {" + getDefaultValueForType(types[0]) + ", " + getDefaultValueForType(types[1]) + "} }"
    elif dataType.startswith("QList"):
        types = getTemplateArguments(dataType)

        assert len(types) == 1

        return "{ " + getDefaultValueForType(types[0]) + " }"
    elif dataType.startswith("std::array"):
        args = getTemplateArguments(dataType)

        # std::array< Type, Size >
        assert len(args) == 2

        args[1] = int(args[1])

        string = "{ "
        for _ in range(args[1]):
            string += getDefaultValueForType(args[0]) + ", "

        if args[1] > 0:
            # remove trailing comma
            string = string[:-len(", ")]

        return string + " }"
    elif dataType in ["KeyPair"]:
        # We can't really create a certificate here, so we have to use a default-constructed value
        return "{}"

    if dataType in defaultValues:
        return defaultValues[dataType]

    raise RuntimeError("No known default value for type " + dataType)



def generateTestBody(settingsFields, settingsClassName, excludeFields = []):
    contents = "#include <QObject>\n"
    contents += "#include <QList>\n"
    contents += "#include <QPair>\n"
    contents += "#include <QString>\n"
    contents += "#include <QtTest>\n"
    contents += "#include \"" + settingsClassName + ".h\"\n"
    contents += "#include \"JSONSerialization.h\"\n"
    contents += "#include \"SpeechCleanup.h\"\n"
    contents += "#include <nlohmann/json.hpp>\n"
    contents += "\n"
    contents += "class TestSettingsJSONSerialization : public QObject {\n"
    contents += "\tQ_OBJECT\n"
    contents += "\t" + settingsClassName + " createSettingsInstance() const {\n"
    contents += "\t\t" + settingsClassName + " settings;\n"
    for fieldName in settingsFields:
        if fieldName in excludeFields:
            continue
        if settingsFields[fieldName] == "OverlaySettings":
            for overlayField in structs["OverlaySettings"]:
                contents += "\t\tsettings." + fieldName + "." + overlayField + " = "\
                        + getDefaultValueForType(structs["OverlaySettings"][overlayField]) + ";"
        elif settingsFields[fieldName] == "bool":
            # For boolean values, we simply use the inverse of whatever the default is
            contents += "\t\tsettings." + fieldName + " = !settings." + fieldName + ";\n"
        else:
            contents += "\t\tsettings." + fieldName + " = " + getDefaultValueForType(settingsFields[fieldName]) + ";\n"
    contents += "\n"
    contents += "\t\treturn settings;\n"
    contents += "\t}\n"
    contents += "\n"

    contents += "private slots:\n"
    contents += "\tvoid noDefaultValuesMatched() {\n"
    contents += "\t\tconst " + settingsClassName + " defaults;\n"
    contents += "\t\tconst " + settingsClassName + " myInstance = createSettingsInstance();\n"
    for fieldName in settingsFields:
        if fieldName in excludeFields:
            continue
        contents += "\t\tQVERIFY2(defaults." + fieldName + "!= myInstance." + fieldName  + ", \"Field '" \
                + fieldName + "' was set to its default value (breaking underlying assumption of the following test)\");\n"
    contents += "\t}\n"
    contents += "\n"
    contents += "\tvoid testJSONSerialization() {\n"
    contents += "\t\tconst " + settingsClassName + " original = createSettingsInstance();\n"
    contents += "\t\tnlohmann::json jsonRepresentation = original;\n"
    contents += "\t\tconst " + settingsClassName + " deserialized = jsonRepresentation;\n"
    contents += "\t\t" + settingsClassName + " expected = original;\n"
    contents += "\t\texpected.noiseCancelModelId = Mumble::SpeechCleanup::normalizedModelId(expected.noiseCancelBackend, expected.noiseCancelModelId);\n"
    contents += "\t\tif (!Mumble::SpeechCleanup::usesCustomModelPath(expected.noiseCancelBackend, expected.noiseCancelModelId)) {\n"
    contents += "\t\t\texpected.noiseCancelCustomModelPath.clear();\n"
    contents += "\t\t}\n"
    contents += "\t\texpected.remoteSpeechCleanupModelId = Mumble::SpeechCleanup::normalizedModelId(expected.remoteSpeechCleanupBackend, expected.remoteSpeechCleanupModelId);\n"
    contents += "\t\tif (!Mumble::SpeechCleanup::usesCustomModelPath(expected.remoteSpeechCleanupBackend, expected.remoteSpeechCleanupModelId)) {\n"
    contents += "\t\t\texpected.remoteSpeechCleanupCustomModelPath.clear();\n"
    contents += "\t\t}\n"
    contents += "\n"
    for fieldName in settingsFields:
        if fieldName in excludeFields:
            continue
        else:
            contents += "\t\tQCOMPARE(expected." + fieldName + ", deserialized." + fieldName + ");\n"
    contents += "\t}\n"
    contents += "\n"
    contents += "\tvoid modernWindowLayoutMigratesToForkPolicy() {\n"
    contents += "\t\tnlohmann::json jsonRepresentation;\n"
    contents += "\t\tjsonRepresentation[\"settings_version\"] = 1;\n"
    contents += "\t\tjsonRepresentation[\"ui\"][\"window_layout\"] = \"Modern\";\n"
    contents += "\n"
    contents += "\t\tconst " + settingsClassName + " deserialized = jsonRepresentation;\n"
    contents += "\t\tQCOMPARE(deserialized.wlWindowLayout, Settings::LayoutHybrid);\n"
    contents += "\t\tQCOMPARE(deserialized.modernLayoutPolicy, Settings::ModernLayoutForced);\n"
    contents += "\n"
    contents += "\t\tnlohmann::json savedRepresentation = deserialized;\n"
    contents += "\t\tQVERIFY(savedRepresentation.contains(\"ui\"));\n"
    contents += "\t\tQCOMPARE(QString::fromStdString(savedRepresentation[\"ui\"][\"window_layout\"].get<std::string>()), QStringLiteral(\"Hybrid\"));\n"
    contents += "\t}\n"
    contents += "\n"
    contents += "\tvoid legacyWindowLayoutsStayCompatible() {\n"
    contents += "\t\tconst QList<QPair<QString, Settings::WindowLayout>> cases = {\n"
    contents += "\t\t\t{ QStringLiteral(\"Classic\"), Settings::LayoutClassic },\n"
    contents += "\t\t\t{ QStringLiteral(\"Stacked\"), Settings::LayoutStacked },\n"
    contents += "\t\t\t{ QStringLiteral(\"Hybrid\"), Settings::LayoutHybrid },\n"
    contents += "\t\t\t{ QStringLiteral(\"Custom\"), Settings::LayoutCustom },\n"
    contents += "\t\t};\n"
    contents += "\n"
    contents += "\t\tfor (const auto &current : cases) {\n"
    contents += "\t\t\tnlohmann::json jsonRepresentation;\n"
    contents += "\t\t\tjsonRepresentation[\"settings_version\"] = 1;\n"
    contents += "\t\t\tjsonRepresentation[\"ui\"][\"window_layout\"] = current.first.toStdString();\n"
    contents += "\n"
    contents += "\t\t\tconst " + settingsClassName + " deserialized = jsonRepresentation;\n"
    contents += "\t\t\tQCOMPARE(deserialized.wlWindowLayout, current.second);\n"
    contents += "\t\t}\n"
    contents += "\t}\n"
    contents += "\n"
    contents += "\tvoid unknownWindowLayoutFallsBackToDefault() {\n"
    contents += "\t\tnlohmann::json jsonRepresentation;\n"
    contents += "\t\tjsonRepresentation[\"settings_version\"] = 1;\n"
    contents += "\t\tjsonRepresentation[\"ui\"][\"window_layout\"] = \"FutureLayout\";\n"
    contents += "\n"
    contents += "\t\tconst " + settingsClassName + " defaults;\n"
    contents += "\t\tconst " + settingsClassName + " deserialized = jsonRepresentation;\n"
    contents += "\t\tQCOMPARE(deserialized.wlWindowLayout, defaults.wlWindowLayout);\n"
    contents += "\t}\n"
    contents += "};\n"
    contents += "\n"
    contents += "QTEST_MAIN(TestSettingsJSONSerialization)\n"
    contents += "#include \"TestSettingsJSONSerialization.moc\"\n"

    return contents


def generateDefaultConstruction(structName, fields):
    contents =  structName + "{"
    for currentField in fields:
        contents += "/*" + currentField + "*/ " + getDefaultValueForType(fields[currentField]) + ","

    if len(fields) > 0:
        # remove trailing comma
        contents = contents[:-1]

    return contents + "}"



def main():
    parser = argparse.ArgumentParser("Generates the test case for the JSON (de)serialization of the settings struct")
    parser.add_argument("--settings-header", help="The path to the header file containing the definition of the Settings struct", metavar="PATH",
            required = True)
    parser.add_argument("--settings-struct-name", help="The name of the settings struct", default="Settings")
    parser.add_argument("--ignore-fields", help="A comma-separated list of fields in the settings struct to exclude from the test", default="kpCertificate")
    parser.add_argument("--output-file", help="Path to where the output shall be written. If none is given, the result is written to stdout", metavar="PATH")

    args = parser.parse_args()

    headerContents = open(args.settings_header, "r").read()

    for currentMatch in re.finditer(r"struct\s*(\w+)\s\{", headerContents):
        definition = extractClassDefinition(currentMatch.group(1), headerContents[currentMatch.start() : ], classIdentifier="struct")

        fields = extractFields(definition)

        defaultValues[currentMatch.group(1)] = generateDefaultConstruction(currentMatch.group(1), fields)

        structs[currentMatch.group(1)] = fields

    ignoredFields = args.ignore_fields.split(",")
    ignoredFields = [x.strip() for x in ignoredFields]

    contents = generateTestBody(structs[args.settings_struct_name], args.settings_struct_name, excludeFields=ignoredFields)

    if args.output_file:
        outFile = open(args.output_file, "w")
        outFile.write(contents)
    else:
        print(contents)




if __name__ == "__main__":
    main()
