// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatListWidget.h"

#include <QtGui/QResizeEvent>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QLayout>
#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <limits>

PersistentChatListWidget::PersistentChatListWidget(QWidget *parent) : QListWidget(parent) {
}

bool PersistentChatListWidget::isScrolledToBottom() const {
	const QScrollBar *scrollBar = verticalScrollBar();
	return scrollBar && scrollBar->value() == scrollBar->maximum();
}

void PersistentChatListWidget::stabilizeVisibleContent() {
	if (m_stabilizingVisibleContent || !viewport()) {
		return;
	}

	m_stabilizingVisibleContent = true;

	const int viewportWidth = std::max(1, viewport()->width());
	bool sizeHintsChanged  = false;

	for (int i = 0; i < count(); ++i) {
		QListWidgetItem *listItem = item(i);
		if (!listItem) {
			continue;
		}

		QWidget *widget = itemWidget(listItem);
		if (!widget) {
			continue;
		}

		int measuredHeight = widget->property("persistentChatItemHeight").toInt();
		if (measuredHeight <= 0) {
			measuredHeight = widget->height();
		}
		if (measuredHeight <= 0) {
			measuredHeight = widget->sizeHint().height();
		}
		if (QLayout *layout = widget->layout()) {
			layout->activate();
			measuredHeight = std::max(measuredHeight, layout->sizeHint().height());
			measuredHeight = std::max(measuredHeight, layout->minimumSize().height());
		}
		measuredHeight = std::max(measuredHeight, widget->minimumSizeHint().height());
		measuredHeight = std::max(1, measuredHeight);

		const QSize desiredHint(viewportWidth, measuredHeight);
		if (listItem->sizeHint() != desiredHint) {
			listItem->setSizeHint(desiredHint);
			sizeHintsChanged = true;
			widget->updateGeometry();
		}
	}

	if (sizeHintsChanged) {
		updateGeometries();
		doItemsLayout();
	}

	const QRect viewportRect = viewport()->rect();
	bool hasVisibleContent   = false;
	int nearestAboveTop      = std::numeric_limits< int >::min();
	int nearestBelowTop      = std::numeric_limits< int >::max();

	for (int i = 0; i < count(); ++i) {
		QListWidgetItem *listItem = item(i);
		QWidget *widget = listItem ? itemWidget(listItem) : nullptr;
		if (!listItem || !widget) {
			continue;
		}

		QRect itemRect = widget->geometry();
		if (!itemRect.isValid() || itemRect.isEmpty()) {
			itemRect = visualItemRect(listItem);
		}
		if (!itemRect.isValid() || itemRect.isEmpty()) {
			continue;
		}

		if (itemRect.intersects(viewportRect)) {
			hasVisibleContent = true;
			break;
		}

		if (itemRect.bottom() <= viewportRect.top()) {
			nearestAboveTop = std::max(nearestAboveTop, itemRect.top());
		} else if (itemRect.top() >= viewportRect.bottom()) {
			nearestBelowTop = std::min(nearestBelowTop, itemRect.top());
		}
	}

	if (!hasVisibleContent) {
		if (QScrollBar *scrollBar = verticalScrollBar()) {
			int correctedValue = scrollBar->value();
			if (nearestAboveTop != std::numeric_limits< int >::min()) {
				correctedValue += nearestAboveTop;
			} else if (nearestBelowTop != std::numeric_limits< int >::max()) {
				correctedValue += nearestBelowTop;
			}

			correctedValue = qBound(scrollBar->minimum(), correctedValue, scrollBar->maximum());
			if (correctedValue != scrollBar->value()) {
				scrollBar->setValue(correctedValue);
				updateGeometries();
				doItemsLayout();
			}
		}
	}

	viewport()->update();
	m_stabilizingVisibleContent = false;
}

void PersistentChatListWidget::resizeEvent(QResizeEvent *event) {
	QListWidget::resizeEvent(event);

	if (event->size().width() != event->oldSize().width()) {
		emit contentWidthChanged(viewport()->width());
	}
}

void PersistentChatListWidget::scrollContentsBy(int dx, int dy) {
	QListWidget::scrollContentsBy(dx, dy);
	stabilizeVisibleContent();
}
