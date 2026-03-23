// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareSessionPlanner.h"

#include "ScreenShareExternalProcess.h"
#include "ScreenShare.h"
#include "ScreenShareIPC.h"
#include "ScreenShareRelayClient.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonValue>
#include <QtCore/QStringList>

namespace {
	struct EncoderBackend {
		QString backendID;
		QString displayName;
		QList< int > codecs;
		bool hardware = false;
		bool available = false;
		QString detail;
	};

	QString streamIDFromPayload(const QJsonObject &payload) {
		return payload.value(QStringLiteral("stream_id")).toString().trimmed();
	}

	QString relayUrlFromPayload(const QJsonObject &payload) {
		return Mumble::ScreenShare::normalizeRelayUrl(payload.value(QStringLiteral("relay_url")).toString());
	}

	QString relayRoomFromPayload(const QJsonObject &payload) {
		return payload.value(QStringLiteral("relay_room_id")).toString().trimmed();
	}

	MumbleProto::ScreenShareCodec codecFromPayload(const QJsonObject &payload) {
		return Mumble::ScreenShare::IPC::codecFromJson(payload.value(QStringLiteral("codec")));
	}

	unsigned int nonNegativePayloadValue(const QJsonObject &payload, const char *key) {
		const int rawValue = payload.value(QLatin1String(key)).toInt();
		if (rawValue <= 0) {
			return 0;
		}

		return static_cast< unsigned int >(rawValue);
	}

	unsigned int limitFromPayload(const QJsonObject &payload, const char *key, const unsigned int fallback,
								  const unsigned int hardMax) {
		return Mumble::ScreenShare::sanitizeLimit(nonNegativePayloadValue(payload, key), fallback, hardMax);
	}

	QString codecBackendToken(const QString &backendID, const MumbleProto::ScreenShareCodec codec) {
		const QString codecToken = Mumble::ScreenShare::codecToConfigToken(codec);
		if (codecToken.isEmpty()) {
			return backendID;
		}

		return QStringLiteral("%1-%2").arg(backendID, codecToken);
	}

	bool anyRenderNodePresent(QString *nodePath = nullptr) {
#ifdef Q_OS_LINUX
		const QDir driDir(QStringLiteral("/dev/dri"));
		const QStringList nodes =
			driDir.entryList(QStringList() << QStringLiteral("renderD*"), QDir::Files | QDir::System | QDir::Readable);
		if (nodes.isEmpty()) {
			return false;
		}

		if (nodePath) {
			*nodePath = driDir.absoluteFilePath(nodes.front());
		}

		return true;
#else
		Q_UNUSED(nodePath);
		return false;
#endif
	}

