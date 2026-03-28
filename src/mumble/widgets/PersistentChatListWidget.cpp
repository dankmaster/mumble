// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatListWidget.h"

#include <QtGui/QResizeEvent>
#include <QtWidgets/QScrollBar>

PersistentChatListWidget::PersistentChatListWidget(QWidget *parent) : QListWidget(parent) {
}

bool PersistentChatListWidget::isScrolledToBottom() const {
	const QScrollBar *scrollBar = verticalScrollBar();
	return scrollBar && scrollBar->value() == scrollBar->maximum();
}

void PersistentChatListWidget::resizeEvent(QResizeEvent *event) {
	QListWidget::resizeEvent(event);

	if (event->size().width() != event->oldSize().width()) {
		emit contentWidthChanged(viewport()->width());
	}
}
