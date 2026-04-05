// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WIDGETS_PERSISTENTCHATLISTWIDGET_H_
#define MUMBLE_MUMBLE_WIDGETS_PERSISTENTCHATLISTWIDGET_H_

#include <QtWidgets/QListView>

struct PersistentChatViewportAnchor {
	QString rowId;
	int intraRowOffset = 0;

	bool isValid() const {
		return !rowId.isEmpty();
	}
};

class PersistentChatListWidget : public QListView {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PersistentChatListWidget)

public:
	explicit PersistentChatListWidget(QWidget *parent = nullptr);

	bool isScrolledToBottom() const;
	PersistentChatViewportAnchor captureViewportAnchor() const;
	void restoreViewportAnchor(const PersistentChatViewportAnchor &anchor);
	bool isRowVisible(const QString &rowId) const;
	void stabilizeVisibleContent();

signals:
	void contentWidthChanged(int width);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	void queueSnapToBottom();
	void updateBottomPinState();

	bool m_stabilizingVisibleContent = false;
	bool m_keepBottomPinned          = true;
	bool m_bottomSnapQueued          = false;
	int m_lastKnownScrollMaximum     = 0;
};

#endif // MUMBLE_MUMBLE_WIDGETS_PERSISTENTCHATLISTWIDGET_H_
