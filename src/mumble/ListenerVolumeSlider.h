// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_LISTENERLOCALVOLUMESLIDER_H_
#define MUMBLE_MUMBLE_LISTENERLOCALVOLUMESLIDER_H_

#include "VolumeAdjustmentController.h"
#include "VolumeSliderWidgetAction.h"

class Channel;

class ListenerVolumeSlider : public VolumeSliderWidgetAction {
	Q_OBJECT

public:
	ListenerVolumeSlider(ListenerVolumeController &controller, QWidget *parent = nullptr);

	/// Must be called before adding this object as an action
	void setListenedChannel(const Channel &channel);

private:
	ListenerVolumeController &m_controller;

private slots:
	void on_VolumeSlider_valueChanged(int value) override;
	void on_VolumeSlider_changeCompleted() override;
};

#endif
