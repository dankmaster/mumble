// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "RelayWindowHost.h"

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include "Global.h"
#include "Log.h"
#include "RelayWebPage.h"
#include "RelayWindowBridge.h"

#include <QtCore/QAbstractItemModel>
#include <QtCore/QTimer>
#include <QtCore/QUrlQuery>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QVBoxLayout>
#include <QtWebChannel/QWebChannel>
#include <QtWebEngineCore/QWebEngineDesktopMediaRequest>
#include <QtWebEngineCore/QWebEnginePage>
#include <QtWebEngineCore/QWebEngineSettings>
#include <QtWebEngineWidgets/QWebEngineView>

namespace {
	QUrl relayWebAppUrl() {
		return QUrl(QStringLiteral("qrc:/relay-webapp/index.html"));
	}

	bool relaySessionRequiresReload(const ScreenShareSession &lhs, const ScreenShareSession &rhs) {
		return lhs.relayUrl != rhs.relayUrl || lhs.relayRoomID != rhs.relayRoomID || lhs.relayToken != rhs.relayToken
			|| lhs.relaySessionID != rhs.relaySessionID || lhs.streamID != rhs.streamID
			|| lhs.relayRole != rhs.relayRole || lhs.codec != rhs.codec || lhs.relayTransport != rhs.relayTransport
			|| lhs.width != rhs.width || lhs.height != rhs.height || lhs.fps != rhs.fps
			|| lhs.bitrateKbps != rhs.bitrateKbps;
	}

	bool presentDesktopMediaRequest(QWidget *parent, const QWebEngineDesktopMediaRequest &request,
								   const QString &windowTitle) {
		const QAbstractItemModel *screensModel = request.screensModel();
		const QAbstractItemModel *windowsModel = request.windowsModel();

		struct Choice {
			bool isScreen = false;
			int row       = -1;
			QString label;
		};

		QList< Choice > choices;
		auto appendChoices = [&choices](const QAbstractItemModel *model, const bool isScreen) {
			if (!model) {
				return;
			}

			for (int row = 0; row < model->rowCount(); ++row) {
				const QModelIndex index = model->index(row, 0);
				if (!index.isValid()) {
					continue;
				}

				QString label = index.data(Qt::DisplayRole).toString().trimmed();
				if (label.isEmpty()) {
					label = isScreen ? QObject::tr("Screen %1").arg(row + 1) : QObject::tr("Window %1").arg(row + 1);
				}

				choices.append(Choice{ isScreen, row, label });
			}
		};

		appendChoices(screensModel, true);
		appendChoices(windowsModel, false);

		if (choices.isEmpty()) {
			request.cancel();
			return false;
		}
		if (choices.size() == 1) {
			const Choice &choice = choices.constFirst();
			const QModelIndex index = choice.isScreen ? screensModel->index(choice.row, 0)
													  : windowsModel->index(choice.row, 0);
			if (!index.isValid()) {
				request.cancel();
				return false;
			}

			if (choice.isScreen) {
				request.selectScreen(index);
			} else {
				request.selectWindow(index);
			}
			return true;
		}

		QDialog dialog(parent);
		dialog.setWindowTitle(windowTitle);
		dialog.setModal(true);
		dialog.setMinimumSize(520, 360);

		auto *layout = new QVBoxLayout(&dialog);
		layout->setContentsMargins(16, 16, 16, 16);
		layout->setSpacing(12);

		auto *label = new QLabel(QObject::tr("Choose a screen or window to share."), &dialog);
		label->setWordWrap(true);
		layout->addWidget(label);

		auto *list = new QListWidget(&dialog);
		list->setSelectionMode(QAbstractItemView::SingleSelection);
		list->setUniformItemSizes(true);
		for (const Choice &choice : choices) {
			auto *item = new QListWidgetItem(choice.label, list);
			item->setData(Qt::UserRole, choice.isScreen);
			item->setData(Qt::UserRole + 1, choice.row);
		}
		if (list->count() > 0) {
			list->setCurrentRow(0);
		}
		layout->addWidget(list, 1);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
		QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
		QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		layout->addWidget(buttons);

		if (dialog.exec() != QDialog::Accepted) {
			request.cancel();
			return false;
		}

		QListWidgetItem *currentItem = list->currentItem();
		if (!currentItem) {
			request.cancel();
			return false;
		}

		const bool isScreen = currentItem->data(Qt::UserRole).toBool();
		const int row = currentItem->data(Qt::UserRole + 1).toInt();
		const QModelIndex index = isScreen ? screensModel->index(row, 0) : windowsModel->index(row, 0);
		if (!index.isValid()) {
			request.cancel();
			return false;
		}

		if (isScreen) {
			request.selectScreen(index);
		} else {
			request.selectWindow(index);
		}

		return true;
	}
} // namespace

