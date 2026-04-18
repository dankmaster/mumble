// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNSHELLBRIDGE_H_
#define MUMBLE_MUMBLE_MODERNSHELLBRIDGE_H_

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include <QtCore/QObject>
#include <QtCore/QVariant>

class ModernShellBridge : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ModernShellBridge)
	Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY snapshotChanged)

public:
	explicit ModernShellBridge(QObject *parent = nullptr);

	QVariantMap snapshot() const;
	void setSnapshot(const QVariantMap &snapshot);

	Q_INVOKABLE void ready();
	Q_INVOKABLE void selectScope(const QString &scopeToken);
	Q_INVOKABLE void joinVoiceChannel(const QString &scopeToken);
	Q_INVOKABLE void sendMessage(const QString &message);
	Q_INVOKABLE void messageParticipant(qulonglong session);
	Q_INVOKABLE void joinParticipant(qulonglong session);
	Q_INVOKABLE void invokeParticipantAction(qulonglong session, const QString &actionId);
	Q_INVOKABLE void loadOlderHistory();
	Q_INVOKABLE void markRead();
	Q_INVOKABLE void toggleSelfMute();
	Q_INVOKABLE void toggleSelfDeaf();
	Q_INVOKABLE void openConnectDialog();
	Q_INVOKABLE void disconnectServer();
	Q_INVOKABLE void openSettings();
	Q_INVOKABLE void openImagePicker();
	Q_INVOKABLE void invokeAppAction(const QString &actionId);

signals:
	void bootReady();
	void snapshotChanged();
	void scopeSelectionRequested(const QString &scopeToken);
	void voiceJoinRequested(const QString &scopeToken);
	void messageSendRequested(const QString &message);
	void participantMessageRequested(qulonglong session);
	void participantJoinRequested(qulonglong session);
	void participantActionRequested(qulonglong session, const QString &actionId);
	void olderHistoryRequested();
	void markReadRequested();
	void selfMuteToggleRequested();
	void selfDeafToggleRequested();
	void connectDialogRequested();
	void disconnectRequested();
	void settingsRequested();
	void imagePickerRequested();
	void appActionRequested(const QString &actionId);

private:
	QVariantMap m_snapshot;
};

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)

#endif // MUMBLE_MUMBLE_MODERNSHELLBRIDGE_H_
