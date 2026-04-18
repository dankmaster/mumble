// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "RelayWebPage.h"

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include "Log.h"

#include <QtCore/QDebug>
#include <QtCore/QTimer>
#include <QtGui/QDesktopServices>

namespace {
	QT_WARNING_PUSH
	QT_WARNING_DISABLE_DEPRECATED
	bool isLocalRelayResource(const QUrl &url) {
		const QString scheme = url.scheme().trimmed().toLower();
		if (scheme != QLatin1String("qrc")) {
			return false;
		}

		const QString path = url.path();
		return path.startsWith(QStringLiteral("/relay-webapp")) || path.startsWith(QStringLiteral("/qtwebchannel/"));
	}

	bool isAllowedRelayFeature(const QWebEnginePage::Feature feature) {
		switch (feature) {
			case QWebEnginePage::MediaAudioCapture:
			case QWebEnginePage::MediaVideoCapture:
			case QWebEnginePage::MediaAudioVideoCapture:
			case QWebEnginePage::DesktopVideoCapture:
			case QWebEnginePage::DesktopAudioVideoCapture:
				return true;
			default:
				return false;
		}
	}
	QT_WARNING_POP
} // namespace

RelayWebPage::RelayWebPage(QObject *parent) : QWebEnginePage(parent) {
	QT_WARNING_PUSH
	QT_WARNING_DISABLE_DEPRECATED
	connect(this, &QWebEnginePage::featurePermissionRequested, this,
			[this](const QUrl &securityOrigin, const QWebEnginePage::Feature feature) {
				if (isLocalRelayResource(securityOrigin) && isAllowedRelayFeature(feature)) {
					setFeaturePermission(securityOrigin, feature, QWebEnginePage::PermissionGrantedByUser);
					return;
				}

				setFeaturePermission(securityOrigin, feature, QWebEnginePage::PermissionDeniedByUser);
				qWarning().noquote() << QStringLiteral("RelayWebPage denied feature permission from %1")
											.arg(securityOrigin.toString(QUrl::FullyEncoded));
			});
	QT_WARNING_POP
}

bool RelayWebPage::acceptNavigationRequest(const QUrl &url, const NavigationType type, const bool isMainFrame) {
	Q_UNUSED(type);
	if (isLocalRelayResource(url)) {
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

void RelayWebPage::javaScriptConsoleMessage(const JavaScriptConsoleMessageLevel level, const QString &message,
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
		<< QStringLiteral("RelayWebPage[%1]: %2:%3 %4")
			   .arg(QString::fromLatin1(levelToken),
					sourceID.isEmpty() ? QStringLiteral("-") : sourceID,
					QString::number(lineNumber),
					message);
}

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)
