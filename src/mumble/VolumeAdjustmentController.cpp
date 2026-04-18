// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "VolumeAdjustmentController.h"

#include "Channel.h"
#include "ChannelListenerManager.h"
#include "ClientUser.h"
#include "Database.h"
#include "Global.h"
#include "MainWindow.h"
#include "ServerHandler.h"

#include <algorithm>

int UserLocalVolumeController::currentDbAdjustment(const unsigned int session) {
	const ClientUser *user = ClientUser::get(session);
	return user ? VolumeAdjustment::toIntegerDBAdjustment(user->getLocalVolumeAdjustments()) : 0;
}

void UserLocalVolumeController::applyDbAdjustment(const unsigned int session, const int dbAdjustment, const bool final) {
	ClientUser *user = ClientUser::get(session);
	if (!user) {
		return;
	}

	user->setLocalVolumeAdjustment(VolumeAdjustment::toFactor(dbAdjustment));
	if (!final) {
		return;
	}

	if (!user->qsHash.isEmpty()) {
		Global::get().db->setUserLocalVolume(user->qsHash, user->getLocalVolumeAdjustments());
	} else if (Global::get().mw) {
		Global::get().mw->logChangeNotPermanent(QObject::tr("Local Volume Adjustment..."), user);
	}
}

ListenerVolumeController::ListenerVolumeController(QObject *parent) : QObject(parent) {
	connect(&m_sendTimer, &QTimer::timeout, this, &ListenerVolumeController::sendToServer);
	connect(&m_resetTimer, &QTimer::timeout, this, [this]() { m_currentSendDelay = 0; });

	m_sendTimer.setSingleShot(true);
	m_resetTimer.setSingleShot(true);
}

void ListenerVolumeController::setListenedChannel(const Channel *channel) {
	m_channel = channel;
}

const Channel *ListenerVolumeController::listenedChannel() const {
	return m_channel;
}

int ListenerVolumeController::currentDbAdjustment() const {
	if (!m_channel) {
		return 0;
	}

	return VolumeAdjustment::toIntegerDBAdjustment(
		Global::get().channelListenerManager->getListenerVolumeAdjustment(Global::get().uiSession, m_channel->iId).factor);
}

void ListenerVolumeController::rememberAdjustment(const VolumeAdjustment &adjustment) {
	if (!m_channel) {
		return;
	}

	m_cachedChannelID  = m_channel->iId;
	m_cachedAdjustment = adjustment;
}

void ListenerVolumeController::advanceBackoff() {
	m_currentSendDelay = std::min(1000u, (m_currentSendDelay + 25) * 2);
}

void ListenerVolumeController::applyDbAdjustment(const int dbAdjustment, const bool final) {
	ServerHandlerPtr handler = Global::get().sh;
	if (!m_channel) {
		return;
	}

	const VolumeAdjustment adjustment = VolumeAdjustment::fromDBAdjustment(dbAdjustment);
	Global::get().channelListenerManager->setListenerVolumeAdjustment(Global::get().uiSession, m_channel->iId,
																	  adjustment);
	if (!handler || handler->m_version < Mumble::Protocol::PROTOBUF_INTRODUCTION_VERSION) {
		return;
	}

	const bool unchanged = m_cachedChannelID == m_channel->iId && m_cachedAdjustment == adjustment;
	if (unchanged) {
		if (final && m_sendTimer.isActive()) {
			m_sendTimer.stop();
			sendToServer();
		}
		return;
	}

	rememberAdjustment(adjustment);
	m_resetTimer.stop();

	if (final) {
		advanceBackoff();
		m_sendTimer.stop();
		sendToServer();
		return;
	}

	m_sendTimer.start(static_cast< int >(m_currentSendDelay));
	advanceBackoff();
}

void ListenerVolumeController::sendToServer() {
	ServerHandlerPtr handler = Global::get().sh;
	m_resetTimer.start(3000);

	if (!handler || m_cachedChannelID == 0) {
		return;
	}

	MumbleProto::UserState state;
	state.set_session(Global::get().uiSession);

	MumbleProto::UserState::VolumeAdjustment *adjustment = state.add_listening_volume_adjustment();
	adjustment->set_listening_channel(m_cachedChannelID);
	adjustment->set_volume_adjustment(m_cachedAdjustment.factor);

	handler->sendMessage(state);
}
