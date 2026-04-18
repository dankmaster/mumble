// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernShellPage.h"

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include <QtCore/QTimer>
#include <QtGui/QDesktopServices>

namespace {
	bool isLocalModernShellResource(const QUrl &url) {
		const QString scheme = url.scheme().trimmed().toLower();
		if (scheme != QLatin1String("qrc")) {
			return false;
		}

		const QString path = url.path();
		return path.startsWith(QStringLiteral("/modern-shell")) || path.startsWith(QStringLiteral("/qtwebchannel/"));
	}
} // namespace

ModernShellPage::ModernShellPage(QObject *parent) : QWebEnginePage(parent) {
}

bool ModernShellPage::acceptNavigationRequest(const QUrl &url, const NavigationType type, const bool isMainFrame) {
	Q_UNUSED(type);
	if (isLocalModernShellResource(url)) {
		return true;
	}

	if (isMainFrame) {
		emit externalNavigationRequested(url);
		const QUrl externalUrl = url;
		// Avoid re-entrant browser launch while WebEngine is still unwinding the click.
		QTimer::singleShot(0, this, [externalUrl]() { QDesktopServices::openUrl(externalUrl); });
	}

	return false;
}

void ModernShellPage::javaScriptConsoleMessage(const JavaScriptConsoleMessageLevel level, const QString &message,
											   const int lineNumber, const QString &sourceID) {
	const char *levelToken = "log";
	switch (level) {
		case JavaScriptConsoleMessageLevel::InfoMessageLevel:
			levelToken = "info";
			break;
		case JavaScriptConsoleMessageLevel::WarningMessageLevel:
			levelToken = "warn";
			break;
		case JavaScriptConsoleMessageLevel::ErrorMessageLevel:
			levelToken = "error";
			break;
		default:
			break;
	}

	qInfo().noquote()
		<< QStringLiteral("ModernShellPage[%1]: %2:%3 %4")
			   .arg(QString::fromLatin1(levelToken),
					sourceID.isEmpty() ? QStringLiteral("-") : sourceID,
					QString::number(lineNumber),
					message);
}

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)
