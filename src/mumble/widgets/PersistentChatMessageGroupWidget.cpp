// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatMessageGroupWidget.h"

#include "Log.h"
#include "UiTheme.h"

#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QClipboard>
#include <QtGui/QMouseEvent>
#include <QtGui/QPixmap>
#include <QtGui/QTextDocumentFragment>
#include <QtGui/QTextFrame>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#include <cmath>

namespace {
	constexpr int PersistentChatGroupHorizontalPadding = 16;
	constexpr int PersistentChatGroupVerticalPadding   = 6;
	constexpr int PersistentChatAvatarGap              = 8;
	constexpr int PersistentChatAvatarSize             = 32;
	constexpr int PersistentChatGroupedBubbleSpacing   = 2;
	constexpr int PersistentChatPreviewBottomSpacing   = 2;
	constexpr int PersistentChatInlineImageMaxWidth    = 480;
	constexpr int PersistentChatInlineImageMaxHeight   = 320;
	constexpr int PersistentChatEmbeddedBrowserDocumentMargin = 1;
	constexpr int PersistentChatEmbeddedBrowserHorizontalSlack = 6;
	constexpr int PersistentChatEmbeddedBrowserVerticalSlack   = 4;
	constexpr int PersistentChatBubbleHorizontalPadding = 12;
	constexpr int PersistentChatBubbleVerticalPadding   = 8;
	constexpr int PersistentChatLinkPreviewMinWidth     = 240;
	constexpr int PersistentChatLinkPreviewMaxWidth     = 520;
	constexpr int PersistentChatLinkPreviewPadding      = 10;
	constexpr int PersistentChatLinkPreviewSpacing      = 12;
	constexpr int PersistentChatLinkPreviewThumbMinWidth = 92;
	constexpr int PersistentChatLinkPreviewThumbMaxWidth = 120;

	bool persistentChatHtmlContainsInlineImage(const QString &html) {
		return html.contains(QLatin1String("<img"), Qt::CaseInsensitive);
	}

	bool persistentChatHtmlHasNonImageText(QString html) {
		if (html.isEmpty()) {
			return false;
		}

		static const QRegularExpression s_imgTagPattern(QLatin1String("<img\\b[^>]*>"),
														QRegularExpression::CaseInsensitiveOption);
		html.remove(s_imgTagPattern);
		return !QTextDocumentFragment::fromHtml(html).toPlainText().trimmed().isEmpty();
	}

	QString persistentChatDocumentStylesheet(const QString &baseStylesheet) {
		return baseStylesheet
			   + QString::fromLatin1(
				   "html, body { margin: 0; padding: 0; border: 0; background: transparent; }"
				   "body, table, tr, td, div, span, p { font-size: 0.875em; line-height: 1.35; }"
				   "p { margin: 0; }"
				   "p + p { margin-top: 6px; }"
				   "table, tr, td { margin: 0; padding: 0; border: none; background: transparent; }"
				   "img { border: none; outline: none; display: block; margin: 0; max-width: %1px; max-height: %2px;"
				   " width: auto; height: auto; background: transparent; }")
					 .arg(PersistentChatInlineImageMaxWidth)
					 .arg(PersistentChatInlineImageMaxHeight);
	}

	void configurePersistentChatDocument(QTextDocument *document, const QString &baseStylesheet) {
		if (!document) {
			return;
		}

		document->setDocumentMargin(PersistentChatEmbeddedBrowserDocumentMargin);
		document->setDefaultStyleSheet(persistentChatDocumentStylesheet(baseStylesheet));
		if (QTextFrame *rootFrame = document->rootFrame()) {
			QTextFrameFormat rootFrameFormat = rootFrame->frameFormat();
			rootFrameFormat.setBorder(0);
			rootFrameFormat.setMargin(0);
			rootFrameFormat.setPadding(0);
			rootFrame->setFrameFormat(rootFrameFormat);
		}
	}

	QSize persistentChatDocumentSize(QTextDocument *document, int maxWidth, bool preferMaxWidth = false) {
		const int clampedMaxWidth = std::max(1, maxWidth);
		if (!document) {
			return QSize(clampedMaxWidth, 20);
		}

		document->setTextWidth(clampedMaxWidth);
		document->adjustSize();

		int measuredWidth =
			preferMaxWidth
				? clampedMaxWidth
				: static_cast< int >(std::ceil(document->idealWidth())) + PersistentChatEmbeddedBrowserHorizontalSlack;
		measuredWidth     = qBound(1, measuredWidth, clampedMaxWidth);

		document->setTextWidth(measuredWidth);
		document->adjustSize();
		const int measuredHeight =
			std::max(20, static_cast< int >(std::ceil(document->size().height())) + PersistentChatEmbeddedBrowserVerticalSlack);
		return QSize(measuredWidth, measuredHeight);
	}

	LogTextBrowser *createEmbeddedBrowser(const QString &html, int width, const QString &baseStylesheet,
										 const QVector< QPair< QUrl, QImage > > &imageResources = {}) {
		const bool containsInlineImage = persistentChatHtmlContainsInlineImage(html);
		const bool preferMaxWidth = containsInlineImage && persistentChatHtmlHasNonImageText(html);
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
		browser->setProperty("persistentChatMaxWidth", width);
		browser->setProperty("persistentChatPreferMaxWidth", preferMaxWidth);
		browser->setProperty("persistentChatContainsInlineImage", containsInlineImage);
		browser->setAutoFillBackground(false);
		browser->resetViewportChrome();
		browser->setStyleSheet(QString::fromLatin1("background: transparent; border: none; margin: 0px; padding: 0px;"));
		if (QWidget *viewport = browser->viewport()) {
			viewport->setProperty("persistentChatEmbeddedBrowser", true);
			viewport->setAutoFillBackground(false);
			viewport->setStyleSheet(
				QString::fromLatin1("background: transparent; border: none; margin: 0px; padding: 0px;"));
		}
		configurePersistentChatDocument(document, baseStylesheet);
		document->setTextWidth(width);
		for (const auto &resource : imageResources) {
			if (resource.first.isValid() && !resource.second.isNull()) {
				document->addResource(QTextDocument::ImageResource, resource.first, resource.second);
			}
		}
		browser->setHtml(html);
		const QSize measuredSize = persistentChatDocumentSize(document, width, preferMaxWidth);
		browser->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		browser->setFixedSize(measuredSize);
		return browser;
	}

