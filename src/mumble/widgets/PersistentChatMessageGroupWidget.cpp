// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatMessageGroupWidget.h"

#include "Log.h"

#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QImageReader>
#include <QtGui/QMouseEvent>
#include <QtGui/QTextDocumentFragment>
#include <QtGui/QTextFrame>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStackedLayout>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#include <cmath>

namespace {
	QString persistentChatDocumentStylesheet(const QString &baseStylesheet) {
		return baseStylesheet
			   + QString::fromLatin1(
				   "html, body { margin: 0; padding: 0; border: 0; background: transparent; }"
				   "p { margin-top: 0; margin-bottom: 4px; }"
				   "table, tr, td { margin: 0; padding: 0; border: none; background: transparent; }"
				   "img { border: none; outline: none; display: block; margin: 0; background: transparent; }");
	}

	void configurePersistentChatDocument(QTextDocument *document, const QString &baseStylesheet) {
		if (!document) {
			return;
		}

		document->setDocumentMargin(0);
		document->setDefaultStyleSheet(persistentChatDocumentStylesheet(baseStylesheet));
		if (QTextFrame *rootFrame = document->rootFrame()) {
			QTextFrameFormat rootFrameFormat = rootFrame->frameFormat();
			rootFrameFormat.setBorder(0);
			rootFrameFormat.setMargin(0);
			rootFrameFormat.setPadding(0);
			rootFrame->setFrameFormat(rootFrameFormat);
		}
	}

	LogTextBrowser *createEmbeddedBrowser(const QString &html, int width, const QString &baseStylesheet,
										 const QVector< QPair< QUrl, QImage > > &imageResources = {}) {
		auto *browser   = new LogTextBrowser();
		auto *document  = new LogDocument(browser);
		browser->setDocument(document);
		browser->setFrameShape(QFrame::NoFrame);
		browser->setFrameStyle(QFrame::NoFrame);
		browser->setLineWidth(0);
		browser->setMidLineWidth(0);
		browser->setReadOnly(true);
		browser->setOpenLinks(false);
		browser->setFocusPolicy(Qt::NoFocus);
		browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		browser->setContextMenuPolicy(Qt::CustomContextMenu);
		browser->setProperty("persistentChatEmbeddedBrowser", true);
		browser->setAutoFillBackground(false);
		browser->resetViewportChrome();
		browser->setStyleSheet(QString::fromLatin1("background: transparent; border: none;"));
		if (QWidget *viewport = browser->viewport()) {
			viewport->setProperty("persistentChatEmbeddedBrowser", true);
			viewport->setAutoFillBackground(false);
			viewport->setStyleSheet(QString::fromLatin1("background: transparent; border: none;"));
		}
		configurePersistentChatDocument(document, baseStylesheet);
		document->setTextWidth(width);
		for (const auto &resource : imageResources) {
			if (resource.first.isValid() && !resource.second.isNull()) {
				document->addResource(QTextDocument::ImageResource, resource.first, resource.second);
			}
		}
		browser->setHtml(html);
		document->adjustSize();
		const int browserHeight = std::max(20, static_cast< int >(std::ceil(document->size().height())) + 4);
		browser->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		browser->setFixedSize(width, browserHeight);
		return browser;
	}

	class PersistentChatBubbleWidget : public QFrame {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(PersistentChatBubbleWidget)

