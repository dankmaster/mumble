// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareMediaSupport.h"

#include "ScreenShare.h"
#include "ScreenShareExternalProcess.h"

#include <QtCore/QLibrary>
#include <QtCore/QStringList>

namespace {
bool envFlagEnabled(const char *name) {
	const QString value = qEnvironmentVariable(name).trimmed().toLower();
	return value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
		   || value == QLatin1String("on");
}

void appendIf(QStringList *values, const bool condition, const QString &value) {
	if (condition && values && !values->contains(value)) {
		values->append(value);
	}
}

bool hasHardwareEncoder(const ScreenShareExternalProcess::RuntimeSupport &support) {
	return support.h264NvencAvailable || support.h264VaapiAvailable || support.h264MfAvailable
		   || support.h264QsvAvailable || support.av1NvencAvailable || support.av1VaapiAvailable
		   || support.av1MfAvailable || support.av1QsvAvailable;
}

QStringList ingestProtocolsForRuntime(const ScreenShareExternalProcess::RuntimeSupport &support) {
	QStringList protocols;
	appendIf(&protocols, support.fileProtocolAvailable, QStringLiteral("file"));
	appendIf(&protocols, support.rtmpProtocolAvailable, QStringLiteral("rtmp"));
	appendIf(&protocols, support.rtmpsProtocolAvailable, QStringLiteral("rtmps"));
	appendIf(&protocols, support.browserWebRtcAvailable, QStringLiteral("webrtc"));
	return protocols;
}

#ifdef Q_OS_LINUX
bool pipeWireRuntimeAvailable(QString *libraryName) {
	const QStringList names{ QStringLiteral("libpipewire.so"), QStringLiteral("libpipewire-0.3.so"),
							 QStringLiteral("libpipewire-0.3.so.0") };

	for (const QString &name : names) {
		QLibrary lib(name);
		if (!lib.load()) {
			continue;
		}

		if (libraryName) {
			*libraryName = lib.fileName();
		}

		lib.unload();
		return true;
	}

	return false;
}
#endif
} // namespace