RelayWindowHost::RelayWindowHost(const ScreenShareSession &session, const Mode mode, QWidget *parent)
	: QWidget(parent), m_session(session), m_mode(mode) {
	setAttribute(Qt::WA_DeleteOnClose, true);
	setWindowTitle(windowTitle());
	setMinimumSize(960, 720);

	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);

	m_view = new QWebEngineView(this);
	m_layout->addWidget(m_view);

	m_page = new RelayWebPage(m_view);
	m_view->setPage(m_page);

	m_channel = new QWebChannel(this);
	m_bridge = new RelayWindowBridge(this);
	m_channel->registerObject(QStringLiteral("relayBridge"), m_bridge);
	m_page->setWebChannel(m_channel);
	m_bootTimeoutTimer = new QTimer(this);
	m_bootTimeoutTimer->setSingleShot(true);
	m_bootTimeoutTimer->setInterval(8000);

	m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
	m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
	m_view->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
	m_view->settings()->setAttribute(QWebEngineSettings::ScreenCaptureEnabled, true);
#endif

	connect(m_view, &QWebEngineView::loadFinished, this, &RelayWindowHost::handleLoadFinished);
	connect(m_page, &QWebEnginePage::renderProcessTerminated, this, &RelayWindowHost::handleRenderProcessTerminated);
	connect(m_page, &QWebEnginePage::desktopMediaRequested, this,
			[this](const QWebEngineDesktopMediaRequest &request) {
				handleDesktopMediaRequested(request);
			});
	connect(m_page, &RelayWebPage::externalNavigationRequested, this, [this](const QUrl &url) {
		if (Global::get().l) {
			Global::get().l->log(Log::Information,
								 tr("Relay window requested external navigation to %1.")
									 .arg(url.toString(QUrl::FullyEncoded).toHtmlEscaped()));
		}
	});

	connect(m_bridge, &RelayWindowBridge::bootReady, this, &RelayWindowHost::handleBridgeBootReady);
	connect(m_bridge, &RelayWindowBridge::fallbackRequested, this, &RelayWindowHost::handleBridgeFallbackRequested);
	connect(m_bridge, &RelayWindowBridge::closeRequested, this, &RelayWindowHost::handleBridgeCloseRequested);
	connect(m_bootTimeoutTimer, &QTimer::timeout, this, &RelayWindowHost::handleBootTimeout);
}

bool RelayWindowHost::start(QString *errorMessage) {
	if (!m_view || !m_page) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Relay window could not be initialized.");
		}
		return false;
	}

	const QUrl url = buildPageUrl();
	if (!url.isValid() || url.isEmpty()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Relay window launch URL is invalid.");
		}
		return false;
	}

	m_view->load(url);
	m_started = true;
	m_bootReady = false;
	m_fallbackIssued = false;
	m_bootTimeoutTimer->start();
	setWindowTitle(windowTitle());
	show();
	raise();
	activateWindow();
	return true;
}

const ScreenShareSession &RelayWindowHost::session() const {
	return m_session;
}

RelayWindowHost::Mode RelayWindowHost::mode() const {
	return m_mode;
}

QString RelayWindowHost::streamID() const {
	return m_session.streamID;
}

void RelayWindowHost::updateSession(const ScreenShareSession &session) {
	const bool requiresReload = relaySessionRequiresReload(m_session, session);
	m_session = session;
	setWindowTitle(windowTitle());
	if (!requiresReload || !m_view) {
		return;
	}

	m_bootReady = false;
	m_fallbackIssued = false;
	m_bootTimeoutTimer->start();
	m_view->load(buildPageUrl());
}

void RelayWindowHost::closeFromManager() {
	m_closingFromManager = true;
	close();
}

void RelayWindowHost::closeEvent(QCloseEvent *event) {
	if (!m_closingFromManager) {
		emit closeRequested(m_session.streamID);
	}

	event->accept();
}

void RelayWindowHost::handleLoadFinished(const bool ok) {
	if (ok || m_closingFromManager) {
		return;
	}

	m_started = false;
	m_bootReady = false;
	m_bootTimeoutTimer->stop();
	requestFallbackOnce(tr("The in-app relay window failed to load its local assets."));
}

