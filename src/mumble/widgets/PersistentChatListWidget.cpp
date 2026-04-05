// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatListWidget.h"

#include "../PersistentChatHistoryModel.h"

#include <QtCore/QEvent>
#include <QtCore/QTimer>
#include <QtGui/QResizeEvent>
#include <QtWidgets/QScrollBar>

namespace {
	constexpr int PersistentChatBottomStickThreshold = 18;

	int persistentChatBottomStickThreshold(const QScrollBar *scrollBar) {
		if (!scrollBar) {
			return PersistentChatBottomStickThreshold;
		}

		return scrollBar->singleStep() > PersistentChatBottomStickThreshold ? scrollBar->singleStep()
																			: PersistentChatBottomStickThreshold;
	}

	bool persistentChatIsNearBottom(const QScrollBar *scrollBar, int maximum) {
		if (!scrollBar) {
			return true;
		}

		return maximum - scrollBar->value() <= persistentChatBottomStickThreshold(scrollBar);
	}
}

PersistentChatListWidget::PersistentChatListWidget(QWidget *parent) : QListView(parent) {
	setMouseTracking(true);
	setProperty("scrollbarVisible", false);
	setAttribute(Qt::WA_Hover, true);
	installEventFilter(this);
	if (viewport()) {
		viewport()->setMouseTracking(true);
		viewport()->setAttribute(Qt::WA_Hover, true);
		viewport()->installEventFilter(this);
	}
	if (QScrollBar *scrollBar = verticalScrollBar()) {
		scrollBar->setAttribute(Qt::WA_Hover, true);
		scrollBar->installEventFilter(this);
		m_lastKnownScrollMaximum = scrollBar->maximum();
		m_keepBottomPinned       = persistentChatIsNearBottom(scrollBar, m_lastKnownScrollMaximum);
		connect(scrollBar, &QScrollBar::valueChanged, this, [this](int) { updateBottomPinState(); });
		connect(scrollBar, &QScrollBar::rangeChanged, this, [this](int, int maximum) {
			const bool wasPinned = m_keepBottomPinned
								   || persistentChatIsNearBottom(verticalScrollBar(), m_lastKnownScrollMaximum);
			m_lastKnownScrollMaximum = maximum;
			if (!wasPinned) {
				updateBottomPinState();
				return;
			}

			m_keepBottomPinned = true;
			queueSnapToBottom();
		});
	}
}

bool PersistentChatListWidget::isScrolledToBottom() const {
	const QScrollBar *scrollBar = verticalScrollBar();
	if (!scrollBar) {
		return true;
	}

	const int remainingDistance = scrollBar->maximum() - scrollBar->value();
	const int stickyThreshold = persistentChatBottomStickThreshold(scrollBar);
	if (remainingDistance <= stickyThreshold) {
		return true;
	}

	const auto *historyModel = qobject_cast< const PersistentChatHistoryModel * >(model());
	return historyModel && isRowVisible(historyModel->lastMessageGroupRowId());
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

void PersistentChatListWidget::queueSnapToBottom() {
	if (m_bottomSnapQueued) {
		return;
	}

	m_bottomSnapQueued = true;
	QTimer::singleShot(0, this, [this]() {
		m_bottomSnapQueued = false;
		if (QScrollBar *scrollBar = verticalScrollBar()) {
			scrollBar->setValue(scrollBar->maximum());
			m_lastKnownScrollMaximum = scrollBar->maximum();
			m_keepBottomPinned       = true;
		}
	});
}

void PersistentChatListWidget::updateBottomPinState() {
	if (QScrollBar *scrollBar = verticalScrollBar()) {
		m_lastKnownScrollMaximum = scrollBar->maximum();
		m_keepBottomPinned       = isScrolledToBottom();
		return;
	}

	m_lastKnownScrollMaximum = 0;
	m_keepBottomPinned       = true;
}

bool PersistentChatListWidget::eventFilter(QObject *watched, QEvent *event) {
	if ((watched == this || watched == viewport() || watched == verticalScrollBar()) && event) {
		switch (event->type()) {
			case QEvent::Enter:
			case QEvent::HoverEnter:
			case QEvent::Leave:
			case QEvent::HoverLeave:
				QTimer::singleShot(0, this, [this]() {
					const bool visible =
						underMouse() || (viewport() && viewport()->underMouse())
						|| (verticalScrollBar() && verticalScrollBar()->underMouse());
					if (property("scrollbarVisible").toBool() == visible) {
						return;
					}

					setProperty("scrollbarVisible", visible);
					style()->unpolish(this);
					style()->polish(this);
					update();
					if (QScrollBar *scrollBar = verticalScrollBar()) {
						scrollBar->style()->unpolish(scrollBar);
						scrollBar->style()->polish(scrollBar);
						scrollBar->update();
					}
				});
				break;
			default:
				break;
		}
	}

	return QListView::eventFilter(watched, event);
}

void PersistentChatListWidget::resizeEvent(QResizeEvent *event) {
	QListView::resizeEvent(event);

	if (event->size().width() != event->oldSize().width()) {
		emit contentWidthChanged(viewport()->width());
	}
}
