// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SCREENSHAREMANAGER_H_
#define MUMBLE_MUMBLE_SCREENSHAREMANAGER_H_

#include "Mumble.pb.h"
#include "ScreenShareHelperClient.h"

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>

struct ScreenShareSession {
	QString streamID;
	unsigned int ownerSession = 0;
	MumbleProto::ScreenShareScope scope = MumbleProto::ScreenShareScopeChannel;
	unsigned int scopeID = 0;
	QString relayUrl;
	QString relayRoomID;
	QString relayToken;
	QString relaySessionID;
	MumbleProto::ScreenShareRelayTransport relayTransport = MumbleProto::ScreenShareRelayTransportUnknown;
	MumbleProto::ScreenShareRelayRole relayRole = MumbleProto::ScreenShareRelayRoleViewer;
	quint64 relayTokenExpiresAt = 0;
	quint64 createdAt = 0;
	MumbleProto::ScreenShareLifecycleState state = MumbleProto::ScreenShareLifecycleStatePending;
	MumbleProto::ScreenShareCodec codec = MumbleProto::ScreenShareCodecUnknown;
	QList< int > codecFallbackOrder;
	unsigned int width = 0;
	unsigned int height = 0;
	unsigned int fps = 0;
	unsigned int bitrateKbps = 0;
};

class ScreenShareManager : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ScreenShareManager)

public:
	explicit ScreenShareManager(QObject *parent = nullptr);

	static ScreenShareHelperClient::CapabilitySnapshot detectAdvertisedCapabilities();

	ScreenShareHelperClient &helperClient();
	const ScreenShareHelperClient &helperClient() const;
	void logLocalShareAvailabilityDiagnostic(const QString &context = QString()) const;

	bool canRequestLocalShare() const;
	QString localShareUnavailableReason() const;
	bool canViewSession(const QString &streamID) const;
	bool isPublishingSession(const QString &streamID) const;
	bool isViewingSession(const QString &streamID) const;
	void requestStartChannelShare(unsigned int channelID = 0);
	void requestStartViewing(const QString &streamID);
	void requestStopViewing(const QString &streamID);
	void requestStopShare(const QString &streamID);

	const QHash< QString, ScreenShareSession > &sessions() const;
	bool hasSession(const QString &streamID) const;

public slots:
	void resetState();
	void handleScreenShareState(const MumbleProto::ScreenShareState &msg);
	void handleScreenShareOffer(const MumbleProto::ScreenShareOffer &msg);
	void handleScreenShareAnswer(const MumbleProto::ScreenShareAnswer &msg);
	void handleScreenShareIceCandidate(const MumbleProto::ScreenShareIceCandidate &msg);
	void handleScreenShareStop(const MumbleProto::ScreenShareStop &msg);

signals:
	void sessionUpdated(const QString &streamID);
	void sessionStopped(const QString &streamID);

private:
	ScreenShareSession sessionFromState(const MumbleProto::ScreenShareState &msg) const;
	bool canViewSession(const ScreenShareSession &session) const;
	bool canPublishSession(const ScreenShareSession &session) const;
	bool shouldAutoViewSession(const ScreenShareSession &session) const;
	void startLocalPublishSession(const ScreenShareSession &session);
	void startLocalViewSession(const ScreenShareSession &session);
	void stopLocalPublishSession(const QString &streamID);
	void stopLocalViewSession(const QString &streamID);
	void logRemoteViewAvailability(const ScreenShareSession &session);
	void stopLocalHelperSessions(const QString &streamID);

	ScreenShareHelperClient *m_helperClient;
	QHash< QString, ScreenShareSession > m_sessions;
	QSet< QString > m_activePublishSessions;
	QSet< QString > m_activeViewSessions;
	QSet< QString > m_announcedViewableSessions;
	mutable QString m_lastLoggedAvailabilityContext;
	mutable QString m_lastLoggedAvailabilityReason;
};

#endif // MUMBLE_MUMBLE_SCREENSHAREMANAGER_H_
