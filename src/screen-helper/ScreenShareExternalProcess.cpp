// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareExternalProcess.h"

#include "ScreenShare.h"
#include "ScreenShareIPC.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QUrlQuery>

#include <optional>

namespace {
constexpr int PROBE_TIMEOUT_MSEC = 5000;
constexpr int START_TIMEOUT_MSEC = 3000;
constexpr int START_SETTLE_MSEC  = 1000;

struct RelayEndpoint {
	bool valid = false;
	QString errorMessage;
	QString endpointUrl;
	QString outputFormat;
	QString localFilePath;
};

ScreenShareExternalProcess::LaunchResult startProcess(const QString &program, const QStringList &arguments,
													  QObject *parent, const QString &executionMode);

QString readMergedProcessOutput(QProcess &process) {
	return QString::fromUtf8(process.readAll());
}

bool envFlagEnabled(const char *name) {
	const QString value = qEnvironmentVariable(name).trimmed().toLower();
	return value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
		   || value == QLatin1String("on");
}

QString runProbeCommand(const QString &program, const QStringList &arguments) {
	if (program.isEmpty()) {
		return QString();
	}

	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.start(program, arguments);
	if (!process.waitForStarted(START_TIMEOUT_MSEC)) {
		return QString();
	}

	process.closeWriteChannel();
	if (!process.waitForFinished(PROBE_TIMEOUT_MSEC)) {
		process.kill();
		process.waitForFinished(500);
	}

	return QString::fromUtf8(process.readAll());
}

QString sanitizeRoomToken(const QString &value) {
	QString sanitized = value.trimmed();
	sanitized.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("-"));
	sanitized.remove(QRegularExpression(QStringLiteral("^-+")));
	sanitized.remove(QRegularExpression(QStringLiteral("-+$")));
	return sanitized.isEmpty() ? QStringLiteral("screen-share") : sanitized;
}

QString fileContainerFormat(const QString &path) {
	const QString suffix = QFileInfo(path).suffix().trimmed().toLower();
	if (suffix == QLatin1String("mp4")) {
		return QStringLiteral("mp4");
	}
	if (suffix == QLatin1String("flv")) {
		return QStringLiteral("flv");
	}
	if (suffix == QLatin1String("ts") || suffix == QLatin1String("mpegts")) {
		return QStringLiteral("mpegts");
	}

	return QStringLiteral("matroska");
}

QString appendPathSegment(const QString &basePath, const QString &segment) {
	QString path = basePath;
	if (path.isEmpty()) {
		path = QStringLiteral("/");
	}
	if (!path.endsWith(QLatin1Char('/'))) {
		path.append(QLatin1Char('/'));
	}
	path.append(segment);
	return path;
}

QString findExecutableAny(const QStringList &candidates) {
	for (const QString &candidate : candidates) {
		const QString resolved = QStandardPaths::findExecutable(candidate);
		if (!resolved.isEmpty()) {
			return resolved;
		}
	}

	return QString();
}

QString configuredExecutablePath(const char *envName) {
	const QString configuredPath = qEnvironmentVariable(envName).trimmed();
	if (configuredPath.isEmpty()) {
		return QString();
	}

	const QFileInfo configuredInfo(configuredPath);
	if (configuredInfo.isFile() && configuredInfo.isExecutable()) {
		return configuredInfo.absoluteFilePath();
	}

	return QStandardPaths::findExecutable(configuredPath);
}

QString preferredExecutablePath(const char *envName, const QStringList &candidates) {
	const QString configuredPath = configuredExecutablePath(envName);
	if (!configuredPath.isEmpty()) {
		return configuredPath;
	}

	return findExecutableAny(candidates);
}

#ifdef Q_OS_WIN
QString existingWindowsBrowserPath(const QStringList &relativePaths) {
	QStringList roots;
	for (const char *envName : { "ProgramFiles", "ProgramFiles(x86)", "LocalAppData" }) {
		const QString root = qEnvironmentVariable(envName).trimmed();
		if (!root.isEmpty() && !roots.contains(root)) {
			roots.append(root);
		}
	}

	for (const QString &root : roots) {
		for (const QString &relativePath : relativePaths) {
			const QString candidate = QDir(root).filePath(relativePath);
			if (QFileInfo(candidate).isExecutable()) {
				return candidate;
			}
		}
	}

	return QString();
}
#endif

QString preferredBrowserPath(const ScreenShareExternalProcess::RuntimeSupport &support, QString *browserID = nullptr) {
	if (support.edgeAvailable) {
		if (browserID) {
			*browserID = QStringLiteral("edge");
		}
		return support.edgePath;
	}
	if (support.chromeAvailable) {
		if (browserID) {
			*browserID = QStringLiteral("chrome");
		}
		return support.chromePath;
	}
	if (support.firefoxAvailable) {
		if (browserID) {
			*browserID = QStringLiteral("firefox");
		}
		return support.firefoxPath;
	}

	if (browserID) {
		browserID->clear();
	}
	return QString();
}

QString browserProfileDirectory(const QJsonObject &plan, const bool publish, const QString &browserID) {
	const QString streamID          = plan.value(QStringLiteral("stream_id")).toString().trimmed();
	const QString sanitizedStreamID = streamID.isEmpty() ? QStringLiteral("screen-share") : sanitizeRoomToken(streamID);
	QString tempBase                = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	if (tempBase.trimmed().isEmpty()) {
		tempBase = QDir::tempPath();
	}

	return QDir(tempBase).filePath(
		QStringLiteral("mumble-screen-share/%1/%2-%3")
			.arg(browserID, publish ? QStringLiteral("publish") : QStringLiteral("view"), sanitizedStreamID));
}