	QList< EncoderBackend > probeEncoderBackends() {
		QList< EncoderBackend > backends;
		const ScreenShareExternalProcess::RuntimeSupport runtimeSupport = ScreenShareExternalProcess::probeRuntimeSupport();

#ifdef Q_OS_LINUX
		QString renderNode;
		const bool renderNodeAvailable = anyRenderNodePresent(&renderNode);
		EncoderBackend vaapi;
		vaapi.backendID  = QStringLiteral("vaapi");
		vaapi.displayName = QStringLiteral("VA-API via ffmpeg");
		if (runtimeSupport.h264VaapiAvailable) {
			vaapi.codecs.append(static_cast< int >(MumbleProto::ScreenShareCodecH264));
		}
		if (runtimeSupport.av1VaapiAvailable) {
			vaapi.codecs.append(static_cast< int >(MumbleProto::ScreenShareCodecAV1));
		}
		vaapi.hardware = true;
		vaapi.available = !vaapi.codecs.isEmpty() && renderNodeAvailable;
		if (vaapi.available) {
			vaapi.detail = QStringLiteral("ffmpeg VA-API encoder(s) available with render node %1").arg(renderNode);
		} else if (vaapi.codecs.isEmpty()) {
			vaapi.detail = QStringLiteral("ffmpeg VA-API encoders are unavailable");
		} else {
			vaapi.detail = QStringLiteral("No readable /dev/dri render node found");
		}
		backends.append(vaapi);

		EncoderBackend nvenc;
		nvenc.backendID  = QStringLiteral("nvenc");
		nvenc.displayName = QStringLiteral("NVENC via ffmpeg");
		if (runtimeSupport.h264NvencAvailable) {
			nvenc.codecs.append(static_cast< int >(MumbleProto::ScreenShareCodecH264));
		}
		if (runtimeSupport.av1NvencAvailable) {
			nvenc.codecs.append(static_cast< int >(MumbleProto::ScreenShareCodecAV1));
		}
		nvenc.hardware = true;
		nvenc.available = !nvenc.codecs.isEmpty();
		nvenc.detail = nvenc.available ? QStringLiteral("ffmpeg NVENC encoder(s) are available")
									   : QStringLiteral("ffmpeg NVENC encoders are unavailable");
		backends.append(nvenc);
#elif defined(Q_OS_WIN)
		EncoderBackend mediaFoundation;
		mediaFoundation.backendID   = QStringLiteral("mf");
		mediaFoundation.displayName = QStringLiteral("Media Foundation via ffmpeg");
		if (runtimeSupport.h264MfAvailable) {
			mediaFoundation.codecs.append(static_cast< int >(MumbleProto::ScreenShareCodecH264));
		}
		if (runtimeSupport.av1MfAvailable) {
			mediaFoundation.codecs.append(static_cast< int >(MumbleProto::ScreenShareCodecAV1));
		}
		mediaFoundation.hardware = true;
		mediaFoundation.available = !mediaFoundation.codecs.isEmpty();
		mediaFoundation.detail =
			mediaFoundation.available
				? QStringLiteral("ffmpeg Media Foundation encoder(s) are available; Windows chooses hardware assistance where supported.")
				: QStringLiteral("ffmpeg Media Foundation encoders are unavailable");
		backends.append(mediaFoundation);

		EncoderBackend qsv;
		qsv.backendID   = QStringLiteral("qsv");
		qsv.displayName = QStringLiteral("Intel Quick Sync via ffmpeg");
		if (runtimeSupport.h264QsvAvailable) {
			qsv.codecs.append(static_cast< int >(MumbleProto::ScreenShareCodecH264));
		}
		if (runtimeSupport.av1QsvAvailable) {
			qsv.codecs.append(static_cast< int >(MumbleProto::ScreenShareCodecAV1));
		}
		qsv.hardware = true;
		qsv.available = !qsv.codecs.isEmpty();
		qsv.detail = qsv.available ? QStringLiteral("ffmpeg Intel Quick Sync encoder(s) are available")
								   : QStringLiteral("ffmpeg Intel Quick Sync encoders are unavailable");
		backends.append(qsv);
#endif

		EncoderBackend x264;
		x264.backendID   = QStringLiteral("libx264");
		x264.displayName = QStringLiteral("libx264");
		x264.codecs      = { static_cast< int >(MumbleProto::ScreenShareCodecH264) };
		x264.hardware    = false;
		x264.available   = runtimeSupport.libx264Available;
		x264.detail      = x264.available ? QStringLiteral("ffmpeg libx264 software encoder is available")
										  : QStringLiteral("ffmpeg libx264 encoder is unavailable");
		backends.append(x264);

		EncoderBackend svtav1;
		svtav1.backendID   = QStringLiteral("libsvtav1");
		svtav1.displayName = QStringLiteral("SVT-AV1");
		svtav1.codecs      = { static_cast< int >(MumbleProto::ScreenShareCodecAV1) };
		svtav1.hardware    = false;
		svtav1.available   = runtimeSupport.libSvtAv1Available;
		svtav1.detail      = svtav1.available ? QStringLiteral("ffmpeg libsvtav1 software encoder is available")
											  : QStringLiteral("ffmpeg libsvtav1 encoder is unavailable");
		backends.append(svtav1);

		EncoderBackend vp9;
		vp9.backendID   = QStringLiteral("libvpx-vp9");
		vp9.displayName = QStringLiteral("libvpx-vp9");
		vp9.codecs      = { static_cast< int >(MumbleProto::ScreenShareCodecVP9) };
		vp9.hardware    = false;
		vp9.available   = runtimeSupport.libVpxVp9Available;
		vp9.detail      = vp9.available ? QStringLiteral("ffmpeg libvpx-vp9 software encoder is available")
										: QStringLiteral("ffmpeg libvpx-vp9 encoder is unavailable");
		backends.append(vp9);

		EncoderBackend stub;
		stub.backendID  = QStringLiteral("stub");
		stub.displayName = QStringLiteral("Planning Stub");
		stub.codecs      = Mumble::ScreenShare::defaultCodecPreferenceList();
		stub.hardware    = false;
		stub.available   = true;
		stub.detail = QStringLiteral("Fallback planner only. Actual session start should prefer executable ffmpeg backends.");
		backends.append(stub);

		return backends;
	}

	QJsonObject backendToJson(const EncoderBackend &backend) {
		QJsonObject json;
		json.insert(QStringLiteral("id"), backend.backendID);
		json.insert(QStringLiteral("name"), backend.displayName);
		json.insert(QStringLiteral("hardware"), backend.hardware);
		json.insert(QStringLiteral("available"), backend.available);
		json.insert(QStringLiteral("detail"), backend.detail);
		json.insert(QStringLiteral("codecs"), Mumble::ScreenShare::IPC::codecListToJson(backend.codecs));
		return json;
	}