	class PersistentChatPreviewWidget : public QFrame {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(PersistentChatPreviewWidget)

	public:
		PersistentChatPreviewWidget(const PersistentChatPreviewSpec &previewSpec, int contentWidth, QWidget *parent = nullptr)
			: QFrame(parent), m_actionUrl(previewSpec.actionUrl) {
			setAttribute(Qt::WA_StyledBackground, true);
			setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
			if (m_actionUrl.isValid()) {
				setCursor(Qt::PointingHandCursor);
			}

			QColor cardBackground = palette().color(QPalette::Base);
			QColor accentColor    = palette().color(QPalette::Highlight);
			QColor titleColor     = palette().color(QPalette::WindowText);
			QColor bodyColor      = palette().color(QPalette::Text);
			QColor mutedColor     = palette().color(QPalette::Mid);
			QColor placeholderColor = palette().color(QPalette::AlternateBase);
			if (const std::optional< UiThemeTokens > tokens = activeUiThemeTokens(); tokens) {
				cardBackground   = tokens->surface0;
				accentColor      = tokens->accent;
				titleColor       = tokens->text;
				bodyColor        = tokens->subtext0;
				mutedColor       = tokens->overlay0;
				placeholderColor = tokens->surface1;
				placeholderColor.setAlphaF(0.7f);
			}

			if (previewSpec.kind == PersistentChatPreviewKind::Image) {
				setObjectName(QLatin1String("qfPersistentChatImagePreview"));
				setStyleSheet(QString::fromLatin1("QFrame#qfPersistentChatImagePreview { background: transparent; border: none; }"));

				QVBoxLayout *layout = new QVBoxLayout(this);
				layout->setContentsMargins(0, 0, 0, 0);
				layout->setSpacing(4);
				layout->setSizeConstraint(QLayout::SetFixedSize);

				if (!previewSpec.thumbnailImage.isNull()) {
					QLabel *imageLabel = new QLabel(this);
					imageLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
					imageLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
					const QPixmap pixmap = QPixmap::fromImage(previewSpec.thumbnailImage)
											   .scaled(std::min(contentWidth, PersistentChatInlineImageMaxWidth),
												   PersistentChatInlineImageMaxHeight, Qt::KeepAspectRatio,
												   Qt::SmoothTransformation);
					imageLabel->setPixmap(pixmap);
					imageLabel->setFixedSize(pixmap.size());
					imageLabel->setStyleSheet(QString::fromLatin1("background: transparent; border-radius: 6px;"));
					layout->addWidget(imageLabel, 0, Qt::AlignLeft);
				} else {
					QFrame *placeholder = new QFrame(this);
					placeholder->setAttribute(Qt::WA_TransparentForMouseEvents, true);
					placeholder->setFixedSize(std::min(contentWidth, 240), 180);
					placeholder->setStyleSheet(
						QString::fromLatin1("background: %1; border-radius: 6px;").arg(placeholderColor.name(QColor::HexArgb)));
					layout->addWidget(placeholder, 0, Qt::AlignLeft);
				}

				if (!previewSpec.statusText.trimmed().isEmpty()) {
					QLabel *statusLabel = new QLabel(previewSpec.statusText, this);
					statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
					statusLabel->setWordWrap(true);
					statusLabel->setStyleSheet(
						QString::fromLatin1("color: %1; font-size: 11px;").arg(mutedColor.name()));
					statusLabel->setMaximumWidth(std::min(contentWidth, PersistentChatInlineImageMaxWidth));
					layout->addWidget(statusLabel, 0, Qt::AlignLeft);
				}
			} else if (previewSpec.kind == PersistentChatPreviewKind::LinkCard) {
				const int cardWidth = std::max(PersistentChatLinkPreviewMinWidth,
											  std::min(PersistentChatLinkPreviewMaxWidth, contentWidth));
				const bool showThumbnail = !previewSpec.thumbnailImage.isNull() || previewSpec.showThumbnailPlaceholder;
				const int thumbWidth =
					showThumbnail
						? qBound(PersistentChatLinkPreviewThumbMinWidth, cardWidth / 4,
								 PersistentChatLinkPreviewThumbMaxWidth)
						: 0;
				const int thumbHeight = thumbWidth > 0 ? std::max(52, (thumbWidth * 9) / 16) : 0;
				const int textColumnWidth =
					std::max(120, cardWidth - (PersistentChatLinkPreviewPadding * 2)
									  - (showThumbnail ? thumbWidth + PersistentChatLinkPreviewSpacing : 0));
				setObjectName(QLatin1String("qfPersistentChatLinkPreview"));
				setFixedWidth(cardWidth);
				setStyleSheet(QString::fromLatin1(
								  "QFrame#qfPersistentChatLinkPreview {"
								  " background: %1;"
								  " border: none;"
								  " border-left: 3px solid %2;"
								  " border-radius: 4px;"
								  "}").arg(cardBackground.name(QColor::HexArgb), accentColor.name()));

				QHBoxLayout *layout = new QHBoxLayout(this);
				layout->setContentsMargins(PersistentChatLinkPreviewPadding, PersistentChatLinkPreviewPadding,
										   PersistentChatLinkPreviewPadding, PersistentChatLinkPreviewPadding);
				layout->setSpacing(PersistentChatLinkPreviewSpacing);

				if (showThumbnail) {
					QFrame *thumbFrame = new QFrame(this);
					thumbFrame->setAttribute(Qt::WA_TransparentForMouseEvents, true);
					thumbFrame->setFixedSize(thumbWidth, thumbHeight);
					thumbFrame->setStyleSheet(QString::fromLatin1("background: %1; border-radius: 3px;")
												 .arg(placeholderColor.name(QColor::HexArgb)));
					QHBoxLayout *thumbLayout = new QHBoxLayout(thumbFrame);
					thumbLayout->setContentsMargins(0, 0, 0, 0);
					thumbLayout->setSpacing(0);
					if (!previewSpec.thumbnailImage.isNull()) {
						QLabel *thumbLabel = new QLabel(thumbFrame);
						thumbLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
						thumbLabel->setAlignment(Qt::AlignCenter);
						const QPixmap pixmap = QPixmap::fromImage(previewSpec.thumbnailImage)
												   .scaled(thumbWidth, thumbHeight, Qt::KeepAspectRatio,
														   Qt::SmoothTransformation);
						thumbLabel->setPixmap(pixmap);
						thumbLabel->setFixedSize(pixmap.size());
						thumbLabel->setStyleSheet(QString::fromLatin1("background: transparent; border-radius: 3px;"));
						thumbLayout->addWidget(thumbLabel, 0, Qt::AlignCenter);
					}
					layout->addWidget(thumbFrame, 0, Qt::AlignTop);
				}

				QWidget *textColumn = new QWidget(this);
				textColumn->setAttribute(Qt::WA_TransparentForMouseEvents, true);
				textColumn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
				textColumn->setFixedWidth(textColumnWidth);
				QVBoxLayout *textLayout = new QVBoxLayout(textColumn);
				textLayout->setContentsMargins(0, 0, 0, 0);
				textLayout->setSpacing(4);

				QLabel *titleLabel = new QLabel(previewSpec.title, textColumn);
				titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
				titleLabel->setWordWrap(true);
				titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
				titleLabel->setMaximumWidth(textColumnWidth);
				titleLabel->setStyleSheet(
					QString::fromLatin1("color: %1; font-size: 13px; font-weight: 600;").arg(titleColor.name()));
				textLayout->addWidget(titleLabel);

				if (!previewSpec.description.trimmed().isEmpty()) {
					QLabel *descriptionLabel = new QLabel(previewSpec.description, textColumn);
					descriptionLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
					descriptionLabel->setWordWrap(true);
					descriptionLabel->setMaximumWidth(textColumnWidth);
					descriptionLabel->setStyleSheet(
						QString::fromLatin1("color: %1; font-size: 12px;").arg(bodyColor.name()));
					textLayout->addWidget(descriptionLabel);
				}

				QLabel *subtitleLabel = new QLabel(previewSpec.subtitle, textColumn);
				subtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
				subtitleLabel->setWordWrap(true);
				subtitleLabel->setMaximumWidth(textColumnWidth);
				subtitleLabel->setStyleSheet(
					QString::fromLatin1("color: %1; font-size: 11px;").arg(mutedColor.name()));
				textLayout->addWidget(subtitleLabel);

				if (!previewSpec.statusText.trimmed().isEmpty()) {
					QLabel *statusLabel = new QLabel(previewSpec.statusText, textColumn);
					statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
					statusLabel->setWordWrap(true);
					statusLabel->setMaximumWidth(textColumnWidth);
					statusLabel->setStyleSheet(
						QString::fromLatin1("color: %1; font-size: 11px;").arg(mutedColor.name()));
					textLayout->addWidget(statusLabel);
				}

				layout->addWidget(textColumn, 1);
			}

			if (QLayout *widgetLayout = layout()) {
				widgetLayout->activate();
			}
			adjustSize();
			setFixedSize(sizeHint());
		}

