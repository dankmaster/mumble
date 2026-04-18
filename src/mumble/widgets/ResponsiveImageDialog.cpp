// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ResponsiveImageDialog.h"

#include "Global.h"
#include "UiTheme.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QStandardPaths>
#include <QtGui/QClipboard>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGestureEvent>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPinchGesture>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QVBoxLayout>

#ifdef Q_OS_WIN
#	include <QtGui/QPalette>
#	include <windows.h>

namespace {
	typedef HRESULT(WINAPI *DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);

	constexpr DWORD DwmUseImmersiveDarkModeLegacyAttribute = 19;
	constexpr DWORD DwmUseImmersiveDarkModeAttribute       = 20;
	constexpr DWORD DwmBorderColorAttribute                = 34;
	constexpr DWORD DwmCaptionColorAttribute               = 35;
	constexpr DWORD DwmTextColorAttribute                  = 36;

	COLORREF colorRefFromQColor(const QColor &color) {
		return RGB(color.red(), color.green(), color.blue());
	}

	bool isDarkPalette(const QPalette &palette) {
		return palette.color(QPalette::WindowText).lightness() > palette.color(QPalette::Window).lightness();
	}
}
#endif

#include <algorithm>

namespace {
	constexpr int PreviewPadding = 16;
	constexpr qreal MinZoomFactor = 0.5;
	constexpr qreal MaxZoomFactor = 8.0;
}

