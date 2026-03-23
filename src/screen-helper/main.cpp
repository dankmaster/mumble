// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareHelperServer.h"

#include "ScreenShareIPC.h"

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QTextStream>

namespace {
	QString g_diagnosticsLogFilePath;
	QMutex g_diagnosticsLogMutex;
	QtMessageHandler g_previousMessageHandler = nullptr;

	QString messageTypeToken(QtMsgType type) {
		switch (type) {
			case QtDebugMsg:
				return QStringLiteral("DEBUG");
			case QtInfoMsg:
				return QStringLiteral("INFO");
			case QtWarningMsg:
				return QStringLiteral("WARN");
			case QtCriticalMsg:
				return QStringLiteral("ERROR");
			case QtFatalMsg:
				return QStringLiteral("FATAL");
		}

		return QStringLiteral("LOG");
	}

	void diagnosticsMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
		Q_UNUSED(context);

		if (!g_diagnosticsLogFilePath.isEmpty()) {
			QMutexLocker locker(&g_diagnosticsLogMutex);
			const QFileInfo logInfo(g_diagnosticsLogFilePath);
			if (!logInfo.absoluteDir().exists()) {
				logInfo.absoluteDir().mkpath(QStringLiteral("."));
			}

			QFile file(g_diagnosticsLogFilePath);
			if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
				QTextStream stream(&file);
				stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << ' '
					   << '[' << messageTypeToken(type) << "] " << msg << '\n';
				stream.flush();
			}
		}

		if (g_previousMessageHandler) {
			g_previousMessageHandler(type, context, msg);
		}
	}

	void installDiagnosticsLogging(const QString &logFilePath) {
		g_diagnosticsLogFilePath = logFilePath.trimmed();
		if (g_diagnosticsLogFilePath.isEmpty()) {
			return;
		}

		const QFileInfo logInfo(g_diagnosticsLogFilePath);
		if (!logInfo.absoluteDir().exists()) {
			logInfo.absoluteDir().mkpath(QStringLiteral("."));
		}

		g_previousMessageHandler = qInstallMessageHandler(diagnosticsMessageHandler);
		qInfo().noquote() << QStringLiteral("ScreenShareHelper: diagnostics log file %1").arg(g_diagnosticsLogFilePath);
	}
} // namespace

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	app.setApplicationName(QStringLiteral("mumble-screen-helper"));

	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Mumble screen-share helper"));
	parser.addHelpOption();
	QCommandLineOption diagnosticsLogFileOption(QStringList{ QStringLiteral("diagnostics-log-file") },
												QStringLiteral("Write helper diagnostics to the given file."),
												QStringLiteral("path"));
	QCommandLineOption printCapabilitiesOption(QStringList{ QStringLiteral("print-capabilities-json") },
											   QStringLiteral("Print the helper capability payload as JSON and exit."));
	QCommandLineOption selfTestOption(QStringList{ QStringLiteral("self-test") },
									  QStringLiteral("Run a local publish/view helper self-test and exit."));
	parser.addOption(diagnosticsLogFileOption);
	parser.addOption(printCapabilitiesOption);
	parser.addOption(selfTestOption);
	parser.process(app);

	if (parser.isSet(diagnosticsLogFileOption)) {
		installDiagnosticsLogging(parser.value(diagnosticsLogFileOption));
	}

	ScreenShareHelperServer helperServer;
	if (parser.isSet(printCapabilitiesOption)) {
		QTextStream(stdout)
			<< QJsonDocument(Mumble::ScreenShare::IPC::makeSuccessReply(helperServer.capabilityPayload()))
				   .toJson(QJsonDocument::Compact)
			<< Qt::endl;
		return 0;
	}
	if (parser.isSet(selfTestOption)) {
		const QJsonObject reply = helperServer.runSelfTest();
		QTextStream(stdout) << QJsonDocument(reply).toJson(QJsonDocument::Compact) << Qt::endl;
		return Mumble::ScreenShare::IPC::replySucceeded(reply) ? 0 : 1;
	}

	QString errorMessage;
	if (!helperServer.start(&errorMessage)) {
		qCritical().noquote() << QStringLiteral("ScreenShareHelper: failed to start: %1").arg(errorMessage);
		return 1;
	}

	return app.exec();
}
