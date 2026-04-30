// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_RELAYWINDOWHOST_H_
#define MUMBLE_MUMBLE_RELAYWINDOWHOST_H_

#include "ScreenShareManager.h"

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#	include "ScreenShare.h"
#	include <QtCore/QPointer>
#	include <QtCore/QString>
#	include <QtCore/QUrl>
#	include <QtWebEngineCore/QWebEnginePage>
#	include <QtWidgets/QWidget>

class QCloseEvent;
class QTimer;
class QVBoxLayout;
class QWebChannel;
class QWebEngineDesktopMediaRequest;
class QWebEngineView;
class RelayWebPage;
class RelayWindowBridge;

class RelayWindowHost : public QWidget {
private:
	Q_OBJECT
	Q_DISABLE_COPY(RelayWindowHost)

public:
	enum class Mode {
		Publish,
		View,
	};

	explicit RelayWindowHost(const ScreenShareSession &session, Mode mode, QWidget *parent = nullptr);

	bool start(QString *errorMessage = nullptr);
	const ScreenShareSession &session() const;
	Mode mode() const;
	QString streamID() const;
	void closeFromManager();
	void updateSession(const ScreenShareSession &session);

signals:
	void bootFailed(const QString &reason);
	void fallbackRequested(const QString &reason);
	void closeRequested(const QString &reason);

protected:
	void closeEvent(QCloseEvent *event) override;

private slots:
	void handleLoadFinished(bool ok);
	void handleRenderProcessTerminated(QWebEnginePage::RenderProcessTerminationStatus status, int exitCode);
	void handleDesktopMediaRequested(const QWebEngineDesktopMediaRequest &request);
	void handleBridgeBootReady();
	void handleBridgeFallbackRequested(const QString &reason);
	void handleBridgeCloseRequested(const QString &reason);
	void handleBridgeStatsReported(const QString &summary, const QString &actualCodec);
	void handleBootTimeout();

private:
	QUrl buildPageUrl() const;
	QString windowTitle() const;
	QString roleLabel() const;
	void requestCloseFromManager();
	void requestFallbackOnce(const QString &reason);

	ScreenShareSession m_session;
	Mode m_mode;
	QVBoxLayout *m_layout       = nullptr;
	QWebEngineView *m_view      = nullptr;
	RelayWebPage *m_page        = nullptr;
	QWebChannel *m_channel      = nullptr;
	RelayWindowBridge *m_bridge = nullptr;
	QTimer *m_bootTimeoutTimer  = nullptr;
	bool m_closingFromManager   = false;
	bool m_started              = false;
	bool m_bootReady            = false;
	bool m_fallbackIssued       = false;
};

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)

#endif // MUMBLE_MUMBLE_RELAYWINDOWHOST_H_