QString browserLaunchUrl(const QJsonObject &plan, QString *errorMessage, QStringList *warnings) {
	const QString relayUrl = Mumble::ScreenShare::normalizeRelayUrl(plan.value(QStringLiteral("relay_url")).toString());
	if (relayUrl.isEmpty()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Missing or invalid relay_url.");
		}
		return QString();
	}

	QUrl launchUrl(relayUrl);
	const QString originalScheme = launchUrl.scheme().trimmed().toLower();
	if (originalScheme == QLatin1String("wss")) {
		launchUrl.setScheme(QStringLiteral("https"));
		if (warnings) {
			warnings->append(QStringLiteral("Launching the WebRTC relay UI over https while preserving the original "
											"wss relay URL as signaling metadata."));
		}
	} else if (originalScheme == QLatin1String("ws")) {
		launchUrl.setScheme(QStringLiteral("http"));
		if (warnings) {
			warnings->append(QStringLiteral("Launching the WebRTC relay UI over http while preserving the original ws "
											"relay URL as signaling metadata."));
		}
	}

	if (launchUrl.scheme().trimmed().isEmpty() || launchUrl.host().trimmed().isEmpty()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Unable to derive a browser launch URL from relay_url.");
		}
		return QString();
	}

	QUrlQuery query(launchUrl);
	auto appendQuery = [&](const QString &key, const QString &value) {
		if (!value.trimmed().isEmpty()) {
			query.addQueryItem(key, value);
		}
	};

	appendQuery(QStringLiteral("mumble_screen_share"), QStringLiteral("1"));
	appendQuery(QStringLiteral("relay_url"), relayUrl);
	appendQuery(QStringLiteral("relay_room_id"), plan.value(QStringLiteral("relay_room_id")).toString());
	appendQuery(QStringLiteral("relay_session_id"), plan.value(QStringLiteral("relay_session_id")).toString());
	appendQuery(QStringLiteral("stream_id"), plan.value(QStringLiteral("stream_id")).toString());
	appendQuery(QStringLiteral("relay_role"), plan.value(QStringLiteral("relay_role_token")).toString());
	appendQuery(QStringLiteral("codec"), plan.value(QStringLiteral("codec_token")).toString());
	appendQuery(QStringLiteral("requested_codec"), plan.value(QStringLiteral("requested_codec_token")).toString());
	appendQuery(QStringLiteral("transport"), plan.value(QStringLiteral("relay_transport_token")).toString());
	appendQuery(QStringLiteral("width"), QString::number(qMax(0, plan.value(QStringLiteral("width")).toInt())));
	appendQuery(QStringLiteral("height"), QString::number(qMax(0, plan.value(QStringLiteral("height")).toInt())));
	appendQuery(QStringLiteral("fps"), QString::number(qMax(0, plan.value(QStringLiteral("fps")).toInt())));
	appendQuery(QStringLiteral("bitrate_kbps"),
				QString::number(qMax(0, plan.value(QStringLiteral("bitrate_kbps")).toInt())));
	const bool browserPublisherCaptureAudio =
		plan.value(QStringLiteral("relay_contract_mode")).toString() == QLatin1String("browser-webrtc-runtime")
		&& plan.value(QStringLiteral("relay_role_token")).toString() == QLatin1String("publisher");
	if (browserPublisherCaptureAudio) {
		appendQuery(QStringLiteral("capture_audio"), QStringLiteral("1"));
		appendQuery(QStringLiteral("system_audio"), QStringLiteral("include"));
		appendQuery(QStringLiteral("surface_switching"), QStringLiteral("include"));
		appendQuery(QStringLiteral("self_browser_surface"), QStringLiteral("exclude"));
	}
	launchUrl.setQuery(query);
	const QString relayToken = plan.value(QStringLiteral("relay_token")).toString().trimmed();
	if (!relayToken.isEmpty()) {
		QUrlQuery fragment;
		fragment.addQueryItem(QStringLiteral("relay_token"), relayToken);
		launchUrl.setFragment(fragment.toString(QUrl::FullyEncoded));
	}
	return launchUrl.toString(QUrl::FullyEncoded);
}

ScreenShareExternalProcess::LaunchResult
	startBrowserWebRtcSession(const ScreenShareExternalProcess::RuntimeSupport &support, const QJsonObject &plan,
							  QObject *parent, const bool publish) {
	ScreenShareExternalProcess::LaunchResult launch;
	if (!support.graphicalSessionAvailable) {
		launch.errorMessage =
			QStringLiteral("A graphical desktop session is required for the helper WebRTC browser runtime.");
		return launch;
	}

	QString browserID;
	const QString browserPath = preferredBrowserPath(support, &browserID);
	if (browserPath.isEmpty()) {
		launch.errorMessage = QStringLiteral("No supported browser runtime was found for WebRTC screen sharing.");
		return launch;
	}

	QString launchUrlError;
	QStringList warnings;
	const QString launchUrl = browserLaunchUrl(plan, &launchUrlError, &warnings);
	if (launchUrl.isEmpty()) {
		launch.errorMessage = launchUrlError;
		return launch;
	}

	const QString profileDir = browserProfileDirectory(plan, publish, browserID);
	QDir(profileDir).removeRecursively();
	QDir().mkpath(profileDir);

	QStringList arguments;
	if (browserID == QLatin1String("edge") || browserID == QLatin1String("chrome")) {
		arguments << QStringLiteral("--new-window") << QStringLiteral("--disable-session-crashed-bubble")
				  << QStringLiteral("--autoplay-policy=no-user-gesture-required")
				  << QStringLiteral("--user-data-dir=%1").arg(profileDir) << QStringLiteral("--app=%1").arg(launchUrl);
	} else if (browserID == QLatin1String("firefox")) {
		arguments << QStringLiteral("-new-instance") << QStringLiteral("-profile") << profileDir
				  << QStringLiteral("-new-window") << launchUrl;
	} else {
		launch.errorMessage = QStringLiteral("Unsupported browser runtime selected for WebRTC.");
		return launch;
	}

	launch = startProcess(browserPath, arguments, parent,
						  publish ? QStringLiteral("browser-webrtc-publish") : QStringLiteral("browser-webrtc-view"));
	if (!launch.started) {
		return launch;
	}

	launch.endpointUrl = launchUrl;
	launch.warnings    = warnings;
	if (publish) {
		launch.selectedCaptureSource = QStringLiteral("browser-webrtc-%1").arg(browserID);
		launch.selectedEncoder       = QStringLiteral("browser-webrtc");
	} else {
		launch.selectedRenderer = QStringLiteral("browser-webrtc-%1").arg(browserID);
	}

	return launch;
}

QString detectRenderNode() {
#ifdef Q_OS_LINUX
	const QDir driDir(QStringLiteral("/dev/dri"));
	const QStringList nodes =
		driDir.entryList(QStringList() << QStringLiteral("renderD*"), QDir::Files | QDir::System | QDir::Readable);
	if (!nodes.isEmpty()) {
		return driDir.absoluteFilePath(nodes.front());
	}
#endif
	return QString();
}

bool anyNvidiaDevicePresent() {
#ifdef Q_OS_LINUX
	if (QFileInfo::exists(QStringLiteral("/dev/nvidiactl"))) {
		return true;
	}

	const QDir devDir(QStringLiteral("/dev"));
	const QStringList nodes =
		devDir.entryList(QStringList() << QStringLiteral("nvidia[0-9]*"), QDir::System | QDir::Files | QDir::Readable);
	return !nodes.isEmpty();
#else
	return false;
#endif
}