ScreenShareMediaSupport::CapabilitySummary ScreenShareMediaSupport::probe() {
	CapabilitySummary summary;
	summary.supportedCodecs = Mumble::ScreenShare::defaultCodecPreferenceList();
	summary.maxWidth        = Mumble::ScreenShare::DEFAULT_MAX_WIDTH;
	summary.maxHeight       = Mumble::ScreenShare::DEFAULT_MAX_HEIGHT;
	summary.maxFps          = Mumble::ScreenShare::DEFAULT_MAX_FPS;

	const ScreenShareExternalProcess::RuntimeSupport runtimeSupport = ScreenShareExternalProcess::probeRuntimeSupport();
	const bool testPatternEnabled =
		envFlagEnabled("MUMBLE_SCREENSHARE_TEST_PATTERN")
		|| qEnvironmentVariable("MUMBLE_SCREENSHARE_CAPTURE_SOURCE").trimmed().toLower()
			   == QLatin1String("test-pattern")
		|| qEnvironmentVariable("MUMBLE_SCREENSHARE_CAPTURE_SOURCE").trimmed().toLower() == QLatin1String("lavfi");
	summary.hardwareEncodeSupported   = hasHardwareEncoder(runtimeSupport);
	summary.hardwareEncodingPreferred = summary.hardwareEncodeSupported;
	summary.hardwareDecodeSupported   = runtimeSupport.browserWebRtcAvailable;
	summary.ingestProtocols           = ingestProtocolsForRuntime(runtimeSupport);

#ifdef Q_OS_LINUX
	QString libraryName;
	const bool pipeWireAvailable = pipeWireRuntimeAvailable(&libraryName);
	const bool x11CaptureAvailable =
		runtimeSupport.ffmpegAvailable && runtimeSupport.x11GrabAvailable && runtimeSupport.x11DisplayAvailable;
	const bool browserCaptureAvailable = runtimeSupport.browserWebRtcAvailable;

	summary.captureSupported = x11CaptureAvailable || browserCaptureAvailable || testPatternEnabled;
	summary.viewSupported =
		runtimeSupport.ffplayAvailable || runtimeSupport.ffmpegAvailable || runtimeSupport.browserWebRtcAvailable;
	appendIf(&summary.captureBackends, x11CaptureAvailable, QStringLiteral("x11grab"));
	appendIf(&summary.captureBackends, browserCaptureAvailable, QStringLiteral("browser-webrtc"));
	appendIf(&summary.captureBackends, testPatternEnabled && runtimeSupport.lavfiAvailable,
			 QStringLiteral("lavfi-test-pattern"));
	summary.captureBackend = testPatternEnabled
								 ? QStringLiteral("lavfi-test-pattern")
								 : (x11CaptureAvailable ? QStringLiteral("x11grab")
														: (browserCaptureAvailable ? QStringLiteral("browser-webrtc")
																				   : QStringLiteral("unavailable")));
	if (summary.captureSupported) {
		if (testPatternEnabled) {
			summary.statusMessage =
				QStringLiteral("ffmpeg test-pattern mode is enabled for headless screen-share verification.");
		} else if (browserCaptureAvailable && !x11CaptureAvailable) {
			summary.statusMessage =
				QStringLiteral("A graphical browser runtime is available for WebRTC relay screen sharing.");
		} else if (pipeWireAvailable) {
			summary.statusMessage = QStringLiteral("PipeWire runtime %1 detected, but the executable helper path "
												   "currently uses ffmpeg x11grab capture.")
										.arg(libraryName);
		} else {
			summary.statusMessage =
				QStringLiteral("ffmpeg x11grab desktop capture is available for the helper runtime.");
		}
	} else {
		summary.statusMessage = QStringLiteral("No executable Linux capture path is available. A graphical X11 session "
											   "or MUMBLE_SCREENSHARE_TEST_PATTERN=1 is required.");
	}
#elif defined(Q_OS_WIN)
	const bool gdiCaptureAvailable     = runtimeSupport.ffmpegAvailable && runtimeSupport.gdigrabAvailable;
	const bool browserCaptureAvailable = runtimeSupport.browserWebRtcAvailable;

	summary.captureSupported = gdiCaptureAvailable || browserCaptureAvailable || testPatternEnabled;
	summary.viewSupported =
		runtimeSupport.ffplayAvailable || runtimeSupport.ffmpegAvailable || runtimeSupport.browserWebRtcAvailable;
	appendIf(&summary.captureBackends, gdiCaptureAvailable, QStringLiteral("gdigrab"));
	appendIf(&summary.captureBackends, browserCaptureAvailable, QStringLiteral("browser-webrtc"));
	appendIf(&summary.captureBackends, testPatternEnabled && runtimeSupport.lavfiAvailable,
			 QStringLiteral("lavfi-test-pattern"));
	summary.captureBackend = testPatternEnabled
								 ? QStringLiteral("lavfi-test-pattern")
								 : (gdiCaptureAvailable ? QStringLiteral("gdigrab")
														: (browserCaptureAvailable ? QStringLiteral("browser-webrtc")
																				   : QStringLiteral("unavailable")));
	if (summary.captureSupported) {
		summary.statusMessage =
			testPatternEnabled
				? QStringLiteral("ffmpeg test-pattern mode is enabled for headless screen-share verification.")
				: (gdiCaptureAvailable ? QStringLiteral("Windows helper runtime can capture the desktop via ffmpeg "
														"gdigrab and prefers H.264-first encoding.")
									   : QStringLiteral("Windows helper runtime can execute WebRTC screen sharing "
														"through a dedicated browser runtime."));
	} else {
		summary.statusMessage =
			QStringLiteral("No executable Windows capture path is available. Install an ffmpeg build with gdigrab "
						   "support or enable MUMBLE_SCREENSHARE_TEST_PATTERN=1 for verification.");
	}
#else
	summary.captureBackend = QStringLiteral("unsupported");
	summary.statusMessage =
		QStringLiteral("Screen-share media helper has no executable capture backend on this platform yet.");
	summary.viewSupported =
		runtimeSupport.ffplayAvailable || runtimeSupport.ffmpegAvailable || runtimeSupport.browserWebRtcAvailable;
#endif
	if (summary.captureBackends.isEmpty() && !summary.captureBackend.isEmpty()
		&& summary.captureBackend != QLatin1String("unavailable")
		&& summary.captureBackend != QLatin1String("unsupported")) {
		summary.captureBackends.append(summary.captureBackend);
	}

	return summary;
}
