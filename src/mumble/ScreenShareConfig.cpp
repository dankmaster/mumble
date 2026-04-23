// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareConfig.h"

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>

const QString ScreenShareConfig::name = QLatin1String("ScreenShareConfig");

static ConfigWidget *ScreenShareConfigNew(Settings &st) {
	return new ScreenShareConfig(st);
}

static ConfigRegistrar registrarScreenShareConfig(1310, ScreenShareConfigNew);

ScreenShareConfig::ScreenShareConfig(Settings &st) : ConfigWidget(st) {
	QVBoxLayout *rootLayout = new QVBoxLayout(this);
	rootLayout->setContentsMargins(0, 0, 0, 0);
	rootLayout->setSpacing(12);

	QGroupBox *behaviorGroup = new QGroupBox(tr("Behavior"), this);
	QVBoxLayout *behaviorLayout = new QVBoxLayout(behaviorGroup);
	behaviorLayout->setSpacing(10);

	m_autoOpenCurrentRoomShare = new QCheckBox(tr("Auto-open the current room's share when I am already in it"), behaviorGroup);
	m_autoOpenCurrentRoomShare->setToolTip(
		tr("Automatically open a detached screen-share window when someone starts sharing in your current voice room."));
	m_autoOpenCurrentRoomShare->setWhatsThis(
		tr("When enabled, Mumble opens the detached screen-share window automatically after a remote share becomes "
		   "available in the voice room you are already in. Developer environment variables can still override this "
		   "behavior."));

	m_preferInAppRelay = new QCheckBox(tr("Prefer the in-app relay window before helper/browser fallback"), behaviorGroup);
	m_preferInAppRelay->setToolTip(
		tr("Use Mumble's detachable in-app relay window first, then fall back to the helper/browser flow if needed."));
	m_preferInAppRelay->setWhatsThis(
		tr("When enabled, Mumble tries the in-app relay window first for publishing and viewing. If that cannot start, "
		   "Mumble falls back to the helper/browser relay flow when available."));

	m_diagnosticsLogging = new QCheckBox(tr("Enable screen-share diagnostics logging"), behaviorGroup);
	m_diagnosticsLogging->setToolTip(
		tr("Write detailed screen-share helper diagnostics to a local log file for troubleshooting."));
	m_diagnosticsLogging->setWhatsThis(
		tr("When enabled, the external screen-share helper writes detailed startup, relay, encoder and crash-related "
		   "diagnostics to a local log file. This is intended for troubleshooting and may include local environment "
		   "details. Changes apply the next time the helper starts."));

	behaviorLayout->addWidget(m_autoOpenCurrentRoomShare);
	behaviorLayout->addWidget(m_preferInAppRelay);
	behaviorLayout->addWidget(m_diagnosticsLogging);

	QGroupBox *capabilitiesGroup = new QGroupBox(tr("Capabilities"), this);
	QVBoxLayout *capabilitiesLayout = new QVBoxLayout(capabilitiesGroup);

	m_capabilitiesNote = new QLabel(
		tr("Quality limits such as resolution, frame rate and available relay modes are negotiated from the server and "
		   "your client's current screen-share capabilities."), capabilitiesGroup);
	m_capabilitiesNote->setWordWrap(true);
	m_capabilitiesNote->setTextFormat(Qt::PlainText);

	capabilitiesLayout->addWidget(m_capabilitiesNote);

	rootLayout->addWidget(behaviorGroup);
	rootLayout->addWidget(capabilitiesGroup);
	rootLayout->addStretch(1);
}

QString ScreenShareConfig::title() const {
	return tr("Screen Sharing");
}

const QString &ScreenShareConfig::getName() const {
	return ScreenShareConfig::name;
}

QIcon ScreenShareConfig::icon() const {
	return QIcon(QLatin1String("skin:config_network.png"));
}

void ScreenShareConfig::save() const {
	s.bScreenShareAutoOpenCurrentRoom = m_autoOpenCurrentRoomShare->isChecked();
	s.bScreenSharePreferInAppRelay    = m_preferInAppRelay->isChecked();
	s.bScreenShareDiagnostics         = m_diagnosticsLogging->isChecked();
}

void ScreenShareConfig::load(const Settings &r) {
	loadCheckBox(m_autoOpenCurrentRoomShare, r.bScreenShareAutoOpenCurrentRoom);
	loadCheckBox(m_preferInAppRelay, r.bScreenSharePreferInAppRelay);
	loadCheckBox(m_diagnosticsLogging, r.bScreenShareDiagnostics);
}
