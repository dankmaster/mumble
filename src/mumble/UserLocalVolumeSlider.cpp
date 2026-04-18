// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "UserLocalVolumeSlider.h"

#include <QSlider>

UserLocalVolumeSlider::UserLocalVolumeSlider(QWidget *parent) : VolumeSliderWidgetAction(parent) {
	setSliderAccessibleName(tr("Local volume adjustment"));
}

void UserLocalVolumeSlider::setUser(unsigned int sessionId) {
	m_clientSession = sessionId;
	updateSliderValue(VolumeAdjustment::toFactor(UserLocalVolumeController::currentDbAdjustment(sessionId)));
}

void UserLocalVolumeSlider::on_VolumeSlider_valueChanged(int value) {
	updateTooltip(value);
	displayTooltip(value);
	UserLocalVolumeController::applyDbAdjustment(m_clientSession, value, false);
}

void UserLocalVolumeSlider::on_VolumeSlider_changeCompleted() {
	UserLocalVolumeController::applyDbAdjustment(m_clientSession, m_volumeSlider ? m_volumeSlider->value() : 0, true);
	updateLabelValue();
}
