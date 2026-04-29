// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATHISTORYDELEGATE_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATHISTORYDELEGATE_H_

#include "PersistentChatHistoryModel.h"

#include <QtCore/QPointer>
#include <QtGui/QPixmap>
#include <QtWidgets/QStyledItemDelegate>

class PersistentChatHistoryDelegate : public QStyledItemDelegate {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PersistentChatHistoryDelegate)

public:
	explicit PersistentChatHistoryDelegate(QObject *parent = nullptr);
	~PersistentChatHistoryDelegate() override;

	void clearCache();
	bool updateBubblePreview(const PersistentChatHistoryModel *model, unsigned int messageID, unsigned int threadID,
							 const PersistentChatPreviewSpec &previewSpec);

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
	bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
					 const QModelIndex &index) override;

signals:
	void loadOlderRequested();
	void previewRequested(const QString &previewKey);
	void replyRequested(unsigned int messageID);
	void deleteRequested(unsigned int messageID);
	void scopeJumpRequested(MumbleProto::ChatScope scope, unsigned int scopeID);
	void logContextMenuRequested(LogTextBrowser *browser, const QPoint &position);
	void logImageActivated(LogTextBrowser *browser, const QTextCursor &cursor);
	void anchorClicked(const QUrl &url);
	void highlighted(const QUrl &url);

private:
	struct WidgetCacheEntry {
		QString rowId;
		QString signature;
		int width       = -1;
		QSize measuredSize;
		QPixmap renderedPixmap;
		bool pixmapDirty = true;
		quint64 lastAccessSerial = 0;
		QPointer< QWidget > widget;
	};

	QWidget *widgetForIndex(const QModelIndex &index, int width, QWidget *host = nullptr) const;
	QWidget *createWidgetForRow(const PersistentChatHistoryRow &row, int width, QWidget *parent) const;
	void syncWidgetLayout(WidgetCacheEntry &cacheEntry, int width) const;
	void updateCachedWidgetHeight(const QString &rowId, int height);
	void invalidateCachedRendering(const QString &rowId);
	void invalidateAllCachedRendering() const;
	void touchCacheEntry(WidgetCacheEntry &cacheEntry) const;
	void pruneCachedWidgets(const QString &preserveRowId = QString()) const;
	bool forwardEditorEvent(QWidget *rootWidget, QEvent *event, const QStyleOptionViewItem &option) const;

	mutable QHash< QString, WidgetCacheEntry > m_widgetCache;
	mutable quint64 m_cacheAccessSerial = 0;
	mutable QPointer< QWidget > m_cacheHost;
};

#endif // MUMBLE_MUMBLE_PERSISTENTCHATHISTORYDELEGATE_H_
