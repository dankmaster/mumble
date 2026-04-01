// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WIDGETS_RESPONSIVEIMAGEDIALOG_H_
#define MUMBLE_MUMBLE_WIDGETS_RESPONSIVEIMAGEDIALOG_H_

#include <QtGui/QPixmap>
#include <QtWidgets/QDialog>

class QLabel;
class QScrollArea;

class ResponsiveImageDialog : public QDialog {
	Q_OBJECT

public:
	explicit ResponsiveImageDialog(const QPixmap &pixmap, QWidget *parent = nullptr);

protected:
	bool event(QEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	void applyWindowChrome();
	void adjustInitialSize();
	void updatePixmap(bool preserveViewportCenter = false);
	void setZoomFactor(qreal zoomFactor, bool preserveViewportCenter = true);
	void zoomBy(qreal factor);
	void showContextMenu(const QPoint &globalPosition);
	void saveImageAs();
	void copyImage();
	void openImageInBrowser();

	QLabel *m_label       = nullptr;
	QScrollArea *m_scrollArea = nullptr;
	QPixmap m_pixmap;
	qreal m_zoomFactor    = 1.0;
};

#endif // MUMBLE_MUMBLE_WIDGETS_RESPONSIVEIMAGEDIALOG_H_
