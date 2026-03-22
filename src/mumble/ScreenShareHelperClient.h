// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SCREENSHAREHELPERCLIENT_H_
#define MUMBLE_MUMBLE_SCREENSHAREHELPERCLIENT_H_

#include "Mumble.pb.h"
#include "ScreenShareIPC.h"

#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>

struct ScreenShareSession;

class ScreenShareHelperClient : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ScreenShareHelperClient)

public:
	struct CapabilitySnapshot {
		bool supportsSignaling = true;
		bool helperAvailable   = false;
		bool captureSupported  = false;
		bool viewSupported     = false;
		QList< int > supportedCodecs;
		QList< int > runtimeRelayTransports;
		unsigned int maxWidth  = 0;
		unsigned int maxHeight = 0;
		unsigned int maxFps    = 0;
		QString helperExecutable;
	};

	explicit ScreenShareHelperClient(QObject *parent = nullptr);

	static CapabilitySnapshot detectLocalCapabilities();
	static void applyAdvertisedCapabilities(MumbleProto::Version &msg);

	const CapabilitySnapshot &capabilities() const;
	bool startPublish(const ScreenShareSession &session, QString *errorMessage = nullptr);
	bool stopPublish(const QString &streamID, QString *errorMessage = nullptr);
	bool startView(const ScreenShareSession &session, QString *errorMessage = nullptr);
	bool stopView(const QString &streamID, QString *errorMessage = nullptr);

public slots:
	void refreshCapabilities();

signals:
	void capabilitiesChanged();

private:
	static QString defaultHelperExecutablePath();
	static CapabilitySnapshot capabilitySnapshotFromPayload(const QJsonObject &payload, const QString &helperExecutable);
	static QJsonObject payloadFromSession(const ScreenShareSession &session);
	static QJsonObject sendRequest(Mumble::ScreenShare::IPC::Command command, const QJsonObject &payload,
								   const QString &helperExecutable, QString *errorMessage, bool launchIfNeeded = true);
	static bool ensureHelperRunning(const QString &helperExecutable, QString *errorMessage = nullptr);

	CapabilitySnapshot m_capabilities;
};

#endif // MUMBLE_MUMBLE_SCREENSHAREHELPERCLIENT_H_
