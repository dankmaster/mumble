(function() {
	"use strict";

	let modernBridge = null;
	let bridgeLoadPromise = null;
	let lastRenderedMessageCount = 0;
	let lastRenderedTailKey = "";
	let lastScopeToken = "";
	let noteExpanded = false;
	let openMenuId = null;
	let railCollapsed = false;
	let contextMenuState = null;
	let unreadDetachedMessages = 0;

	const refs = {
		appShell: document.querySelector(".app-shell"),
		brandTitle: document.getElementById("brand-title"),
		brandSubtitle: document.getElementById("brand-subtitle"),
		menuBar: document.getElementById("menu-bar"),
		railToggleButton: document.getElementById("rail-toggle-button"),
		serverEyebrow: document.getElementById("server-eyebrow"),
		serverTitle: document.getElementById("server-title"),
		serverSubtitle: document.getElementById("server-subtitle"),
		layoutPill: document.getElementById("layout-pill"),
		connectionPill: document.getElementById("connection-pill"),
		compatPill: document.getElementById("compat-pill"),
		connectButton: document.getElementById("connect-button"),
		disconnectButton: document.getElementById("disconnect-button"),
		settingsButton: document.getElementById("settings-button"),
		muteButton: document.getElementById("mute-button"),
		deafButton: document.getElementById("deaf-button"),
		noteToggleButton: document.getElementById("note-toggle-button"),
		textRoomCount: document.getElementById("text-room-count"),
		voiceRoomCount: document.getElementById("voice-room-count"),
		textRoomList: document.getElementById("text-room-list"),
		voiceRoomList: document.getElementById("voice-room-list"),
		scopeTitle: document.getElementById("scope-title"),
		scopeDescription: document.getElementById("scope-description"),
		scopeBanner: document.getElementById("scope-banner"),
		conversationMeta: document.getElementById("conversation-meta"),
		voicePresenceStack: document.getElementById("voice-presence-stack"),
		messageList: document.getElementById("message-list"),
		jumpLatestButton: document.getElementById("jump-latest-button"),
		composerForm: document.getElementById("composer-form"),
		composerInput: document.getElementById("composer-input"),
		composerHint: document.getElementById("composer-hint"),
		attachButton: document.getElementById("attach-button"),
		sendButton: document.getElementById("send-button"),
		loadOlderButton: document.getElementById("load-older-button"),
		markReadButton: document.getElementById("mark-read-button"),
		selfCardSettingsButton: document.getElementById("self-card-settings-button"),
		selfAvatar: document.getElementById("self-avatar"),
		selfName: document.getElementById("self-name"),
		selfStatus: document.getElementById("self-status"),
		contextMenu: document.getElementById("context-menu")
	};

	function notifyBridge(method) {
		if (!modernBridge || typeof modernBridge[method] !== "function") {
			return;
		}

		const args = Array.prototype.slice.call(arguments, 1);
		try {
			modernBridge[method].apply(modernBridge, args);
		} catch (error) {
			console.warn("Modern bridge call failed:", method, error);
		}
	}

	async function ensureBridge() {
		if (!window.qt || !window.qt.webChannelTransport) {
			return;
		}

		if (modernBridge) {
			return;
		}

		async function bindBridge() {
			return new Promise(function(resolve) {
				try {
					new QWebChannel(qt.webChannelTransport, function(channel) {
						modernBridge = channel.objects.modernBridge || null;
						if (modernBridge) {
							if (modernBridge.snapshotChanged && typeof modernBridge.snapshotChanged.connect === "function") {
								modernBridge.snapshotChanged.connect(syncSnapshot);
							}
							notifyBridge("ready");
							syncSnapshot();
						}
						resolve();
					});
				} catch (error) {
					console.warn("Modern bridge initialization failed:", error);
					resolve();
				}
			});
		}

		if (window.QWebChannel) {
			await bindBridge();
			return;
		}

		if (!bridgeLoadPromise) {
			bridgeLoadPromise = new Promise(function(resolve) {
				const script = document.createElement("script");
				script.src = "qrc:///qtwebchannel/qwebchannel.js";
				script.async = true;
				script.onload = function() {
					bindBridge().then(resolve);
				};
				script.onerror = function() {
					console.warn("Unable to load qwebchannel.js for the modern layout.");
					resolve();
				};
				document.head.appendChild(script);
			});
		}

		await bridgeLoadPromise;
	}

	function getSnapshot() {
		return modernBridge ? (modernBridge.snapshot || {}) : {};
	}

	function escapeHtml(value) {
		return String(value || "")
			.replace(/&/g, "&amp;")
			.replace(/</g, "&lt;")
			.replace(/>/g, "&gt;")
			.replace(/\"/g, "&quot;");
	}

	function initialsFor(label) {
		const parts = String(label || "").trim().split(/\s+/).filter(Boolean);
		if (!parts.length) {
			return "?";
		}
		if (parts.length === 1) {
			return parts[0].slice(0, 1).toUpperCase();
		}
		return (parts[0].slice(0, 1) + parts[1].slice(0, 1)).toUpperCase();
	}

	function hueForLabel(label, own) {
		if (own) {
			return 173;
		}

		let hash = 0;
		const source = String(label || "");
		for (let index = 0; index < source.length; index += 1) {
			hash = ((hash << 5) - hash) + source.charCodeAt(index);
			hash |= 0;
		}

		return Math.abs(hash) % 360;
	}

	function styleAvatar(element, label, own, avatarUrl) {
		const hue = hueForLabel(label, own);
		element.style.setProperty("--avatar-hue", String(hue));
		if (avatarUrl) {
			element.classList.add("has-image");
			element.style.backgroundImage = "url(\"" + String(avatarUrl).replace(/"/g, "%22") + "\")";
			element.textContent = "";
			return;
		}

		element.classList.remove("has-image");
		element.style.backgroundImage = "";
		element.textContent = initialsFor(label);
	}

	function applyStatePill(element, label, tone) {
		element.textContent = label;
		element.className = "state-pill";
		if (tone) {
			element.classList.add("is-" + tone);
		}
	}

	function kindChipText(kindLabel) {
		switch (String(kindLabel || "").toLowerCase()) {
			case "activity":
				return "LOG";
			case "voice room":
				return "VC";
			case "text room":
				return "TXT";
			case "direct message":
				return "DM";
			default:
				return "TXT";
		}
	}

	function dayLabelFromMs(createdAtMs) {
		if (!createdAtMs) {
			return "";
		}

		const target = new Date(Number(createdAtMs));
		const now = new Date();
		const today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
		const yesterday = new Date(today);
		yesterday.setDate(today.getDate() - 1);
		const targetDay = new Date(target.getFullYear(), target.getMonth(), target.getDate());

		if (targetDay.getTime() === today.getTime()) {
			return "TODAY";
		}
		if (targetDay.getTime() === yesterday.getTime()) {
			return "YESTERDAY";
		}

		return target.toLocaleDateString(undefined, { month: "short", day: "numeric" }).toUpperCase();
	}

	function isSameDay(leftMs, rightMs) {
		if (!leftMs || !rightMs) {
			return false;
		}

		const left = new Date(Number(leftMs));
		const right = new Date(Number(rightMs));
		return left.getFullYear() === right.getFullYear()
			&& left.getMonth() === right.getMonth()
			&& left.getDate() === right.getDate();
	}

	function shouldGroupWith(previous, current) {
		if (!previous || !current || previous.system || current.system) {
			return false;
		}

		if (!!previous.own !== !!current.own) {
			return false;
		}

		if (String(previous.actor || "") !== String(current.actor || "")) {
			return false;
		}

		if (!isSameDay(previous.createdAtMs, current.createdAtMs)) {
			return false;
		}

		if (!previous.createdAtMs || !current.createdAtMs) {
			return true;
		}

		const gapMs = Math.abs(Number(current.createdAtMs) - Number(previous.createdAtMs));
		return gapMs <= (8 * 60 * 1000);
	}

	function renderMeta(meta) {
		refs.conversationMeta.innerHTML = "";
		(meta || []).forEach(function(entry) {
			const pill = document.createElement("span");
			pill.className = "meta-pill";
			pill.textContent = entry;
			refs.conversationMeta.appendChild(pill);
		});
	}

	function renderVoicePresenceStack(people) {
		refs.voicePresenceStack.innerHTML = "";
		(people || []).slice(0, 5).forEach(function(person) {
			const avatar = document.createElement("div");
			avatar.className = "stack-avatar" + (person.isSelf ? " is-self" : "");
			styleAvatar(avatar, person.label, !!person.isSelf, person.avatarUrl || "");
			avatar.title = person.label || "";
			refs.voicePresenceStack.appendChild(avatar);
		});
	}

	function renderPresenceList(container, people) {
		const list = document.createElement("div");
		list.className = "presence-list";

		(people || []).forEach(function(person) {
			const row = document.createElement("div");
			row.className = "presence-row" + (person.isSelf ? " is-self" : "");

			const dot = document.createElement("span");
			dot.className = "presence-dot";

			const label = document.createElement("span");
			label.className = "presence-name";
			label.textContent = person.label || "Unknown";

			row.appendChild(dot);
			row.appendChild(label);
			list.appendChild(row);
		});

		container.appendChild(list);
	}

	function buildRoomRow(room, joinable, selectedVoicePresence) {
		const wrapper = document.createElement("div");
		wrapper.className = "rail-row-wrapper";

		const button = document.createElement("button");
		button.type = "button";
		button.className = "rail-row"
			+ (room.selected ? " is-selected" : "")
			+ (room.joined ? " is-joined" : "");
		button.dataset.scopeToken = room.token || "";
		button.dataset.canJoin = joinable && !room.joined ? "true" : "false";
		button.dataset.roomLabel = room.label || "";

		const chip = document.createElement("span");
		chip.className = "kind-chip";
		chip.textContent = kindChipText(room.kindLabel);

		const copy = document.createElement("span");
		copy.className = "rail-row-copy";

		const title = document.createElement("span");
		title.className = "rail-row-title";
		title.textContent = room.label || "Room";

		const subtitle = document.createElement("span");
		subtitle.className = "rail-row-subtitle";
		subtitle.textContent = room.description || "";

		copy.appendChild(title);
		copy.appendChild(subtitle);

		const meta = document.createElement("span");
		meta.className = "rail-row-meta";

		if (room.unreadCount > 0) {
			const unread = document.createElement("span");
			unread.className = "row-badge";
			unread.textContent = String(room.unreadCount);
			meta.appendChild(unread);
		} else if (room.memberCount > 0) {
			const count = document.createElement("span");
			count.className = "row-count";
			count.textContent = String(room.memberCount);
			meta.appendChild(count);
		}

		if (joinable) {
			const joinButton = document.createElement("button");
			joinButton.type = "button";
			joinButton.className = "mini-action";
			joinButton.textContent = room.joined ? "Live" : "Join";
			joinButton.disabled = !!room.joined;
			joinButton.addEventListener("click", function(event) {
				event.stopPropagation();
				notifyBridge("joinVoiceChannel", room.token);
			});
			meta.appendChild(joinButton);
		}

		button.appendChild(chip);
		button.appendChild(copy);
		button.appendChild(meta);
		button.addEventListener("click", function() {
			notifyBridge("selectScope", room.token);
		});

		wrapper.appendChild(button);

		if (selectedVoicePresence && selectedVoicePresence.length && room.joined) {
			renderPresenceList(wrapper, selectedVoicePresence);
		}

		return wrapper;
	}

	function renderRoomList(container, rooms, options) {
		container.innerHTML = "";

		if (!(rooms || []).length) {
			const empty = document.createElement("div");
			empty.className = "rail-empty";
			empty.textContent = options.emptyText || "Waiting for room state.";
			container.appendChild(empty);
			return;
		}

		rooms.forEach(function(room) {
			container.appendChild(buildRoomRow(room, options.joinable, options.voicePresence));
		});
	}

	function renderSystemMessage(message) {
		const article = document.createElement("article");
		article.className = "system-message";
		article.innerHTML =
			"<span class=\"system-label\"></span><span class=\"system-time\"></span><div class=\"system-body\"></div>";
		article.querySelector(".system-label").textContent = message.actor || "System";
		article.querySelector(".system-time").textContent = message.timeLabel || "";
		article.querySelector(".system-body").innerHTML = message.bodyHtml || escapeHtml(message.bodyText || "");
		return article;
	}

	function appendReplyBlock(container, message) {
		if (!message.replyActor && !message.replySnippet) {
			return;
		}

		const reply = document.createElement("div");
		reply.className = "reply-block";
		reply.innerHTML =
			"<div class=\"reply-actor\"></div><div class=\"reply-snippet\"></div>";
		reply.querySelector(".reply-actor").textContent = message.replyActor || "Reply";
		reply.querySelector(".reply-snippet").textContent = message.replySnippet || "";
		container.appendChild(reply);
	}

	function renderMessageBubble(message) {
		const bubble = document.createElement("div");
		bubble.className = "message-bubble" + (message.own ? " is-own" : "");
		bubble.dataset.bodyText = message.bodyText || "";
		appendReplyBlock(bubble, message);

		const body = document.createElement("div");
		body.className = "bubble-copy";
		body.innerHTML = message.bodyHtml || escapeHtml(message.bodyText || "");
		bubble.appendChild(body);
		return bubble;
	}

	function messageKey(message) {
		if (!message) {
			return "";
		}

		if (message.messageId) {
			return "id:" + String(message.messageId);
		}

		return [
			String(message.createdAtMs || ""),
			String(message.actor || ""),
			String(message.bodyText || ""),
			message.system ? "system" : "message"
		].join("|");
	}

	function latestTailMessageKey(messages) {
		for (let index = (messages || []).length - 1; index >= 0; index -= 1) {
			const key = messageKey(messages[index]);
			if (key) {
				return key;
			}
		}

		return "";
	}

	function countFreshTailMessages(messages, previousTailKey) {
		if (!previousTailKey) {
			return 0;
		}

		let count = 0;
		for (let index = (messages || []).length - 1; index >= 0; index -= 1) {
			const message = messages[index];
			if (messageKey(message) === previousTailKey) {
				break;
			}
			if (!message.system) {
				count += 1;
			}
		}

		return count;
	}

	function renderMessageGroups(messages) {
		const groups = [];
		let currentGroup = null;
		let previousMessage = null;

		(messages || []).forEach(function(message) {
			const dayLabel = dayLabelFromMs(message.createdAtMs);
			const needsDayDivider = !previousMessage || dayLabelFromMs(previousMessage.createdAtMs) !== dayLabel;

			if (needsDayDivider && dayLabel) {
				groups.push({ type: "day", label: dayLabel });
			}

			if (message.system) {
				groups.push({ type: "system", message: message });
				currentGroup = null;
				previousMessage = message;
				return;
			}

			if (!currentGroup || !shouldGroupWith(previousMessage, message)) {
				currentGroup = {
					type: "cluster",
					own: !!message.own,
					actor: message.actor || "Unknown",
					messages: []
				};
				groups.push(currentGroup);
			}

			currentGroup.messages.push(message);
			previousMessage = message;
		});

		return groups;
	}

	function renderTimeline(messages, emptyCopy, freshTailCount) {
		const indexedMessages = (messages || []).map(function(message, index) {
			return Object.assign({ renderIndex: index }, message);
		});
		const groups = renderMessageGroups(indexedMessages);
		refs.messageList.innerHTML = "";

		if (!groups.length) {
			const empty = document.createElement("div");
			empty.className = "empty-state";
			empty.innerHTML = "<h2>No history yet</h2><p></p>";
			empty.querySelector("p").textContent =
				emptyCopy || "Messages will appear here once the selected room has activity.";
			refs.messageList.appendChild(empty);
			return;
		}

		const freshStartIndex = Math.max(0, indexedMessages.length - Math.max(0, freshTailCount || 0));

		groups.forEach(function(group) {
			if (group.type === "day") {
				const divider = document.createElement("div");
				divider.className = "day-divider";
				divider.innerHTML = "<span></span><strong></strong><span></span>";
				divider.querySelector("strong").textContent = group.label;
				refs.messageList.appendChild(divider);
				return;
			}

			if (group.type === "system") {
				refs.messageList.appendChild(renderSystemMessage(group.message));
				return;
			}

			const firstMessage = group.messages[0];
			const cluster = document.createElement("section");
			cluster.className = "message-cluster" + (group.own ? " is-own" : "");
			cluster.style.setProperty("--avatar-hue", String(hueForLabel(group.actor, group.own)));
			cluster.classList.toggle("is-fresh", !!freshTailCount && group.messages.some(function(message) {
				return message.renderIndex >= freshStartIndex;
			}));

			if (!group.own) {
				const avatar = document.createElement("div");
				avatar.className = "message-avatar";
				styleAvatar(avatar, group.actor, false, firstMessage.avatarUrl || "");
				cluster.appendChild(avatar);
			}

			const stack = document.createElement("div");
			stack.className = "message-stack";

			const meta = document.createElement("div");
			meta.className = "message-meta";
			meta.innerHTML =
				"<span class=\"message-author\"></span><span class=\"message-time\"></span>";
			meta.querySelector(".message-author").textContent = group.own ? "" : group.actor;
			meta.querySelector(".message-time").textContent =
				group.own
					? group.messages[group.messages.length - 1].timeLabel || ""
					: firstMessage.timeLabel || "";
			stack.appendChild(meta);

			group.messages.forEach(function(message) {
				stack.appendChild(renderMessageBubble(message));
			});

			cluster.appendChild(stack);
			refs.messageList.appendChild(cluster);
		});
	}

	function messageListMetrics() {
		const maxScrollTop = Math.max(0, refs.messageList.scrollHeight - refs.messageList.clientHeight);
		const scrollTop = Math.max(0, refs.messageList.scrollTop);
		const distanceFromBottom = Math.max(0, maxScrollTop - scrollTop);
		return {
			scrollTop: scrollTop,
			maxScrollTop: maxScrollTop,
			distanceFromBottom: distanceFromBottom,
			nearBottom: distanceFromBottom <= 28
		};
	}

	function syncJumpLatestButton(metrics) {
		const state = metrics || messageListMetrics();
		const shouldShow = state.maxScrollTop > 0 && !state.nearBottom;
		refs.jumpLatestButton.classList.toggle("hidden", !shouldShow);
		refs.jumpLatestButton.textContent = unreadDetachedMessages > 0
			? "Jump to latest (" + String(unreadDetachedMessages) + " new)"
			: "Jump to latest";
	}

	function syncScrollState() {
		const metrics = messageListMetrics();
		if (metrics.nearBottom) {
			unreadDetachedMessages = 0;
		}

		refs.appShell.classList.toggle("chat-has-overflow", metrics.maxScrollTop > 0);
		refs.appShell.classList.toggle("chat-is-scrolled", metrics.scrollTop > 8);
		refs.appShell.classList.toggle("chat-is-detached", !metrics.nearBottom);
		syncJumpLatestButton(metrics);
		return metrics;
	}

	function scrollMessageListToBottom(behavior) {
		unreadDetachedMessages = 0;
		if (typeof refs.messageList.scrollTo === "function") {
			refs.messageList.scrollTo({
				top: refs.messageList.scrollHeight,
				behavior: behavior || "smooth"
			});
		} else {
			refs.messageList.scrollTop = refs.messageList.scrollHeight;
		}
		requestAnimationFrame(syncScrollState);
	}

	function renderMessages(snapshot) {
		const scope = snapshot.activeScope || {};
		const messages = snapshot.messages || [];
		const scopeToken = scope.scopeToken || [
			String(scope.kindLabel || ""),
			String(scope.label || ""),
			String(scope.description || "")
		].join("|");
		const scopeChanged = scopeToken !== lastScopeToken;
		const metricsBefore = messageListMetrics();
		const detachedBeforeRender = !scopeChanged && !metricsBefore.nearBottom;
		const distanceFromBottom = metricsBefore.distanceFromBottom;
		const latestTailKey = latestTailMessageKey(messages);
		const previousTailKey = scopeChanged ? "" : lastRenderedTailKey;
		const freshTailCount = latestTailKey && latestTailKey !== previousTailKey
			? countFreshTailMessages(messages, previousTailKey)
			: 0;

		if (scopeChanged) {
			unreadDetachedMessages = 0;
		}

		if (detachedBeforeRender && freshTailCount > 0) {
			unreadDetachedMessages += freshTailCount;
		}

		if (scope.serverLogHtml) {
			refs.messageList.innerHTML = "";
			const log = document.createElement("div");
			log.className = "message-log";
			log.innerHTML = scope.serverLogHtml;
			refs.messageList.appendChild(log);
			requestAnimationFrame(function() {
				if (detachedBeforeRender) {
					refs.messageList.scrollTop = Math.max(0,
						refs.messageList.scrollHeight - refs.messageList.clientHeight - distanceFromBottom);
				}
				syncScrollState();
			});
			lastRenderedMessageCount = 0;
			lastRenderedTailKey = "";
			lastScopeToken = scopeToken;
			return;
		}

		renderTimeline(messages, scope.emptyCopy || "", freshTailCount);

		const shouldStickToBottom = (scope.scrollToBottom !== false)
			&& (scopeChanged || (!detachedBeforeRender && messages.length >= lastRenderedMessageCount));

		requestAnimationFrame(function() {
			if (shouldStickToBottom) {
				scrollMessageListToBottom("auto");
				return;
			}

			if (detachedBeforeRender) {
				refs.messageList.scrollTop = Math.max(0,
					refs.messageList.scrollHeight - refs.messageList.clientHeight - distanceFromBottom);
			}
			syncScrollState();
		});

		lastRenderedMessageCount = messages.length;
		lastRenderedTailKey = latestTailKey;
		lastScopeToken = scopeToken;
	}

	function renderNote(app, scope) {
		const body = (scope.banner && scope.banner.trim()) || app.serverSubtitle || scope.description || "";
		refs.serverTitle.textContent = app.serverTitle || "Modern Layout";
		refs.serverSubtitle.textContent = body;
		refs.serverSubtitle.classList.toggle("is-collapsed", !noteExpanded && !scope.banner);
		refs.noteToggleButton.textContent = noteExpanded || scope.banner ? "Hide" : "Open";
	}

	function renderSelfCard(app) {
		styleAvatar(refs.selfAvatar, app.selfName || "You", true, app.selfAvatarUrl || "");
		refs.selfName.textContent = app.selfName || "You";
		refs.selfStatus.textContent = app.selfStatusLabel || "Offline";
		refs.selfStatus.className = "self-status";
		if (app.selfStatusTone) {
			refs.selfStatus.classList.add("is-" + app.selfStatusTone);
		}
	}

	function fallbackMenus(app) {
		return [
			{
				id: "server",
				label: "Server",
				items: [
					{ id: "server.connect", label: "Connect", enabled: !!app.canConnect },
					{ id: "server.disconnect", label: "Disconnect", enabled: !!app.canDisconnect, tone: "danger" },
					{ id: "server.information", label: "Server info", enabled: !!app.canDisconnect },
					{ id: "server.favorite", label: "Add favorite", enabled: !!app.canDisconnect }
				]
			},
			{
				id: "self",
				label: "Self",
				items: [
					{ id: "self.comment", label: "Comment", enabled: !!app.canDisconnect },
					{ id: "self.register", label: "Register", enabled: !!app.canDisconnect },
					{ id: "self.prioritySpeaker", label: "Priority speaker", enabled: !!app.canDisconnect },
					{ id: "self.audioStats", label: "Audio stats", enabled: true }
				]
			},
			{
				id: "configure",
				label: "Configure",
				items: [
					{ id: "configure.settings", label: "Settings", enabled: true },
					{ id: "configure.audioWizard", label: "Audio wizard", enabled: true },
					{ id: "configure.certificate", label: "Certificate wizard", enabled: true },
					{ id: "configure.minimal", label: "Minimal view", enabled: true },
					{ id: "configure.hideFrame", label: "Hide native window border", enabled: true }
				]
			},
			{
				id: "help",
				label: "Help",
				items: [
					{ id: "help.whatsThis", label: "What's this", enabled: true },
					{ id: "help.versionCheck", label: "Check for updates", enabled: true },
					{ id: "help.about", label: "About Mumble", enabled: true },
					{ id: "help.aboutQt", label: "About Qt", enabled: true }
				]
			}
		];
	}

	function syncComposerHeight() {
		refs.composerInput.style.height = "0px";
		refs.composerInput.style.height = Math.min(refs.composerInput.scrollHeight, 160) + "px";
	}

	function syncAmbientState(snapshot) {
		const app = snapshot.app || {};
		const scope = snapshot.activeScope || {};
		const toneSource = scope.label || app.serverTitle || "Mumble";
		const scopeHue = hueForLabel(toneSource, false);
		refs.appShell.style.setProperty("--scope-hue", String(scopeHue));
		refs.appShell.dataset.scopeKind = String(scope.kindLabel || "conversation").toLowerCase().replace(/\s+/g, "-");
	}

	function renderMenus(menus) {
		refs.menuBar.innerHTML = "";

		(menus || []).forEach(function(menu) {
			const group = document.createElement("div");
			group.className = "menu-group" + (openMenuId === menu.id ? " is-open" : "");

			const trigger = document.createElement("button");
			trigger.type = "button";
			trigger.className = "menu-trigger";
			trigger.textContent = menu.label || "Menu";
			trigger.setAttribute("aria-expanded", openMenuId === menu.id ? "true" : "false");
			trigger.addEventListener("click", function(event) {
				event.stopPropagation();
				openMenuId = openMenuId === menu.id ? null : menu.id;
				syncSnapshot();
			});
			group.appendChild(trigger);

			if ((menu.items || []).length) {
				const panel = document.createElement("div");
				panel.className = "menu-panel";

				menu.items.forEach(function(item) {
					const itemButton = document.createElement("button");
					itemButton.type = "button";
					itemButton.className = "menu-item"
						+ (item.checked ? " is-checked" : "")
						+ (item.tone ? " is-" + item.tone : "");
					itemButton.disabled = item.enabled === false;
					itemButton.innerHTML = "<span class=\"menu-item-label\"></span><span class=\"menu-item-state\"></span>";
					itemButton.querySelector(".menu-item-label").textContent = item.label || "Action";
					itemButton.querySelector(".menu-item-state").textContent = item.checked ? "On" : "";
					itemButton.addEventListener("click", function(event) {
						event.stopPropagation();
						openMenuId = null;
						notifyBridge("invokeAppAction", item.id);
					});
					panel.appendChild(itemButton);
				});

				group.appendChild(panel);
			}

			refs.menuBar.appendChild(group);
		});
	}

	function render(snapshot) {
		const app = snapshot.app || {};
		const scope = snapshot.activeScope || {};
		const textRooms = snapshot.textRooms || [];
		const voiceRooms = snapshot.voiceRooms || [];
		const voicePresence = snapshot.voicePresence || [];
		const headerPresence = voicePresence.length ? voicePresence : (snapshot.participants || []);
		const classicServer = String(app.compatibilityLabel || "").toLowerCase() === "standard server";

		refs.brandTitle.textContent = app.serverTitle || "Mumble";
		refs.brandSubtitle.textContent = app.serverSubtitle || "Room-first shell";
		refs.serverEyebrow.textContent = app.serverEyebrow || scope.kindLabel || "Mumble";
		refs.scopeTitle.textContent = scope.label || "Modern Layout";
		refs.scopeDescription.textContent = scope.description || "Select a room to see shared history.";

		applyStatePill(refs.layoutPill, app.layoutLabel || "Modern", app.layoutTone || "");
		applyStatePill(refs.connectionPill, app.connectionLabel || "Disconnected", app.connectionTone || "");
		applyStatePill(refs.compatPill, app.compatibilityLabel || "Standard server", app.compatibilityTone || "");

		refs.connectButton.disabled = !app.canConnect;
		refs.disconnectButton.disabled = !app.canDisconnect;
		refs.muteButton.classList.toggle("is-active", !!app.selfMuted);
		refs.deafButton.classList.toggle("is-active", !!app.selfDeafened);
		refs.railToggleButton.classList.toggle("is-active", !railCollapsed);
		renderMenus((app.menus && app.menus.length) ? app.menus : fallbackMenus(app));

		refs.textRoomCount.textContent = String(textRooms.length);
		refs.voiceRoomCount.textContent = String(voiceRooms.length);
		renderRoomList(refs.voiceRoomList, voiceRooms, { joinable: true, voicePresence: voicePresence });
		renderRoomList(refs.textRoomList, textRooms, {
			joinable: false,
			voicePresence: null,
			emptyText: classicServer ? "Classic servers do not expose room-backed text state." : "Waiting for room state."
		});

		renderVoicePresenceStack(headerPresence);
		renderMeta(scope.meta || []);
		renderNote(app, scope);
		renderMessages(snapshot);
		renderSelfCard(app);
		syncAmbientState(snapshot);

		refs.scopeBanner.textContent = scope.banner || "";
		refs.scopeBanner.classList.toggle("hidden", !scope.banner);
		refs.loadOlderButton.disabled = !scope.canLoadOlder;
		refs.markReadButton.disabled = !scope.canMarkRead;
		refs.composerInput.disabled = !scope.canSend;
		refs.attachButton.disabled = !scope.canAttachImages;
		refs.sendButton.disabled = !scope.canSend;
		refs.composerInput.placeholder = scope.composerPlaceholder || "Write a message";
		refs.composerHint.textContent = scope.composerHint || "Persistent room history stays with the selected room.";
		refs.appShell.classList.toggle("rail-is-collapsed", railCollapsed);
		syncComposerHeight();

		if (scope.autoMarkRead) {
			notifyBridge("markRead");
		}
	}

	function syncSnapshot() {
		render(getSnapshot());
	}

	function hideContextMenu() {
		contextMenuState = null;
		refs.contextMenu.classList.add("hidden");
		refs.contextMenu.setAttribute("aria-hidden", "true");
		refs.contextMenu.innerHTML = "";
	}

	function copyPlainText(text) {
		const value = String(text || "");
		if (!value) {
			return Promise.resolve(false);
		}

		if (navigator.clipboard && typeof navigator.clipboard.writeText === "function") {
			return navigator.clipboard.writeText(value).then(function() {
				return true;
			}).catch(function() {
				return false;
			});
		}

		const scratch = document.createElement("textarea");
		scratch.value = value;
		scratch.setAttribute("readonly", "readonly");
		scratch.style.position = "fixed";
		scratch.style.left = "-9999px";
		document.body.appendChild(scratch);
		scratch.select();
		const copied = document.execCommand("copy");
		document.body.removeChild(scratch);
		return Promise.resolve(copied);
	}

	function replaceComposerSelection(replacement) {
		const input = refs.composerInput;
		const start = input.selectionStart || 0;
		const end = input.selectionEnd || 0;
		const before = input.value.slice(0, start);
		const after = input.value.slice(end);
		input.value = before + replacement + after;
		const caret = start + replacement.length;
		input.selectionStart = caret;
		input.selectionEnd = caret;
		syncComposerHeight();
		input.dispatchEvent(new Event("input", { bubbles: true }));
	}

	function buildContextMenuItems(event) {
		const snapshot = getSnapshot();
		const scope = snapshot.activeScope || {};
		const roomRow = event.target.closest(".rail-row");
		const bubble = event.target.closest(".message-bubble");
		const composer = event.target.closest("#composer-input");
		const selfCard = event.target.closest("#self-card");

		if (composer) {
			const input = refs.composerInput;
			const hasSelection = (input.selectionEnd || 0) > (input.selectionStart || 0);
			return [
				{
					label: "Cut",
					enabled: hasSelection,
					action: function() {
						copyPlainText(input.value.slice(input.selectionStart || 0, input.selectionEnd || 0)).then(function(copied) {
							if (copied) {
								replaceComposerSelection("");
							}
						});
					}
				},
				{
					label: "Copy",
					enabled: hasSelection,
					action: function() {
						copyPlainText(input.value.slice(input.selectionStart || 0, input.selectionEnd || 0));
					}
				},
				{
					label: "Paste",
					enabled: !input.disabled,
					action: function() {
						if (navigator.clipboard && typeof navigator.clipboard.readText === "function") {
							navigator.clipboard.readText().then(function(text) {
								if (text) {
									replaceComposerSelection(text);
								}
							}).catch(function() {});
						} else {
							document.execCommand("paste");
						}
					}
				},
				{
					label: "Attach image",
					enabled: !!scope.canAttachImages,
					action: function() {
						notifyBridge("openImagePicker");
					}
				},
				{
					label: "Clear",
					enabled: !!input.value,
					action: function() {
						input.value = "";
						syncComposerHeight();
						input.dispatchEvent(new Event("input", { bubbles: true }));
					}
				}
			];
		}

		if (roomRow) {
			return [
				{
					label: "Open room",
					enabled: !!roomRow.dataset.scopeToken,
					action: function() {
						notifyBridge("selectScope", roomRow.dataset.scopeToken);
					}
				},
				{
					label: "Join voice",
					enabled: roomRow.dataset.canJoin === "true",
					action: function() {
						notifyBridge("joinVoiceChannel", roomRow.dataset.scopeToken);
					}
				},
				{
					label: "Mark read",
					enabled: !!scope.canMarkRead,
					action: function() {
						notifyBridge("markRead");
					}
				}
			];
		}

		if (bubble) {
			return [
				{
					label: "Copy message",
					enabled: !!bubble.dataset.bodyText,
					action: function() {
						copyPlainText(bubble.dataset.bodyText);
					}
				},
				{
					label: "Load older",
					enabled: !!scope.canLoadOlder,
					action: function() {
						notifyBridge("loadOlderHistory");
					}
				},
				{
					label: "Mark read",
					enabled: !!scope.canMarkRead,
					action: function() {
						notifyBridge("markRead");
					}
				}
			];
		}

		if (selfCard) {
			return [
				{
					label: "Toggle mute",
					enabled: true,
					action: function() {
						notifyBridge("toggleSelfMute");
					}
				},
				{
					label: "Toggle deaf",
					enabled: true,
					action: function() {
						notifyBridge("toggleSelfDeaf");
					}
				},
				{
					label: "Settings",
					enabled: true,
					action: function() {
						notifyBridge("openSettings");
					}
				}
			];
		}

		return [
			{
				label: "Load older",
				enabled: !!scope.canLoadOlder,
				action: function() {
					notifyBridge("loadOlderHistory");
				}
			},
			{
				label: "Mark read",
				enabled: !!scope.canMarkRead,
				action: function() {
					notifyBridge("markRead");
				}
			},
			{
				label: "Attach image",
				enabled: !!scope.canAttachImages,
				action: function() {
					notifyBridge("openImagePicker");
				}
			}
		];
	}

	function showContextMenu(items, clientX, clientY) {
		if (!items.length) {
			hideContextMenu();
			return;
		}

		refs.contextMenu.innerHTML = "";
		items.forEach(function(item) {
			const button = document.createElement("button");
			button.type = "button";
			button.className = "context-menu-item";
			button.textContent = item.label;
			button.disabled = item.enabled === false;
			button.addEventListener("click", function() {
				hideContextMenu();
				if (item.enabled === false) {
					return;
				}
				item.action();
			});
			refs.contextMenu.appendChild(button);
		});

		refs.contextMenu.classList.remove("hidden");
		refs.contextMenu.setAttribute("aria-hidden", "false");
		const bounds = refs.contextMenu.getBoundingClientRect();
		const left = Math.max(8, Math.min(clientX, window.innerWidth - bounds.width - 8));
		const top = Math.max(8, Math.min(clientY, window.innerHeight - bounds.height - 8));
		refs.contextMenu.style.left = left + "px";
		refs.contextMenu.style.top = top + "px";
		contextMenuState = { left: left, top: top };
	}

	function wireActions() {
		refs.connectButton.addEventListener("click", function() { notifyBridge("openConnectDialog"); });
		refs.disconnectButton.addEventListener("click", function() { notifyBridge("disconnectServer"); });
		refs.settingsButton.addEventListener("click", function() { notifyBridge("openSettings"); });
		refs.muteButton.addEventListener("click", function() { notifyBridge("toggleSelfMute"); });
		refs.deafButton.addEventListener("click", function() { notifyBridge("toggleSelfDeaf"); });
		refs.loadOlderButton.addEventListener("click", function() { notifyBridge("loadOlderHistory"); });
		refs.markReadButton.addEventListener("click", function() { notifyBridge("markRead"); });
		refs.attachButton.addEventListener("click", function() { notifyBridge("openImagePicker"); });
		refs.selfCardSettingsButton.addEventListener("click", function() { notifyBridge("openSettings"); });
		refs.noteToggleButton.addEventListener("click", function() {
			noteExpanded = !noteExpanded;
			syncSnapshot();
		});
		refs.jumpLatestButton.addEventListener("click", function() {
			scrollMessageListToBottom("smooth");
		});
		refs.railToggleButton.addEventListener("click", function() {
			railCollapsed = !railCollapsed;
			syncSnapshot();
		});
		refs.composerInput.addEventListener("input", syncComposerHeight);
		refs.composerForm.addEventListener("submit", function(event) {
			event.preventDefault();
			const value = refs.composerInput.value.trim();
			if (!value) {
				return;
			}
			notifyBridge("sendMessage", value);
			refs.composerInput.value = "";
			syncComposerHeight();
		});
		document.addEventListener("click", function(event) {
			if (!event.target.closest(".context-menu")) {
				hideContextMenu();
			}
			if (!event.target.closest(".menu-group") && openMenuId !== null) {
				openMenuId = null;
				syncSnapshot();
			}
		});
		document.addEventListener("contextmenu", function(event) {
			if (event.target.closest(".context-menu")) {
				return;
			}
			const items = buildContextMenuItems(event).filter(function(item) {
				return !!item;
			});
			event.preventDefault();
			showContextMenu(items, event.clientX, event.clientY);
		});
		window.addEventListener("keydown", function(event) {
			if (event.key === "Escape" && openMenuId !== null) {
				openMenuId = null;
				syncSnapshot();
				return;
			}
			if (event.key === "Escape") {
				hideContextMenu();
			}
		});
		window.addEventListener("resize", hideContextMenu);
		window.addEventListener("blur", hideContextMenu);
		refs.messageList.addEventListener("scroll", function() {
			if (contextMenuState) {
				hideContextMenu();
			}
			syncScrollState();
		});
	}

	async function boot() {
		wireActions();
		syncComposerHeight();
		await ensureBridge();
		syncSnapshot();
	}

	boot();
})();
