// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATHISTORYDELEGATE_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATHISTORYDELEGATE_H_

#include "PersistentChatHistoryModel.h"

#include <QtWidgets/QStyledItemDelegate>

class PersistentChatHistoryDelegate : public QStyledItemDelegate {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PersistentChatHistoryDelegate)

public:
	explicit PersistentChatHistoryDelegate(QObject *parent = nullptr);
	~PersistentChatHistoryDelegate() override;

	void clearCache();

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
	bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
					 const QModelIndex &index) override;

signals:
	void loadOlderRequested();
	void replyRequested(unsigned int messageID);
	void scopeJumpRequested(MumbleProto::ChatScope scope, unsigned int scopeID);
	void logContextMenuRequested(LogTextBrowser *browser, const QPoint &position);
	void logImageActivated(LogTextBrowser *browser, const QTextCursor &cursor);
	void anchorClicked(const QUrl &url);
	void highlighted(const QUrl &url);

private:
	struct WidgetCacheEntry {
		QString rowId;
		QString signature;
		int width      = -1;
		QWidget *widget = nullptr;
	};

	QWidget *widgetForIndex(const QModelIndex &index, int width) const;
	QWidget *createWidgetForRow(const PersistentChatHistoryRow &row, int width) const;
	bool forwardEditorEvent(QWidget *rootWidget, QEvent *event, const QStyleOptionViewItem &option) const;

	mutable QHash< QString, WidgetCacheEntry > m_widgetCache;
};

#endif // MUMBLE_MUMBLE_PERSISTENTCHATHISTORYDELEGATE_H_
