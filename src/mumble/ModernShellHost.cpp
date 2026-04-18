// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernShellHost.h"

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include "Global.h"
#include "Log.h"
#include "ModernShellBridge.h"
#include "ModernShellPage.h"

#include <QtCore/QEvent>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QMimeData>
#include <QtCore/QTimer>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QImageReader>
#include <QtWidgets/QVBoxLayout>
#include <QtWebChannel/QWebChannel>
#include <QtWebEngineCore/QWebEnginePage>
#include <QtWebEngineCore/QWebEngineSettings>
#include <QtWebEngineWidgets/QWebEngineView>

namespace {
	QUrl modernShellUrl() {
		return QUrl(QStringLiteral("qrc:/modern-shell/index.html"));
	}

	void appendModernShellConnectTrace(const QString &message) {
		if (qEnvironmentVariableIntValue("MUMBLE_CONNECT_TRACE") == 0) {
			return;
		}

		QFile traceFile(Global::get().qdBasePath.filePath(QLatin1String("shared-modern-connect-trace.log")));
		if (!traceFile.open(QIODevice::Append | QIODevice::Text)) {
			return;
		}

		const QByteArray line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8()
								+ " UI " + message.toUtf8() + '\n';
		traceFile.write(line);
		traceFile.flush();
	}
} // namespace

ModernShellHost::ModernShellHost(QWidget *parent) : QWidget(parent) {
	setAttribute(Qt::WA_StyledBackground, true);
	setObjectName(QLatin1String("qwModernShellHost"));

	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);

	m_view = new QWebEngineView(this);
	m_layout->addWidget(m_view);
	m_view->setAcceptDrops(true);
	m_view->setContextMenuPolicy(Qt::NoContextMenu);
	m_view->installEventFilter(this);

	m_page = new ModernShellPage(m_view);
	m_view->setPage(m_page);

	m_channel = new QWebChannel(this);
	m_bridge  = new ModernShellBridge(this);
	m_channel->registerObject(QStringLiteral("modernBridge"), m_bridge);
	m_page->setWebChannel(m_channel);
	m_bootTimeoutTimer = new QTimer(this);
	m_bootTimeoutTimer->setSingleShot(true);
	m_bootTimeoutTimer->setInterval(8000);

	m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
	m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
	m_view->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);

	connect(m_view, &QWebEngineView::loadFinished, this, &ModernShellHost::handleLoadFinished);
	connect(m_page, &QWebEnginePage::renderProcessTerminated, this, &ModernShellHost::handleRenderProcessTerminated);
	connect(m_bridge, &ModernShellBridge::bootReady, this, &ModernShellHost::handleBridgeBootReady);
	connect(m_bootTimeoutTimer, &QTimer::timeout, this, &ModernShellHost::handleBootTimeout);
	connect(m_page, &ModernShellPage::externalNavigationRequested, this, [](const QUrl &url) {
		if (Global::get().l) {
			Global::get().l->log(Log::Information,
								 QObject::tr("Modern layout requested external navigation to %1.")
									 .arg(url.toString(QUrl::FullyEncoded).toHtmlEscaped()));
		}
	});
}

bool ModernShellHost::start(QString *errorMessage) {
	appendModernShellConnectTrace(QStringLiteral("ModernShellHost::start enter started=%1 view=%2")
									  .arg(m_started ? 1 : 0)
									  .arg(m_view ? 1 : 0));
	if (m_started) {
		appendModernShellConnectTrace(QStringLiteral("ModernShellHost::start already-started"));
		return true;
	}

	if (!m_view) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Modern shell view could not be initialized.");
		}
		appendModernShellConnectTrace(QStringLiteral("ModernShellHost::start missing-view"));
		return false;
	}

	const QUrl url = modernShellUrl();
	if (!url.isValid() || url.isEmpty()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Modern shell URL is invalid.");
		}
		appendModernShellConnectTrace(QStringLiteral("ModernShellHost::start invalid-url"));
		return false;
	}

	m_view->load(url);
	m_started = true;
	m_bootReady = false;
	m_bootTimeoutTimer->start();
	appendModernShellConnectTrace(QStringLiteral("ModernShellHost::start load=%1 timeout=%2")
									  .arg(url.toString())
									  .arg(m_bootTimeoutTimer->interval()));
	return true;
}

ModernShellBridge *ModernShellHost::bridge() const {
	return m_bridge;
}

bool ModernShellHost::eventFilter(QObject *watched, QEvent *event) {
	if (watched == m_view) {
		const auto extractImageUrls = [](const QMimeData *mimeData) {
			QList< QUrl > imageUrls;
			if (!mimeData) {
				return imageUrls;
			}

			const QList< QUrl > urls = mimeData->urls();
			for (const QUrl &url : urls) {
				if (!url.isLocalFile()) {
					continue;
				}

				const QString localPath = url.toLocalFile();
				if (QImageReader::imageFormat(localPath).isEmpty()) {
					continue;
				}

				imageUrls.push_back(url);
			}

			return imageUrls;
		};

		if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
			QDropEvent *dropEvent = static_cast< QDropEvent * >(event);
			const QList< QUrl > imageUrls = extractImageUrls(dropEvent->mimeData());
			if (!imageUrls.isEmpty()) {
				dropEvent->acceptProposedAction();
				return true;
			}
		} else if (event->type() == QEvent::Drop) {
			QDropEvent *dropEvent = static_cast< QDropEvent * >(event);
			const QList< QUrl > imageUrls = extractImageUrls(dropEvent->mimeData());
			if (!imageUrls.isEmpty()) {
				dropEvent->acceptProposedAction();
				emit imageUrlsDropped(imageUrls);
				return true;
			}
		}
	}

	return QWidget::eventFilter(watched, event);
}

void ModernShellHost::handleLoadFinished(const bool ok) {
	appendModernShellConnectTrace(QStringLiteral("ModernShellHost::handleLoadFinished ok=%1").arg(ok ? 1 : 0));
	if (ok) {
		return;
	}

	m_started = false;
	m_bootReady = false;
	m_bootTimeoutTimer->stop();
	emit bootFailed(tr("The modern layout failed to load its local web assets."));
}

void ModernShellHost::handleRenderProcessTerminated(const QWebEnginePage::RenderProcessTerminationStatus status,
													const int exitCode) {
	appendModernShellConnectTrace(QStringLiteral("ModernShellHost::handleRenderProcessTerminated status=%1 exit=%2")
									  .arg(static_cast< int >(status))
									  .arg(exitCode));
	Q_UNUSED(status);
	m_started = false;
	m_bootReady = false;
	m_bootTimeoutTimer->stop();
	emit bootFailed(tr("The modern layout renderer stopped unexpectedly with exit code %1.").arg(exitCode));
}

void ModernShellHost::handleBridgeBootReady() {
	appendModernShellConnectTrace(QStringLiteral("ModernShellHost::handleBridgeBootReady"));
	m_bootReady = true;
	m_bootTimeoutTimer->stop();
}

void ModernShellHost::handleBootTimeout() {
	appendModernShellConnectTrace(QStringLiteral("ModernShellHost::handleBootTimeout started=%1 bootReady=%2")
									  .arg(m_started ? 1 : 0)
									  .arg(m_bootReady ? 1 : 0));
	if (!m_started || m_bootReady) {
		return;
	}

	m_started = false;
	emit bootFailed(tr("The modern layout did not finish initializing its local bridge."));
}

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)
