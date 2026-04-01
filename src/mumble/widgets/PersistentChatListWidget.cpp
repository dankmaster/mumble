// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatListWidget.h"

#include "../PersistentChatHistoryModel.h"

#include <QtGui/QResizeEvent>
#include <QtWidgets/QScrollBar>

PersistentChatListWidget::PersistentChatListWidget(QWidget *parent) : QListView(parent) {
	setMouseTracking(true);
}

bool PersistentChatListWidget::isScrolledToBottom() const {
	const QScrollBar *scrollBar = verticalScrollBar();
	return scrollBar && scrollBar->value() == scrollBar->maximum();
}

PersistentChatViewportAnchor PersistentChatListWidget::captureViewportAnchor() const {
	PersistentChatViewportAnchor anchor;
	const auto *historyModel = qobject_cast< const PersistentChatHistoryModel * >(model());
	if (!historyModel || !viewport()) {
		return anchor;
	}

	PersistentChatViewportAnchor fallbackAnchor;
	for (int row = 0; row < historyModel->rowCount(); ++row) {
		const QModelIndex modelIndex = historyModel->index(row, 0);
		const QRect rect             = visualRect(modelIndex);
		if (!rect.isValid() || rect.bottom() <= 0) {
			continue;
		}

		const PersistentChatHistoryRow *rowData = historyModel->rowAt(row);
		if (!rowData) {
			continue;
		}

		if (!fallbackAnchor.isValid()) {
			fallbackAnchor.rowId          = rowData->rowId;
			fallbackAnchor.intraRowOffset = -rect.top();
		}

		if (rowData->kind == PersistentChatHistoryRowKind::LoadOlder) {
			continue;
		}

		anchor.rowId          = rowData->rowId;
		anchor.intraRowOffset = -rect.top();
		return anchor;
	}

	return fallbackAnchor;
}

void PersistentChatListWidget::restoreViewportAnchor(const PersistentChatViewportAnchor &anchor) {
	if (!anchor.isValid()) {
		return;
	}

	const auto *historyModel = qobject_cast< const PersistentChatHistoryModel * >(model());
	QScrollBar *scrollBar    = verticalScrollBar();
	if (!historyModel || !scrollBar) {
		return;
	}

	const int row = historyModel->rowForId(anchor.rowId);
	if (row < 0) {
		return;
	}

	const QRect rect = visualRect(historyModel->index(row, 0));
	if (!rect.isValid()) {
		return;
	}

	const int desiredValue = scrollBar->value() + rect.top() + anchor.intraRowOffset;
	scrollBar->setValue(qBound(scrollBar->minimum(), desiredValue, scrollBar->maximum()));
}

bool PersistentChatListWidget::isRowVisible(const QString &rowId) const {
	const auto *historyModel = qobject_cast< const PersistentChatHistoryModel * >(model());
	if (!historyModel || rowId.isEmpty() || !viewport()) {
		return false;
	}

	const int row = historyModel->rowForId(rowId);
	if (row < 0) {
		return false;
	}

	return visualRect(historyModel->index(row, 0)).intersects(viewport()->rect());
}

void PersistentChatListWidget::stabilizeVisibleContent() {
	if (m_stabilizingVisibleContent) {
		return;
	}

	m_stabilizingVisibleContent = true;
	doItemsLayout();
	updateGeometries();
	if (viewport()) {
		viewport()->update();
	}
	m_stabilizingVisibleContent = false;
}

void PersistentChatListWidget::resizeEvent(QResizeEvent *event) {
	QListView::resizeEvent(event);

	if (event->size().width() != event->oldSize().width()) {
		emit contentWidthChanged(viewport()->width());
	}
}