	const EncoderBackend *selectEncoderBackend(const QList< EncoderBackend > &backends,
											   const MumbleProto::ScreenShareCodec codec,
											   const bool preferHardware) {
		const int codecValue = static_cast< int >(codec);
		if (preferHardware) {
			for (const EncoderBackend &backend : backends) {
				if (backend.hardware && backend.available && backend.codecs.contains(codecValue)) {
					return &backend;
				}
			}
		}

		for (const EncoderBackend &backend : backends) {
			if (backend.available && backend.codecs.contains(codecValue)) {
				return &backend;
			}
		}

		return nullptr;
	}

	ScreenShareSessionPlanner::Plan buildPlan(const QJsonObject &payload,
											  const ScreenShareMediaSupport::CapabilitySummary &capabilities,
											  const bool publish) {
		ScreenShareSessionPlanner::Plan plan;

		const QString streamID = streamIDFromPayload(payload);
		if (streamID.isEmpty()) {
			plan.errorMessage = QStringLiteral("Missing stream_id.");
			return plan;
		}

		const QString relayUrl = relayUrlFromPayload(payload);
		if (relayUrl.isEmpty()) {
			plan.errorMessage = QStringLiteral("Missing or invalid relay_url.");
			return plan;
		}

		const QString relayRoomID = relayRoomFromPayload(payload);
		if (relayRoomID.isEmpty()) {
			plan.errorMessage = QStringLiteral("Missing relay_room_id.");
			return plan;
		}

		if (publish && !capabilities.captureSupported) {
			plan.errorMessage = QStringLiteral("Local capture backend is unavailable.");
			return plan;
		}
		if (!publish && !capabilities.viewSupported) {
			plan.errorMessage = QStringLiteral("Local viewer backend is unavailable.");
			return plan;
		}

		const MumbleProto::ScreenShareCodec codec = codecFromPayload(payload);
		if (!Mumble::ScreenShare::isValidCodec(codec)) {
			plan.errorMessage = QStringLiteral("Missing or invalid codec selection.");
			return plan;
		}

		const unsigned int width =
			limitFromPayload(payload, "width", capabilities.maxWidth, Mumble::ScreenShare::HARD_MAX_WIDTH);
		const unsigned int height =
			limitFromPayload(payload, "height", capabilities.maxHeight, Mumble::ScreenShare::HARD_MAX_HEIGHT);
		const unsigned int fps =
			limitFromPayload(payload, "fps", capabilities.maxFps, Mumble::ScreenShare::HARD_MAX_FPS);
		const unsigned int bitrate =
			Mumble::ScreenShare::sanitizeBitrateKbps(nonNegativePayloadValue(payload, "bitrate_kbps"), codec, width,
													 height, fps);

		const bool preferHardware = payload.value(QStringLiteral("prefer_hardware_encoding")).toBool(true);
		const QList< EncoderBackend > backends = probeEncoderBackends();
		const ScreenShareExternalProcess::RuntimeSupport runtimeSupport = ScreenShareExternalProcess::probeRuntimeSupport();
		const ScreenShareRelayClient::Contract relayContract =
			publish ? ScreenShareRelayClient::contractForPublish(payload)
					: ScreenShareRelayClient::contractForView(payload);
		if (!relayContract.valid) {
			plan.errorMessage = relayContract.errorMessage;
			return plan;
		}
		const bool browserManagedRuntime =
			publish && relayContract.contractMode == QLatin1String("browser-webrtc-runtime");
		const EncoderBackend *selectedBackend =
			browserManagedRuntime ? nullptr : selectEncoderBackend(backends, codec, preferHardware);
		if (!browserManagedRuntime && !selectedBackend) {
			plan.errorMessage = QStringLiteral("No encoder backend can be planned for the negotiated codec.");
			return plan;
		}

		QJsonArray warnings;
		if (browserManagedRuntime) {
			warnings.append(
				QStringLiteral("The negotiated WebRTC relay will be executed through a dedicated browser runtime, so final codec and hardware acceleration decisions are delegated to the browser."));
		} else if (selectedBackend->backendID == QLatin1String("stub")) {
			warnings.append(QStringLiteral("No executable encoder backend was detected for the negotiated codec; start-publish will only work if stub fallback is explicitly enabled."));
		}
		if (!browserManagedRuntime && codec == MumbleProto::ScreenShareCodecAV1
			&& selectedBackend->backendID == QLatin1String("stub")) {
			warnings.append(QStringLiteral("AV1 is negotiable, but this host currently has no executable AV1 encode backend."));
		}
		for (const QString &warning : relayContract.warnings) {
			warnings.append(warning);
		}
		const QString relayScheme = QUrl(relayUrl).scheme().toLower();

		QJsonObject planPayload;
		planPayload.insert(QStringLiteral("stream_id"), streamID);
		planPayload.insert(QStringLiteral("mode"), publish ? QStringLiteral("publish-plan") : QStringLiteral("view-plan"));
		planPayload.insert(QStringLiteral("execution_mode"), QStringLiteral("planned"));
		planPayload.insert(QStringLiteral("relay_url"), relayUrl);
		planPayload.insert(QStringLiteral("relay_scheme"), relayScheme);
		planPayload.insert(QStringLiteral("relay_room_id"), relayRoomID);
		planPayload.insert(QStringLiteral("relay_token"), relayContract.relayToken);
		planPayload.insert(QStringLiteral("relay_token_present"), !relayContract.relayToken.isEmpty());
		planPayload.insert(QStringLiteral("relay_session_id"), relayContract.relaySessionID);
		planPayload.insert(QStringLiteral("relay_transport"), static_cast< int >(relayContract.relayTransport));
		planPayload.insert(QStringLiteral("relay_transport_token"),
						   Mumble::ScreenShare::relayTransportToConfigToken(relayContract.relayTransport));
		planPayload.insert(QStringLiteral("relay_role"), static_cast< int >(relayContract.relayRole));
		planPayload.insert(QStringLiteral("relay_role_token"),
						   Mumble::ScreenShare::relayRoleToConfigToken(relayContract.relayRole));
		planPayload.insert(QStringLiteral("relay_token_expires_at"),
						   QString::number(relayContract.relayTokenExpiresAt));
		planPayload.insert(QStringLiteral("relay_requires_signaling"), relayContract.requiresSignaling);
		planPayload.insert(QStringLiteral("relay_runtime_executable"), relayContract.runtimeExecutable);
		planPayload.insert(QStringLiteral("relay_contract_mode"), relayContract.contractMode);
		planPayload.insert(QStringLiteral("relay_contract_description"), relayContract.description);
		planPayload.insert(QStringLiteral("capture_backend"), capabilities.captureBackend);
		planPayload.insert(QStringLiteral("codec"), static_cast< int >(codec));
		planPayload.insert(QStringLiteral("codec_token"), Mumble::ScreenShare::codecToConfigToken(codec));
		planPayload.insert(QStringLiteral("width"), static_cast< int >(width));
		planPayload.insert(QStringLiteral("height"), static_cast< int >(height));
		planPayload.insert(QStringLiteral("fps"), static_cast< int >(fps));
		planPayload.insert(QStringLiteral("bitrate_kbps"), static_cast< int >(bitrate));
		planPayload.insert(QStringLiteral("prefer_hardware_encoding"), preferHardware);
		planPayload.insert(QStringLiteral("planned_encoder_backend"),
						   browserManagedRuntime ? QStringLiteral("browser-webrtc-%1")
													 .arg(Mumble::ScreenShare::codecToConfigToken(codec))
												 : codecBackendToken(selectedBackend->backendID, codec));
		planPayload.insert(QStringLiteral("planned_encoder_detail"),
						   browserManagedRuntime
							   ? QStringLiteral("Browser-managed WebRTC encode path. Codec preference is negotiated by Mumble, while final encoder selection happens inside the browser runtime.")
							   : selectedBackend->detail);
		planPayload.insert(QStringLiteral("planned_renderer_backend"),
						   relayContract.contractMode == QLatin1String("browser-webrtc-runtime")
							   ? QStringLiteral("browser-webrtc")
						   : publish ? QStringLiteral("ffmpeg-publish")
								   : (runtimeSupport.graphicalSessionAvailable ? QStringLiteral("ffplay-view")
																			  : QStringLiteral("ffmpeg-view")));
		planPayload.insert(QStringLiteral("warnings"), warnings);

		plan.valid = true;
		plan.payload = planPayload;
		return plan;
	}
} // namespace

QJsonArray ScreenShareSessionPlanner::advertisedEncoderBackends() {
	QJsonArray jsonBackends;
	for (const EncoderBackend &backend : probeEncoderBackends()) {
		jsonBackends.push_back(backendToJson(backend));
	}

	return jsonBackends;
}

ScreenShareSessionPlanner::Plan
	ScreenShareSessionPlanner::planPublish(const QJsonObject &payload,
										   const ScreenShareMediaSupport::CapabilitySummary &capabilities) {
	return buildPlan(payload, capabilities, true);
}

ScreenShareSessionPlanner::Plan
	ScreenShareSessionPlanner::planView(const QJsonObject &payload,
										const ScreenShareMediaSupport::CapabilitySummary &capabilities) {
	return buildPlan(payload, capabilities, false);
}
