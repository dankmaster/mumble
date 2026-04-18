// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_RELAYWEBPAGE_H_
#define MUMBLE_MUMBLE_RELAYWEBPAGE_H_

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include <QtCore/QUrl>
#include <QtWebEngineCore/QWebEnginePage>

class RelayWebPage : public QWebEnginePage {
private:
	Q_OBJECT
	Q_DISABLE_COPY(RelayWebPage)

public:
	explicit RelayWebPage(QObject *parent = nullptr);

signals:
	void externalNavigationRequested(const QUrl &url);

protected:
	bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override;
	void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message, int lineNumber,
								  const QString &sourceID) override;
};

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)

#endif // MUMBLE_MUMBLE_RELAYWEBPAGE_H_
