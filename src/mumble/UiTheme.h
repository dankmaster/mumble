// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_UITHEME_H_
#define MUMBLE_MUMBLE_UITHEME_H_

#include <QtCore/QString>
#include <QtGui/QColor>

#include <optional>

enum class UiThemePreset {
	MumbleDark,
	MumbleLight,
	CatppuccinMocha
};

struct UiThemeTokens {
	UiThemePreset preset = UiThemePreset::MumbleDark;
	QColor crust;
	QColor mantle;
	QColor base;
	QColor surface0;
	QColor surface1;
	QColor surface2;
	QColor text;
	QColor subtext0;
	QColor overlay0;
	QColor red;
	QColor green;
	QColor yellow;
	QColor peach;
	QColor mauve;
	QColor lavender;
	QColor teal;
	QColor pink;
	QColor rosewater;

	// Aliases used by existing runtime styling code.
	QColor surface;
	QColor overlay;
	QColor highlight;
	QColor border;
	QColor textPrimary;
	QColor textSecondary;
	QColor textMuted;
	QColor accent;
	QColor accentHover;
	QColor accentSubtle;
	QColor success;
	QColor warning;
	QColor danger;
	QColor orange;
	QColor purple;
	QColor focusAccent;
};

std::optional< UiThemeTokens > activeUiThemeTokens();
QColor uiThemeColorWithAlpha(const QColor &color, qreal alpha);
QString uiThemeQssColor(const QColor &color);

#endif // MUMBLE_MUMBLE_UITHEME_H_
