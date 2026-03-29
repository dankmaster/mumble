// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WIDGETS_PERSISTENTCHATMESSAGEGROUPWIDGET_H_
#define MUMBLE_MUMBLE_WIDGETS_PERSISTENTCHATMESSAGEGROUPWIDGET_H_

#include "CustomElements.h"
#include "Mumble.pb.h"

#include <QtCore/QDateTime>
#include <QtCore/QUrl>
#include <QtGui/QImage>
#include <QtGui/QResizeEvent>
#include <QtGui/QTextCursor>
#include <QtWidgets/QWidget>

class QLabel;
class QToolButton;
class QVBoxLayout;

struct PersistentChatGroupHeaderSpec {
	bool selfAuthored           = false;
	bool aggregateScope         = false;
	QString actorLabelHtml;
	QString timeLabel;
	QString timeToolTip;
	QString scopeLabel;
	MumbleProto::ChatScope scope = MumbleProto::Channel;
	unsigned int scopeID         = 0;
};

struct PersistentChatBubbleSpec {
	unsigned int messageID = 0;
	unsigned int threadID  = 0;
	QString bodyHtml;
	QString previewHtml;
	QVector< QPair< QUrl, QImage > > previewResources;
	bool hasReply          = false;
	unsigned int replyMessageID = 0;
	QString replyActor;
	QString replySnippet;
	QString timeToolTip;
	bool replyEnabled      = true;
	bool readOnlyAction    = false;
	QString actionText;
	MumbleProto::ChatScope actionScope = MumbleProto::Channel;
	unsigned int actionScopeID         = 0;
};

class PersistentChatMessageGroupWidget : public QWidget {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PersistentChatMessageGroupWidget)

public:
	explicit PersistentChatMessageGroupWidget(int availableWidth, const QString &baseStylesheet, QWidget *parent = nullptr);

	void setHeader(const PersistentChatGroupHeaderSpec &headerSpec, const QImage &avatarImage,
				   const QString &avatarFallbackText);
	void addBubble(const PersistentChatBubbleSpec &bubbleSpec);
	bool bubbleAnchorAtOffset(int offset, unsigned int &messageID, unsigned int &threadID, int &topOffset) const;
	bool bubbleTopOffset(unsigned int messageID, unsigned int threadID, int &topOffset) const;
	unsigned int firstMessageID() const;
	unsigned int lastMessageID() const;
	unsigned int lastThreadID() const;
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	void measuredHeightChanged(int height);
	void replyRequested(unsigned int messageID);
	void scopeJumpRequested(MumbleProto::ChatScope scope, unsigned int scopeID);
	void logContextMenuRequested(LogTextBrowser *browser, const QPoint &position);
	void logImageActivated(LogTextBrowser *browser, const QTextCursor &cursor);
	void anchorClicked(const QUrl &url);
	void highlighted(const QUrl &url);

protected:
	void resizeEvent(QResizeEvent *event) override;

private:
	int measuredHeight() const;
	void syncMeasuredHeight();

	QString m_baseStylesheet;
	int m_availableWidth = 0;
	bool m_selfAuthored  = false;
	int m_lastMeasuredHeight = -1;
	unsigned int m_firstMessageID = 0;
	unsigned int m_lastMessageID  = 0;
	unsigned int m_lastThreadID   = 0;
	struct BubbleEntry {
		unsigned int messageID = 0;
		unsigned int threadID  = 0;
		QWidget *widget        = nullptr;
	};
	QVector< BubbleEntry > m_bubbleEntries;

	QWidget *m_contentColumn = nullptr;
	QWidget *m_avatarFrame   = nullptr;
	QLabel *m_avatarLabel    = nullptr;
	QLabel *m_avatarFallbackLabel = nullptr;
	QWidget *m_headerWidget  = nullptr;
	QLabel *m_actorLabel     = nullptr;
	QLabel *m_timeLabel      = nullptr;
	QToolButton *m_scopeButton = nullptr;
	QVBoxLayout *m_bubblesLayout = nullptr;
};

#endif // MUMBLE_MUMBLE_WIDGETS_PERSISTENTCHATMESSAGEGROUPWIDGET_H_
