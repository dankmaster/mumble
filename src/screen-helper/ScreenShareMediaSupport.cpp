// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareMediaSupport.h"

#include "ScreenShareExternalProcess.h"
#include "ScreenShare.h"

#include <QtCore/QLibrary>
#include <QtCore/QStringList>

namespace {
	bool envFlagEnabled(const char *name) {
		const QString value = qEnvironmentVariable(name).trimmed().toLower();
		return value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
			|| value == QLatin1String("on");
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
	summary.maxWidth = Mumble::ScreenShare::DEFAULT_MAX_WIDTH;
	summary.maxHeight = Mumble::ScreenShare::DEFAULT_MAX_HEIGHT;
	summary.maxFps = Mumble::ScreenShare::DEFAULT_MAX_FPS;

	const ScreenShareExternalProcess::RuntimeSupport runtimeSupport = ScreenShareExternalProcess::probeRuntimeSupport();
	const bool testPatternEnabled = envFlagEnabled("MUMBLE_SCREENSHARE_TEST_PATTERN")
		|| qEnvironmentVariable("MUMBLE_SCREENSHARE_CAPTURE_SOURCE").trimmed().toLower() == QLatin1String("test-pattern")
		|| qEnvironmentVariable("MUMBLE_SCREENSHARE_CAPTURE_SOURCE").trimmed().toLower() == QLatin1String("lavfi");

#ifdef Q_OS_LINUX
	QString libraryName;
	const bool pipeWireAvailable = pipeWireRuntimeAvailable(&libraryName);
	const bool x11CaptureAvailable =
		runtimeSupport.ffmpegAvailable && runtimeSupport.x11GrabAvailable && runtimeSupport.x11DisplayAvailable;

	summary.captureSupported = x11CaptureAvailable || testPatternEnabled;
	summary.viewSupported = runtimeSupport.ffplayAvailable || runtimeSupport.ffmpegAvailable;
	summary.captureBackend = testPatternEnabled ? QStringLiteral("lavfi-test-pattern")
												: (x11CaptureAvailable ? QStringLiteral("x11grab") : QStringLiteral("unavailable"));
	if (summary.captureSupported) {
		if (testPatternEnabled) {
			summary.statusMessage =
				QStringLiteral("ffmpeg test-pattern mode is enabled for headless screen-share verification.");
		} else if (pipeWireAvailable) {
			summary.statusMessage =
				QStringLiteral("PipeWire runtime %1 detected, but the executable helper path currently uses ffmpeg x11grab capture.")
					.arg(libraryName);
		} else {
			summary.statusMessage = QStringLiteral("ffmpeg x11grab desktop capture is available for the helper runtime.");
		}
	} else {
		summary.statusMessage =
			QStringLiteral("No executable Linux capture path is available. A graphical X11 session or MUMBLE_SCREENSHARE_TEST_PATTERN=1 is required.");
	}
#else
	summary.captureBackend = QStringLiteral("unsupported");
	summary.statusMessage =
		QStringLiteral("Screen-share media helper currently has only a Linux PipeWire capture stub.");
	summary.viewSupported = runtimeSupport.ffplayAvailable || runtimeSupport.ffmpegAvailable;
#endif

	return summary;
}
