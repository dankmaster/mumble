// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "UiTheme.h"

#include "Global.h"
#include "Themes.h"

#include <QtCore/QtGlobal>

namespace {
	void applyRuntimeAliases(UiThemeTokens &tokens) {
		tokens.surface       = tokens.mantle;
		tokens.overlay       = tokens.surface0;
		tokens.highlight     = tokens.surface1;
		tokens.border        = tokens.surface1;
		tokens.textPrimary   = tokens.text;
		tokens.textSecondary = tokens.subtext0;
		tokens.textMuted     = tokens.overlay0;
		tokens.success       = tokens.green;
		tokens.warning       = tokens.yellow;
		tokens.danger        = tokens.red;
		tokens.orange        = tokens.peach;
		tokens.purple        = tokens.mauve;
		if (!tokens.focusAccent.isValid()) {
			tokens.focusAccent = tokens.lavender.isValid() ? tokens.lavender : tokens.accent;
		}
	}
}

QColor uiThemeColorWithAlpha(const QColor &color, qreal alpha) {
	QColor adjusted = color;
	adjusted.setAlphaF(qBound< qreal >(0.0, alpha, 1.0));
	return adjusted;
}

QString uiThemeQssColor(const QColor &color) {
	if (color.alpha() < 255) {
		return QString::fromLatin1("rgba(%1, %2, %3, %4)")
			.arg(color.red())
			.arg(color.green())
			.arg(color.blue())
			.arg(QString::number(color.alphaF(), 'f', 3));
	}

	return color.name();
}

std::optional< UiThemeTokens > activeUiThemeTokens() {
	const std::optional< ThemeInfo::StyleInfo > style = Themes::getConfiguredStyle(Global::get().s);
	if (!style) {
		return std::nullopt;
	}

	if (style->themeName == QLatin1String("Mumble") && style->name == QLatin1String("Dark")) {
		UiThemeTokens tokens;
		tokens.preset       = UiThemePreset::MumbleDark;
		tokens.crust        = QColor(QStringLiteral("#191f26"));
		tokens.mantle       = QColor(QStringLiteral("#252c34"));
		tokens.base         = QColor(QStringLiteral("#191f26"));
		tokens.surface0     = QColor(QStringLiteral("#313a44"));
		tokens.surface1     = QColor(QStringLiteral("#39424d"));
		tokens.surface2     = QColor(QStringLiteral("#343d48"));
		tokens.text         = QColor(QStringLiteral("#e0e7ef"));
		tokens.subtext0     = QColor(QStringLiteral("#7d8996"));
		tokens.overlay0     = QColor(QStringLiteral("#7d8996"));
		tokens.accent       = QColor(QStringLiteral("#6aa6cf"));
		tokens.accentHover  = QColor(QStringLiteral("#82c1e0"));
		tokens.accentSubtle = uiThemeColorWithAlpha(tokens.accent, 0.15);
		tokens.red          = QColor(QStringLiteral("#c46a74"));
		tokens.green        = QColor(QStringLiteral("#69b28c"));
		tokens.yellow       = QColor(QStringLiteral("#c7925b"));
		tokens.peach        = QColor(QStringLiteral("#d97a73"));
		tokens.mauve        = QColor(QStringLiteral("#c46a74"));
		tokens.lavender     = tokens.accent;
		tokens.teal         = tokens.green;
		tokens.pink         = tokens.mauve;
		tokens.rosewater    = QColor(QStringLiteral("#edf3f8"));
		tokens.focusAccent  = tokens.accent;
		applyRuntimeAliases(tokens);
		return tokens;
	}

	if (style->themeName == QLatin1String("Mumble") && style->name == QLatin1String("Lite")) {
		UiThemeTokens tokens;
		tokens.preset       = UiThemePreset::MumbleLight;
		tokens.crust        = QColor(QStringLiteral("#edf1f5"));
		tokens.mantle       = QColor(QStringLiteral("#edf1f5"));
		tokens.base         = QColor(QStringLiteral("#ffffff"));
		tokens.surface0     = QColor(QStringLiteral("#ffffff"));
		tokens.surface1     = QColor(QStringLiteral("#cfd7e0"));
		tokens.surface2     = QColor(QStringLiteral("#dcecf6"));
		tokens.text         = QColor(QStringLiteral("#16212c"));
		tokens.subtext0     = QColor(QStringLiteral("#25313d"));
		tokens.overlay0     = QColor(QStringLiteral("#95a1ad"));
		tokens.accent       = QColor(QStringLiteral("#4d8fbe"));
		tokens.accentHover  = QColor(QStringLiteral("#257ea8"));
		tokens.accentSubtle = uiThemeColorWithAlpha(tokens.accent, 0.15);
		tokens.red          = QColor(QStringLiteral("#c46a74"));
		tokens.green        = QColor(QStringLiteral("#69b28c"));
		tokens.yellow       = QColor(QStringLiteral("#c7925b"));
		tokens.peach        = QColor(QStringLiteral("#d97a73"));
		tokens.mauve        = QColor(QStringLiteral("#c46a74"));
		tokens.lavender     = tokens.accent;
		tokens.teal         = QColor(QStringLiteral("#69b28c"));
		tokens.pink         = tokens.mauve;
		tokens.rosewater    = QColor(QStringLiteral("#25313d"));
		tokens.focusAccent  = tokens.accent;
		applyRuntimeAliases(tokens);
		return tokens;
	}

	if (style->themeName == QLatin1String("Catppuccin") && style->name == QLatin1String("Mocha")) {
		UiThemeTokens tokens;
		tokens.preset       = UiThemePreset::CatppuccinMocha;
		tokens.crust        = QColor(QStringLiteral("#11111b"));
		tokens.mantle       = QColor(QStringLiteral("#181825"));
		tokens.base         = QColor(QStringLiteral("#1e1e2e"));
		tokens.surface0     = QColor(QStringLiteral("#313244"));
		tokens.surface1     = QColor(QStringLiteral("#45475a"));
		tokens.surface2     = QColor(QStringLiteral("#585b70"));
		tokens.text         = QColor(QStringLiteral("#cdd6f4"));
		tokens.subtext0     = QColor(QStringLiteral("#a6adc8"));
		tokens.overlay0     = QColor(QStringLiteral("#6c7086"));
		tokens.accent       = QColor(QStringLiteral("#89b4fa"));
		tokens.accentHover  = QColor(QStringLiteral("#b4d0fb"));
		tokens.accentSubtle = uiThemeColorWithAlpha(tokens.accent, 0.15);
		tokens.red          = QColor(QStringLiteral("#f38ba8"));
		tokens.green        = QColor(QStringLiteral("#a6e3a1"));
		tokens.yellow       = QColor(QStringLiteral("#f9e2af"));
		tokens.peach        = QColor(QStringLiteral("#fab387"));
		tokens.mauve        = QColor(QStringLiteral("#cba6f7"));
		tokens.lavender     = QColor(QStringLiteral("#b4befe"));
		tokens.teal         = QColor(QStringLiteral("#94e2d5"));
		tokens.pink         = QColor(QStringLiteral("#f5c2e7"));
		tokens.rosewater    = QColor(QStringLiteral("#f5e0dc"));
		tokens.focusAccent  = tokens.lavender;
		applyRuntimeAliases(tokens);
		return tokens;
	}

	return std::nullopt;
}