	signals:
		void activated(const QUrl &url);

	protected:
		void mouseReleaseEvent(QMouseEvent *event) override {
			if (event && event->button() == Qt::LeftButton && m_actionUrl.isValid() && rect().contains(event->pos())) {
				emit activated(m_actionUrl);
				event->accept();
				return;
			}

			QFrame::mouseReleaseEvent(event);
		}

	private:
		QUrl m_actionUrl;
	};

	class PersistentChatBubbleWidget : public QFrame {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(PersistentChatBubbleWidget)

	public:
		PersistentChatBubbleWidget(const PersistentChatBubbleSpec &bubbleSpec, int contentWidth,
								   const QString &baseStylesheet, QWidget *parent = nullptr)
			: QFrame(parent), m_messageID(bubbleSpec.messageID), m_readOnlyAction(bubbleSpec.readOnlyAction),
			  m_actionScope(bubbleSpec.actionScope), m_actionScopeID(bubbleSpec.actionScopeID),
			  m_baseStylesheet(baseStylesheet), m_contentWidth(contentWidth), m_copyText(bubbleSpec.copyText),
			  m_systemMessage(bubbleSpec.systemMessage), m_actionText(bubbleSpec.actionText),
			  m_selfAuthored(bubbleSpec.selfAuthored) {
			const Qt::Alignment bubbleContentAlignment =
				m_systemMessage ? Qt::AlignHCenter : (m_selfAuthored ? Qt::AlignRight : Qt::AlignLeft);
			setObjectName(QLatin1String("qfPersistentChatBubbleContainer"));
			setAttribute(Qt::WA_StyledBackground, true);

			QVBoxLayout *outerLayout = new QVBoxLayout(this);
			outerLayout->setContentsMargins(0, 0, 0, 0);
			outerLayout->setSpacing(0);
			outerLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

			m_surface = new QFrame(this);
			m_surface->setObjectName(QLatin1String("qfPersistentChatBubble"));
			m_surface->setProperty("persistentChatSystem", bubbleSpec.systemMessage);
			m_surface->setProperty("selfAuthored", bubbleSpec.selfAuthored);
			m_surface->setProperty("bubbleActive", false);
			m_surface->setToolTip(bubbleSpec.timeToolTip);
			m_surface->setAttribute(Qt::WA_StyledBackground, true);

			m_surfaceLayout = new QVBoxLayout(m_surface);
			m_surfaceLayout->setContentsMargins(bubbleSpec.systemMessage ? 0 : PersistentChatBubbleHorizontalPadding,
											 bubbleSpec.systemMessage ? 0 : PersistentChatBubbleVerticalPadding,
											 bubbleSpec.systemMessage ? 0 : PersistentChatBubbleHorizontalPadding,
											 bubbleSpec.systemMessage ? 0 : PersistentChatBubbleVerticalPadding);
			m_surfaceLayout->setSpacing(bubbleSpec.systemMessage ? 0 : 6);
			m_surfaceLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

			if (bubbleSpec.hasReply) {
				m_replyFrame = new QFrame(m_surface);
				m_replyFrame->setObjectName(QLatin1String("qfPersistentChatBubbleQuote"));
				m_replyFrame->setAttribute(Qt::WA_StyledBackground, true);
				QVBoxLayout *replyLayout = new QVBoxLayout(m_replyFrame);
				replyLayout->setContentsMargins(10, 6, 10, 6);
				replyLayout->setSpacing(2);
				QLabel *replyActor = new QLabel(bubbleSpec.replyActor.toHtmlEscaped(), m_replyFrame);
				replyActor->setObjectName(QLatin1String("qlPersistentChatBubbleQuoteActor"));
				replyActor->setTextFormat(Qt::RichText);
				QLabel *replySnippet = new QLabel(bubbleSpec.replySnippet.toHtmlEscaped(), m_replyFrame);
				replySnippet->setObjectName(QLatin1String("qlPersistentChatBubbleQuoteSnippet"));
				replySnippet->setTextFormat(Qt::RichText);
				replySnippet->setWordWrap(true);
				replyLayout->addWidget(replyActor);
				replyLayout->addWidget(replySnippet);
				m_surfaceLayout->addWidget(m_replyFrame, 0, bubbleContentAlignment);
				registerTrackedObject(m_replyFrame);
				registerTrackedObject(replyActor);
				registerTrackedObject(replySnippet);
			}

			m_bodyBrowser = createEmbeddedBrowser(bubbleSpec.bodyHtml, m_contentWidth, m_baseStylesheet);
			m_bodyBrowser->setObjectName(QLatin1String("qtePersistentChatBodyBrowser"));
			connectBrowser(m_bodyBrowser);
			m_surfaceLayout->addWidget(m_bodyBrowser, 0, bubbleContentAlignment);

			outerLayout->addWidget(m_surface, 0, bubbleContentAlignment);

			if (!bubbleSpec.systemMessage && (!bubbleSpec.actionText.isEmpty() || !bubbleSpec.copyText.trimmed().isEmpty())) {
				m_actions = new QWidget(this);
				m_actions->setObjectName(QLatin1String("qwPersistentChatBubbleActions"));
				m_actions->setAttribute(Qt::WA_StyledBackground, true);
				QHBoxLayout *actionsLayout = new QHBoxLayout(m_actions);
				actionsLayout->setContentsMargins(4, 2, 4, 2);
				actionsLayout->setSpacing(2);

				if (!bubbleSpec.actionText.isEmpty()) {
					m_actionButton = new QToolButton(m_actions);
					m_actionButton->setObjectName(QLatin1String("qtbPersistentChatBubbleAction"));
					m_actionButton->setAutoRaise(true);
					m_actionButton->setToolTip(bubbleSpec.actionText);
					m_actionButton->setText(bubbleSpec.readOnlyAction ? QStringLiteral("↗") : QStringLiteral("⤶"));
					m_actionButton->setEnabled(bubbleSpec.replyEnabled);
					actionsLayout->addWidget(m_actionButton);
					connect(m_actionButton, &QToolButton::clicked, this, [this]() {
						if (m_readOnlyAction) {
							emit scopeJumpRequested(m_actionScope, m_actionScopeID);
						} else {
							emit replyRequested(m_messageID);
						}
					});
					registerTrackedObject(m_actionButton);
				}

				m_moreButton = new QToolButton(m_actions);
				m_moreButton->setObjectName(QLatin1String("qtbPersistentChatBubbleAction"));
				m_moreButton->setAutoRaise(true);
				m_moreButton->setToolTip(tr("More"));
				m_moreButton->setText(QStringLiteral("⋯"));
				actionsLayout->addWidget(m_moreButton);
				connect(m_moreButton, &QToolButton::clicked, this, &PersistentChatBubbleWidget::showMoreMenu);
				registerTrackedObject(m_moreButton);

				m_actions->hide();
				m_actions->raise();
				registerTrackedObject(m_actions);
			}

			setPreviewContent(bubbleSpec.previewSpec);
			registerTrackedObject(this);
			registerTrackedObject(m_surface);
		}

