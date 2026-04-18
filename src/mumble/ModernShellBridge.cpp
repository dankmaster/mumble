// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernShellBridge.h"

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include "Global.h"

#include <QtCore/QDateTime>
#include <QtCore/QFile>

namespace {
	void appendModernShellConnectTrace(const QString &message) {
		if (qEnvironmentVariableIntValue("MUMBLE_CONNECT_TRACE") == 0) {
			return;
		}

		QFile traceFile(Global::get().qdBasePath.filePath(QLatin1String("shared-modern-connect-trace.log")));
		if (!traceFile.open(QIODevice::Append | QIODevice::Text)) {
			return;
		}

		const QByteArray line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8()
								+ " UI " + message.toUtf8() + '\n';
		traceFile.write(line);
		traceFile.flush();
	}
} // namespace

ModernShellBridge::ModernShellBridge(QObject *parent) : QObject(parent) {
}

QVariantMap ModernShellBridge::snapshot() const {
	return m_snapshot;
}

void ModernShellBridge::setSnapshot(const QVariantMap &snapshot) {
	appendModernShellConnectTrace(
		QStringLiteral("ModernShellBridge::setSnapshot enter keys=%1").arg(snapshot.keys().size()));
	m_snapshot = snapshot;
	appendModernShellConnectTrace(QStringLiteral("ModernShellBridge::setSnapshot before-emit"));
	emit snapshotChanged();
	appendModernShellConnectTrace(QStringLiteral("ModernShellBridge::setSnapshot after-emit"));
}

void ModernShellBridge::ready() {
	appendModernShellConnectTrace(QStringLiteral("ModernShellBridge::ready"));
	emit bootReady();
}

void ModernShellBridge::selectScope(const QString &scopeToken) {
	emit scopeSelectionRequested(scopeToken.trimmed());
}

void ModernShellBridge::joinVoiceChannel(const QString &scopeToken) {
	emit voiceJoinRequested(scopeToken.trimmed());
}

void ModernShellBridge::sendMessage(const QString &message) {
	emit messageSendRequested(message);
}

void ModernShellBridge::messageParticipant(const qulonglong session) {
	emit participantMessageRequested(session);
}

void ModernShellBridge::joinParticipant(const qulonglong session) {
	emit participantJoinRequested(session);
}

void ModernShellBridge::invokeParticipantAction(const qulonglong session, const QString &actionId) {
	emit participantActionRequested(session, actionId.trimmed());
}

void ModernShellBridge::loadOlderHistory() {
	emit olderHistoryRequested();
}

void ModernShellBridge::markRead() {
	emit markReadRequested();
}

void ModernShellBridge::toggleSelfMute() {
	emit selfMuteToggleRequested();
}

void ModernShellBridge::toggleSelfDeaf() {
	emit selfDeafToggleRequested();
}

void ModernShellBridge::openConnectDialog() {
	emit connectDialogRequested();
}

void ModernShellBridge::disconnectServer() {
	emit disconnectRequested();
}

void ModernShellBridge::openSettings() {
	emit settingsRequested();
}

void ModernShellBridge::openImagePicker() {
	emit imagePickerRequested();
}

void ModernShellBridge::invokeAppAction(const QString &actionId) {
	emit appActionRequested(actionId.trimmed());
}

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)
