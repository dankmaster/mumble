(function() {
	"use strict";

	const livekit = window.LivekitClient;
	let relayBridge = null;
	let bridgeLoadPromise = null;

	const DEFAULT_SCREEN_SHARE_WIDTH = 1280;
	const DEFAULT_SCREEN_SHARE_HEIGHT = 720;
	const DEFAULT_SCREEN_SHARE_FPS = 30;
	const MAX_IN_APP_RELAY_WIDTH = 1280;
	const MAX_IN_APP_RELAY_HEIGHT = 720;
	const MAX_IN_APP_RELAY_FPS = 30;
	const MIN_SCREEN_SHARE_BITRATE_KBPS = 1200;
	const DEFAULT_SCREEN_SHARE_BITRATE_KBPS = 3000;
	const MAX_SCREEN_SHARE_BITRATE_KBPS = 5000;
	const STATS_SAMPLE_INTERVAL_MSEC = 5000;
	const STATS_BRIDGE_INTERVAL_MSEC = 15000;
	const SECRET_PARAM_NAMES = [ "relay_token" ];

	const refs = {
		title: document.getElementById("title"),
		subtitle: document.getElementById("subtitle"),
		rolePill: document.getElementById("role-pill"),
		connectionPill: document.getElementById("connection-pill"),
		publisherActions: document.getElementById("publisher-actions"),
		viewerActions: document.getElementById("viewer-actions"),
		startShare: document.getElementById("start-share"),
		stopShare: document.getElementById("stop-share"),
		toggleAudio: document.getElementById("toggle-audio"),
		reconnectView: document.getElementById("reconnect-view"),
		hint: document.getElementById("hint"),
		videoStage: document.getElementById("video-stage"),
		emptyState: document.getElementById("empty-state"),
		emptyTitle: document.getElementById("empty-title"),
		emptyCopy: document.getElementById("empty-copy"),
		metaStream: document.getElementById("meta-stream"),
		metaRoom: document.getElementById("meta-room"),
		metaRelay: document.getElementById("meta-relay"),
		metaCodec: document.getElementById("meta-codec"),
		metaStats: document.getElementById("meta-stats"),
		log: document.getElementById("log")
	};

	const launchParams = collectLaunchParams();
	const config = parseConfig(launchParams);
	const state = {
		room: null,
		connectPromise: null,
		intentionalDisconnect: false,
		isPublisher: config.relayRole === "publisher",
		isConnected: false,
		isSharing: false,
		streamAudioMuted: false,
		currentTrackKey: "",
		currentTrackElement: null,
		currentAudioTrackKey: "",
		currentAudioElement: null,
		currentStatsTrack: null,
		currentStatsPublication: null,
		currentStatsLocal: false,
		statsTimer: null,
		statsLastSamples: {},
		statsLastBridgeAt: 0,
		statsUnavailableLogged: false,
		actualCodec: "",
		actualCodecLogged: false,
		codecMismatchLogged: false
	};
	scrubLaunchSecrets();

	function collectLaunchParams() {
		const params = new URLSearchParams(window.location.search);
		const fragment = window.location.hash ? window.location.hash.substring(1) : "";
		const fragmentParams = new URLSearchParams(fragment.charAt(0) === "?" ? fragment.substring(1) : fragment);
		fragmentParams.forEach(function(value, key) {
			params.set(key, value);
		});
		return params;
	}

	function scrubLaunchSecrets() {
		if (!window.history || typeof window.history.replaceState !== "function") {
			return;
		}

		const query = new URLSearchParams(window.location.search);
		let changed = Boolean(window.location.hash);
		SECRET_PARAM_NAMES.forEach(function(key) {
			if (query.has(key)) {
				query.delete(key);
				changed = true;
			}
		});
		if (!changed) {
			return;
		}

		const safeQuery = query.toString();
		const safeUrl = window.location.pathname + (safeQuery ? "?" + safeQuery : "");
		try {
			window.history.replaceState(null, document.title, safeUrl);
		} catch (error) {
			console.warn("Unable to scrub relay launch secrets from the address bar:", error);
		}
	}

	function timestamp() {
		return new Date().toLocaleTimeString([], {
			hour: "2-digit",
			minute: "2-digit",
			second: "2-digit"
		});
	}

	function log(message, level) {
		const prefix = (level || "info").toUpperCase();
		const line = "[" + timestamp() + "] " + prefix + " " + message;
		if (refs.log.textContent.length > 0) {
			refs.log.textContent += "\n";
		}
		refs.log.textContent += line;
		refs.log.scrollTop = refs.log.scrollHeight;

		if (level === "error") {
			console.error(message);
		} else if (level === "warn") {
			console.warn(message);
		} else {
			console.log(message);
		}
	}

	function notifyBridge(method) {
		if (!relayBridge || typeof relayBridge[method] !== "function") {
			return;
		}

		const args = Array.prototype.slice.call(arguments, 1);
		try {
			relayBridge[method].apply(relayBridge, args);
		} catch (error) {
			console.warn("Relay bridge call failed:", error);
		}
	}

	async function ensureBridge() {
		if (!window.qt || !window.qt.webChannelTransport) {
			return;
		}

		if (relayBridge) {
			return;
		}

		if (window.QWebChannel) {
			await new Promise(function(resolve) {
				try {
					new QWebChannel(qt.webChannelTransport, function(channel) {
						relayBridge = channel.objects.relayBridge || null;
						if (relayBridge) {
							notifyBridge("ready");
						}
						resolve();
					});
				} catch (error) {
					console.warn("Relay bridge initialization failed:", error);
					resolve();
				}
			});
			return;
		}

		if (!bridgeLoadPromise) {
			bridgeLoadPromise = new Promise(function(resolve) {
				const script = document.createElement("script");
				script.src = "qrc:///qtwebchannel/qwebchannel.js";
				script.async = true;
				script.onload = function() {
					try {
						new QWebChannel(qt.webChannelTransport, function(channel) {
							relayBridge = channel.objects.relayBridge || null;
							if (relayBridge) {
								notifyBridge("ready");
							}
							resolve();
						});
					} catch (error) {
						console.warn("Relay bridge initialization failed:", error);
						resolve();
					}
				};
				script.onerror = function() {
					console.warn("Relay bridge script could not be loaded.");
					resolve();
				};
				document.head.appendChild(script);
			});
		}

		await bridgeLoadPromise;
	}

	function requestFallback(reason) {
		const message = String(reason || "").trim() || "Relay runtime requested fallback.";
		log(message, "warn");
		notifyBridge("requestFallback", message);
	}

	function statsProviderForTrack(track, publication, localTrack) {
		const candidates = [ track, publication, track && track.track, publication && publication.track ];
		const keys = localTrack
			? [ "sender", "_sender", "rtcSender", "_rtcSender" ]
			: [ "receiver", "_receiver", "rtcReceiver", "_rtcReceiver" ];

		for (const candidate of candidates) {
			if (!candidate) {
				continue;
			}
			for (const key of keys) {
				const provider = candidate[key];
				if (provider && typeof provider.getStats === "function") {
					return provider;
				}
			}
			if (typeof candidate.getStats === "function") {
				return candidate;
			}
		}

		return null;
	}

	function forEachStats(report, callback) {
		if (!report) {
			return;
		}
		if (typeof report.forEach === "function") {
			report.forEach(callback);
			return;
		}
		if (Array.isArray(report)) {
			report.forEach(callback);
		}
	}

	function statNumber(stat, keys) {
		for (const key of keys) {
			if (typeof stat[key] === "number" && Number.isFinite(stat[key])) {
				return stat[key];
			}
		}
		return null;
	}

	function isVideoRtpStat(stat, localTrack) {
		const expectedType = localTrack ? "outbound-rtp" : "inbound-rtp";
		if (!stat || stat.type !== expectedType) {
			return false;
		}

		const kind = String(stat.kind || stat.mediaType || "").trim().toLowerCase();
		return !kind || kind === "video";
	}

	function selectPrimaryVideoRtpStat(report, localTrack) {
		let selected = null;
		let selectedBytes = -1;
		forEachStats(report, function(stat) {
			if (!isVideoRtpStat(stat, localTrack)) {
				return;
			}

			const bytes = statNumber(stat, localTrack ? [ "bytesSent" ] : [ "bytesReceived" ]) || 0;
			if (!selected || bytes >= selectedBytes) {
				selected = stat;
				selectedBytes = bytes;
			}
		});
		return selected;
	}

	function selectedRoundTripTimeMs(report) {
		let rtt = null;
		forEachStats(report, function(stat) {
			if (stat && stat.type === "candidate-pair" && stat.state === "succeeded"
				&& (stat.nominated || stat.selected || stat.selectedCandidatePairId)) {
				const current = statNumber(stat, [ "currentRoundTripTime", "totalRoundTripTime" ]);
				if (current !== null) {
					rtt = Math.round(current * 1000);
				}
			}
		});
		return rtt;
	}

	function codecLabelForStat(report, rtpStat) {
		if (!rtpStat || !rtpStat.codecId) {
			return "";
		}

		let label = "";
		forEachStats(report, function(stat) {
			if (stat && stat.id === rtpStat.codecId && stat.mimeType) {
				label = String(stat.mimeType).replace(/^video\//i, "").toUpperCase();
			}
		});
		return label;
	}

	function videoPlaybackQuality() {
		const element = state.currentTrackElement;
		if (!element || typeof element.getVideoPlaybackQuality !== "function") {
			return null;
		}

		try {
			return element.getVideoPlaybackQuality();
		} catch (error) {
			return null;
		}
	}

	function summarizeStatsReport(report, localTrack) {
		const rtpStat = selectPrimaryVideoRtpStat(report, localTrack);
		if (!rtpStat) {
			return null;
		}

		const bytes = statNumber(rtpStat, localTrack ? [ "bytesSent" ] : [ "bytesReceived" ]);
		const frames = statNumber(rtpStat, localTrack ? [ "framesEncoded", "framesSent" ] : [ "framesDecoded" ]);
		const lastSample = state.statsLastSamples[rtpStat.id] || null;
		const timestamp = statNumber(rtpStat, [ "timestamp" ]) || performance.now();
		let bitrateKbps = null;
		let fps = null;
		if (lastSample && bytes !== null && timestamp > lastSample.timestamp) {
			const elapsedSeconds = (timestamp - lastSample.timestamp) / 1000;
			bitrateKbps = Math.max(0, Math.round(((bytes - lastSample.bytes) * 8) / elapsedSeconds / 1000));
			if (frames !== null && lastSample.frames !== null) {
				fps = Math.max(0, Math.round((frames - lastSample.frames) / elapsedSeconds));
			}
		}
		if (bytes !== null) {
			state.statsLastSamples[rtpStat.id] = {
				bytes,
				frames,
				timestamp
			};
		}

		const packetsLost = statNumber(rtpStat, [ "packetsLost" ]);
		const packetsReceived = statNumber(rtpStat, [ "packetsReceived" ]);
		const packetsSent = statNumber(rtpStat, [ "packetsSent" ]);
		const packetsTotal = localTrack ? packetsSent : ((packetsReceived || 0) + Math.max(0, packetsLost || 0));
		const lossPercent = !localTrack && packetsLost !== null && packetsTotal > 0
			? Math.max(0, (packetsLost / packetsTotal) * 100)
			: null;
		const jitter = statNumber(rtpStat, [ "jitter" ]);
		const jitterBufferDelay = statNumber(rtpStat, [ "jitterBufferDelay" ]);
		const jitterBufferEmitted = statNumber(rtpStat, [ "jitterBufferEmittedCount" ]);
		const quality = videoPlaybackQuality();
		const framesDroppedStat = statNumber(rtpStat, [ "framesDropped" ]);
		const framesDropped = quality && typeof quality.droppedVideoFrames === "number"
			? quality.droppedVideoFrames
			: framesDroppedStat;

		return {
			direction: localTrack ? "out" : "in",
			bitrateKbps,
			fps,
			codec: codecLabelForStat(report, rtpStat),
			rttMs: selectedRoundTripTimeMs(report),
			jitterMs: jitter !== null ? Math.round(jitter * 1000) : null,
			jitterBufferMs: jitterBufferDelay !== null && jitterBufferEmitted
				? Math.round((jitterBufferDelay / jitterBufferEmitted) * 1000)
				: null,
			lossPercent,
			framesDropped,
			nackCount: statNumber(rtpStat, [ "nackCount" ]),
			pliCount: statNumber(rtpStat, [ "pliCount" ]),
			firCount: statNumber(rtpStat, [ "firCount" ]),
			retransmittedPackets: statNumber(rtpStat, localTrack ? [ "retransmittedPacketsSent" ] : [ "retransmittedPacketsReceived" ]),
			qualityLimitationReason: String(rtpStat.qualityLimitationReason || "").trim()
		};
	}

	function formatStatsSummary(summary) {
		if (!summary) {
			return "warming up";
		}

		const parts = [ summary.direction ];
		parts.push(summary.bitrateKbps !== null ? summary.bitrateKbps + " kbps" : "bitrate warming");
		if (summary.fps !== null) {
			parts.push(summary.fps + " fps");
		}
		if (summary.codec) {
			parts.push(summary.codec);
		}
		if (summary.rttMs !== null) {
			parts.push("rtt " + summary.rttMs + " ms");
		}
		if (summary.jitterMs !== null) {
			parts.push("jitter " + summary.jitterMs + " ms");
		}
		if (summary.jitterBufferMs !== null) {
			parts.push("buffer " + summary.jitterBufferMs + " ms");
		}
		if (summary.lossPercent !== null) {
			parts.push("loss " + summary.lossPercent.toFixed(1) + "%");
		}
		if (summary.framesDropped !== null && summary.framesDropped > 0) {
			parts.push("dropped " + summary.framesDropped);
		}
		if (summary.nackCount || summary.pliCount || summary.firCount) {
			parts.push("repair n" + (summary.nackCount || 0) + "/p" + (summary.pliCount || 0)
				+ "/f" + (summary.firCount || 0));
		}
		if (summary.retransmittedPackets) {
			parts.push("rtx " + summary.retransmittedPackets);
		}
		if (summary.qualityLimitationReason && summary.qualityLimitationReason !== "none") {
			parts.push("limited " + summary.qualityLimitationReason);
		}
		return parts.join(" / ");
	}

	function formatCodecMetadata() {
		const parts = [
			"requested " + configuredCodecDisplayName(config.requestedCodec),
			"session " + configuredCodecDisplayName(config.codec)
		];
		if (state.actualCodec) {
			parts.push("actual " + observedCodecDisplayName(state.actualCodec));
		}
		parts.push(config.width + "x" + config.height + " @ " + config.fps + "fps");
		parts.push(config.bitrateKbps + " kbps");
		return parts.join(" / ");
	}

	function observeActualCodec(summary) {
		if (!summary || !summary.codec) {
			return;
		}

		const actualCodec = normalizeObservedCodec(summary.codec);
		if (!actualCodec) {
			return;
		}

		state.actualCodec = actualCodec;
		renderStaticMetadata();

		const requestedCodec = normalizeCodec(config.requestedCodec);
		const negotiatedCodec = normalizeCodec(config.codec);
		if (!state.actualCodecLogged) {
			log("Relay actual WebRTC codec: " + observedCodecDisplayName(actualCodec) + ".");
			notifyBridge("reportStats", "actual_codec=" + observedCodecDisplayName(actualCodec)
				+ " requested_codec=" + configuredCodecDisplayName(requestedCodec)
				+ " negotiated_codec=" + configuredCodecDisplayName(negotiatedCodec),
				observedCodecDisplayName(actualCodec));
			state.actualCodecLogged = true;
		}

		if (!state.codecMismatchLogged && actualCodec !== requestedCodec) {
			log("Relay codec mismatch: requested " + configuredCodecDisplayName(requestedCodec)
				+ ", negotiated " + configuredCodecDisplayName(negotiatedCodec)
				+ ", actual " + observedCodecDisplayName(actualCodec) + ".", "warn");
			notifyBridge("reportStats", "codec_mismatch requested_codec=" + configuredCodecDisplayName(requestedCodec)
				+ " negotiated_codec=" + configuredCodecDisplayName(negotiatedCodec)
				+ " actual_codec=" + observedCodecDisplayName(actualCodec),
				observedCodecDisplayName(actualCodec));
			state.codecMismatchLogged = true;
		}
	}

	async function sampleStats() {
		if (!state.currentStatsTrack) {
			if (refs.metaStats) {
				setText(refs.metaStats, state.isConnected ? "waiting for video" : "idle");
			}
			return;
		}

		const provider = statsProviderForTrack(state.currentStatsTrack, state.currentStatsPublication, state.currentStatsLocal);
		if (!provider) {
			if (refs.metaStats) {
				setText(refs.metaStats, "stats unavailable");
			}
			if (!state.statsUnavailableLogged) {
				log("WebRTC track stats are not exposed by this relay runtime.", "warn");
				notifyBridge("reportStats", "stats_unavailable reason=track_stats_not_exposed", "");
				state.statsUnavailableLogged = true;
			}
			return;
		}

		try {
			const report = await provider.getStats();
			const summary = summarizeStatsReport(report, state.currentStatsLocal);
			const summaryText = formatStatsSummary(summary);
			observeActualCodec(summary);
			if (refs.metaStats) {
				setText(refs.metaStats, summaryText);
			}

			const now = Date.now();
			if (summary && now - state.statsLastBridgeAt >= STATS_BRIDGE_INTERVAL_MSEC) {
				state.statsLastBridgeAt = now;
				log("Relay stats: " + summaryText + ".");
				notifyBridge("reportStats", summaryText, state.actualCodec ? observedCodecDisplayName(state.actualCodec) : "");
			}
		} catch (error) {
			if (refs.metaStats) {
				setText(refs.metaStats, "stats unavailable");
			}
			if (!state.statsUnavailableLogged) {
				log("Unable to collect relay stats: " + describeError(error), "warn");
				notifyBridge("reportStats", "stats_unavailable reason=" + describeError(error), "");
				state.statsUnavailableLogged = true;
			}
		}
	}

	function startStatsMonitor() {
		if (state.statsTimer) {
			return;
		}

		void sampleStats();
		state.statsTimer = window.setInterval(function() {
			void sampleStats();
		}, STATS_SAMPLE_INTERVAL_MSEC);
	}

	function stopStatsMonitor() {
		if (state.statsTimer) {
			window.clearInterval(state.statsTimer);
			state.statsTimer = null;
		}
		state.statsLastSamples = {};
		state.statsLastBridgeAt = 0;
		if (refs.metaStats) {
			setText(refs.metaStats, "idle");
		}
	}

	function isUserCanceledScreenShareError(error) {
		if (!error) {
			return false;
		}

		const name = String(error.name || "").trim();
		const message = String(error.message || error.toString() || "").toLowerCase();
		if (name === "AbortError") {
			return !message.includes("invalid state");
		}

		return message.includes("user aborted") || message.includes("screen share request was cancelled")
			|| message.includes("screen share request was canceled") || message.includes("picker was closed")
			|| message.includes("selection was canceled") || message.includes("selection was cancelled");
	}

	function setText(element, value) {
		element.textContent = value;
	}

	function setPill(element, label, tone) {
		element.textContent = label;
		element.className = "pill";
		if (tone) {
			element.classList.add("pill-" + tone);
		} else {
			element.classList.add("pill-muted");
		}
	}

	function setHint(message) {
		setText(refs.hint, message);
	}

	function updateAudioToggle() {
		if (!refs.toggleAudio) {
			return;
		}

		const muted = state.streamAudioMuted;
		refs.toggleAudio.textContent = muted ? "Unmute stream audio" : "Mute stream audio";
		refs.toggleAudio.setAttribute("aria-pressed", muted ? "true" : "false");
		refs.toggleAudio.classList.toggle("is-active", muted);
	}

	function showEmpty(title, copy) {
		setText(refs.emptyTitle, title);
		setText(refs.emptyCopy, copy);
		refs.emptyState.classList.remove("hidden");
		if (state.currentTrackElement) {
			state.currentTrackElement.remove();
			state.currentTrackElement = null;
			state.currentTrackKey = "";
		}
		state.currentStatsTrack = null;
		state.currentStatsPublication = null;
		state.currentStatsLocal = false;
	}

	function normalizeToken(value, fallback) {
		const token = String(value || "").trim().toLowerCase();
		return token || fallback;
	}

	function normalizeCodec(value) {
		switch (normalizeToken(value, "")) {
			case "h264":
			case "vp8":
			case "vp9":
			case "av1":
				return normalizeToken(value, "");
			default:
				return "vp8";
		}
	}

	function normalizeObservedCodec(value) {
		const token = String(value || "").trim().toLowerCase().replace(/[^a-z0-9]/g, "");
		if (token === "h264" || token === "vp8" || token === "vp9" || token === "av1") {
			return token;
		}
		return "";
	}

	function configuredCodecDisplayName(value) {
		const token = normalizeCodec(value);
		return token ? token.toUpperCase() : "UNKNOWN";
	}

	function observedCodecDisplayName(value) {
		const token = normalizeObservedCodec(value);
		return token ? token.toUpperCase() : "UNKNOWN";
	}

	function parsePositiveInteger(value, fallback) {
		const parsed = Number.parseInt(String(value || "").trim(), 10);
		return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
	}

	function clampInteger(value, minimum, maximum) {
		return Math.min(Math.max(value, minimum), maximum);
	}

	function recommendedScreenShareBitrateKbps(codec, width, height, fps) {
		const pixelsPerFrame = Math.max(1, width) * Math.max(1, height);
		const frameRateFactor = Math.max(1, fps) / DEFAULT_SCREEN_SHARE_FPS;
		let targetKbps = DEFAULT_SCREEN_SHARE_BITRATE_KBPS
			* (pixelsPerFrame / (DEFAULT_SCREEN_SHARE_WIDTH * DEFAULT_SCREEN_SHARE_HEIGHT))
			* frameRateFactor;

		switch (codec) {
			case "av1":
				targetKbps *= 0.7;
				break;
			case "vp8":
				targetKbps *= 1.05;
				break;
			case "vp9":
				targetKbps *= 0.8;
				break;
			case "h264":
			default:
				break;
		}

		return clampInteger(Math.round(targetKbps), MIN_SCREEN_SHARE_BITRATE_KBPS, MAX_SCREEN_SHARE_BITRATE_KBPS);
	}

	function parseScreenShareBitrateKbps(value, codec, width, height, fps) {
		const recommended = recommendedScreenShareBitrateKbps(codec, width, height, fps);
		const parsed = parsePositiveInteger(value, 0);
		if (parsed <= 0) {
			return recommended;
		}

		return clampInteger(Math.min(parsed, recommended), MIN_SCREEN_SHARE_BITRATE_KBPS, MAX_SCREEN_SHARE_BITRATE_KBPS);
	}

	function parseBooleanFlag(value, fallback) {
		const token = normalizeToken(value, "");
		if (!token) {
			return fallback;
		}

		if (token === "1" || token === "true" || token === "yes" || token === "on" || token === "include") {
			return true;
		}
		if (token === "0" || token === "false" || token === "no" || token === "off" || token === "exclude") {
			return false;
		}

		return fallback;
	}

	function parseIncludeExclude(value, fallback) {
		const token = normalizeToken(value, fallback);
		return token === "include" || token === "exclude" ? token : fallback;
	}

	function deriveWebSocketUrl(relayUrl) {
		if (!relayUrl) {
			return "";
		}

		let parsed;
		try {
			parsed = new URL(relayUrl, window.location.href);
		} catch (error) {
			return "";
		}

		if (parsed.protocol === "http:") {
			parsed.protocol = "ws:";
		} else if (parsed.protocol === "https:") {
			parsed.protocol = "wss:";
		}

		return parsed.protocol === "ws:" || parsed.protocol === "wss:" ? parsed.toString() : "";
	}

	function parseConfig(params) {
		const relayUrl = (params.get("relay_url") || "").trim();
		const relayRole = normalizeToken(params.get("relay_role"), "viewer");
		const codec = normalizeCodec(params.get("codec"));
		const requestedCodec = normalizeCodec(params.get("requested_codec") || params.get("codec"));
		const width = clampInteger(parsePositiveInteger(params.get("width"), DEFAULT_SCREEN_SHARE_WIDTH), 1, MAX_IN_APP_RELAY_WIDTH);
		const height = clampInteger(parsePositiveInteger(params.get("height"), DEFAULT_SCREEN_SHARE_HEIGHT), 1, MAX_IN_APP_RELAY_HEIGHT);
		const fps = clampInteger(parsePositiveInteger(params.get("fps"), DEFAULT_SCREEN_SHARE_FPS), 1, MAX_IN_APP_RELAY_FPS);
		return {
			relayUrl,
			wsRelayUrl: deriveWebSocketUrl(relayUrl),
			relayRoomId: (params.get("relay_room_id") || "").trim(),
			relayToken: (params.get("relay_token") || "").trim(),
			relaySessionId: (params.get("relay_session_id") || "").trim(),
			streamId: (params.get("stream_id") || "").trim(),
			relayRole: relayRole === "publisher" ? "publisher" : "viewer",
			transport: normalizeToken(params.get("transport"), "webrtc"),
			codec,
			requestedCodec,
			width,
			height,
			fps,
			bitrateKbps: parseScreenShareBitrateKbps(params.get("bitrate_kbps"), codec, width, height, fps),
			captureAudio: parseBooleanFlag(params.get("capture_audio"), false),
			systemAudio: parseIncludeExclude(params.get("system_audio"), "exclude"),
			surfaceSwitching: parseIncludeExclude(params.get("surface_switching"), "include"),
			selfBrowserSurface: parseIncludeExclude(params.get("self_browser_surface"), "exclude")
		};
	}

	function missingRelayConfigFields() {
		const fields = [];
		if (!config.wsRelayUrl) {
			fields.push("relay_url");
		}
		if (!config.relayRoomId) {
			fields.push("relay_room_id");
		}
		if (!config.relayToken) {
			fields.push("relay_token");
		}
		return fields;
	}

	function normalizeSourceToken(value) {
		return String(value || "").trim().toLowerCase().replace(/[^a-z]/g, "");
	}

	function screenShareSourceMatches(source) {
		return normalizeSourceToken(source) === "screenshare";
	}

	function screenShareAudioSourceMatches(source) {
		return normalizeSourceToken(source) === "screenshareaudio";
	}

	function isVideoTrack(track, publication) {
		const kind = String((track && track.kind) || (publication && publication.kind) || "").trim().toLowerCase();
		return kind === "video";
	}

	function isAudioTrack(track, publication) {
		const kind = String((track && track.kind) || (publication && publication.kind) || "").trim().toLowerCase();
		return kind === "audio";
	}

	function isScreenShareTrack(track, publication) {
		if (!isVideoTrack(track, publication)) {
			return false;
		}

		const livekitSource = livekit && livekit.Track && livekit.Track.Source ? livekit.Track.Source.ScreenShare : "";
		const publicationSource = publication && publication.source ? publication.source : "";
		const trackSource = track && track.source ? track.source : "";
		if (screenShareSourceMatches(livekitSource)
			? publicationSource === livekitSource || trackSource === livekitSource || screenShareSourceMatches(publicationSource)
				|| screenShareSourceMatches(trackSource)
			: screenShareSourceMatches(publicationSource) || screenShareSourceMatches(trackSource)) {
			return true;
		}

		// The relay room is dedicated to one screen share, so a remote video track
		// is still the share even when the SFU/browser omits the ScreenShare source.
		return !state.isPublisher;
	}

	function isScreenShareAudioTrack(track, publication) {
		if (!isAudioTrack(track, publication)) {
			return false;
		}

		const livekitSource = livekit && livekit.Track && livekit.Track.Source ? livekit.Track.Source.ScreenShareAudio : "";
		const publicationSource = publication && publication.source ? publication.source : "";
		const trackSource = track && track.source ? track.source : "";
		if (screenShareAudioSourceMatches(livekitSource)
			? publicationSource === livekitSource || trackSource === livekitSource || screenShareAudioSourceMatches(publicationSource)
				|| screenShareAudioSourceMatches(trackSource)
			: screenShareAudioSourceMatches(publicationSource) || screenShareAudioSourceMatches(trackSource)) {
			return true;
		}

		return !state.isPublisher;
	}

	function describeParticipant(participant) {
		if (!participant) {
			return "unknown participant";
		}
		return participant.name || participant.identity || "unknown participant";
	}

	function trackKey(publication, participant, localTrack) {
		const participantKey = participant && (participant.identity || participant.sid) ? participant.identity || participant.sid : "local";
		const publicationKey = publication && publication.trackSid ? publication.trackSid : publication && publication.sid ? publication.sid : "";
		return (localTrack ? "local:" : "remote:") + participantKey + ":" + publicationKey;
	}

	function applyScreenShareContentHint(track) {
		const mediaTrack = track && track.mediaStreamTrack ? track.mediaStreamTrack : null;
		if (!mediaTrack || !("contentHint" in mediaTrack)) {
			return;
		}

		try {
			mediaTrack.contentHint = "detail";
		} catch (error) {
			console.warn("Unable to apply screen-share content hint:", error);
		}
	}

	function attachTrack(track, publication, participant, localTrack) {
		if (!isScreenShareTrack(track, publication)) {
			return;
		}
		applyScreenShareContentHint(track);

		const key = trackKey(publication, participant, localTrack);
		if (state.currentTrackKey === key && state.currentTrackElement) {
			return;
		}

		showEmpty(localTrack ? "Share is live" : "Connecting video", localTrack
			? "Preparing local screen-share preview."
			: "Waiting for the screen-share track to render.");

		let element;
		try {
			element = track.attach();
		} catch (error) {
			log("Unable to attach screen-share track: " + error.message, "error");
			showEmpty("Unable to render video", "The relay connected, but the screen-share track could not be attached.");
			return;
		}

		if (!(element instanceof HTMLVideoElement)) {
			log("Ignoring non-video screen-share attachment.", "warn");
			if (element && typeof element.remove === "function") {
				element.remove();
			}
			return;
		}

		element.autoplay = true;
		element.playsInline = true;
		element.controls = false;
		element.muted = Boolean(localTrack);
		element.className = "stage-video" + (localTrack ? " is-local-preview" : "");
		refs.videoStage.appendChild(element);
		refs.emptyState.classList.add("hidden");
		state.currentTrackElement = element;
		state.currentTrackKey = key;
		state.currentStatsTrack = track;
		state.currentStatsPublication = publication;
		state.currentStatsLocal = Boolean(localTrack);
		state.statsLastSamples = {};
		state.statsUnavailableLogged = false;
		startStatsMonitor();

		if (typeof element.play === "function") {
			element.play().catch(function(error) {
				log("Video playback is waiting for browser permission: " + error.message, "warn");
			});
		}

		const actor = describeParticipant(participant);
		log((localTrack ? "Showing local preview for " : "Showing remote screen share from ") + actor + ".");
	}

	function attachAudioTrack(track, publication, participant) {
		if (!isScreenShareAudioTrack(track, publication)) {
			return;
		}

		const key = trackKey(publication, participant, false);
		if (state.currentAudioTrackKey === key && state.currentAudioElement) {
			return;
		}

		let element;
		try {
			element = track.attach();
		} catch (error) {
			log("Unable to attach screen-share audio: " + error.message, "error");
			return;
		}

		if (!(element instanceof HTMLAudioElement)) {
			log("Ignoring non-audio screen-share attachment.", "warn");
			if (element && typeof element.remove === "function") {
				element.remove();
			}
			return;
		}

		if (state.currentAudioElement) {
			state.currentAudioElement.remove();
		}

		element.autoplay = true;
		element.controls = false;
		element.muted = state.streamAudioMuted;
		element.hidden = true;
		refs.videoStage.appendChild(element);
		state.currentAudioElement = element;
		state.currentAudioTrackKey = key;

		if (typeof element.play === "function") {
			element.play().catch(function(error) {
				log("Screen-share audio playback is waiting for browser permission: " + error.message, "warn");
			});
		}

		updateAudioToggle();
		log("Playing remote screen-share audio from " + describeParticipant(participant) + ".");
	}

	function detachTrack(track, publication, participant, localTrack) {
		if (!isScreenShareTrack(track, publication)) {
			return;
		}
		const key = trackKey(publication, participant, localTrack);

		if (track && typeof track.detach === "function") {
			const detached = track.detach();
			if (Array.isArray(detached)) {
				detached.forEach(function(node) {
					if (node && typeof node.remove === "function") {
						node.remove();
					}
				});
			}
		}

		if (state.currentTrackKey !== key) {
			log((localTrack ? "Stale local" : "Stale remote") + " screen-share track ended for " + describeParticipant(participant) + ".");
			return;
		}

		if (state.currentTrackElement) {
			state.currentTrackElement.remove();
			state.currentTrackElement = null;
			state.currentTrackKey = "";
		}

		if (localTrack) {
			showEmpty("Share stopped", "Your screen is no longer being published.");
		} else {
			showEmpty("Waiting for screen share", "The screen-share track is not currently active.");
		}

		log((localTrack ? "Local" : "Remote") + " screen-share track ended for " + describeParticipant(participant) + ".");
	}

	function detachAudioTrack(track, publication, participant) {
		if (!isScreenShareAudioTrack(track, publication)) {
			return;
		}
		const key = trackKey(publication, participant, false);

		if (track && typeof track.detach === "function") {
			const detached = track.detach();
			if (Array.isArray(detached)) {
				detached.forEach(function(node) {
					if (node && typeof node.remove === "function") {
						node.remove();
					}
				});
			}
		}

		if (state.currentAudioTrackKey !== key) {
			log("Stale remote screen-share audio ended for " + describeParticipant(participant) + ".");
			return;
		}

		if (state.currentAudioElement) {
			state.currentAudioElement.remove();
			state.currentAudioElement = null;
			state.currentAudioTrackKey = "";
		}

		updateAudioToggle();
		log("Remote screen-share audio ended for " + describeParticipant(participant) + ".");
	}

	function setStreamAudioMuted(muted) {
		state.streamAudioMuted = Boolean(muted);
		if (state.currentAudioElement) {
			state.currentAudioElement.muted = state.streamAudioMuted;
		}
		updateAudioToggle();
	}

	function toggleStreamAudioMuted() {
		const nextMuted = !state.streamAudioMuted;
		setStreamAudioMuted(nextMuted);
		log(nextMuted ? "Muted remote screen-share audio." : "Unmuted remote screen-share audio.");
	}

	function publicationValues(collection) {
		if (!collection) {
			return [];
		}
		if (typeof collection.values === "function") {
			return Array.from(collection.values());
		}
		return Array.isArray(collection) ? collection : [];
	}

	function findLocalScreenSharePublication(room) {
		if (!room || !room.localParticipant) {
			return null;
		}

		const participant = room.localParticipant;
		const collections = [
			participant.videoTrackPublications,
			participant.trackPublications
		];

		for (const collection of collections) {
			for (const publication of publicationValues(collection)) {
				if (publication && isScreenShareTrack(publication.track, publication)) {
					return publication;
				}
			}
		}

		return null;
	}

	function scanForRemoteScreenShare(room) {
		if (!room) {
			return;
		}

		const participants = room.remoteParticipants && typeof room.remoteParticipants.values === "function"
			? Array.from(room.remoteParticipants.values())
			: [];

		for (const participant of participants) {
			for (const publication of publicationValues(participant.trackPublications)) {
				if (!publication) {
					continue;
				}
				if (typeof publication.setSubscribed === "function"
					&& (isScreenShareTrack(publication.track, publication) || isScreenShareAudioTrack(publication.track, publication))) {
					publication.setSubscribed(true);
				}
				if (publication.track && isScreenShareTrack(publication.track, publication)) {
					attachTrack(publication.track, publication, participant, false);
				}
				if (publication.track && isScreenShareAudioTrack(publication.track, publication)) {
					attachAudioTrack(publication.track, publication, participant);
				}
			}
		}

		return Boolean(state.currentTrackElement || state.currentAudioElement);
	}

	function buildScreenShareCaptureOptions() {
		const options = {
			video: {
				width: { ideal: config.width, max: config.width },
				height: { ideal: config.height, max: config.height },
				frameRate: { ideal: config.fps, max: config.fps }
			},
			resolution: {
				width: config.width,
				height: config.height,
				frameRate: config.fps
			},
			contentHint: "detail",
			selfBrowserSurface: config.selfBrowserSurface,
			surfaceSwitching: config.surfaceSwitching
		};

		if (config.captureAudio) {
			options.audio = true;
			options.systemAudio = config.systemAudio;
			options.suppressLocalAudioPlayback = false;
		}

		return options;
	}

	function buildScreenSharePublishOptions() {
		return {
			source: livekit && livekit.Track && livekit.Track.Source ? livekit.Track.Source.ScreenShare : undefined,
			simulcast: false,
			backupCodec: false,
			videoCodec: config.codec,
			degradationPreference: "maintain-framerate",
			screenShareEncoding: {
				maxBitrate: config.bitrateKbps * 1000,
				maxFramerate: config.fps,
				priority: "high"
			}
		};
	}

	function buildRoomOptions() {
		const publishDefaults = {
			degradationPreference: "maintain-framerate",
			simulcast: false,
			backupCodec: false,
			videoCodec: config.codec,
			screenShareEncoding: {
				maxBitrate: config.bitrateKbps * 1000,
				maxFramerate: config.fps,
				priority: "high"
			}
		};

		return {
			adaptiveStream: false,
			dynacast: false,
			publishDefaults: publishDefaults
		};
	}

	function installRoomHandlers(room) {
		const roomEvent = livekit.RoomEvent || {};

		if (roomEvent.Connected) {
			room.on(roomEvent.Connected, function() {
				state.isConnected = true;
				setPill(refs.connectionPill, "connected", "success");
				log("Connected to relay room " + config.relayRoomId + ".");
				if (state.isPublisher) {
					refs.startShare.disabled = false;
					setHint("Connected. Click Start sharing to choose a screen or window.");
				} else if (!scanForRemoteScreenShare(room)) {
					setHint("Connected. Waiting for the publisher to start screen sharing.");
				}
			});
		}

		if (roomEvent.ConnectionStateChanged) {
			room.on(roomEvent.ConnectionStateChanged, function(connectionState) {
				const token = String(connectionState || "").trim().toLowerCase();
				if (token === "connected") {
					setPill(refs.connectionPill, "connected", "success");
				} else if (token === "connecting" || token === "reconnecting") {
					setPill(refs.connectionPill, token, "warning");
				} else if (token === "disconnected") {
					setPill(refs.connectionPill, "disconnected", "muted");
				} else {
					setPill(refs.connectionPill, token || "state", "muted");
				}
			});
		}

		if (roomEvent.Disconnected) {
			room.on(roomEvent.Disconnected, function(reason) {
				state.isConnected = false;
				state.connectPromise = null;
				state.isSharing = false;
				stopStatsMonitor();
				refs.startShare.disabled = false;
				refs.stopShare.disabled = true;
				setPill(refs.connectionPill, "disconnected", state.intentionalDisconnect ? "muted" : "warning");
				if (state.intentionalDisconnect) {
					log("Disconnected from relay room.");
				} else {
					log("Relay room disconnected unexpectedly" + (reason ? ": " + reason : "."), "warn");
					showEmpty("Disconnected", "The relay connection closed. Reconnect from Mumble if needed.");
				}
				if (state.currentAudioElement) {
					state.currentAudioElement.remove();
					state.currentAudioElement = null;
					state.currentAudioTrackKey = "";
				}
				updateAudioToggle();
				state.intentionalDisconnect = false;
			});
		}

		if (roomEvent.TrackSubscribed) {
			room.on(roomEvent.TrackSubscribed, function(track, publication, participant) {
				if (isScreenShareTrack(track, publication)) {
					attachTrack(track, publication, participant, false);
				} else if (isScreenShareAudioTrack(track, publication)) {
					attachAudioTrack(track, publication, participant);
				}
			});
		}

		if (roomEvent.TrackUnsubscribed) {
			room.on(roomEvent.TrackUnsubscribed, function(track, publication, participant) {
				if (isScreenShareTrack(track, publication)) {
					detachTrack(track, publication, participant, false);
				} else if (isScreenShareAudioTrack(track, publication)) {
					detachAudioTrack(track, publication, participant);
				}
			});
		}

		if (roomEvent.LocalTrackPublished) {
			room.on(roomEvent.LocalTrackPublished, function(publication, participant) {
				if (publication && isScreenShareTrack(publication.track, publication)) {
					attachTrack(publication.track, publication, participant || room.localParticipant, true);
				}
			});
		}

		if (roomEvent.LocalTrackUnpublished) {
			room.on(roomEvent.LocalTrackUnpublished, function(publication, participant) {
				if (publication && isScreenShareTrack(publication.track, publication)) {
					detachTrack(publication.track, publication, participant || room.localParticipant, true);
				}
			});
		}

		if (roomEvent.TrackSubscriptionFailed) {
			room.on(roomEvent.TrackSubscriptionFailed, function(trackSid, participant, error) {
				log("Track subscription failed for " + describeParticipant(participant) + ": " + (error ? error.message : trackSid), "error");
			});
		}

		if (roomEvent.MediaDevicesError) {
			room.on(roomEvent.MediaDevicesError, function(error) {
				log("Screen-capture device error: " + describeError(error), "error");
			});
		}
	}

	function describeError(error) {
		if (!error) {
			return "unknown error";
		}

		const name = String(error.name || "").trim();
		const message = String(error.message || error.toString() || "").trim();
		if (name && message) {
			return name + ": " + message;
		}
		return name || message || "unknown error";
	}

	function createRoom() {
		const room = new livekit.Room(buildRoomOptions());
		installRoomHandlers(room);
		return room;
	}

	async function ensureConnected() {
		if (state.isConnected && state.room) {
			return state.room;
		}

		if (state.connectPromise) {
			return state.connectPromise;
		}

		const missingFields = missingRelayConfigFields();
		if (missingFields.length > 0) {
			throw new Error("Missing relay session field(s): " + missingFields.join(", ") + ".");
		}

		if (!state.room) {
			state.room = createRoom();
		}

		setPill(refs.connectionPill, "connecting", "warning");
		setHint(state.isPublisher
			? "Connecting to the relay before capture starts."
			: "Connecting to the relay and waiting for the live screen share.");

		state.connectPromise = state.room.connect(config.wsRelayUrl, config.relayToken).then(function() {
			state.isConnected = true;
			if (!state.isPublisher) {
				scanForRemoteScreenShare(state.room);
			}
			return state.room;
		}).catch(function(error) {
			state.isConnected = false;
			state.connectPromise = null;
			setPill(refs.connectionPill, "error", "danger");
			log("Unable to connect to relay at " + (config.wsRelayUrl || "missing endpoint") + ": "
				+ describeError(error), "error");
			throw error;
		});

		return state.connectPromise;
	}

	async function startSharing() {
		refs.startShare.disabled = true;
		refs.stopShare.disabled = true;
		try {
			const room = state.isConnected && state.room ? state.room : await ensureConnected();
			setHint(config.captureAudio
				? "Choose a screen, window, or tab with audio in the browser picker."
				: "Choose a screen or window in the browser picker.");
			log(config.captureAudio
				? "Requesting screen capture and shared audio from the browser."
				: "Requesting screen capture from the browser.");
			await room.localParticipant.setScreenShareEnabled(true, buildScreenShareCaptureOptions(), buildScreenSharePublishOptions());
			state.isSharing = true;
			refs.stopShare.disabled = false;
			setPill(refs.connectionPill, "live", "success");
			setHint("Screen share is live. You can keep this window open while sharing.");

			const publication = findLocalScreenSharePublication(room);
			if (publication && publication.track) {
				attachTrack(publication.track, publication, room.localParticipant, true);
			}
		} catch (error) {
			state.isSharing = false;
			refs.startShare.disabled = false;
			refs.stopShare.disabled = true;
			setHint("Screen sharing could not be started.");
			if (isUserCanceledScreenShareError(error)) {
				log("Screen sharing was canceled by the user.", "info");
				showEmpty("Share canceled", "No screen-share source was selected.");
				return;
			}

			log("Unable to start screen sharing: " + describeError(error), "error");
			showEmpty("Share did not start", "The browser did not grant or keep screen-capture permission.");
			requestFallback("The in-app relay window could not start screen sharing: " + describeError(error));
		}
	}

	async function stopSharing() {
		if (!state.room || !state.isConnected) {
			return;
		}

		refs.stopShare.disabled = true;
		try {
			await state.room.localParticipant.setScreenShareEnabled(false);
			state.isSharing = false;
			refs.startShare.disabled = false;
			setPill(refs.connectionPill, "connected", "success");
			setHint("Share stopped. You can start again without reopening this window.");
			showEmpty("Share stopped", "Your screen is no longer being published.");
		} catch (error) {
			refs.stopShare.disabled = false;
			log("Unable to stop screen sharing cleanly: " + error.message, "error");
		}
	}

	async function reconnectViewer() {
		setHint("Reconnecting viewer to the relay.");
		await disconnectRoom();
		try {
			await ensureConnected();
		} catch (error) {
			showEmpty("Reconnect failed", "The viewer could not reconnect to the relay session.");
			requestFallback("The in-app relay viewer could not reconnect.");
		}
	}

	async function disconnectRoom() {
		if (!state.room) {
			return;
		}

		try {
			state.intentionalDisconnect = true;
			stopStatsMonitor();
			state.room.disconnect();
		} catch (error) {
			log("Relay disconnect raised an error: " + error.message, "warn");
		} finally {
			state.isConnected = false;
			state.connectPromise = null;
		}
	}

	function renderStaticMetadata() {
		setText(refs.metaStream, config.streamId || "unknown");
		setText(refs.metaRoom, config.relayRoomId || "unknown");
		setText(refs.metaRelay, config.wsRelayUrl || "unknown");
		setText(refs.metaCodec, formatCodecMetadata());
	}

	function renderShell() {
		renderStaticMetadata();
		setPill(refs.rolePill, state.isPublisher ? "publisher" : "viewer", state.isPublisher ? "accent" : "muted");
		setPill(refs.connectionPill, "idle", "muted");
		updateAudioToggle();

		if (state.isPublisher) {
			setText(refs.title, "Ready to share your screen");
			setText(refs.subtitle, "Mumble opened this screen-share window automatically. Start sharing when you are ready.");
			refs.publisherActions.classList.remove("hidden");
			refs.viewerActions.classList.add("hidden");
			refs.startShare.disabled = true;
			refs.stopShare.disabled = true;
			setHint("Connecting to the relay before screen capture starts.");
			showEmpty("No screen selected yet", "Click Start sharing to choose a screen or window.");
		} else {
			setText(refs.title, "Joining screen share");
			setText(refs.subtitle, "Mumble opened this screen-share window automatically.");
			refs.publisherActions.classList.add("hidden");
			refs.viewerActions.classList.remove("hidden");
			setHint("Connecting to the relay and waiting for the publisher.");
			showEmpty("Waiting for screen share", "The viewer is connected but no screen-share video is active yet.");
		}
	}

	async function boot() {
		await ensureBridge();
		renderShell();
		log("Relay role: " + config.relayRole + ".");
		log("Relay transport: " + config.transport + ".");
		log("Relay endpoint: " + (config.relayUrl || "missing") + ".");
		log("Relay requested codec: " + configuredCodecDisplayName(config.requestedCodec) + ".");
		log("Relay negotiated session codec: " + configuredCodecDisplayName(config.codec) + ".");
		log("Relay media profile: " + configuredCodecDisplayName(config.codec) + " " + config.width + "x" + config.height
			+ " @ " + config.fps + "fps, " + config.bitrateKbps + " kbps, single low-latency stream.");
		if (state.isPublisher && config.captureAudio) {
			log("Screen-share audio capture is enabled for compatible browser sources.");
		}

		if (!livekit) {
			setPill(refs.connectionPill, "error", "danger");
			setHint("LiveKit client runtime failed to load.");
			log("The LiveKit browser SDK did not load. Check the bundled runtime or host a local copy.", "error");
			requestFallback("LiveKit client runtime failed to load.");
			return;
		}

		const missingFields = missingRelayConfigFields();
		if (missingFields.length > 0) {
			setPill(refs.connectionPill, "error", "danger");
			setHint("Relay session metadata is incomplete.");
			showEmpty("Relay launch incomplete", "Mumble did not provide the relay session details needed to connect.");
			log("Missing relay launch field(s): " + missingFields.join(", ") + ".", "error");
			requestFallback("Relay session metadata is incomplete: " + missingFields.join(", "));
			return;
		}

		refs.startShare.addEventListener("click", function() {
			void startSharing();
		});
		refs.stopShare.addEventListener("click", function() {
			void stopSharing();
		});
		refs.toggleAudio.addEventListener("click", function() {
			toggleStreamAudioMuted();
		});
		refs.reconnectView.addEventListener("click", function() {
			void reconnectViewer();
		});

		window.addEventListener("beforeunload", function() {
			void disconnectRoom();
		});

		try {
			await ensureConnected();
		} catch (error) {
			showEmpty("Relay connection failed", "The window could not connect to the configured WebRTC relay.");
			requestFallback("The in-app relay window could not connect to the relay server: " + describeError(error));
			return;
		}
	}

	boot().catch(function(error) {
		const message = error && error.message ? error.message : String(error || "unknown error");
		log("Relay bootstrap failed: " + message, "error");
		showEmpty("Relay startup failed", "The in-app relay window did not finish booting.");
		requestFallback("The in-app relay window failed during bootstrap.");
	});
})();
