// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_SCREENHELPER_SCREENSHAREEXTERNALPROCESS_H_
#define MUMBLE_SCREENHELPER_SCREENSHAREEXTERNALPROCESS_H_

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

class QObject;
class QProcess;

class ScreenShareExternalProcess {
public:
	struct RuntimeSupport {
		QString ffmpegPath;
		QString ffplayPath;
		QString edgePath;
		QString chromePath;
		QString firefoxPath;
		bool ffmpegAvailable           = false;
		bool ffplayAvailable           = false;
		bool graphicalSessionAvailable = false;
		bool x11GrabAvailable          = false;
		bool gdigrabAvailable          = false;
		bool lavfiAvailable            = false;
		bool x11DisplayAvailable       = false;
		bool windowedViewerAvailable   = false;
		bool h264NvencAvailable        = false;
		bool h264VaapiAvailable        = false;
		bool h264MfAvailable           = false;
		bool h264QsvAvailable          = false;
		bool libx264Available          = false;
		bool av1NvencAvailable         = false;
		bool av1VaapiAvailable         = false;
		bool av1MfAvailable            = false;
		bool av1QsvAvailable           = false;
		bool libSvtAv1Available        = false;
		bool libVpxVp8Available        = false;
		bool libVpxVp9Available        = false;
		bool fileProtocolAvailable     = false;
		bool rtmpProtocolAvailable     = false;
		bool rtmpsProtocolAvailable    = false;
		bool edgeAvailable             = false;
		bool chromeAvailable           = false;
		bool firefoxAvailable          = false;
		bool browserWebRtcAvailable    = false;
	};

	struct LaunchResult {
		bool started  = false;
		bool usedStub = false;
		QString errorMessage;
		QString executionMode;
		QString endpointUrl;
		QString selectedEncoder;
		QString selectedCaptureSource;
		QString selectedRenderer;
		QString program;
		QStringList warnings;
		QProcess *process = nullptr;
	};

	static RuntimeSupport probeRuntimeSupport(bool refresh = false);
	static QJsonObject runtimeSupportToJson(const RuntimeSupport &support);
	static LaunchResult startPublish(const QJsonObject &plan, QObject *parent = nullptr);
	static LaunchResult startView(const QJsonObject &plan, QObject *parent = nullptr);
	static void stop(QProcess *process, int timeoutMsec = 2000);
};

#endif // MUMBLE_SCREENHELPER_SCREENSHAREEXTERNALPROCESS_H_
