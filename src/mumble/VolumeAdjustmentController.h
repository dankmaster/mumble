// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_VOLUMEADJUSTMENTCONTROLLER_H_
#define MUMBLE_MUMBLE_VOLUMEADJUSTMENTCONTROLLER_H_

#include "VolumeAdjustment.h"

#include <QtCore/QObject>
#include <QtCore/QTimer>

class Channel;

class UserLocalVolumeController {
public:
	static int currentDbAdjustment(unsigned int session);
	static void applyDbAdjustment(unsigned int session, int dbAdjustment, bool final);
};

class ListenerVolumeController : public QObject {
	Q_OBJECT

public:
	explicit ListenerVolumeController(QObject *parent = nullptr);

	void setListenedChannel(const Channel *channel);
	const Channel *listenedChannel() const;
	int currentDbAdjustment() const;
	void applyDbAdjustment(int dbAdjustment, bool final);

private:
	const Channel *m_channel = nullptr;
	QTimer m_sendTimer;
	QTimer m_resetTimer;
	unsigned int m_currentSendDelay = 0;
	unsigned int m_cachedChannelID  = 0;
	VolumeAdjustment m_cachedAdjustment = VolumeAdjustment::fromFactor(1.0f);

	void sendToServer();
	void rememberAdjustment(const VolumeAdjustment &adjustment);
	void advanceBackoff();
};

#endif // MUMBLE_MUMBLE_VOLUMEADJUSTMENTCONTROLLER_H_
