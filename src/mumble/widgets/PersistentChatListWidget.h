// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WIDGETS_PERSISTENTCHATLISTWIDGET_H_
#define MUMBLE_MUMBLE_WIDGETS_PERSISTENTCHATLISTWIDGET_H_

#include <QtWidgets/QListWidget>

class PersistentChatListWidget : public QListWidget {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PersistentChatListWidget)

public:
	explicit PersistentChatListWidget(QWidget *parent = nullptr);

	bool isScrolledToBottom() const;
	void stabilizeVisibleContent();

signals:
	void contentWidthChanged(int width);

protected:
	void resizeEvent(QResizeEvent *event) override;
	void scrollContentsBy(int dx, int dy) override;

private:
	bool m_stabilizingVisibleContent = false;
};

#endif // MUMBLE_MUMBLE_WIDGETS_PERSISTENTCHATLISTWIDGET_H_