bool hasWindowedViewerSurface() {
#ifdef Q_OS_WIN
	return true;
#else
	return !qEnvironmentVariable("DISPLAY").trimmed().isEmpty();
#endif
}

QStringList candidateBackendOrder(const ScreenShareExternalProcess::RuntimeSupport &support,
								  const MumbleProto::ScreenShareCodec codec, const QString &plannedBackend) {
	QStringList candidates;
	auto appendUnique = [&](const QString &candidate) {
		if (!candidate.isEmpty() && !candidates.contains(candidate)) {
			candidates.append(candidate);
		}
	};

	appendUnique(plannedBackend.trimmed().toLower());

	switch (codec) {
		case MumbleProto::ScreenShareCodecH264:
			if (support.h264NvencAvailable) {
				appendUnique(QStringLiteral("nvenc-h264"));
			}
			if (support.h264VaapiAvailable) {
				appendUnique(QStringLiteral("vaapi-h264"));
			}
			if (support.h264MfAvailable) {
				appendUnique(QStringLiteral("mf-h264"));
			}
			if (support.h264QsvAvailable) {
				appendUnique(QStringLiteral("qsv-h264"));
			}
			if (support.libx264Available) {
				appendUnique(QStringLiteral("libx264-h264"));
			}
			break;
		case MumbleProto::ScreenShareCodecAV1:
			if (support.av1NvencAvailable) {
				appendUnique(QStringLiteral("nvenc-av1"));
			}
			if (support.av1VaapiAvailable) {
				appendUnique(QStringLiteral("vaapi-av1"));
			}
			if (support.av1MfAvailable) {
				appendUnique(QStringLiteral("mf-av1"));
			}
			if (support.av1QsvAvailable) {
				appendUnique(QStringLiteral("qsv-av1"));
			}
			if (support.libSvtAv1Available) {
				appendUnique(QStringLiteral("libsvtav1-av1"));
			}
			break;
		case MumbleProto::ScreenShareCodecVP8:
			if (support.libVpxVp8Available) {
				appendUnique(QStringLiteral("libvpx-vp8"));
			}
			break;
		case MumbleProto::ScreenShareCodecVP9:
			if (support.libVpxVp9Available) {
				appendUnique(QStringLiteral("libvpx-vp9"));
			}
			break;
		case MumbleProto::ScreenShareCodecUnknown:
		default:
			break;
	}

	return candidates;
}

RelayEndpoint materializeRelayEndpoint(const QJsonObject &plan,
									   const ScreenShareExternalProcess::RuntimeSupport &support) {
	RelayEndpoint endpoint;

	const QString relayUrl = Mumble::ScreenShare::normalizeRelayUrl(plan.value(QStringLiteral("relay_url")).toString());
	if (relayUrl.isEmpty()) {
		endpoint.errorMessage = QStringLiteral("Missing or invalid relay_url.");
		return endpoint;
	}

	const QString relayRoomID = sanitizeRoomToken(plan.value(QStringLiteral("relay_room_id")).toString());
	const QString relayToken  = plan.value(QStringLiteral("relay_token")).toString().trimmed();

	QUrl url(relayUrl);
	const QString scheme = url.scheme().trimmed().toLower();
	if (scheme == QLatin1String("file")) {
		if (!support.fileProtocolAvailable) {
			endpoint.errorMessage = QStringLiteral("ffmpeg on this host has no file protocol support.");
			return endpoint;
		}

		const QString originalPath = url.toLocalFile();
		if (originalPath.isEmpty()) {
			endpoint.errorMessage = QStringLiteral("The configured file relay path is empty.");
			return endpoint;
		}

		const QFileInfo originalInfo(originalPath);
		const bool treatAsDirectory =
			relayUrl.endsWith(QLatin1Char('/')) || (originalInfo.exists() && originalInfo.isDir());
		QString resolvedPath;
		if (treatAsDirectory) {
			resolvedPath = QDir(originalPath).filePath(relayRoomID + QStringLiteral(".mkv"));
		} else {
			const QString suffix    = originalInfo.suffix().trimmed().toLower();
			const QString extension = suffix.isEmpty() ? QStringLiteral("mkv") : suffix;
			const QString baseName  = originalInfo.completeBaseName().trimmed().isEmpty()
										 ? relayRoomID
										 : originalInfo.completeBaseName() + QLatin1Char('-') + relayRoomID;
			resolvedPath = originalInfo.dir().filePath(baseName + QLatin1Char('.') + extension);
		}

		const QFileInfo resolvedInfo(resolvedPath);
		QDir().mkpath(resolvedInfo.absolutePath());
		endpoint.valid         = true;
		endpoint.localFilePath = resolvedPath;
		endpoint.endpointUrl   = QUrl::fromLocalFile(resolvedPath).toString();
		endpoint.outputFormat  = fileContainerFormat(resolvedPath);
		return endpoint;
	}

	if (scheme != QLatin1String("rtmp") && scheme != QLatin1String("rtmps")) {
		endpoint.errorMessage =
			QStringLiteral("Relay scheme %1 is advertised by the server but this helper build cannot publish it yet.")
				.arg(scheme.toHtmlEscaped());
		return endpoint;
	}

	if (scheme == QLatin1String("rtmp") && !support.rtmpProtocolAvailable) {
		endpoint.errorMessage = QStringLiteral("ffmpeg on this host has no RTMP output support.");
		return endpoint;
	}
	if (scheme == QLatin1String("rtmps") && !support.rtmpsProtocolAvailable) {
		endpoint.errorMessage = QStringLiteral("ffmpeg on this host has no RTMPS output support.");
		return endpoint;
	}

	url.setPath(appendPathSegment(url.path(), relayRoomID));
	if (!relayToken.isEmpty()) {
		QUrlQuery query(url);
		query.addQueryItem(QStringLiteral("token"), relayToken);
		url.setQuery(query);
	}

	endpoint.valid        = true;
	endpoint.endpointUrl  = url.toString(QUrl::FullyEncoded);
	endpoint.outputFormat = QStringLiteral("flv");
	return endpoint;
}