ResponsiveImageDialog::ResponsiveImageDialog(const QPixmap &pixmap, QWidget *parent)
	: QDialog(parent), m_pixmap(pixmap) {
	setObjectName(QLatin1String("qdResponsiveImageDialog"));
	setWindowTitle(tr("Image Preview"));
	setModal(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setMinimumSize(400, 300);
	setAttribute(Qt::WA_DeleteOnClose, false);
	setStyleSheet(QString::fromLatin1(
		"QDialog#qdResponsiveImageDialog { background: #111111; }"
		"QScrollArea#qsaResponsiveImageDialog { background: transparent; border: none; }"
		"QLabel#qlResponsiveImageDialogImage { background: transparent; }"));

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	m_scrollArea = new QScrollArea(this);
	m_scrollArea->setObjectName(QLatin1String("qsaResponsiveImageDialog"));
	m_scrollArea->setWidgetResizable(false);
	m_scrollArea->setAlignment(Qt::AlignCenter);
	m_scrollArea->setFrameShape(QFrame::NoFrame);
	m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scrollArea->viewport()->installEventFilter(this);
	m_scrollArea->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
	m_scrollArea->viewport()->setAutoFillBackground(true);
	layout->addWidget(m_scrollArea);

	m_label = new QLabel(this);
	m_label->setObjectName(QLatin1String("qlResponsiveImageDialogImage"));
	m_label->setAlignment(Qt::AlignCenter);
	m_label->setScaledContents(false);
	m_label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	m_label->installEventFilter(this);
	m_scrollArea->setWidget(m_label);

	grabGesture(Qt::PinchGesture);
	if (!Global::get().s.qbaImagePreviewGeometry.isEmpty() && !restoreGeometry(Global::get().s.qbaImagePreviewGeometry)) {
		adjustInitialSize();
	} else if (Global::get().s.qbaImagePreviewGeometry.isEmpty()) {
		adjustInitialSize();
	}
	updatePixmap();
}

bool ResponsiveImageDialog::event(QEvent *event) {
	if (event && event->type() == QEvent::Gesture) {
		auto *gestureEvent = static_cast< QGestureEvent * >(event);
		if (QGesture *gesture = gestureEvent->gesture(Qt::PinchGesture)) {
			auto *pinchGesture = static_cast< QPinchGesture * >(gesture);
			if (pinchGesture->changeFlags() & QPinchGesture::ScaleFactorChanged) {
				const qreal scaleDelta =
					pinchGesture->lastScaleFactor() > 0.0
						? (pinchGesture->scaleFactor() / pinchGesture->lastScaleFactor())
						: pinchGesture->scaleFactor();
				zoomBy(scaleDelta);
			}
			return true;
		}
	}

	return QDialog::event(event);
}

bool ResponsiveImageDialog::eventFilter(QObject *watched, QEvent *event) {
	if (!event) {
		return QDialog::eventFilter(watched, event);
	}

	if (watched == m_scrollArea->viewport()) {
		if (event->type() == QEvent::MouseButtonPress) {
			auto *mouseEvent = static_cast< QMouseEvent * >(event);
			if (mouseEvent->button() == Qt::LeftButton && m_label && !m_label->geometry().contains(mouseEvent->pos())) {
				reject();
				return true;
			}
		} else if (event->type() == QEvent::Wheel) {
			auto *wheelEvent = static_cast< QWheelEvent * >(event);
			zoomBy(wheelEvent->angleDelta().y() >= 0 ? 1.1 : 1.0 / 1.1);
			return true;
		} else if (event->type() == QEvent::ContextMenu) {
			auto *contextMenuEvent = static_cast< QContextMenuEvent * >(event);
			showContextMenu(contextMenuEvent->globalPos());
			return true;
		}
	} else if (watched == m_label) {
		if (event->type() == QEvent::MouseButtonDblClick) {
			setZoomFactor(1.0);
			return true;
		} else if (event->type() == QEvent::ContextMenu) {
			auto *contextMenuEvent = static_cast< QContextMenuEvent * >(event);
			showContextMenu(contextMenuEvent->globalPos());
			return true;
		}
	}

	return QDialog::eventFilter(watched, event);
}

void ResponsiveImageDialog::moveEvent(QMoveEvent *event) {
	QDialog::moveEvent(event);
	rememberGeometry();
}

void ResponsiveImageDialog::resizeEvent(QResizeEvent *event) {
	QDialog::resizeEvent(event);
	rememberGeometry();
	updatePixmap(true);
}

void ResponsiveImageDialog::showEvent(QShowEvent *event) {
	QDialog::showEvent(event);
	applyWindowChrome();
	rememberGeometry();
	updatePixmap(true);
}

void ResponsiveImageDialog::keyPressEvent(QKeyEvent *event) {
	if (event && event->key() == Qt::Key_Escape) {
		reject();
		return;
	}

	QDialog::keyPressEvent(event);
}

void ResponsiveImageDialog::rememberGeometry() {
	if (!isVisible() || isMinimized() || isMaximized() || isFullScreen()) {
		return;
	}

	Global::get().s.qbaImagePreviewGeometry = saveGeometry();
}

void ResponsiveImageDialog::applyWindowChrome() {
	if (const std::optional< UiThemeTokens > tokens = activeUiThemeTokens(); tokens) {
		QPalette palette = this->palette();
		palette.setColor(QPalette::Window, tokens->crust);
		palette.setColor(QPalette::WindowText, tokens->text);
		palette.setColor(QPalette::Base, tokens->crust);
		setPalette(palette);
		setStyleSheet(QString::fromLatin1(
			"QDialog#qdResponsiveImageDialog { background: %1; color: %2; }"
			"QScrollArea#qsaResponsiveImageDialog { background: transparent; border: none; }"
			"QLabel#qlResponsiveImageDialogImage { background: transparent; }")
						  .arg(tokens->crust.name(), tokens->text.name()));
	}

#ifdef Q_OS_WIN
	const HWND hwnd = reinterpret_cast< HWND >(winId());
	if (!hwnd) {
		return;
	}

	static const HMODULE dwmapiModule = GetModuleHandleW(L"dwmapi.dll");
	if (!dwmapiModule) {
		return;
	}

	static const DwmSetWindowAttributeFn setWindowAttribute =
		reinterpret_cast< DwmSetWindowAttributeFn >(GetProcAddress(dwmapiModule, "DwmSetWindowAttribute"));
	if (!setWindowAttribute) {
		return;
	}

	const QPalette palette = this->palette();
	const bool darkTheme   = isDarkPalette(palette);
	const std::optional< UiThemeTokens > tokens = activeUiThemeTokens();
	const QColor captionColor =
		tokens ? tokens->mantle : palette.color(QPalette::Window);
	const QColor titleTextColor =
		tokens ? tokens->text : palette.color(QPalette::WindowText);
	const QColor borderColor =
		tokens ? tokens->surface1 : palette.color(QPalette::Mid);
	const BOOL immersiveDarkMode = darkTheme ? TRUE : FALSE;
	HRESULT result = setWindowAttribute(hwnd, DwmUseImmersiveDarkModeAttribute, &immersiveDarkMode,
										 sizeof(immersiveDarkMode));
	if (FAILED(result)) {
		setWindowAttribute(hwnd, DwmUseImmersiveDarkModeLegacyAttribute, &immersiveDarkMode,
						   sizeof(immersiveDarkMode));
	}

	const COLORREF captionColorRef = colorRefFromQColor(captionColor);
	const COLORREF textColorRef    = colorRefFromQColor(titleTextColor);
	const COLORREF borderColorRef  = colorRefFromQColor(borderColor);
	setWindowAttribute(hwnd, DwmCaptionColorAttribute, &captionColorRef, sizeof(captionColorRef));
	setWindowAttribute(hwnd, DwmTextColorAttribute, &textColorRef, sizeof(textColorRef));
	setWindowAttribute(hwnd, DwmBorderColorAttribute, &borderColorRef, sizeof(borderColorRef));
#endif
}

void ResponsiveImageDialog::adjustInitialSize() {
	QScreen *screen = windowHandle() && windowHandle()->screen() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
	if (!screen || m_pixmap.isNull()) {
		resize(720, 540);
		return;
	}

	const QSize maxSize = QSize(static_cast< int >(screen->availableGeometry().width() * 0.9),
								static_cast< int >(screen->availableGeometry().height() * 0.9));
	const QSize contentSize =
		m_pixmap.size().scaled(maxSize - QSize(PreviewPadding * 2, PreviewPadding * 2), Qt::KeepAspectRatio);
	resize(std::max(400, contentSize.width() + (PreviewPadding * 2)),
		   std::max(300, contentSize.height() + (PreviewPadding * 2)));
}

void ResponsiveImageDialog::updatePixmap(bool preserveViewportCenter) {
	if (m_pixmap.isNull() || !m_label || !m_scrollArea) {
		return;
	}

	const bool fitToWindow = m_zoomFactor <= 1.001;
	m_scrollArea->setHorizontalScrollBarPolicy(fitToWindow ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
	m_scrollArea->setVerticalScrollBarPolicy(fitToWindow ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);

	const QSize viewportSize = m_scrollArea->viewport()->size() - QSize(PreviewPadding * 2, PreviewPadding * 2);
	if (!viewportSize.isValid() || viewportSize.isEmpty()) {
		return;
	}

	// KeepAspectRatio is deliberate here: the preview must always show the full image.
	const QSize fitSize = m_pixmap.size().scaled(viewportSize, Qt::KeepAspectRatio);
	if (!fitSize.isValid() || fitSize.isEmpty()) {
		return;
	}

	const QSize targetSize =
		QSize(std::max(1, static_cast< int >(std::round(fitSize.width() * m_zoomFactor))),
			  std::max(1, static_cast< int >(std::round(fitSize.height() * m_zoomFactor))));

	qreal horizontalRatio = 0.0;
	qreal verticalRatio   = 0.0;
	if (!fitToWindow && preserveViewportCenter) {
		const int horizontalRange = std::max(1, m_scrollArea->horizontalScrollBar()->maximum());
		const int verticalRange   = std::max(1, m_scrollArea->verticalScrollBar()->maximum());
		horizontalRatio = m_scrollArea->horizontalScrollBar()->value() / static_cast< qreal >(horizontalRange);
		verticalRatio   = m_scrollArea->verticalScrollBar()->value() / static_cast< qreal >(verticalRange);
	}

	const QPixmap displayPixmap =
		m_pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	m_label->setPixmap(displayPixmap);
	m_label->setFixedSize(displayPixmap.width() + (PreviewPadding * 2), displayPixmap.height() + (PreviewPadding * 2));

	if (fitToWindow) {
		m_scrollArea->horizontalScrollBar()->setValue(0);
		m_scrollArea->verticalScrollBar()->setValue(0);
	} else if (preserveViewportCenter) {
		m_scrollArea->horizontalScrollBar()->setValue(
			static_cast< int >(horizontalRatio * m_scrollArea->horizontalScrollBar()->maximum()));
		m_scrollArea->verticalScrollBar()->setValue(
			static_cast< int >(verticalRatio * m_scrollArea->verticalScrollBar()->maximum()));
	}
}

void ResponsiveImageDialog::setZoomFactor(qreal zoomFactor, bool preserveViewportCenter) {
	m_zoomFactor = qBound(MinZoomFactor, zoomFactor, MaxZoomFactor);
	updatePixmap(preserveViewportCenter);
}

void ResponsiveImageDialog::zoomBy(qreal factor) {
	if (factor <= 0.0) {
		return;
	}

	setZoomFactor(m_zoomFactor * factor);
}

void ResponsiveImageDialog::showContextMenu(const QPoint &globalPosition) {
	if (m_pixmap.isNull()) {
		return;
	}

	QMenu menu(this);
	menu.addAction(tr("Save image as..."), this, &ResponsiveImageDialog::saveImageAs);
	menu.addAction(tr("Copy image"), this, &ResponsiveImageDialog::copyImage);
	menu.addAction(tr("Open in browser"), this, &ResponsiveImageDialog::openImageInBrowser);
	menu.exec(globalPosition);
}

void ResponsiveImageDialog::saveImageAs() {
	if (m_pixmap.isNull()) {
		return;
	}

	const QString picturesDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
	const QString defaultPath = QDir(picturesDir.isEmpty() ? QDir::tempPath() : picturesDir)
									.filePath(QString::fromLatin1("Mumble-%1.png")
												  .arg(QDateTime::currentDateTime().toString(QString::fromLatin1("yyyy-MM-dd-HHmmss"))));
	const QString fileName = QFileDialog::getSaveFileName(this, tr("Save Image File"), defaultPath,
														  tr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
	if (fileName.isEmpty()) {
		return;
	}

	m_pixmap.save(fileName);
}

void ResponsiveImageDialog::copyImage() {
	if (!m_pixmap.isNull()) {
		QApplication::clipboard()->setPixmap(m_pixmap);
	}
}

void ResponsiveImageDialog::openImageInBrowser() {
	if (m_pixmap.isNull()) {
		return;
	}

	const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	const QString tempPath =
		QDir(tempDir.isEmpty() ? QDir::tempPath() : tempDir)
			.filePath(QString::fromLatin1("mumble-preview-%1.png")
						  .arg(QDateTime::currentDateTime().toString(QString::fromLatin1("yyyyMMdd-HHmmsszzz"))));
	if (m_pixmap.save(tempPath, "PNG")) {
		QDesktopServices::openUrl(QUrl::fromLocalFile(tempPath));
	}
}