		bool isActive() const {
			return m_active;
		}

		bool setPreviewContent(const PersistentChatPreviewSpec &previewSpec) {
			const bool hadPreview = m_previewWidget != nullptr || m_previewContainer != nullptr;
			if (m_previewContainer) {
				m_surfaceLayout->removeWidget(m_previewContainer);
				delete m_previewContainer;
				m_previewContainer = nullptr;
				m_previewWidget = nullptr;
			}

			if (previewSpec.kind == PersistentChatPreviewKind::None) {
				updateGeometry();
				repositionActions();
				return hadPreview;
			}

			m_previewContainer = new QWidget(m_surface);
			m_previewContainer->setAttribute(Qt::WA_StyledBackground, false);
			m_previewContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
			QVBoxLayout *previewLayout = new QVBoxLayout(m_previewContainer);
			previewLayout->setContentsMargins(0, 0, 0, PersistentChatPreviewBottomSpacing);
			previewLayout->setSpacing(0);
			previewLayout->setSizeConstraint(QLayout::SetFixedSize);

			m_previewWidget = new PersistentChatPreviewWidget(previewSpec, m_contentWidth, m_previewContainer);
			connect(m_previewWidget, SIGNAL(activated(QUrl)), this, SIGNAL(anchorClicked(QUrl)));
			const Qt::Alignment previewAlignment =
				m_systemMessage ? Qt::AlignHCenter : (m_selfAuthored ? Qt::AlignRight : Qt::AlignLeft);
			previewLayout->addWidget(m_previewWidget, 0, previewAlignment);
			registerWidgetTree(m_previewContainer);
			m_surfaceLayout->addWidget(m_previewContainer, 0, previewAlignment);
			updateGeometry();
			repositionActions();
			return true;
		}

	signals:
		void activeChanged(bool active);
		void contentSizeChanged();
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

		void resizeEvent(QResizeEvent *event) override {
			QFrame::resizeEvent(event);
			repositionActions();
		}

