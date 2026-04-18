// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ListenerVolumeSlider.h"
#include "Channel.h"

#include <QSlider>

ListenerVolumeSlider::ListenerVolumeSlider(ListenerVolumeController &controller, QWidget *parent)
	: VolumeSliderWidgetAction(parent), m_controller(controller) {
	setSliderAccessibleName(tr("Listener volume adjustment"));
}

void ListenerVolumeSlider::setListenedChannel(const Channel &channel) {
	m_controller.setListenedChannel(&channel);
	updateSliderValue(VolumeAdjustment::toFactor(m_controller.currentDbAdjustment()));
}

void ListenerVolumeSlider::on_VolumeSlider_valueChanged(int value) {
	updateTooltip(value);
	displayTooltip(value);
	m_controller.applyDbAdjustment(value, false);
}

void ListenerVolumeSlider::on_VolumeSlider_changeCompleted() {
	m_controller.applyDbAdjustment(m_volumeSlider ? m_volumeSlider->value() : 0, true);
	updateLabelValue();
}