	public:
	PersistentChatBubbleWidget(const PersistentChatBubbleSpec &bubbleSpec, int bubbleWidth, bool selfAuthored,
							   const QString &baseStylesheet, QWidget *parent = nullptr)
			: QFrame(parent), m_messageID(bubbleSpec.messageID), m_readOnlyAction(bubbleSpec.readOnlyAction),
			  m_actionScope(bubbleSpec.actionScope), m_actionScopeID(bubbleSpec.actionScopeID) {
			setObjectName(QLatin1String("qfPersistentChatBubbleContainer"));
			setProperty("bubbleSelf", selfAuthored);
			setAttribute(Qt::WA_StyledBackground, true);

			QVBoxLayout *outerLayout = new QVBoxLayout(this);
			outerLayout->setContentsMargins(0, 0, 0, 0);
			outerLayout->setSpacing(3);
			outerLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

			m_surface = new QFrame(this);
			m_surface->setObjectName(QLatin1String("qfPersistentChatBubble"));
			m_surface->setProperty("bubbleSelf", selfAuthored);
			m_surface->setToolTip(bubbleSpec.timeToolTip);
			m_surface->setAttribute(Qt::WA_StyledBackground, true);

			QVBoxLayout *surfaceLayout = new QVBoxLayout(m_surface);
			surfaceLayout->setContentsMargins(12, 9, 12, 10);
			surfaceLayout->setSpacing(6);
			surfaceLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

			const int contentWidth = std::max(120, bubbleWidth - 24);
			if (bubbleSpec.hasReply) {
				QFrame *replyFrame = new QFrame(m_surface);
				replyFrame->setObjectName(QLatin1String("qfPersistentChatBubbleQuote"));
				replyFrame->setAttribute(Qt::WA_StyledBackground, true);
				QVBoxLayout *replyLayout = new QVBoxLayout(replyFrame);
				replyLayout->setContentsMargins(10, 7, 10, 8);
				replyLayout->setSpacing(2);
				QLabel *replyActor = new QLabel(
					QString::fromLatin1("<strong>%1</strong>").arg(bubbleSpec.replyActor.toHtmlEscaped()), replyFrame);
				replyActor->setObjectName(QLatin1String("qlPersistentChatBubbleQuoteActor"));
				replyActor->setTextFormat(Qt::RichText);
				QLabel *replySnippet = new QLabel(bubbleSpec.replySnippet.toHtmlEscaped(), replyFrame);
				replySnippet->setObjectName(QLatin1String("qlPersistentChatBubbleQuoteSnippet"));
				replySnippet->setTextFormat(Qt::RichText);
				replySnippet->setWordWrap(true);
				replyLayout->addWidget(replyActor);
				replyLayout->addWidget(replySnippet);
				surfaceLayout->addWidget(replyFrame);
				registerTrackedObject(replyFrame);
				registerTrackedObject(replyActor);
				registerTrackedObject(replySnippet);
			}

			LogTextBrowser *bodyBrowser = createEmbeddedBrowser(bubbleSpec.bodyHtml, contentWidth, baseStylesheet);
			connect(bodyBrowser, &LogTextBrowser::anchorClicked, this, &PersistentChatBubbleWidget::anchorClicked);
			connect(bodyBrowser, QOverload< const QUrl & >::of(&QTextBrowser::highlighted), this,
					&PersistentChatBubbleWidget::highlighted);
			connect(bodyBrowser, &LogTextBrowser::customContextMenuRequested, this,
					[this, bodyBrowser](const QPoint &position) {
						activate();
						emit logContextMenuRequested(bodyBrowser, position);
					});
			connect(bodyBrowser, &LogTextBrowser::imageActivated, this,
					[this, bodyBrowser](const QTextCursor &cursor) { emit logImageActivated(bodyBrowser, cursor); });
			surfaceLayout->addWidget(bodyBrowser);
			registerTrackedObject(bodyBrowser);
			registerTrackedObject(bodyBrowser->viewport());

			if (!bubbleSpec.previewHtml.isEmpty()) {
				LogTextBrowser *previewBrowser =
					createEmbeddedBrowser(bubbleSpec.previewHtml, contentWidth, baseStylesheet, bubbleSpec.previewResources);
				previewBrowser->setObjectName(QLatin1String("qtePersistentChatPreviewBrowser"));
				connect(previewBrowser, &LogTextBrowser::anchorClicked, this, &PersistentChatBubbleWidget::anchorClicked);
				connect(previewBrowser, QOverload< const QUrl & >::of(&QTextBrowser::highlighted), this,
						&PersistentChatBubbleWidget::highlighted);
				connect(previewBrowser, &LogTextBrowser::customContextMenuRequested, this,
						[this, previewBrowser](const QPoint &position) {
							activate();
							emit logContextMenuRequested(previewBrowser, position);
						});
				connect(previewBrowser, &LogTextBrowser::imageActivated, this,
						[this, previewBrowser](const QTextCursor &cursor) { emit logImageActivated(previewBrowser, cursor); });
				surfaceLayout->addWidget(previewBrowser);
				registerTrackedObject(previewBrowser);
				registerTrackedObject(previewBrowser->viewport());
			}

			outerLayout->addWidget(m_surface, 0, selfAuthored ? Qt::AlignRight : Qt::AlignLeft);

			m_actions = new QWidget(this);
			m_actions->setObjectName(QLatin1String("qwPersistentChatBubbleActions"));
			m_actions->setAttribute(Qt::WA_StyledBackground, true);
			QHBoxLayout *actionsLayout = new QHBoxLayout(m_actions);
			actionsLayout->setContentsMargins(0, 0, 0, 0);
			actionsLayout->setSpacing(6);
			m_actions->hide();

			if (bubbleSpec.actionText.isEmpty()) {
				m_actionButton = nullptr;
			} else {
				m_actionButton = new QToolButton(m_actions);
				m_actionButton->setObjectName(QLatin1String("qtbPersistentChatBubbleAction"));
				m_actionButton->setAutoRaise(true);
				m_actionButton->setText(bubbleSpec.actionText);
				m_actionButton->setEnabled(bubbleSpec.replyEnabled);
				actionsLayout->addWidget(m_actionButton);
				if (selfAuthored) {
					actionsLayout->insertStretch(0, 1);
				} else {
					actionsLayout->addStretch(1);
				}
				connect(m_actionButton, &QToolButton::clicked, this, [this]() {
					if (m_readOnlyAction) {
						emit scopeJumpRequested(m_actionScope, m_actionScopeID);
					} else {
						emit replyRequested(m_messageID);
					}
				});
				registerTrackedObject(m_actionButton);
			}

			outerLayout->addWidget(m_actions, 0, selfAuthored ? Qt::AlignRight : Qt::AlignLeft);

			m_surface->setMaximumWidth(bubbleWidth);
			registerTrackedObject(this);
			registerTrackedObject(m_surface);
			registerTrackedObject(m_actions);
		}