	private:
		unsigned int m_messageID = 0;
		bool m_readOnlyAction    = false;
		MumbleProto::ChatScope m_actionScope = MumbleProto::Channel;
		unsigned int m_actionScopeID = 0;
		QString m_baseStylesheet;
		int m_contentWidth       = 0;
		QString m_copyText;
		bool m_systemMessage     = false;
		QString m_actionText;
		bool m_active            = false;
		QFrame *m_surface        = nullptr;
		QVBoxLayout *m_surfaceLayout = nullptr;
		QWidget *m_replyFrame    = nullptr;
		LogTextBrowser *m_bodyBrowser = nullptr;
		QWidget *m_previewContainer = nullptr;
		QWidget *m_previewWidget = nullptr;
		QWidget *m_actions       = nullptr;
		QToolButton *m_actionButton = nullptr;
		QToolButton *m_moreButton = nullptr;
		bool m_selfAuthored      = false;
		bool m_embeddedBrowserResizePending = false;
		bool m_embeddedBrowserResizeInProgress = false;
		bool m_embeddedBrowserResizeDeferred = false;
		QList< QPointer< QObject > > m_trackedObjects;

		void scheduleEmbeddedBrowserResize(LogTextBrowser *browser) {
			if (!browser) {
				return;
			}

			if (m_embeddedBrowserResizeInProgress) {
				m_embeddedBrowserResizeDeferred = true;
				return;
			}

			if (m_embeddedBrowserResizePending) {
				return;
			}

			m_embeddedBrowserResizePending = true;
			QPointer< LogTextBrowser > guardedBrowser(browser);
			QTimer::singleShot(0, this, [this, guardedBrowser]() {
				m_embeddedBrowserResizePending = false;
				if (!guardedBrowser) {
					return;
				}

				refreshEmbeddedBrowserSize(guardedBrowser.data());
			});
		}

		void refreshEmbeddedBrowserSize(LogTextBrowser *browser, bool notify = true) {
			if (!browser || !browser->document()) {
				return;
			}
			if (m_embeddedBrowserResizeInProgress) {
				m_embeddedBrowserResizeDeferred = true;
				return;
			}

			m_embeddedBrowserResizeInProgress = true;
			m_embeddedBrowserResizeDeferred   = false;

			const int maxWidth = std::max(1, browser->property("persistentChatMaxWidth").toInt());
			const bool preferMaxWidth = browser->property("persistentChatPreferMaxWidth").toBool();
			const bool containsInlineImage = browser->property("persistentChatContainsInlineImage").toBool();
			const auto measureBrowserSize = [&](int widthLimit) {
				QSize measuredSize =
					persistentChatDocumentSize(browser->document(), std::max(1, widthLimit), preferMaxWidth);
				if (containsInlineImage) {
					measuredSize.rheight() += 8;
				}
				return measuredSize;
			};

			QSize adjustedSize = measureBrowserSize(maxWidth);
			const QSize previousSize = browser->size();

			for (int pass = 0; pass < 4; ++pass) {
				browser->setFixedSize(adjustedSize);

				const int horizontalOverflow =
					browser->horizontalScrollBar() ? browser->horizontalScrollBar()->maximum() : 0;
				const int verticalOverflow = browser->verticalScrollBar() ? browser->verticalScrollBar()->maximum() : 0;
				if (horizontalOverflow <= 0 && verticalOverflow <= 0) {
					break;
				}

				const int nextWidth = std::min(maxWidth,
											   adjustedSize.width() + horizontalOverflow
												   + PersistentChatEmbeddedBrowserHorizontalSlack);
				QSize nextSize      = measureBrowserSize(nextWidth);
				nextSize.setWidth(std::max(adjustedSize.width(), nextSize.width()));
				if (verticalOverflow > 0) {
					nextSize.rheight() += verticalOverflow + PersistentChatEmbeddedBrowserVerticalSlack;
				}
				if (nextSize == adjustedSize) {
					break;
				}

				adjustedSize = nextSize;
			}

			if (browser->size() != adjustedSize) {
				browser->setFixedSize(adjustedSize);
			}

			const bool sizeChanged = previousSize != browser->size();
			const bool hadOverflow =
				(browser->horizontalScrollBar() && browser->horizontalScrollBar()->maximum() > 0)
				|| (browser->verticalScrollBar() && browser->verticalScrollBar()->maximum() > 0);
			if (sizeChanged || hadOverflow) {
				if (QScrollBar *verticalScrollBar = browser->verticalScrollBar()) {
					verticalScrollBar->setValue(verticalScrollBar->minimum());
				}
				if (QScrollBar *horizontalScrollBar = browser->horizontalScrollBar()) {
					horizontalScrollBar->setValue(horizontalScrollBar->minimum());
				}
				browser->updateGeometry();
				if (m_surface) {
					m_surface->updateGeometry();
				}
				updateGeometry();
				repositionActions();
				if (notify) {
					emit contentSizeChanged();
				}
			}

			m_embeddedBrowserResizeInProgress = false;
			if (m_embeddedBrowserResizeDeferred) {
				m_embeddedBrowserResizeDeferred = false;
				scheduleEmbeddedBrowserResize(browser);
			}
		}

