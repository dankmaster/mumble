(function() {
	"use strict";

	const livekit = window.LivekitClient;
	let relayBridge = null;
	let bridgeLoadPromise = null;

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
		log: document.getElementById("log")
	};

	const query = new URLSearchParams(window.location.search);
	const config = parseConfig(query);
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
		currentAudioElement: null
	};

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

	function notifyBridge(method, value) {
		if (!relayBridge || typeof relayBridge[method] !== "function") {
			return;
		}

		try {
			relayBridge[method](value);
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

	function isUserCanceledScreenShareError(error) {
		if (!error) {
			return false;
		}

		const name = String(error.name || "").trim();
		if (name === "AbortError") {
			return true;
		}

		const message = String(error.message || error.toString() || "").toLowerCase();
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
	}

	function normalizeToken(value, fallback) {
		const token = String(value || "").trim().toLowerCase();
		return token || fallback;
	}

	function normalizeCodec(value) {
		switch (normalizeToken(value, "")) {
			case "h264":
			case "vp9":
			case "av1":
				return normalizeToken(value, "");
			default:
				return "h264";
		}
	}

	function parsePositiveInteger(value, fallback) {
		const parsed = Number.parseInt(String(value || "").trim(), 10);
		return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
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
			width: parsePositiveInteger(params.get("width"), 1920),
			height: parsePositiveInteger(params.get("height"), 1080),
			fps: parsePositiveInteger(params.get("fps"), 60),
			bitrateKbps: parsePositiveInteger(params.get("bitrate_kbps"), 12000),
			captureAudio: parseBooleanFlag(params.get("capture_audio"), false),
			systemAudio: parseIncludeExclude(params.get("system_audio"), "exclude"),
			surfaceSwitching: parseIncludeExclude(params.get("surface_switching"), "include"),
			selfBrowserSurface: parseIncludeExclude(params.get("self_browser_surface"), "exclude")
		};
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
		return screenShareSourceMatches(livekitSource)
			? publicationSource === livekitSource || trackSource === livekitSource || screenShareSourceMatches(publicationSource)
				|| screenShareSourceMatches(trackSource)
			: screenShareSourceMatches(publicationSource) || screenShareSourceMatches(trackSource);
	}

	function isScreenShareAudioTrack(track, publication) {
		if (!isAudioTrack(track, publication)) {
			return false;
		}

		const livekitSource = livekit && livekit.Track && livekit.Track.Source ? livekit.Track.Source.ScreenShareAudio : "";
		const publicationSource = publication && publication.source ? publication.source : "";
		const trackSource = track && track.source ? track.source : "";
		return screenShareAudioSourceMatches(livekitSource)
			? publicationSource === livekitSource || trackSource === livekitSource || screenShareAudioSourceMatches(publicationSource)
				|| screenShareAudioSourceMatches(trackSource)
			: screenShareAudioSourceMatches(publicationSource) || screenShareAudioSourceMatches(trackSource);
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

	function attachTrack(track, publication, participant, localTrack) {
		if (!isScreenShareTrack(track, publication)) {
			return;
		}

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
			video: true,
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

	function buildRoomOptions() {
		const publishDefaults = {
			simulcast: true,
			videoCodec: config.codec,
			screenShareEncoding: {
				maxBitrate: config.bitrateKbps * 1000,
				maxFramerate: config.fps
			}
		};

		return {
			adaptiveStream: true,
			dynacast: true,
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
				log("Screen-capture device error: " + error.message, "error");
			});
		}
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

		if (!config.wsRelayUrl || !config.relayToken || !config.relayRoomId) {
			throw new Error("Missing relay_url, relay_token, or relay_room_id.");
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
			log("Unable to connect to relay: " + error.message, "error");
			throw error;
		});

		return state.connectPromise;
	}

	async function startSharing() {
		refs.startShare.disabled = true;
		refs.stopShare.disabled = true;
		try {
			const room = await ensureConnected();
			setHint(config.captureAudio
				? "Choose a screen, window, or tab with audio in the browser picker."
				: "Choose a screen or window in the browser picker.");
			log(config.captureAudio
				? "Requesting screen capture and shared audio from the browser."
				: "Requesting screen capture from the browser.");
			await room.localParticipant.setScreenShareEnabled(true, buildScreenShareCaptureOptions());
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

			log("Unable to start screen sharing: " + error.message, "error");
			showEmpty("Share did not start", "The browser did not grant or keep screen-capture permission.");
			requestFallback("The in-app relay window could not start screen sharing.");
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
		setText(refs.metaCodec, config.codec.toUpperCase() + " / " + config.width + "x" + config.height + " @ " + config.fps + "fps");
	}

	function renderShell() {
		renderStaticMetadata();
		setPill(refs.rolePill, state.isPublisher ? "publisher" : "viewer", state.isPublisher ? "accent" : "muted");
		setPill(refs.connectionPill, "idle", "muted");
		updateAudioToggle();

		if (state.isPublisher) {
			setText(refs.title, "Ready to share your screen");
			setText(refs.subtitle, "Mumble opened this relay window automatically. Start sharing when you are ready.");
			refs.publisherActions.classList.remove("hidden");
			refs.viewerActions.classList.add("hidden");
			refs.startShare.disabled = true;
			refs.stopShare.disabled = true;
			setHint("Connecting to the relay before screen capture starts.");
			showEmpty("No screen selected yet", "Click Start sharing to choose a screen or window.");
		} else {
			setText(refs.title, "Joining screen share");
			setText(refs.subtitle, "Mumble opened this relay window automatically.");
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

		if (!config.wsRelayUrl || !config.relayToken || !config.relayRoomId) {
			setPill(refs.connectionPill, "error", "danger");
			setHint("Relay session metadata is incomplete.");
			log("Missing relay_url, relay_room_id, or relay_token in the helper launch query.", "error");
			requestFallback("Relay session metadata is incomplete.");
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
			requestFallback("The in-app relay window could not connect to the relay server.");
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