bool selectCaptureSource(const ScreenShareExternalProcess::RuntimeSupport &support, QString *captureSource,
						 QString *errorMessage, QStringList *arguments) {
	const QString forcedSource = qEnvironmentVariable("MUMBLE_SCREENSHARE_CAPTURE_SOURCE").trimmed().toLower();
	const QString configuredDisplay =
		qEnvironmentVariable("MUMBLE_SCREENSHARE_CAPTURE_DISPLAY", qEnvironmentVariable("DISPLAY")).trimmed();
	const bool useTestPattern = forcedSource == QLatin1String("test-pattern") || forcedSource == QLatin1String("lavfi")
								|| envFlagEnabled("MUMBLE_SCREENSHARE_TEST_PATTERN");

	if (useTestPattern) {
		if (!support.lavfiAvailable) {
			if (errorMessage) {
				*errorMessage = QStringLiteral("ffmpeg on this host has no lavfi input for test-pattern mode.");
			}
			return false;
		}

		if (captureSource) {
			*captureSource = QStringLiteral("lavfi-test-pattern");
		}
		if (arguments) {
			const int width  = qMax(1, arguments->takeFirst().toInt());
			const int height = qMax(1, arguments->takeFirst().toInt());
			const int fps    = qMax(1, arguments->takeFirst().toInt());
			arguments->clear();
			arguments->append(QStringLiteral("-re"));
			arguments->append(QStringLiteral("-f"));
			arguments->append(QStringLiteral("lavfi"));
			arguments->append(QStringLiteral("-i"));
			arguments->append(QStringLiteral("testsrc2=size=%1x%2:rate=%3").arg(width).arg(height).arg(fps));
		}
		return true;
	}

#ifdef Q_OS_WIN
	if (forcedSource == QLatin1String("gdi") || forcedSource == QLatin1String("gdigrab")
		|| forcedSource == QLatin1String("desktop") || forcedSource.isEmpty()) {
		if (!support.gdigrabAvailable) {
			if (errorMessage) {
				*errorMessage = QStringLiteral("ffmpeg on this host has no gdigrab input support.");
			}
			return false;
		}

		if (captureSource) {
			*captureSource = QStringLiteral("gdigrab");
		}
		if (arguments) {
			const int width  = qMax(1, arguments->takeFirst().toInt());
			const int height = qMax(1, arguments->takeFirst().toInt());
			const int fps    = qMax(1, arguments->takeFirst().toInt());
			arguments->clear();
			arguments->append(QStringLiteral("-f"));
			arguments->append(QStringLiteral("gdigrab"));
			arguments->append(QStringLiteral("-draw_mouse"));
			arguments->append(QStringLiteral("1"));
			arguments->append(QStringLiteral("-framerate"));
			arguments->append(QString::number(fps));
			arguments->append(QStringLiteral("-video_size"));
			arguments->append(QStringLiteral("%1x%2").arg(width).arg(height));
			arguments->append(QStringLiteral("-i"));
			arguments->append(QStringLiteral("desktop"));
		}
		return true;
	}
#endif

	if (forcedSource == QLatin1String("x11") || forcedSource.isEmpty()) {
		if (!support.x11GrabAvailable) {
			if (errorMessage) {
				*errorMessage = QStringLiteral("ffmpeg on this host has no x11grab input support.");
			}
			return false;
		}
		if (configuredDisplay.isEmpty()) {
			if (errorMessage) {
				*errorMessage = QStringLiteral("No DISPLAY is available for live desktop capture. Set "
											   "MUMBLE_SCREENSHARE_TEST_PATTERN=1 for headless verification.");
			}
			return false;
		}

		if (captureSource) {
			*captureSource = QStringLiteral("x11grab");
		}
		if (arguments) {
			const int width  = qMax(1, arguments->takeFirst().toInt());
			const int height = qMax(1, arguments->takeFirst().toInt());
			const int fps    = qMax(1, arguments->takeFirst().toInt());
			arguments->clear();
			arguments->append(QStringLiteral("-f"));
			arguments->append(QStringLiteral("x11grab"));
			arguments->append(QStringLiteral("-draw_mouse"));
			arguments->append(QStringLiteral("1"));
			arguments->append(QStringLiteral("-framerate"));
			arguments->append(QString::number(fps));
			arguments->append(QStringLiteral("-video_size"));
			arguments->append(QStringLiteral("%1x%2").arg(width).arg(height));
			arguments->append(QStringLiteral("-i"));
			arguments->append(configuredDisplay + QStringLiteral("+0,0"));
		}
		return true;
	}

	if (errorMessage) {
		*errorMessage = QStringLiteral("Unsupported capture source override: %1").arg(forcedSource);
	}
	return false;
}