		void connectBrowser(LogTextBrowser *browser) {
			if (!browser) {
				return;
			}

			const auto scheduleBrowserResize = [this, browser]() { scheduleEmbeddedBrowserResize(browser); };

			connect(browser, &LogTextBrowser::anchorClicked, this, &PersistentChatBubbleWidget::anchorClicked);
			connect(browser, QOverload< const QUrl & >::of(&QTextBrowser::highlighted), this,
					&PersistentChatBubbleWidget::highlighted);
			connect(browser, &LogTextBrowser::customContextMenuRequested, this,
					[this, browser](const QPoint &position) {
						activate();
						emit logContextMenuRequested(browser, position);
					});
			connect(browser, &LogTextBrowser::imageActivated, this,
					[this, browser](const QTextCursor &cursor) { emit logImageActivated(browser, cursor); });
			connect(browser, &LogTextBrowser::contentWidthChanged, this, [scheduleBrowserResize](int) {
				scheduleBrowserResize();
			});
			if (QScrollBar *horizontalScrollBar = browser->horizontalScrollBar()) {
				connect(horizontalScrollBar, &QScrollBar::rangeChanged, this,
						[this, scheduleBrowserResize](int minimum, int maximum) {
							Q_UNUSED(minimum);
							if (maximum > 0) {
								scheduleBrowserResize();
							}
						});
			}
			if (QScrollBar *verticalScrollBar = browser->verticalScrollBar()) {
				connect(verticalScrollBar, &QScrollBar::rangeChanged, this,
						[this, scheduleBrowserResize](int minimum, int maximum) {
							Q_UNUSED(minimum);
							if (maximum > 0) {
								scheduleBrowserResize();
							}
						});
			}
			if (QTextDocument *document = browser->document()) {
				connect(document, &QTextDocument::contentsChanged, this, scheduleBrowserResize);
				if (QAbstractTextDocumentLayout *layout = document->documentLayout()) {
					connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged, this,
							[this, scheduleBrowserResize](const QSizeF &) { scheduleBrowserResize(); });
				}
			}
			registerTrackedObject(browser);
			registerTrackedObject(browser->viewport());
			scheduleBrowserResize();
		}

		void registerTrackedObject(QObject *object) {
			if (!object) {
				return;
			}

			for (const QPointer< QObject > &trackedObject : m_trackedObjects) {
				if (trackedObject == object) {
					return;
				}
			}

			object->installEventFilter(this);
			if (QWidget *widget = qobject_cast< QWidget * >(object)) {
				widget->setAttribute(Qt::WA_Hover, true);
			}
			m_trackedObjects.push_back(object);
		}

		void registerWidgetTree(QWidget *widget) {
			if (!widget) {
				return;
			}

			registerTrackedObject(widget);
			const QList< QObject * > childObjects = widget->findChildren< QObject * >();
			for (QObject *childObject : childObjects) {
				registerTrackedObject(childObject);
			}
		}

		void repositionActions() {
			if (!m_actions || !m_surface) {
				return;
			}

			m_actions->adjustSize();
			const QSize actionSize = m_actions->sizeHint();
			const QPoint topRight = m_surface->geometry().topRight();
			m_actions->move(std::max(0, topRight.x() - actionSize.width() - 8), std::max(0, topRight.y() - 12));
			m_actions->raise();
		}

