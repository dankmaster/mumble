// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SCREENSHARECONFIG_H_
#define MUMBLE_MUMBLE_SCREENSHARECONFIG_H_

#include "ConfigDialog.h"

class QCheckBox;
class QLabel;

class ScreenShareConfig : public ConfigWidget {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ScreenShareConfig)

	QCheckBox *m_autoOpenCurrentRoomShare = nullptr;
	QCheckBox *m_preferInAppRelay         = nullptr;
	QCheckBox *m_diagnosticsLogging       = nullptr;
	QLabel *m_capabilitiesNote            = nullptr;

public:
	/// The unique name of this ConfigWidget
	static const QString name;

	ScreenShareConfig(Settings &st);
	virtual QString title() const Q_DECL_OVERRIDE;
	virtual const QString &getName() const Q_DECL_OVERRIDE;
	virtual QIcon icon() const Q_DECL_OVERRIDE;

public slots:
	void save() const Q_DECL_OVERRIDE;
	void load(const Settings &r) Q_DECL_OVERRIDE;
};

#endif