	signals:
		void replyRequested(unsigned int messageID);
		void scopeJumpRequested(MumbleProto::ChatScope scope, unsigned int scopeID);
		void logContextMenuRequested(LogTextBrowser *browser, const QPoint &position);
		void logImageActivated(LogTextBrowser *browser, const QTextCursor &cursor);
		void anchorClicked(const QUrl &url);
		void highlighted(const QUrl &url);

	protected:
		bool eventFilter(QObject *watched, QEvent *event) override {
			Q_UNUSED(watched);
			if (!event) {
				return QFrame::eventFilter(watched, event);
			}

			switch (event->type()) {
				case QEvent::Enter:
				case QEvent::HoverEnter:
				case QEvent::FocusIn:
					activate();
					break;
				case QEvent::Leave:
				case QEvent::HoverLeave:
				case QEvent::FocusOut:
					QTimer::singleShot(0, this, &PersistentChatBubbleWidget::reevaluateActiveState);
					break;
				default:
					break;
			}

			return QFrame::eventFilter(watched, event);
		}

	private:
		unsigned int m_messageID = 0;
		bool m_readOnlyAction    = false;
		MumbleProto::ChatScope m_actionScope = MumbleProto::Channel;
		unsigned int m_actionScopeID = 0;
		bool m_active            = false;
		QFrame *m_surface        = nullptr;
		QWidget *m_actions       = nullptr;
		QToolButton *m_actionButton = nullptr;
		QList< QObject * > m_trackedObjects;

		void registerTrackedObject(QObject *object) {
			if (!object || m_trackedObjects.contains(object)) {
				return;
			}

			object->installEventFilter(this);
			if (QWidget *widget = qobject_cast< QWidget * >(object)) {
				widget->setAttribute(Qt::WA_Hover, true);
			}
			m_trackedObjects.push_back(object);
		}

		bool shouldStayActive() const {
			if (QWidget *focusWidget = QApplication::focusWidget()) {
				if (focusWidget == this || isAncestorOf(focusWidget)) {
					return true;
				}
			}

			for (QObject *object : m_trackedObjects) {
				if (QWidget *widget = qobject_cast< QWidget * >(object); widget && widget->underMouse()) {
					return true;
				}
			}

			return false;
		}

	private slots:
		void activate() {
			setActive(true);
		}

		void reevaluateActiveState() {
			setActive(shouldStayActive());
		}