		bool shouldStayActive() const {
			if (m_systemMessage) {
				return false;
			}

			if (QWidget *focusWidget = QApplication::focusWidget()) {
				if (focusWidget == this || isAncestorOf(focusWidget)) {
					return true;
				}
			}

			for (const QPointer< QObject > &trackedObject : m_trackedObjects) {
				if (QWidget *widget = qobject_cast< QWidget * >(trackedObject.data()); widget && widget->underMouse()) {
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
			if (m_systemMessage || m_active == active) {
				return;
			}

			m_active = active;
			if (m_surface) {
				m_surface->setProperty("bubbleActive", active);
				m_surface->style()->unpolish(m_surface);
				m_surface->style()->polish(m_surface);
				m_surface->update();
			}
			if (m_actions) {
				m_actions->setVisible(active);
			}
			emit activeChanged(active);
		}

		void showMoreMenu() {
			QMenu menu(this);
			QAction *primaryAction = nullptr;
			if (m_actionButton && !m_actionText.isEmpty()) {
				primaryAction = menu.addAction(m_actionText);
				primaryAction->setEnabled(m_actionButton->isEnabled());
			}
			QAction *copyAction = nullptr;
			if (!m_copyText.trimmed().isEmpty()) {
				copyAction = menu.addAction(tr("Copy message"));
			}
			if (!primaryAction && !copyAction) {
				return;
			}

			QAction *selectedAction = menu.exec(m_moreButton ? m_moreButton->mapToGlobal(QPoint(0, m_moreButton->height()))
															 : QCursor::pos());
			if (!selectedAction) {
				return;
			}

			if (selectedAction == primaryAction) {
				if (m_actionButton) {
					m_actionButton->click();
				}
				return;
			}

			if (selectedAction == copyAction) {
				QApplication::clipboard()->setText(m_copyText);
			}
		}
	};
} // namespace

PersistentChatMessageGroupWidget::PersistentChatMessageGroupWidget(int availableWidth, const QString &baseStylesheet,
																   QWidget *parent)
	: QWidget(parent), m_baseStylesheet(baseStylesheet), m_availableWidth(availableWidth) {
	setObjectName(QLatin1String("qwPersistentChatMessageGroup"));
	setAttribute(Qt::WA_StyledBackground, true);
	setAttribute(Qt::WA_Hover, true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	setProperty("persistentChatSystem", false);
	setProperty("rowActive", false);

	m_rowLayout = new QHBoxLayout(this);
	m_rowLayout->setContentsMargins(PersistentChatGroupHorizontalPadding, PersistentChatGroupVerticalPadding,
									PersistentChatGroupHorizontalPadding, PersistentChatGroupVerticalPadding);
	m_rowLayout->setSpacing(PersistentChatAvatarGap);
	m_rowLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

	m_leadingSpacer = new QWidget(this);
	m_leadingSpacer->setObjectName(QLatin1String("qwPersistentChatLeadingSpacer"));
	m_leadingSpacer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	m_leadingSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	m_leadingSpacer->hide();

	m_avatarFallbackLabel = new QLabel(this);
	m_avatarFallbackLabel->setObjectName(QLatin1String("qlPersistentChatAvatarFallback"));
	m_avatarFallbackLabel->setAttribute(Qt::WA_StyledBackground, true);
	m_avatarFallbackLabel->setAlignment(Qt::AlignCenter);
	m_avatarFallbackLabel->setFixedSize(PersistentChatAvatarSize, PersistentChatAvatarSize);

	m_avatarFrame = new QFrame(this);
	m_avatarFrame->setObjectName(QLatin1String("qfPersistentChatAvatarFrame"));
	m_avatarFrame->setAttribute(Qt::WA_StyledBackground, true);
	m_avatarFrame->setFixedSize(PersistentChatAvatarSize, PersistentChatAvatarSize);
	QHBoxLayout *avatarLayout = new QHBoxLayout(m_avatarFrame);
	avatarLayout->setContentsMargins(0, 0, 0, 0);
	avatarLayout->addWidget(m_avatarFallbackLabel);

	m_contentColumn = new QWidget(this);
	m_contentColumn->setObjectName(QLatin1String("qwPersistentChatMessageColumn"));
	m_contentColumn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	QVBoxLayout *columnLayout = new QVBoxLayout(m_contentColumn);
	columnLayout->setContentsMargins(0, 0, 0, 0);
	columnLayout->setSpacing(3);
	columnLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

	m_headerWidget = new QWidget(m_contentColumn);
	m_headerWidget->setObjectName(QLatin1String("qwPersistentChatGroupHeader"));
	m_headerLayout = new QHBoxLayout(m_headerWidget);
	m_headerLayout->setContentsMargins(0, 0, 0, 0);
	m_headerLayout->setSpacing(6);

	m_actorLabel = new QLabel(m_headerWidget);
	m_actorLabel->setObjectName(QLatin1String("qlPersistentChatGroupActor"));
	m_actorLabel->setTextFormat(Qt::PlainText);

	m_timeLabel = new QLabel(m_headerWidget);
	m_timeLabel->setObjectName(QLatin1String("qlPersistentChatGroupTime"));

	m_scopeButton = new QToolButton(m_headerWidget);
	m_scopeButton->setObjectName(QLatin1String("qtbPersistentChatGroupScope"));
	m_scopeButton->setAutoRaise(true);
	m_scopeButton->hide();

	m_headerLayout->addWidget(m_actorLabel);
	m_headerLayout->addWidget(m_timeLabel);
	m_headerLayout->addWidget(m_scopeButton);
	m_headerLayout->addStretch(1);

	columnLayout->addWidget(m_headerWidget);

	m_bubblesLayout = new QVBoxLayout();
	m_bubblesLayout->setContentsMargins(0, 0, 0, 0);
	m_bubblesLayout->setSpacing(PersistentChatGroupedBubbleSpacing);
	columnLayout->addLayout(m_bubblesLayout);

	m_rowLayout->addWidget(m_leadingSpacer, 1);
	m_rowLayout->addWidget(m_avatarFrame, 0, Qt::AlignTop);
	m_rowLayout->addWidget(m_contentColumn, 0, Qt::AlignTop);
	m_rowLayout->addStretch(1);

	syncMeasuredHeight();
}

void PersistentChatMessageGroupWidget::setHeader(const PersistentChatGroupHeaderSpec &headerSpec,
												 const QString &avatarFallbackText) {
	m_systemMessage = headerSpec.systemMessage;
	m_selfAuthored  = headerSpec.selfAuthored && !headerSpec.systemMessage;
	setProperty("selfAuthored", m_selfAuthored);
	setProperty("persistentChatSystem", headerSpec.systemMessage);
	m_actorLabel->setText(headerSpec.actorLabel);
	const bool showActor = !headerSpec.systemMessage && !m_selfAuthored && !headerSpec.actorLabel.trimmed().isEmpty();
	const bool showTime  = !headerSpec.systemMessage && !headerSpec.timeLabel.trimmed().isEmpty();
	m_actorLabel->setVisible(showActor);
	m_actorLabel->setStyleSheet(
		QString::fromLatin1("color: %1;").arg(headerSpec.actorColor.isValid() ? headerSpec.actorColor.name() : QString()));
	m_actorLabel->setAlignment(m_selfAuthored ? (Qt::AlignRight | Qt::AlignVCenter) : (Qt::AlignLeft | Qt::AlignVCenter));

	m_timeLabel->setText(headerSpec.timeLabel.toHtmlEscaped());
	m_timeLabel->setToolTip(headerSpec.timeToolTip);
	m_timeLabel->setVisible(showTime);
	m_timeLabel->setAlignment(m_selfAuthored ? (Qt::AlignRight | Qt::AlignVCenter) : (Qt::AlignLeft | Qt::AlignVCenter));

	bool showScope = false;
	if (headerSpec.aggregateScope && !headerSpec.scopeLabel.isEmpty()) {
		const QString scopeText = Qt::mightBeRichText(headerSpec.scopeLabel)
									  ? QTextDocumentFragment::fromHtml(headerSpec.scopeLabel).toPlainText()
									  : headerSpec.scopeLabel;
		m_scopeButton->setText(scopeText);
		m_scopeButton->setToolTip(scopeText);
		m_scopeButton->show();
		showScope = true;
		m_scopeButton->disconnect();
		connect(m_scopeButton, &QToolButton::clicked, this,
				[this, headerSpec]() { emit scopeJumpRequested(headerSpec.scope, headerSpec.scopeID); });
	} else {
		m_scopeButton->hide();
	}

	if (m_avatarFrame) {
		m_avatarFrame->setVisible(!headerSpec.systemMessage && !m_selfAuthored);
	}

	if (m_leadingSpacer) {
		m_leadingSpacer->setVisible(headerSpec.systemMessage);
	}

	m_avatarFrame->setStyleSheet(QString::fromLatin1("background-color: %1; border-radius: 16px;")
									 .arg(headerSpec.avatarBackgroundColor.isValid()
											  ? headerSpec.avatarBackgroundColor.name(QColor::HexArgb)
											  : QString()));
	m_avatarFallbackLabel->setText(avatarFallbackText.isEmpty() ? QStringLiteral("?")
																: avatarFallbackText.left(2).toUpper());
	m_avatarFallbackLabel->setStyleSheet(QString::fromLatin1("color: %1; border-radius: 16px;")
											 .arg(headerSpec.avatarForegroundColor.isValid()
												  ? headerSpec.avatarForegroundColor.name()
												  : QString()));

	if (m_headerWidget) {
		m_headerWidget->setVisible(showActor || showTime || showScope);
		m_headerWidget->setProperty("selfAuthored", m_selfAuthored);
	}

	if (m_contentColumn) {
		m_contentColumn->setProperty("selfAuthored", m_selfAuthored);
	}

	if (m_headerLayout) {
		m_headerLayout->setDirection(m_selfAuthored ? QBoxLayout::RightToLeft : QBoxLayout::LeftToRight);
	}

	if (m_rowLayout) {
		m_rowLayout->setDirection(m_selfAuthored ? QBoxLayout::RightToLeft : QBoxLayout::LeftToRight);
		m_rowLayout->setSpacing((headerSpec.systemMessage || m_selfAuthored) ? 0 : PersistentChatAvatarGap);
	}

	if (m_rowLayout) {
		m_rowLayout->setContentsMargins(PersistentChatGroupHorizontalPadding,
										headerSpec.systemMessage ? 1 : PersistentChatGroupVerticalPadding,
										PersistentChatGroupHorizontalPadding,
										headerSpec.systemMessage ? 1 : PersistentChatGroupVerticalPadding);
	}

	setRowActive(false);
	syncMeasuredHeight();
}

void PersistentChatMessageGroupWidget::addBubble(const PersistentChatBubbleSpec &bubbleSpec) {
	const bool showAvatar = !m_systemMessage && !m_selfAuthored;
	const int contentChrome =
		(PersistentChatGroupHorizontalPadding * 2) + (showAvatar ? (PersistentChatAvatarSize + PersistentChatAvatarGap) : 0);
	const int contentWidth = std::max(180, std::min(600, m_availableWidth - contentChrome));
	auto *bubble = new PersistentChatBubbleWidget(bubbleSpec, contentWidth, m_baseStylesheet, this);
	connect(bubble, &PersistentChatBubbleWidget::replyRequested, this, &PersistentChatMessageGroupWidget::replyRequested);
	connect(bubble, &PersistentChatBubbleWidget::scopeJumpRequested, this,
			&PersistentChatMessageGroupWidget::scopeJumpRequested);
	connect(bubble, &PersistentChatBubbleWidget::logContextMenuRequested, this,
			&PersistentChatMessageGroupWidget::logContextMenuRequested);
	connect(bubble, &PersistentChatBubbleWidget::logImageActivated, this,
			&PersistentChatMessageGroupWidget::logImageActivated);
	connect(bubble, &PersistentChatBubbleWidget::anchorClicked, this, &PersistentChatMessageGroupWidget::anchorClicked);
	connect(bubble, &PersistentChatBubbleWidget::highlighted, this, &PersistentChatMessageGroupWidget::highlighted);
	connect(bubble, &PersistentChatBubbleWidget::activeChanged, this,
			[this]() { reevaluateRowActive(); });
	connect(bubble, &PersistentChatBubbleWidget::contentSizeChanged, this, [this]() {
		syncMeasuredHeight();
		updateGeometry();
	});
	const Qt::Alignment bubbleAlignment =
		m_systemMessage ? Qt::AlignHCenter : (m_selfAuthored ? Qt::AlignRight : Qt::AlignLeft);
	m_bubblesLayout->addWidget(bubble, 0, bubbleAlignment);
	m_bubbleEntries.push_back(BubbleEntry { bubbleSpec.messageID, bubbleSpec.threadID, bubble });
	reevaluateRowActive();
	syncMeasuredHeight();
	updateGeometry();

	if (m_firstMessageID == 0) {
		m_firstMessageID = bubbleSpec.messageID;
	}
	m_lastMessageID = bubbleSpec.messageID;
	m_lastThreadID  = bubbleSpec.threadID;
}

bool PersistentChatMessageGroupWidget::updateBubblePreview(unsigned int messageID, unsigned int threadID,
														   const PersistentChatPreviewSpec &previewSpec) {
	for (const BubbleEntry &entry : m_bubbleEntries) {
		if (!entry.widget || entry.messageID != messageID || entry.threadID != threadID) {
			continue;
		}

		auto *bubble = qobject_cast< PersistentChatBubbleWidget * >(entry.widget);
		if (!bubble) {
			return false;
		}

		bubble->setPreviewContent(previewSpec);
		syncMeasuredHeight();
		updateGeometry();
		return true;
	}

	return false;
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

bool PersistentChatMessageGroupWidget::event(QEvent *event) {
	const bool handled = QWidget::event(event);
	if (!event) {
		return handled;
	}

	switch (event->type()) {
		case QEvent::Enter:
		case QEvent::HoverEnter:
		case QEvent::Leave:
		case QEvent::HoverLeave:
		case QEvent::FocusIn:
		case QEvent::FocusOut:
			reevaluateRowActive();
			break;
		default:
			break;
	}

	return handled;
}

void PersistentChatMessageGroupWidget::resizeEvent(QResizeEvent *event) {
	QWidget::resizeEvent(event);
	syncMeasuredHeight();
}

bool PersistentChatMessageGroupWidget::hasActiveBubble() const {
	for (const BubbleEntry &entry : m_bubbleEntries) {
		const auto *bubble = qobject_cast< const PersistentChatBubbleWidget * >(entry.widget);
		if (bubble && bubble->isActive()) {
			return true;
		}
	}

	return false;
}

int PersistentChatMessageGroupWidget::measuredHeight() const {
	int heightHint = QWidget::minimumSizeHint().height();
	if (QLayout *groupLayout = layout()) {
		groupLayout->activate();
		heightHint = std::max(heightHint, groupLayout->sizeHint().height());
		heightHint = std::max(heightHint, groupLayout->minimumSize().height());
	}
	if (m_contentColumn) {
		heightHint = std::max(heightHint, m_contentColumn->sizeHint().height());
	}
	return std::max(1, heightHint);
}

void PersistentChatMessageGroupWidget::reevaluateRowActive() {
	QWidget *focusWidget    = QApplication::focusWidget();
	const bool groupFocused = focusWidget && (focusWidget == this || isAncestorOf(focusWidget));
	setRowActive(!m_systemMessage && (underMouse() || groupFocused || hasActiveBubble()));
}

void PersistentChatMessageGroupWidget::setRowActive(bool active) {
	if (m_systemMessage) {
		active = false;
	}
	if (m_rowActive == active) {
		return;
	}

	m_rowActive = active;
	setProperty("rowActive", active);
	style()->unpolish(this);
	style()->polish(this);
	update();
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
