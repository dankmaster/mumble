// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ResponsiveImageDialog.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>

ResponsiveImageDialog::ResponsiveImageDialog(const QPixmap &pixmap, QWidget *parent)
	: QDialog(parent), m_label(nullptr), m_scrollArea(nullptr), m_pixmap(pixmap) {
	setWindowTitle(tr("Image Preview"));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setMinimumSize(200, 150);
	setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(0);

	m_scrollArea = new QScrollArea(this);
	m_scrollArea->setWidgetResizable(true);
	m_scrollArea->setAlignment(Qt::AlignCenter);
	m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	m_label = new QLabel(this);
	m_label->setAlignment(Qt::AlignCenter);
	m_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_label->setMinimumSize(100, 75);
	m_label->setScaledContents(false);

	m_scrollArea->setWidget(m_label);
	layout->addWidget(m_scrollArea);

	// Set initial size to image size, but clamp to reasonable min/max
	int initialWidth  = std::clamp(pixmap.width() + 60, 300, 1200);
	int initialHeight = std::clamp(pixmap.height() + 60, 200, 900);
	resize(initialWidth, initialHeight);
	updatePixmap();
}

void ResponsiveImageDialog::updatePixmap() {
	if (m_pixmap.isNull() || !m_label || !m_scrollArea) {
		return;
	}

	const QSize viewportSize = m_scrollArea->viewport()->size();
	QPixmap displayPixmap    = m_pixmap;

	if (viewportSize.isValid() && !viewportSize.isEmpty()) {
		const QSize scaledSize = m_pixmap.size().scaled(viewportSize, Qt::KeepAspectRatio);
		if (scaledSize.isValid() && !scaledSize.isEmpty() && scaledSize != m_pixmap.size()) {
			displayPixmap = m_pixmap.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		}
	}

	m_label->setPixmap(displayPixmap);
	m_label->resize(displayPixmap.size());
}

void ResponsiveImageDialog::resizeEvent(QResizeEvent *event) {
	QDialog::resizeEvent(event);
	updatePixmap();
}