void RelayWindowHost::handleRenderProcessTerminated(const QWebEnginePage::RenderProcessTerminationStatus status,
													const int exitCode) {
	Q_UNUSED(status);
	if (m_closingFromManager) {
		return;
	}

	m_started = false;
	m_bootReady = false;
	m_bootTimeoutTimer->stop();
	requestFallbackOnce(tr("The in-app relay renderer stopped unexpectedly with exit code %1.").arg(exitCode));
}

void RelayWindowHost::handleDesktopMediaRequested(const QWebEngineDesktopMediaRequest &request) {
	if (m_closingFromManager) {
		request.cancel();
		return;
	}

	if (!presentDesktopMediaRequest(this, request, tr("Select relay capture source"))) {
		if (Global::get().l) {
			Global::get().l->log(Log::Information, tr("The relay capture source request was canceled."));
		}
	}
}

void RelayWindowHost::handleBridgeBootReady() {
	m_bootReady = true;
	m_bootTimeoutTimer->stop();
	if (Global::get().l) {
		Global::get().l->log(Log::Information,
							 tr("Opened in-app screen-share relay window for %1.").arg(m_session.streamID.toHtmlEscaped()));
	}
}

void RelayWindowHost::handleBridgeFallbackRequested(const QString &reason) {
	if (m_closingFromManager) {
		return;
	}

	requestFallbackOnce(reason.isEmpty() ? tr("The relay page requested fallback.") : reason);
}

void RelayWindowHost::handleBridgeCloseRequested(const QString &reason) {
	Q_UNUSED(reason);
	if (m_closingFromManager) {
		return;
	}

	emit closeRequested(m_session.streamID);
}

void RelayWindowHost::handleBootTimeout() {
	if (m_closingFromManager || m_bootReady || !m_started) {
		return;
	}

	m_started = false;
	requestFallbackOnce(tr("The in-app relay window did not finish initializing its local bridge."));
}

QUrl RelayWindowHost::buildPageUrl() const {
	QUrl url = relayWebAppUrl();
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("mumble_screen_share"), QStringLiteral("1"));
	query.addQueryItem(QStringLiteral("relay_url"), m_session.relayUrl);
	query.addQueryItem(QStringLiteral("relay_room_id"), m_session.relayRoomID);
	query.addQueryItem(QStringLiteral("relay_token"), m_session.relayToken);
	query.addQueryItem(QStringLiteral("relay_session_id"), m_session.relaySessionID);
	query.addQueryItem(QStringLiteral("stream_id"), m_session.streamID);
	query.addQueryItem(QStringLiteral("relay_role"), Mumble::ScreenShare::relayRoleToConfigToken(m_session.relayRole));
	query.addQueryItem(QStringLiteral("codec"), Mumble::ScreenShare::codecToConfigToken(m_session.codec));
	query.addQueryItem(QStringLiteral("transport"), Mumble::ScreenShare::relayTransportToConfigToken(m_session.relayTransport));
	query.addQueryItem(QStringLiteral("width"), QString::number(qMax(0U, m_session.width)));
	query.addQueryItem(QStringLiteral("height"), QString::number(qMax(0U, m_session.height)));
	query.addQueryItem(QStringLiteral("fps"), QString::number(qMax(0U, m_session.fps)));
	query.addQueryItem(QStringLiteral("bitrate_kbps"), QString::number(qMax(0U, m_session.bitrateKbps)));
	if (m_mode == Mode::Publish) {
		query.addQueryItem(QStringLiteral("capture_audio"), QStringLiteral("1"));
		query.addQueryItem(QStringLiteral("system_audio"), QStringLiteral("include"));
		query.addQueryItem(QStringLiteral("surface_switching"), QStringLiteral("include"));
		query.addQueryItem(QStringLiteral("self_browser_surface"), QStringLiteral("exclude"));
	}

	url.setQuery(query);
	return url;
}

QString RelayWindowHost::windowTitle() const {
	const QString role = roleLabel();
	if (m_session.streamID.trimmed().isEmpty()) {
		return tr("Mumble Screen Share Relay (%1)").arg(role);
	}

	return tr("Mumble Screen Share Relay (%1) - %2")
		.arg(role, m_session.streamID.toHtmlEscaped());
}

QString RelayWindowHost::roleLabel() const {
	return m_mode == Mode::Publish ? tr("Publisher") : tr("Viewer");
}

void RelayWindowHost::requestCloseFromManager() {
	m_closingFromManager = true;
	close();
}

void RelayWindowHost::requestFallbackOnce(const QString &reason) {
	if (m_closingFromManager || m_fallbackIssued) {
		return;
	}

	m_fallbackIssued = true;
	emit fallbackRequested(reason);
}

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)