		void setActive(bool active) {
			if (m_active == active) {
				return;
			}

			m_active = active;
			m_surface->setProperty("bubbleActive", active);
			m_surface->style()->unpolish(m_surface);
			m_surface->style()->polish(m_surface);
			if (m_actions) {
				m_actions->setVisible(active && m_actionButton && !m_actionButton->text().isEmpty());
			}
		}
	};
} // namespace

PersistentChatMessageGroupWidget::PersistentChatMessageGroupWidget(int availableWidth, const QString &baseStylesheet,
																   QWidget *parent)
	: QWidget(parent), m_baseStylesheet(baseStylesheet), m_availableWidth(availableWidth) {
	setObjectName(QLatin1String("qwPersistentChatMessageGroup"));
	setAttribute(Qt::WA_StyledBackground, true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

	QHBoxLayout *rowLayout = new QHBoxLayout(this);
	rowLayout->setContentsMargins(12, 3, 12, 3);
	rowLayout->setSpacing(8);
	rowLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

	m_avatarLabel = new QLabel(this);
	m_avatarLabel->setObjectName(QLatin1String("qlPersistentChatAvatar"));
	m_avatarLabel->setFixedSize(32, 32);
	m_avatarLabel->setScaledContents(true);

	m_avatarFallbackLabel = new QLabel(this);
	m_avatarFallbackLabel->setObjectName(QLatin1String("qlPersistentChatAvatarFallback"));
	m_avatarFallbackLabel->setAttribute(Qt::WA_StyledBackground, true);
	m_avatarFallbackLabel->setAlignment(Qt::AlignCenter);
	m_avatarFallbackLabel->setFixedSize(32, 32);
	m_avatarFallbackLabel->hide();

	m_avatarFrame = new QFrame(this);
	m_avatarFrame->setObjectName(QLatin1String("qfPersistentChatAvatarFrame"));
	m_avatarFrame->setAttribute(Qt::WA_StyledBackground, true);
	m_avatarFrame->setFixedSize(32, 32);
	QStackedLayout *avatarStack = new QStackedLayout(m_avatarFrame);
	avatarStack->setStackingMode(QStackedLayout::StackAll);
	avatarStack->setContentsMargins(0, 0, 0, 0);
	avatarStack->addWidget(m_avatarLabel);
	avatarStack->addWidget(m_avatarFallbackLabel);

	m_contentColumn = new QWidget(this);
	m_contentColumn->setObjectName(QLatin1String("qwPersistentChatMessageColumn"));
	m_contentColumn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	QVBoxLayout *columnLayout = new QVBoxLayout(m_contentColumn);
	columnLayout->setContentsMargins(0, 0, 0, 0);
	columnLayout->setSpacing(4);
	columnLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

	m_headerWidget = new QWidget(m_contentColumn);
	m_headerWidget->setObjectName(QLatin1String("qwPersistentChatGroupHeader"));
	QHBoxLayout *headerLayout = new QHBoxLayout(m_headerWidget);
	headerLayout->setContentsMargins(0, 0, 0, 0);
	headerLayout->setSpacing(6);

	m_actorLabel = new QLabel(m_headerWidget);
	m_actorLabel->setObjectName(QLatin1String("qlPersistentChatGroupActor"));
	m_actorLabel->setTextFormat(Qt::RichText);
	m_timeLabel = new QLabel(m_headerWidget);
	m_timeLabel->setObjectName(QLatin1String("qlPersistentChatGroupTime"));
	m_scopeButton = new QToolButton(m_headerWidget);
	m_scopeButton->setObjectName(QLatin1String("qtbPersistentChatGroupScope"));
	m_scopeButton->setAutoRaise(true);
	m_scopeButton->hide();

	headerLayout->addWidget(m_actorLabel);
	headerLayout->addWidget(m_scopeButton);
	headerLayout->addStretch(1);
	headerLayout->addWidget(m_timeLabel);

	columnLayout->addWidget(m_headerWidget);

	m_bubblesLayout = new QVBoxLayout();
	m_bubblesLayout->setContentsMargins(0, 0, 0, 0);
	m_bubblesLayout->setSpacing(3);
	columnLayout->addLayout(m_bubblesLayout);

	rowLayout->addWidget(m_avatarFrame, 0, Qt::AlignTop);
	rowLayout->addWidget(m_contentColumn, 0, Qt::AlignTop);
	rowLayout->addStretch(1);

	syncMeasuredHeight();
}

void PersistentChatMessageGroupWidget::setHeader(const PersistentChatGroupHeaderSpec &headerSpec, const QImage &avatarImage,
												 const QString &avatarFallbackText) {
	m_selfAuthored = headerSpec.selfAuthored;
	m_actorLabel->setText(headerSpec.actorLabelHtml);
	m_actorLabel->setVisible(!headerSpec.selfAuthored && !headerSpec.actorLabelHtml.isEmpty());
	m_timeLabel->setText(headerSpec.timeLabel.toHtmlEscaped());
	m_timeLabel->setToolTip(headerSpec.timeToolTip);

	if (headerSpec.aggregateScope && !headerSpec.scopeLabel.isEmpty()) {
		m_scopeButton->setText(headerSpec.scopeLabel);
		m_scopeButton->setToolTip(headerSpec.scopeLabel);
		m_scopeButton->show();
		m_scopeButton->disconnect();
		connect(m_scopeButton, &QToolButton::clicked, this,
				[this, headerSpec]() { emit scopeJumpRequested(headerSpec.scope, headerSpec.scopeID); });
	} else {
		m_scopeButton->hide();
	}

	QHBoxLayout *rowLayout = qobject_cast< QHBoxLayout * >(layout());
	if (rowLayout) {
		if (headerSpec.selfAuthored) {
			rowLayout->setDirection(QBoxLayout::RightToLeft);
		} else {
			rowLayout->setDirection(QBoxLayout::LeftToRight);
		}
	}

	if (m_avatarFrame) {
		m_avatarFrame->setVisible(!headerSpec.selfAuthored);
	}

	const bool hasAvatarImage = !avatarImage.isNull();
	m_avatarLabel->setVisible(!headerSpec.selfAuthored && hasAvatarImage);
	m_avatarFallbackLabel->setVisible(!headerSpec.selfAuthored && !hasAvatarImage);

	if (hasAvatarImage) {
		QPixmap avatarPixmap = QPixmap::fromImage(avatarImage);
		m_avatarLabel->setPixmap(avatarPixmap.scaled(m_avatarLabel->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
	}

	if (!hasAvatarImage) {
		m_avatarFallbackLabel->setText(avatarFallbackText.isEmpty() ? QStringLiteral("?") : avatarFallbackText.left(2).toUpper());
	}

	syncMeasuredHeight();
}

void PersistentChatMessageGroupWidget::addBubble(const PersistentChatBubbleSpec &bubbleSpec) {
	const int usableWidth = std::max(220, m_availableWidth - (m_selfAuthored ? 60 : 110));
	const int bubbleWidth = std::max(180, m_availableWidth < 420 ? usableWidth : static_cast< int >(std::floor(usableWidth * 0.72)));

	auto *bubble = new PersistentChatBubbleWidget(bubbleSpec, bubbleWidth, m_selfAuthored, m_baseStylesheet, this);
	connect(bubble, &PersistentChatBubbleWidget::replyRequested, this, &PersistentChatMessageGroupWidget::replyRequested);
	connect(bubble, &PersistentChatBubbleWidget::scopeJumpRequested, this,
			&PersistentChatMessageGroupWidget::scopeJumpRequested);
	connect(bubble, &PersistentChatBubbleWidget::logContextMenuRequested, this,
			&PersistentChatMessageGroupWidget::logContextMenuRequested);
	connect(bubble, &PersistentChatBubbleWidget::logImageActivated, this,
			&PersistentChatMessageGroupWidget::logImageActivated);
	connect(bubble, &PersistentChatBubbleWidget::anchorClicked, this, &PersistentChatMessageGroupWidget::anchorClicked);
	connect(bubble, &PersistentChatBubbleWidget::highlighted, this, &PersistentChatMessageGroupWidget::highlighted);
	m_bubblesLayout->addWidget(bubble, 0, m_selfAuthored ? Qt::AlignRight : Qt::AlignLeft);
	m_bubbleEntries.push_back(BubbleEntry { bubbleSpec.messageID, bubbleSpec.threadID, bubble });
	syncMeasuredHeight();
	updateGeometry();

	if (m_firstMessageID == 0) {
		m_firstMessageID = bubbleSpec.messageID;
	}
	m_lastMessageID = bubbleSpec.messageID;
	m_lastThreadID  = bubbleSpec.threadID;
}

bool PersistentChatMessageGroupWidget::bubbleAnchorAtOffset(int offset, unsigned int &messageID, unsigned int &threadID,
															int &topOffset) const {
	messageID = 0;
	threadID  = 0;
	topOffset = 0;

	if (m_bubbleEntries.isEmpty()) {
		return false;
	}

	if (QLayout *groupLayout = layout()) {
		groupLayout->activate();
	}
	if (QLayout *bubbleLayout = m_bubblesLayout) {
		bubbleLayout->activate();
	}

	const int localOffset = std::max(0, offset);
	const BubbleEntry *fallbackEntry = nullptr;
	int fallbackTopOffset            = 0;
	for (const BubbleEntry &entry : m_bubbleEntries) {
		if (!entry.widget) {
			continue;
		}

		const QPoint topLeft = entry.widget->mapTo(const_cast< PersistentChatMessageGroupWidget * >(this), QPoint(0, 0));
		const int bubbleTop  = topLeft.y();
		const int bubbleHeight =
			entry.widget->height() > 0 ? entry.widget->height() : entry.widget->sizeHint().height();
		fallbackEntry     = &entry;
		fallbackTopOffset = bubbleTop;
		if (bubbleTop + bubbleHeight > localOffset) {
			messageID = entry.messageID;
			threadID  = entry.threadID;
			topOffset = bubbleTop;
			return true;
		}
	}

	if (!fallbackEntry) {
		return false;
	}

	messageID = fallbackEntry->messageID;
	threadID  = fallbackEntry->threadID;
	topOffset = fallbackTopOffset;
	return true;
}

bool PersistentChatMessageGroupWidget::bubbleTopOffset(unsigned int messageID, unsigned int threadID, int &topOffset) const {
	topOffset = 0;

	if (QLayout *groupLayout = layout()) {
		groupLayout->activate();
	}
	if (QLayout *bubbleLayout = m_bubblesLayout) {
		bubbleLayout->activate();
	}

	for (const BubbleEntry &entry : m_bubbleEntries) {
		if (!entry.widget || entry.messageID != messageID || entry.threadID != threadID) {
			continue;
		}

		topOffset = entry.widget->mapTo(const_cast< PersistentChatMessageGroupWidget * >(this), QPoint(0, 0)).y();
		return true;
	}

	return false;
}

QSize PersistentChatMessageGroupWidget::sizeHint() const {
	const int hintHeight = measuredHeight();
	const int hintWidth  = std::max(width(), QWidget::sizeHint().width());
	return QSize(std::max(1, hintWidth), hintHeight);
}

QSize PersistentChatMessageGroupWidget::minimumSizeHint() const {
	const int hintHeight = measuredHeight();
	const int hintWidth  = std::max(1, QWidget::minimumSizeHint().width());
	return QSize(hintWidth, hintHeight);
}

void PersistentChatMessageGroupWidget::resizeEvent(QResizeEvent *event) {
	QWidget::resizeEvent(event);
	syncMeasuredHeight();
}

int PersistentChatMessageGroupWidget::measuredHeight() const {
	int heightHint = QWidget::minimumSizeHint().height();
	if (QLayout *groupLayout = layout()) {
		groupLayout->activate();
		heightHint = std::max(heightHint, groupLayout->sizeHint().height());
		heightHint = std::max(heightHint, groupLayout->minimumSize().height());
	}
	if (m_contentColumn) {
		heightHint = std::max(heightHint, m_contentColumn->sizeHint().height() + 6);
	}
	return std::max(1, heightHint);
}

void PersistentChatMessageGroupWidget::syncMeasuredHeight() {
	const int height = measuredHeight();
	if (m_lastMeasuredHeight == height) {
		return;
	}

	m_lastMeasuredHeight = height;
	setProperty("persistentChatItemHeight", height);
	updateGeometry();
	emit measuredHeightChanged(height);
}

unsigned int PersistentChatMessageGroupWidget::firstMessageID() const {
	return m_firstMessageID;
}

unsigned int PersistentChatMessageGroupWidget::lastMessageID() const {
	return m_lastMessageID;
}

unsigned int PersistentChatMessageGroupWidget::lastThreadID() const {
	return m_lastThreadID;
}

#include "PersistentChatMessageGroupWidget.moc"