bool appendEncoderArguments(const ScreenShareExternalProcess::RuntimeSupport &support, const QJsonObject &plan,
							QString *selectedEncoder, QStringList *warnings, QStringList *arguments,
							QString *errorMessage) {
	const MumbleProto::ScreenShareCodec codec =
		Mumble::ScreenShare::IPC::codecFromJson(plan.value(QStringLiteral("codec")));
	const QString plannedBackend = plan.value(QStringLiteral("planned_encoder_backend")).toString().trimmed().toLower();
	const int bitrateKbps        = qMax(2500, plan.value(QStringLiteral("bitrate_kbps")).toInt());
	const int fps                = qMax(1, plan.value(QStringLiteral("fps")).toInt());
	const int maxRateKbps        = qMax(bitrateKbps, bitrateKbps + (bitrateKbps / 8));
	const int bufferSizeKbps     = qMax(maxRateKbps, 2500);
	const QString renderNode     = detectRenderNode();

	auto appendRateControl = [&](const QString &encoder) {
		arguments->append(QStringLiteral("-c:v"));
		arguments->append(encoder);
		arguments->append(QStringLiteral("-g"));
		arguments->append(QString::number(fps));
		arguments->append(QStringLiteral("-bf"));
		arguments->append(QStringLiteral("0"));
		arguments->append(QStringLiteral("-b:v"));
		arguments->append(QString::number(bitrateKbps) + QStringLiteral("k"));
		arguments->append(QStringLiteral("-maxrate"));
		arguments->append(QString::number(maxRateKbps) + QStringLiteral("k"));
		arguments->append(QStringLiteral("-bufsize"));
		arguments->append(QString::number(bufferSizeKbps) + QStringLiteral("k"));
	};

	switch (codec) {
		case MumbleProto::ScreenShareCodecH264:
			if (plannedBackend.contains(QLatin1String("nvenc")) && support.h264NvencAvailable) {
				appendRateControl(QStringLiteral("h264_nvenc"));
				arguments->append(QStringLiteral("-preset"));
				arguments->append(QStringLiteral("p5"));
				arguments->append(QStringLiteral("-tune"));
				arguments->append(QStringLiteral("ll"));
				arguments->append(QStringLiteral("-pix_fmt"));
				arguments->append(QStringLiteral("yuv420p"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("h264_nvenc");
				}
				return true;
			}
			if (plannedBackend.contains(QLatin1String("vaapi")) && support.h264VaapiAvailable
				&& !renderNode.isEmpty()) {
				arguments->append(QStringLiteral("-vaapi_device"));
				arguments->append(renderNode);
				arguments->append(QStringLiteral("-vf"));
				arguments->append(QStringLiteral("format=nv12,hwupload"));
				appendRateControl(QStringLiteral("h264_vaapi"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("h264_vaapi");
				}
				return true;
			}
			if (plannedBackend.contains(QLatin1String("mf")) && support.h264MfAvailable) {
				appendRateControl(QStringLiteral("h264_mf"));
				arguments->append(QStringLiteral("-pix_fmt"));
				arguments->append(QStringLiteral("nv12"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("h264_mf");
				}
				return true;
			}
			if (plannedBackend.contains(QLatin1String("qsv")) && support.h264QsvAvailable) {
				appendRateControl(QStringLiteral("h264_qsv"));
				arguments->append(QStringLiteral("-pix_fmt"));
				arguments->append(QStringLiteral("nv12"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("h264_qsv");
				}
				return true;
			}
			if (support.libx264Available) {
				if (warnings && plannedBackend.contains(QLatin1String("nvenc"))) {
					warnings->append(QStringLiteral("Falling back from NVENC to libx264 on this host."));
				} else if (warnings && plannedBackend.contains(QLatin1String("vaapi"))) {
					warnings->append(QStringLiteral("Falling back from VA-API to libx264 on this host."));
				} else if (warnings && plannedBackend.contains(QLatin1String("mf"))) {
					warnings->append(
						QStringLiteral("Falling back from Media Foundation H.264 to libx264 on this host."));
				} else if (warnings && plannedBackend.contains(QLatin1String("qsv"))) {
					warnings->append(
						QStringLiteral("Falling back from Intel Quick Sync H.264 to libx264 on this host."));
				}
				appendRateControl(QStringLiteral("libx264"));
				arguments->append(QStringLiteral("-preset"));
				arguments->append(QStringLiteral("veryfast"));
				arguments->append(QStringLiteral("-tune"));
				arguments->append(QStringLiteral("zerolatency"));
				arguments->append(QStringLiteral("-profile:v"));
				arguments->append(QStringLiteral("high"));
				arguments->append(QStringLiteral("-pix_fmt"));
				arguments->append(QStringLiteral("yuv420p"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("libx264");
				}
				return true;
			}
			break;
		case MumbleProto::ScreenShareCodecAV1:
			if (plannedBackend.contains(QLatin1String("nvenc")) && support.av1NvencAvailable) {
				appendRateControl(QStringLiteral("av1_nvenc"));
				arguments->append(QStringLiteral("-preset"));
				arguments->append(QStringLiteral("p5"));
				arguments->append(QStringLiteral("-tune"));
				arguments->append(QStringLiteral("ll"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("av1_nvenc");
				}
				return true;
			}
			if (plannedBackend.contains(QLatin1String("vaapi")) && support.av1VaapiAvailable && !renderNode.isEmpty()) {
				arguments->append(QStringLiteral("-vaapi_device"));
				arguments->append(renderNode);
				arguments->append(QStringLiteral("-vf"));
				arguments->append(QStringLiteral("format=nv12,hwupload"));
				appendRateControl(QStringLiteral("av1_vaapi"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("av1_vaapi");
				}
				return true;
			}
			if (plannedBackend.contains(QLatin1String("mf")) && support.av1MfAvailable) {
				appendRateControl(QStringLiteral("av1_mf"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("av1_mf");
				}
				return true;
			}
			if (plannedBackend.contains(QLatin1String("qsv")) && support.av1QsvAvailable) {
				appendRateControl(QStringLiteral("av1_qsv"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("av1_qsv");
				}
				return true;
			}
			if (support.libSvtAv1Available) {
				if (warnings
					&& (plannedBackend.contains(QLatin1String("nvenc"))
						|| plannedBackend.contains(QLatin1String("vaapi")))) {
					warnings->append(QStringLiteral("Falling back to libsvtav1 on this host."));
				} else if (warnings && plannedBackend.contains(QLatin1String("mf"))) {
					warnings->append(
						QStringLiteral("Falling back from Media Foundation AV1 to libsvtav1 on this host."));
				} else if (warnings && plannedBackend.contains(QLatin1String("qsv"))) {
					warnings->append(
						QStringLiteral("Falling back from Intel Quick Sync AV1 to libsvtav1 on this host."));
				}
				appendRateControl(QStringLiteral("libsvtav1"));
				arguments->append(QStringLiteral("-preset"));
				arguments->append(QStringLiteral("8"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("libsvtav1");
				}
				return true;
			}
			break;
		case MumbleProto::ScreenShareCodecVP8:
			if (support.libVpxVp8Available) {
				appendRateControl(QStringLiteral("libvpx"));
				arguments->append(QStringLiteral("-deadline"));
				arguments->append(QStringLiteral("realtime"));
				arguments->append(QStringLiteral("-cpu-used"));
				arguments->append(QStringLiteral("6"));
				arguments->append(QStringLiteral("-lag-in-frames"));
				arguments->append(QStringLiteral("0"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("libvpx");
				}
				return true;
			}
			break;
		case MumbleProto::ScreenShareCodecVP9:
			if (support.libVpxVp9Available) {
				appendRateControl(QStringLiteral("libvpx-vp9"));
				arguments->append(QStringLiteral("-deadline"));
				arguments->append(QStringLiteral("realtime"));
				arguments->append(QStringLiteral("-cpu-used"));
				arguments->append(QStringLiteral("4"));
				arguments->append(QStringLiteral("-lag-in-frames"));
				arguments->append(QStringLiteral("0"));
				arguments->append(QStringLiteral("-row-mt"));
				arguments->append(QStringLiteral("1"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("libvpx-vp9");
				}
				return true;
			}
			break;
		case MumbleProto::ScreenShareCodecUnknown:
		default:
			break;
	}

	if (errorMessage) {
		*errorMessage = QStringLiteral("No executable ffmpeg encoder is available for codec %1.")
							.arg(Mumble::ScreenShare::codecToConfigToken(codec));
	}
	return false;
}

ScreenShareExternalProcess::LaunchResult startProcess(const QString &program, const QStringList &arguments,
													  QObject *parent, const QString &executionMode) {
	ScreenShareExternalProcess::LaunchResult launch;
	launch.program       = program;
	launch.executionMode = executionMode;

	QProcess *process = new QProcess(parent);
	process->setProcessChannelMode(QProcess::MergedChannels);
	process->start(program, arguments);
	if (!process->waitForStarted(START_TIMEOUT_MSEC)) {
		launch.errorMessage = process->errorString();
		process->deleteLater();
		return launch;
	}

	if (process->waitForFinished(START_SETTLE_MSEC)) {
		const QString output = readMergedProcessOutput(*process).trimmed();
		launch.errorMessage =
			output.isEmpty() ? QStringLiteral("The external media process exited immediately.") : output;
		process->deleteLater();
		return launch;
	}

	launch.started = true;
	launch.process = process;
	return launch;
}
} // namespace

ScreenShareExternalProcess::RuntimeSupport ScreenShareExternalProcess::probeRuntimeSupport(const bool refresh) {
	static std::optional< RuntimeSupport > cachedSupport;
	if (!refresh && cachedSupport.has_value()) {
		return *cachedSupport;
	}

	RuntimeSupport support;
	support.ffmpegPath = preferredExecutablePath("MUMBLE_SCREENSHARE_FFMPEG_PATH",
												 QStringList{ QStringLiteral("ffmpeg"), QStringLiteral("ffmpeg.exe") });
	support.ffplayPath = preferredExecutablePath("MUMBLE_SCREENSHARE_FFPLAY_PATH",
												 QStringList{ QStringLiteral("ffplay"), QStringLiteral("ffplay.exe") });
#ifdef Q_OS_WIN
	support.edgePath = findExecutableAny(QStringList{ QStringLiteral("msedge.exe") });
	if (support.edgePath.isEmpty()) {
		support.edgePath =
			existingWindowsBrowserPath(QStringList{ QStringLiteral("Microsoft/Edge/Application/msedge.exe"),
													QStringLiteral("Microsoft/Edge Beta/Application/msedge.exe") });
	}
	support.chromePath = findExecutableAny(QStringList{ QStringLiteral("chrome.exe") });
	if (support.chromePath.isEmpty()) {
		support.chromePath =
			existingWindowsBrowserPath(QStringList{ QStringLiteral("Google/Chrome/Application/chrome.exe"),
													QStringLiteral("Chromium/Application/chrome.exe") });
	}
	support.firefoxPath = findExecutableAny(QStringList{ QStringLiteral("firefox.exe") });
	if (support.firefoxPath.isEmpty()) {
		support.firefoxPath = existingWindowsBrowserPath(QStringList{ QStringLiteral("Mozilla Firefox/firefox.exe") });
	}
#else
	support.edgePath =
		findExecutableAny(QStringList{ QStringLiteral("microsoft-edge"), QStringLiteral("microsoft-edge-stable") });
	support.chromePath =
		findExecutableAny(QStringList{ QStringLiteral("google-chrome"), QStringLiteral("google-chrome-stable"),
									   QStringLiteral("chromium"), QStringLiteral("chromium-browser") });
	support.firefoxPath               = findExecutableAny(QStringList{ QStringLiteral("firefox") });
#endif
	support.ffmpegAvailable  = !support.ffmpegPath.isEmpty();
	support.ffplayAvailable  = !support.ffplayPath.isEmpty();
	support.edgeAvailable    = !support.edgePath.isEmpty();
	support.chromeAvailable  = !support.chromePath.isEmpty();
	support.firefoxAvailable = !support.firefoxPath.isEmpty();
#ifdef Q_OS_WIN
	support.graphicalSessionAvailable = true;
#else
	support.graphicalSessionAvailable = !qEnvironmentVariable("DISPLAY").trimmed().isEmpty()
										|| !qEnvironmentVariable("WAYLAND_DISPLAY").trimmed().isEmpty();
#endif
	support.browserWebRtcAvailable = support.graphicalSessionAvailable
									 && (support.edgeAvailable || support.chromeAvailable || support.firefoxAvailable);
	support.x11DisplayAvailable     = !qEnvironmentVariable("DISPLAY").trimmed().isEmpty();
	support.windowedViewerAvailable = hasWindowedViewerSurface();

	if (support.ffmpegAvailable) {
		const QString encoders =
			runProbeCommand(support.ffmpegPath, { QStringLiteral("-hide_banner"), QStringLiteral("-encoders") });
		const QString devices =
			runProbeCommand(support.ffmpegPath, { QStringLiteral("-hide_banner"), QStringLiteral("-devices") });
		const QString protocols =
			runProbeCommand(support.ffmpegPath, { QStringLiteral("-hide_banner"), QStringLiteral("-protocols") });

		support.gdigrabAvailable = devices.contains(QLatin1String("gdigrab"));
		support.x11GrabAvailable = devices.contains(QLatin1String("x11grab"));
		support.gdigrabAvailable = devices.contains(QLatin1String("gdigrab"));
		support.lavfiAvailable   = devices.contains(QLatin1String("lavfi"));
#ifdef Q_OS_LINUX
		const bool nvidiaDeviceAvailable = anyNvidiaDevicePresent();
#else
		const bool nvidiaDeviceAvailable = true;
#endif

		support.h264NvencAvailable     = encoders.contains(QLatin1String("h264_nvenc")) && nvidiaDeviceAvailable;
		support.h264VaapiAvailable     = encoders.contains(QLatin1String("h264_vaapi"));
		support.h264MfAvailable        = encoders.contains(QLatin1String("h264_mf"));
		support.h264QsvAvailable       = encoders.contains(QLatin1String("h264_qsv"));
		support.libx264Available       = encoders.contains(QLatin1String("libx264"));
		support.av1NvencAvailable      = encoders.contains(QLatin1String("av1_nvenc")) && nvidiaDeviceAvailable;
		support.av1VaapiAvailable      = encoders.contains(QLatin1String("av1_vaapi"));
		support.av1MfAvailable         = encoders.contains(QLatin1String("av1_mf"));
		support.av1QsvAvailable        = encoders.contains(QLatin1String("av1_qsv"));
		support.libSvtAv1Available     = encoders.contains(QLatin1String("libsvtav1"));
		support.libVpxVp8Available     = encoders.contains(QLatin1String("libvpx"));
		support.libVpxVp9Available     = encoders.contains(QLatin1String("libvpx-vp9"));
		support.fileProtocolAvailable  = protocols.contains(QLatin1String("file"));
		support.rtmpProtocolAvailable  = protocols.contains(QLatin1String("rtmp"));
		support.rtmpsProtocolAvailable = protocols.contains(QLatin1String("rtmps"));
	}

	cachedSupport = support;
	return support;
}

QJsonObject ScreenShareExternalProcess::runtimeSupportToJson(const RuntimeSupport &support) {
	QJsonObject payload;
	payload.insert(QStringLiteral("ffmpeg_available"), support.ffmpegAvailable);
	payload.insert(QStringLiteral("ffplay_available"), support.ffplayAvailable);
	payload.insert(QStringLiteral("browser_webrtc_available"), support.browserWebRtcAvailable);
	payload.insert(QStringLiteral("edge_available"), support.edgeAvailable);
	payload.insert(QStringLiteral("chrome_available"), support.chromeAvailable);
	payload.insert(QStringLiteral("firefox_available"), support.firefoxAvailable);
	payload.insert(QStringLiteral("graphical_session_available"), support.graphicalSessionAvailable);
	payload.insert(QStringLiteral("x11_display_available"), support.x11DisplayAvailable);
	payload.insert(QStringLiteral("x11grab_available"), support.x11GrabAvailable);
	payload.insert(QStringLiteral("gdigrab_available"), support.gdigrabAvailable);
	payload.insert(QStringLiteral("lavfi_available"), support.lavfiAvailable);
	payload.insert(QStringLiteral("windowed_viewer_available"), support.windowedViewerAvailable);
	payload.insert(QStringLiteral("h264_nvenc_available"), support.h264NvencAvailable);
	payload.insert(QStringLiteral("h264_vaapi_available"), support.h264VaapiAvailable);
	payload.insert(QStringLiteral("h264_mf_available"), support.h264MfAvailable);
	payload.insert(QStringLiteral("h264_qsv_available"), support.h264QsvAvailable);
	payload.insert(QStringLiteral("libx264_available"), support.libx264Available);
	payload.insert(QStringLiteral("av1_nvenc_available"), support.av1NvencAvailable);
	payload.insert(QStringLiteral("av1_vaapi_available"), support.av1VaapiAvailable);
	payload.insert(QStringLiteral("av1_mf_available"), support.av1MfAvailable);
	payload.insert(QStringLiteral("av1_qsv_available"), support.av1QsvAvailable);
	payload.insert(QStringLiteral("libsvtav1_available"), support.libSvtAv1Available);
	payload.insert(QStringLiteral("libvpx_vp8_available"), support.libVpxVp8Available);
	payload.insert(QStringLiteral("libvpx_vp9_available"), support.libVpxVp9Available);
	payload.insert(QStringLiteral("file_protocol_available"), support.fileProtocolAvailable);
	payload.insert(QStringLiteral("rtmp_protocol_available"), support.rtmpProtocolAvailable);
	payload.insert(QStringLiteral("rtmps_protocol_available"), support.rtmpsProtocolAvailable);

	QJsonArray publishSchemes;
	if (support.fileProtocolAvailable) {
		publishSchemes.push_back(QStringLiteral("file"));
	}
	if (support.rtmpProtocolAvailable) {
		publishSchemes.push_back(QStringLiteral("rtmp"));
	}
	if (support.rtmpsProtocolAvailable) {
		publishSchemes.push_back(QStringLiteral("rtmps"));
	}
	if (support.browserWebRtcAvailable) {
		publishSchemes.push_back(QStringLiteral("http"));
		publishSchemes.push_back(QStringLiteral("https"));
		publishSchemes.push_back(QStringLiteral("ws"));
		publishSchemes.push_back(QStringLiteral("wss"));
	}
	payload.insert(QStringLiteral("publish_relay_schemes"), publishSchemes);
	payload.insert(QStringLiteral("view_relay_schemes"), publishSchemes);
	return payload;
}

ScreenShareExternalProcess::LaunchResult ScreenShareExternalProcess::startPublish(const QJsonObject &plan,
																				  QObject *parent) {
	LaunchResult launch;
	const RuntimeSupport support = probeRuntimeSupport();
	if (plan.value(QStringLiteral("relay_contract_mode")).toString() == QLatin1String("browser-webrtc-runtime")) {
		return startBrowserWebRtcSession(support, plan, parent, true);
	}
	if (!plan.value(QStringLiteral("relay_runtime_executable")).toBool(true)) {
		launch.errorMessage =
			plan.value(QStringLiteral("relay_contract_description"))
				.toString(QStringLiteral("The negotiated relay transport is not executable in this helper build."));
		if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
			launch.started       = true;
			launch.usedStub      = true;
			launch.executionMode = QStringLiteral("stub");
			launch.warnings.append(launch.errorMessage);
		}
		return launch;
	}
	if (!support.ffmpegAvailable) {
		launch.errorMessage = QStringLiteral("ffmpeg is not installed on this host.");
		if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
			launch.started       = true;
			launch.usedStub      = true;
			launch.executionMode = QStringLiteral("stub");
			launch.warnings.append(QStringLiteral(
				"ffmpeg is unavailable; using helper stub mode because MUMBLE_SCREENSHARE_ALLOW_STUB=1."));
		}
		return launch;
	}

	const RelayEndpoint endpoint = materializeRelayEndpoint(plan, support);
	if (!endpoint.valid) {
		launch.errorMessage = endpoint.errorMessage;
		if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
			launch.started       = true;
			launch.usedStub      = true;
			launch.executionMode = QStringLiteral("stub");
			launch.warnings.append(endpoint.errorMessage);
		}
		return launch;
	}

	const int width  = qMax(1, plan.value(QStringLiteral("width")).toInt());
	const int height = qMax(1, plan.value(QStringLiteral("height")).toInt());
	const int fps    = qMax(1, plan.value(QStringLiteral("fps")).toInt());
	QString captureSource;
	QString captureError;
	QStringList inputArguments{ QString::number(width), QString::number(height), QString::number(fps) };
	if (!selectCaptureSource(support, &captureSource, &captureError, &inputArguments)) {
		launch.errorMessage = captureError;
		if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
			launch.started       = true;
			launch.usedStub      = true;
			launch.executionMode = QStringLiteral("stub");
			launch.warnings.append(captureError);
		}
		return launch;
	}

	const MumbleProto::ScreenShareCodec codec =
		Mumble::ScreenShare::IPC::codecFromJson(plan.value(QStringLiteral("codec")));
	const QString plannedBackend        = plan.value(QStringLiteral("planned_encoder_backend")).toString();
	const QStringList candidateBackends = candidateBackendOrder(support, codec, plannedBackend);

	QString lastError;
	for (const QString &candidateBackend : candidateBackends) {
		QJsonObject attemptPlan = plan;
		attemptPlan.insert(QStringLiteral("planned_encoder_backend"), candidateBackend);

		QStringList arguments;
		arguments.append(QStringLiteral("-hide_banner"));
		arguments.append(QStringLiteral("-loglevel"));
		arguments.append(QStringLiteral("warning"));
		arguments.append(QStringLiteral("-nostdin"));
		arguments.append(QStringLiteral("-y"));
		arguments.append(inputArguments);
		arguments.append(QStringLiteral("-an"));

		QString selectedEncoder;
		QStringList attemptWarnings = launch.warnings;
		QString encoderError;
		if (!appendEncoderArguments(support, attemptPlan, &selectedEncoder, &attemptWarnings, &arguments,
									&encoderError)) {
			lastError = encoderError;
			continue;
		}

		arguments.append(QStringLiteral("-f"));
		arguments.append(endpoint.outputFormat);
		arguments.append(endpoint.localFilePath.isEmpty() ? endpoint.endpointUrl : endpoint.localFilePath);

		LaunchResult attemptLaunch =
			startProcess(support.ffmpegPath, arguments, parent, QStringLiteral("ffmpeg-publish"));
		if (!attemptLaunch.started) {
			lastError = attemptLaunch.errorMessage;
			if (candidateBackend != candidateBackends.back()) {
				launch.warnings.append(
					QStringLiteral("Encoder backend %1 failed to start; trying the next available backend.")
						.arg(candidateBackend));
			}
			continue;
		}

		launch                       = attemptLaunch;
		launch.endpointUrl           = endpoint.endpointUrl;
		launch.selectedEncoder       = selectedEncoder;
		launch.selectedCaptureSource = captureSource;
		launch.warnings              = attemptWarnings;
		if (candidateBackend.trimmed().toLower() != plannedBackend.trimmed().toLower()) {
			launch.warnings.append(
				QStringLiteral("Fell back from %1 to %2 during helper startup.").arg(plannedBackend, candidateBackend));
		}
		return launch;
	}

	launch.errorMessage =
		lastError.isEmpty() ? QStringLiteral("No external ffmpeg encoder backend could be started.") : lastError;
	if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
		launch.started       = true;
		launch.usedStub      = true;
		launch.executionMode = QStringLiteral("stub");
		launch.warnings.append(launch.errorMessage);
	}
	return launch;
}

ScreenShareExternalProcess::LaunchResult ScreenShareExternalProcess::startView(const QJsonObject &plan,
																			   QObject *parent) {
	LaunchResult launch;
	const RuntimeSupport support = probeRuntimeSupport();
	if (plan.value(QStringLiteral("relay_contract_mode")).toString() == QLatin1String("browser-webrtc-runtime")) {
		return startBrowserWebRtcSession(support, plan, parent, false);
	}
	if (!plan.value(QStringLiteral("relay_runtime_executable")).toBool(true)) {
		launch.errorMessage =
			plan.value(QStringLiteral("relay_contract_description"))
				.toString(QStringLiteral("The negotiated relay transport is not executable in this helper build."));
		if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
			launch.started       = true;
			launch.usedStub      = true;
			launch.executionMode = QStringLiteral("stub");
			launch.warnings.append(launch.errorMessage);
		}
		return launch;
	}
	const bool headlessView = !support.graphicalSessionAvailable || envFlagEnabled("MUMBLE_SCREENSHARE_HEADLESS_VIEW");
	if (headlessView ? !support.ffmpegAvailable : !support.ffplayAvailable) {
		launch.errorMessage = headlessView
								  ? QStringLiteral("ffmpeg is not installed on this host for headless viewer mode.")
								  : QStringLiteral("ffplay is not installed on this host.");
		if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
			launch.started       = true;
			launch.usedStub      = true;
			launch.executionMode = QStringLiteral("stub");
			launch.warnings.append(
				headlessView
					? QStringLiteral("ffmpeg is unavailable for headless viewer mode; using helper stub mode because "
									 "MUMBLE_SCREENSHARE_ALLOW_STUB=1.")
					: QStringLiteral(
						"ffplay is unavailable; using helper stub mode because MUMBLE_SCREENSHARE_ALLOW_STUB=1."));
		}
		return launch;
	}

	const RelayEndpoint endpoint = materializeRelayEndpoint(plan, support);
	if (!endpoint.valid) {
		launch.errorMessage = endpoint.errorMessage;
		if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
			launch.started       = true;
			launch.usedStub      = true;
			launch.executionMode = QStringLiteral("stub");
			launch.warnings.append(endpoint.errorMessage);
		}
		return launch;
	}

	QStringList arguments;
	const QString inputLocation = endpoint.localFilePath.isEmpty() ? endpoint.endpointUrl : endpoint.localFilePath;
	const QString program       = headlessView ? support.ffmpegPath : support.ffplayPath;
	const QString executionMode = headlessView ? QStringLiteral("ffmpeg-view") : QStringLiteral("ffplay-view");

	if (headlessView) {
		arguments.append(QStringLiteral("-hide_banner"));
		arguments.append(QStringLiteral("-loglevel"));
		arguments.append(QStringLiteral("warning"));
		arguments.append(QStringLiteral("-nostdin"));
		if (!endpoint.localFilePath.isEmpty()) {
			arguments.append(QStringLiteral("-re"));
		}
		arguments.append(QStringLiteral("-i"));
		arguments.append(inputLocation);
		arguments.append(QStringLiteral("-an"));
		arguments.append(QStringLiteral("-f"));
		arguments.append(QStringLiteral("null"));
		arguments.append(QStringLiteral("-"));
	} else {
		arguments.append(QStringLiteral("-hide_banner"));
		arguments.append(QStringLiteral("-loglevel"));
		arguments.append(QStringLiteral("warning"));
		arguments.append(QStringLiteral("-fflags"));
		arguments.append(QStringLiteral("nobuffer"));
		arguments.append(QStringLiteral("-flags"));
		arguments.append(QStringLiteral("low_delay"));
		arguments.append(QStringLiteral("-framedrop"));
		arguments.append(inputLocation);
	}

	launch = startProcess(program, arguments, parent, executionMode);
	if (!launch.started) {
		return launch;
	}

	launch.endpointUrl = endpoint.localFilePath.isEmpty() ? endpoint.endpointUrl
														  : QUrl::fromLocalFile(endpoint.localFilePath).toString();
	launch.selectedRenderer = headlessView ? QStringLiteral("ffmpeg-null-view") : QStringLiteral("ffplay");
	return launch;
}

void ScreenShareExternalProcess::stop(QProcess *process, const int timeoutMsec) {
	if (!process) {
		return;
	}

	if (process->state() == QProcess::NotRunning) {
		process->deleteLater();
		return;
	}

	process->terminate();
	if (!process->waitForFinished(timeoutMsec)) {
		process->kill();
		process->waitForFinished(500);
	}
	process->deleteLater();
}
