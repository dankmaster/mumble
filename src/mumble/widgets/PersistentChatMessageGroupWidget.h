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
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QResizeEvent>
#include <QtGui/QTextCursor>
#include <QtWidgets/QWidget>

class QLabel;
class QEvent;
class QHBoxLayout;
class QToolButton;
class QVBoxLayout;

enum class PersistentChatDisplayMode {
	Bubble,
	CompactTranscript
};

struct PersistentChatGroupHeaderSpec {
	bool selfAuthored           = false;
	bool aggregateScope         = false;
	bool systemMessage          = false;
	PersistentChatDisplayMode displayMode = PersistentChatDisplayMode::Bubble;
	QString actorLabel;
	QColor actorColor;
	QColor avatarForegroundColor;
	QColor avatarBackgroundColor;
	QString timeLabel;
	QString timeToolTip;
	QString scopeLabel;
	MumbleProto::ChatScope scope = MumbleProto::Channel;
	unsigned int scopeID         = 0;
};

enum class PersistentChatPreviewKind {
	None,
	LinkCard,
	Image
};

struct PersistentChatPreviewSpec {
	PersistentChatPreviewKind kind = PersistentChatPreviewKind::None;
	QUrl actionUrl;
	QString title;
	QString description;
	QString subtitle;
	QString statusText;
	QImage thumbnailImage;
	bool showThumbnailPlaceholder = false;
};

struct PersistentChatBubbleSpec {
	unsigned int messageID = 0;
	unsigned int threadID  = 0;
	PersistentChatDisplayMode displayMode = PersistentChatDisplayMode::Bubble;
	QString bodyHtml;
	QString previewKey;
	QVector< QPair< QUrl, QImage > > imageResources;
	bool selfAuthored      = false;
	PersistentChatPreviewSpec previewSpec;
	QString copyText;
	bool systemMessage     = false;
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
	QString transcriptActorLabel;
	QColor transcriptActorColor;
	QString transcriptTimeLabel;
};

enum class PersistentChatConversationLaneAnchor {
	Leading,
	Trailing,
	Center
};

struct PersistentChatConversationLaneMetrics {
	PersistentChatConversationLaneAnchor anchor = PersistentChatConversationLaneAnchor::Leading;
	int outerLeadingInset    = 0;
	int outerTrailingInset   = 0;
	int verticalInset        = 0;
	int leadingPresenceSlotWidth = 0;
	int trailingPresenceSlotWidth = 0;
	int conversationLaneWidth = 0;
	int bubbleMinWidth       = 0;
	int bubbleMaxWidth       = 0;
	int previewMinWidth      = 0;
	int previewMaxWidth      = 0;
	bool showLeadingAvatar   = false;
	bool showTrailingAvatar  = false;
};

class PersistentChatMessageGroupWidget : public QWidget {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PersistentChatMessageGroupWidget)

public:
	explicit PersistentChatMessageGroupWidget(int availableWidth, const QString &baseStylesheet, QWidget *parent = nullptr);

	void setHeader(const PersistentChatGroupHeaderSpec &headerSpec, const QString &avatarFallbackText);
	void addBubble(const PersistentChatBubbleSpec &bubbleSpec);
	bool updateBubblePreview(unsigned int messageID, unsigned int threadID, const PersistentChatPreviewSpec &previewSpec);
	bool bubbleAnchorAtOffset(int offset, unsigned int &messageID, unsigned int &threadID, int &topOffset) const;
	bool bubbleTopOffset(unsigned int messageID, unsigned int threadID, int &topOffset) const;
	unsigned int firstMessageID() const;
	unsigned int lastMessageID() const;
	unsigned int lastThreadID() const;
	bool hasHeightForWidth() const override;
	int heightForWidth(int width) const override;
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	void measuredHeightChanged(int height);
	void contentUpdated();
	void replyRequested(unsigned int messageID);
	void scopeJumpRequested(MumbleProto::ChatScope scope, unsigned int scopeID);
	void logContextMenuRequested(LogTextBrowser *browser, const QPoint &position);
	void logImageActivated(LogTextBrowser *browser, const QTextCursor &cursor);
	void anchorClicked(const QUrl &url);
	void highlighted(const QUrl &url);

protected:
	bool event(QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	bool hasActiveBubble() const;
	int measuredHeightForWidth(int width) const;
	int measuredHeight() const;
	PersistentChatConversationLaneMetrics conversationLaneMetricsForWidth(int width) const;
	void applyConversationLaneMetrics(const PersistentChatConversationLaneMetrics &metrics);
	void updateBubbleClusterShapes();
	void reevaluateRowActive();
	void setRowActive(bool active);
	void syncMeasuredHeight();

	QString m_baseStylesheet;
	int m_availableWidth = 0;
	bool m_systemMessage = false;
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

	QHBoxLayout *m_rowLayout = nullptr;
	QWidget *m_leadingSpacer = nullptr;
	QWidget *m_leadingPresenceSlot = nullptr;
	QWidget *m_trailingSpacer = nullptr;
	QWidget *m_contentColumn = nullptr;
	QWidget *m_leadingAvatarFrame = nullptr;
	QWidget *m_trailingPresenceSlot = nullptr;
	QWidget *m_trailingAvatarFrame = nullptr;
	QWidget *m_headerWidget  = nullptr;
	QHBoxLayout *m_headerLayout = nullptr;
	QWidget *m_headerLeadingSpacer = nullptr;
	QLabel *m_actorLabel     = nullptr;
	QLabel *m_timeLabel      = nullptr;
	QToolButton *m_scopeButton = nullptr;
	QWidget *m_headerTrailingSpacer = nullptr;
	QVBoxLayout *m_bubblesLayout = nullptr;
	PersistentChatConversationLaneMetrics m_laneMetrics;
	PersistentChatDisplayMode m_displayMode = PersistentChatDisplayMode::Bubble;
	bool m_selfAuthored      = false;
	bool m_rowActive         = false;
};

#endif // MUMBLE_MUMBLE_WIDGETS_PERSISTENTCHATMESSAGEGROUPWIDGET_H_
