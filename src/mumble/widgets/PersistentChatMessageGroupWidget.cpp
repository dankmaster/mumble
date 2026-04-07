// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatMessageGroupWidget.h"

#include "ChatPerfTrace.h"
#include "Log.h"
#include "UiTheme.h"

#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QClipboard>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>
#include <QtGui/QTextBlock>
#include <QtGui/QTextDocumentFragment>
#include <QtGui/QTextFrame>
#include <QtGui/QTextOption>
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
	constexpr int PersistentChatGroupHorizontalPadding = 8;
	constexpr int PersistentChatGroupVerticalPadding   = 0;
	constexpr int PersistentChatAvatarGap              = 1;
	constexpr int PersistentChatAvatarSize             = 22;
	constexpr int PersistentChatGroupedBubbleSpacing   = 0;
	constexpr int PersistentChatPreviewBottomSpacing   = 0;
	constexpr int PersistentChatInlineImageMaxWidth    = 480;
	constexpr int PersistentChatInlineImageMaxHeight   = 320;
	constexpr int PersistentChatEmbeddedBrowserDocumentMargin = 0;
	constexpr int PersistentChatEmbeddedBrowserHorizontalSlack = 4;
	constexpr int PersistentChatEmbeddedBrowserVerticalSlack   = 5;
	constexpr int PersistentChatBubbleHorizontalPadding = 6;
	constexpr int PersistentChatBubbleVerticalPadding   = 0;
	constexpr int PersistentChatBubbleContentSpacing    = 0;
	constexpr int PersistentChatConversationLaneMinWidth = 176;
	constexpr int PersistentChatConversationLaneMaxWidth = 312;
	constexpr int PersistentChatConversationLaneReserve = PersistentChatAvatarSize + PersistentChatAvatarGap;
	constexpr int PersistentChatCompactTranscriptHorizontalInset = 8;
	constexpr int PersistentChatCompactTranscriptTimeColumnWidth = 40;
	constexpr int PersistentChatCompactTranscriptActorColumnWidth = 74;
	constexpr int PersistentChatCompactTranscriptColumnGap = 8;
	constexpr int PersistentChatCompactTranscriptMinBodyWidth = 144;
	constexpr int PersistentChatCompactTranscriptPreviewMaxWidth = 276;
	constexpr int PersistentChatSingleLineBubbleMinWidth = 136;
	constexpr int PersistentChatSelfAuthoredSingleLineBubbleMinWidth = 186;
	constexpr qreal PersistentChatBubbleCornerRadius    = 13.0;
	constexpr qreal PersistentChatBubbleStackCornerRadius = 4.0;
	constexpr int PersistentChatLinkPreviewMinWidth     = 160;
	constexpr int PersistentChatLinkPreviewMaxWidth     = 198;
	constexpr int PersistentChatLinkPreviewPadding      = 3;
	constexpr int PersistentChatLinkPreviewSpacing      = 2;
	constexpr int PersistentChatLinkPreviewThumbMinWidth = 36;
	constexpr int PersistentChatLinkPreviewThumbMaxWidth = 46;
	constexpr int PersistentChatLargeDocumentThreshold  = 8192;

	Qt::Alignment persistentChatLaneHorizontalAlignment(PersistentChatConversationLaneAnchor anchor) {
		switch (anchor) {
			case PersistentChatConversationLaneAnchor::Trailing:
				return Qt::AlignRight;
			case PersistentChatConversationLaneAnchor::Center:
				return Qt::AlignHCenter;
			case PersistentChatConversationLaneAnchor::Leading:
			default:
				return Qt::AlignLeft;
		}
	}

	Qt::Alignment persistentChatLaneTextAlignment(PersistentChatConversationLaneAnchor anchor) {
		return persistentChatLaneHorizontalAlignment(anchor) | Qt::AlignVCenter;
	}

	bool persistentChatLaneIsTrailing(PersistentChatConversationLaneAnchor anchor) {
		return anchor == PersistentChatConversationLaneAnchor::Trailing;
	}

	QColor persistentChatAlphaColor(const QColor &color, qreal alpha) {
		QColor adjusted = color;
		adjusted.setAlphaF(qBound< qreal >(0.0, alpha, 1.0));
		return adjusted;
	}

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

	bool persistentChatHtmlContainsUrlLikeText(const QString &html) {
		if (html.isEmpty()) {
			return false;
		}

		const QString plainText = QTextDocumentFragment::fromHtml(html).toPlainText();
		if (plainText.contains(QLatin1String("://")) || plainText.contains(QLatin1String("www."), Qt::CaseInsensitive)) {
			return true;
		}

		static const QRegularExpression s_longTokenPattern(QLatin1String("\\S{32,}"));
		return s_longTokenPattern.match(plainText).hasMatch();
	}

	bool persistentChatPlainTextContainsUrlLikeText(const QString &plainText) {
		if (plainText.isEmpty()) {
			return false;
		}

		if (plainText.contains(QLatin1String("://")) || plainText.contains(QLatin1String("www."), Qt::CaseInsensitive)) {
			return true;
		}

		static const QRegularExpression s_longTokenPattern(QLatin1String("\\S{32,}"));
		return s_longTokenPattern.match(plainText).hasMatch();
	}

	bool persistentChatHtmlContainsAnchor(const QString &html) {
		if (html.isEmpty()) {
			return false;
		}

		static const QRegularExpression s_anchorPattern(QLatin1String("<\\s*a\\b"),
														QRegularExpression::CaseInsensitiveOption);
		return s_anchorPattern.match(html).hasMatch();
	}

	bool persistentChatHtmlIsPreviewSourceUrl(const QString &html) {
		return html.contains(QLatin1String("<!--mumble-preview-url-->"), Qt::CaseInsensitive);
	}

	bool persistentChatHtmlHasVisibleContent(const QString &html) {
		return persistentChatHtmlContainsInlineImage(html)
			   || !QTextDocumentFragment::fromHtml(html).toPlainText().trimmed().isEmpty();
	}

	bool persistentChatHtmlIsCompactPlainTextCandidate(const QString &html, const QString &plainText) {
		if (plainText.trimmed().isEmpty() || persistentChatHtmlContainsInlineImage(html)
			|| persistentChatHtmlContainsUrlLikeText(html) || persistentChatHtmlContainsAnchor(html)
			|| persistentChatHtmlIsPreviewSourceUrl(html)) {
			return false;
		}

		static const QRegularExpression s_structuralBlockPattern(
			QLatin1String("<\\s*(?:table|blockquote|pre|ul|ol|li|hr|h[1-6])\\b"),
			QRegularExpression::CaseInsensitiveOption);
		return !s_structuralBlockPattern.match(html).hasMatch();
	}

	QFont persistentChatCompactBodyFont(const QFont &baseFont) {
		QFont compactFont(baseFont);
		if (compactFont.pointSizeF() > 0.0) {
			compactFont.setPointSizeF(std::max(6.5, compactFont.pointSizeF() * 0.83));
		} else if (compactFont.pixelSize() > 0) {
			compactFont.setPixelSize(std::max(8, static_cast< int >(std::lround(compactFont.pixelSize() * 0.83))));
		}
		return compactFont;
	}

	bool persistentChatPlainTextIsSingleLine(const QString &plainText, const QFont &font, int maxWidth) {
		if (plainText.trimmed().isEmpty()) {
			return false;
		}

		const QFontMetrics metrics(font);
		const QRect measuredRect =
			metrics.boundingRect(QRect(0, 0, std::max(1, maxWidth), 4096),
								 Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap | Qt::TextExpandTabs, plainText);
		return measuredRect.height() <= metrics.lineSpacing() + PersistentChatEmbeddedBrowserVerticalSlack;
	}

	QWidget *createCompactPlainTextLabel(const QString &html, int width, const QString &sourceTextHint,
										 int singleLineMinimumWidth, QWidget *parent = nullptr) {
		if (Qt::mightBeRichText(html)) {
			return nullptr;
		}

		const QString renderedPlainText = QTextDocumentFragment::fromHtml(html).toPlainText();
		const QString normalizedSourceTextHint =
			QString(sourceTextHint).replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1Char('\r'),
																									 QLatin1Char('\n'));
		if (normalizedSourceTextHint.trimmed().isEmpty()
			|| normalizedSourceTextHint.simplified() != renderedPlainText.simplified()
			|| persistentChatPlainTextContainsUrlLikeText(normalizedSourceTextHint)
			|| normalizedSourceTextHint.contains(QLatin1Char('\n'))
			|| normalizedSourceTextHint.contains(QChar::ParagraphSeparator)) {
			return nullptr;
		}

		QLabel *label = new QLabel(parent);
		const QFont labelFont = persistentChatCompactBodyFont(label->font());
		const QString labelText = normalizedSourceTextHint.trimmed();
		if (!persistentChatPlainTextIsSingleLine(labelText, labelFont, width)) {
			delete label;
			return nullptr;
		}

		const QFontMetrics metrics(labelFont);
		const int textWidth =
			metrics.horizontalAdvance(labelText) + PersistentChatEmbeddedBrowserHorizontalSlack;
		const int measuredWidth = qBound(1, std::max(singleLineMinimumWidth, textWidth), std::max(1, width));
		const int measuredHeight =
			std::max(14, metrics.lineSpacing() + std::max(2, PersistentChatEmbeddedBrowserVerticalSlack - 2));
		const QSize labelSize(measuredWidth, measuredHeight);

		label->setAttribute(Qt::WA_StyledBackground, false);
		label->setAutoFillBackground(false);
		label->setObjectName(QLatin1String("qlPersistentChatCompactBody"));
		label->setTextFormat(Qt::PlainText);
		label->setText(labelText);
		label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		label->setWordWrap(false);
		label->setContentsMargins(0, 0, 0, 0);
		label->setMargin(0);
		label->setIndent(0);
		label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		label->setFont(labelFont);
		label->setProperty("persistentChatSingleLineBody", true);
		label->setProperty("persistentChatExplicitSizeHint", labelSize);
		label->setStyleSheet(QStringLiteral("background: transparent; border: none; padding: 0px; margin: 0px;"));
		label->setFixedSize(labelSize);
		return label;
	}

	enum class PersistentChatBubbleClusterPosition {
		Single,
		Top,
		Middle,
		Bottom
	};

	QPainterPath persistentChatRoundedRectPath(const QRectF &rect, qreal topLeftRadius, qreal topRightRadius,
											   qreal bottomRightRadius, qreal bottomLeftRadius) {
		QPainterPath path;
		if (!rect.isValid()) {
			return path;
		}

		const qreal maxRadius = std::min(rect.width(), rect.height()) / 2.0;
		const auto clampRadius = [maxRadius](qreal radius) {
			return qBound< qreal >(0.0, radius, maxRadius);
		};

		topLeftRadius     = clampRadius(topLeftRadius);
		topRightRadius    = clampRadius(topRightRadius);
		bottomRightRadius = clampRadius(bottomRightRadius);
		bottomLeftRadius  = clampRadius(bottomLeftRadius);

		path.moveTo(rect.left() + topLeftRadius, rect.top());
		path.lineTo(rect.right() - topRightRadius, rect.top());
		if (topRightRadius > 0.0) {
			path.arcTo(QRectF(rect.right() - (topRightRadius * 2.0), rect.top(), topRightRadius * 2.0,
							  topRightRadius * 2.0),
					   90.0, -90.0);
		} else {
			path.lineTo(rect.right(), rect.top());
		}

		path.lineTo(rect.right(), rect.bottom() - bottomRightRadius);
		if (bottomRightRadius > 0.0) {
			path.arcTo(QRectF(rect.right() - (bottomRightRadius * 2.0), rect.bottom() - (bottomRightRadius * 2.0),
							  bottomRightRadius * 2.0, bottomRightRadius * 2.0),
					   0.0, -90.0);
		} else {
			path.lineTo(rect.right(), rect.bottom());
		}

		path.lineTo(rect.left() + bottomLeftRadius, rect.bottom());
		if (bottomLeftRadius > 0.0) {
			path.arcTo(QRectF(rect.left(), rect.bottom() - (bottomLeftRadius * 2.0), bottomLeftRadius * 2.0,
							  bottomLeftRadius * 2.0),
					   270.0, -90.0);
		} else {
			path.lineTo(rect.left(), rect.bottom());
		}

		path.lineTo(rect.left(), rect.top() + topLeftRadius);
		if (topLeftRadius > 0.0) {
			path.arcTo(QRectF(rect.left(), rect.top(), topLeftRadius * 2.0, topLeftRadius * 2.0), 180.0, -90.0);
		} else {
			path.lineTo(rect.left(), rect.top());
		}

		path.closeSubpath();
		return path;
	}

	QString persistentChatDocumentStylesheet(const QString &baseStylesheet, int maxInlineImageWidth) {
		const int effectiveInlineImageWidth = std::max(1, std::min(maxInlineImageWidth, PersistentChatInlineImageMaxWidth));
		return baseStylesheet
			   + QString::fromLatin1(
				   "html, body { margin: 0; padding: 0; border: 0; background: transparent; }"
				   "body, table, tr, td, div, span, p, a { font-size: 0.83em; line-height: 1.16; }"
				   "a.mumble-preview-url:hover { text-decoration: underline; }"
				   "p { margin: 0; }"
				   "p + p { margin-top: 1px; }"
				   "table, tr, td { margin: 0; padding: 0; border: none; background: transparent; }"
				   "img { border: none; outline: none; display: block; margin: 0; max-width: %1px; max-height: %2px;"
				   " width: auto; height: auto; background: transparent; }")
					 .arg(effectiveInlineImageWidth)
					 .arg(PersistentChatInlineImageMaxHeight);
	}

	void configurePersistentChatDocument(QTextDocument *document, const QString &baseStylesheet, int maxInlineImageWidth) {
		if (!document) {
			return;
		}

		document->setDocumentMargin(PersistentChatEmbeddedBrowserDocumentMargin);
		QTextOption textOption = document->defaultTextOption();
		textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
		textOption.setAlignment(Qt::AlignLeft);
		document->setDefaultTextOption(textOption);
		document->setDefaultStyleSheet(persistentChatDocumentStylesheet(baseStylesheet, maxInlineImageWidth));
		if (QTextFrame *rootFrame = document->rootFrame()) {
			QTextFrameFormat rootFrameFormat = rootFrame->frameFormat();
			rootFrameFormat.setBorder(0);
			rootFrameFormat.setMargin(0);
			rootFrameFormat.setPadding(0);
			rootFrame->setFrameFormat(rootFrameFormat);
		}
	}

	int persistentChatDocumentMeasuredHeight(QTextDocument *document) {
		if (!document) {
			return 0;
		}

		if (QAbstractTextDocumentLayout *layout = document->documentLayout()) {
			int measuredHeight = 0;
			for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
				const QRectF blockRect = layout->blockBoundingRect(block);
				measuredHeight =
					std::max(measuredHeight, static_cast< int >(std::ceil(blockRect.y() + blockRect.height())));
			}
			if (measuredHeight > 0) {
				return measuredHeight;
			}
			return static_cast< int >(std::ceil(layout->documentSize().height()));
		}

		return static_cast< int >(std::ceil(document->size().height()));
	}

	QSize persistentChatDocumentSize(QTextDocument *document, int maxWidth, bool preferMaxWidth = false,
									 bool singleLineBody = false, const QFont &font = QFont(),
									 int singleLineMinimumWidth = 0, int singleLinePlainTextWidth = 0) {
		const int clampedMaxWidth = std::max(1, maxWidth);
		if (!document) {
			return QSize(clampedMaxWidth, 20);
		}

		const auto measuredHeightWithSlack = [&](bool currentSingleLineBody) {
			int measuredHeight = persistentChatDocumentMeasuredHeight(document);
			if (currentSingleLineBody) {
				const QFont effectiveFont = font.resolve(document->defaultFont());
				const QFontMetrics metrics(effectiveFont);
				measuredHeight = std::max(measuredHeight, metrics.lineSpacing());
			}
			return std::max(16, measuredHeight + PersistentChatEmbeddedBrowserVerticalSlack);
		};

		document->setTextWidth(clampedMaxWidth);
		document->adjustSize();
		if (document->characterCount() > PersistentChatLargeDocumentThreshold) {
			const int measuredHeight = measuredHeightWithSlack(false);
			return QSize(clampedMaxWidth, measuredHeight);
		}

		int measuredWidth =
			preferMaxWidth
				? clampedMaxWidth
				: static_cast< int >(std::ceil(document->idealWidth())) + PersistentChatEmbeddedBrowserHorizontalSlack;
		if (singleLineBody) {
			measuredWidth =
				std::max(measuredWidth, std::max(singleLineMinimumWidth, singleLinePlainTextWidth));
		}
		measuredWidth     = qBound(1, measuredWidth, clampedMaxWidth);

		if (singleLineBody) {
			const QFont effectiveFont = font.resolve(document->defaultFont());
			const QFontMetrics metrics(effectiveFont);
			const int singleLineHeight =
				std::max(metrics.lineSpacing(), metrics.tightBoundingRect(QStringLiteral("Ag")).height());
			const int compactHeight = std::max(14, singleLineHeight + std::max(2, PersistentChatEmbeddedBrowserVerticalSlack - 2));
			return QSize(measuredWidth, compactHeight);
		}

		document->setTextWidth(measuredWidth);
		document->adjustSize();
		const int measuredHeight = measuredHeightWithSlack(singleLineBody);
		return QSize(measuredWidth, measuredHeight);
	}

	LogTextBrowser *createEmbeddedBrowser(const QString &html, int width, const QString &baseStylesheet,
										 const QVector< QPair< QUrl, QImage > > &imageResources = {},
										 const QString &sourceTextHint = QString(),
										 int singleLineMinimumWidth = 0) {
		mumble::chatperf::ScopedDuration trace("chat.group.create_browser");
		const bool containsInlineImage = persistentChatHtmlContainsInlineImage(html);
		const bool previewSourceUrl    = persistentChatHtmlIsPreviewSourceUrl(html);
		const QString renderedPlainText = QTextDocumentFragment::fromHtml(html).toPlainText();
		const QString normalizedSourceTextHint =
			QString(sourceTextHint).replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1Char('\r'),
																									 QLatin1Char('\n'));
		const bool sourceTextMatchesRenderedText =
			!normalizedSourceTextHint.trimmed().isEmpty()
			&& normalizedSourceTextHint.simplified() == renderedPlainText.simplified();
		const bool sourceTextCompactCandidate =
			!normalizedSourceTextHint.trimmed().isEmpty() && !persistentChatPlainTextContainsUrlLikeText(normalizedSourceTextHint)
			&& !normalizedSourceTextHint.contains(QLatin1Char('\n')) && !normalizedSourceTextHint.contains(QChar::ParagraphSeparator)
			&& !containsInlineImage && !persistentChatHtmlContainsAnchor(html) && !previewSourceUrl;
		const QString plainText = sourceTextMatchesRenderedText ? normalizedSourceTextHint : renderedPlainText;
		const QString compactText = sourceTextCompactCandidate ? normalizedSourceTextHint : plainText;
		const bool compactPlainTextCandidate =
			sourceTextCompactCandidate || sourceTextMatchesRenderedText
			|| persistentChatHtmlIsCompactPlainTextCandidate(html, plainText);
		const bool preferMaxWidth =
			previewSourceUrl
			|| ((containsInlineImage && persistentChatHtmlHasNonImageText(html))
				|| persistentChatHtmlContainsUrlLikeText(html));
		mumble::chatperf::recordValue("chat.group.browser.html_chars", html.size());
		if (containsInlineImage) {
			mumble::chatperf::recordValue("chat.group.browser.inline_image", 1);
		}
		if (preferMaxWidth) {
			mumble::chatperf::recordValue("chat.group.browser.prefer_max_width", 1);
		}
		if (previewSourceUrl) {
			mumble::chatperf::recordValue("chat.group.browser.preview_source_url", 1);
		}
		if (!imageResources.isEmpty()) {
			mumble::chatperf::recordValue("chat.group.browser.image_resources", imageResources.size());
		}
		auto *browser   = new LogTextBrowser();
		auto *document  = new LogDocument(browser);
		const bool singleLineBody =
			compactPlainTextCandidate && persistentChatPlainTextIsSingleLine(compactText, browser->font(), width);
		int singleLinePlainTextWidth = 0;
		if (singleLineBody) {
			const QString normalizedSingleLineText = compactText.simplified();
			if (!normalizedSingleLineText.isEmpty()) {
				const QFontMetrics metrics(browser->font());
				singleLinePlainTextWidth =
					metrics.horizontalAdvance(normalizedSingleLineText) + PersistentChatEmbeddedBrowserHorizontalSlack;
			}
		}
		browser->setDocument(document);
		browser->setFrameShape(QFrame::NoFrame);
		browser->setFrameStyle(QFrame::NoFrame);
		browser->setLineWidth(0);
		browser->setMidLineWidth(0);
		browser->setReadOnly(true);
		browser->setOpenLinks(false);
		browser->setTextInteractionFlags(Qt::TextBrowserInteraction);
		browser->setFocusPolicy(Qt::NoFocus);
		browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		browser->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
		browser->setContextMenuPolicy(Qt::CustomContextMenu);
		browser->setProperty("persistentChatEmbeddedBrowser", true);
		browser->setProperty("persistentChatMaxWidth", width);
		browser->setProperty("persistentChatPreferMaxWidth", preferMaxWidth);
		browser->setProperty("persistentChatContainsInlineImage", containsInlineImage);
		browser->setAutoFillBackground(false);
		QPalette transparentPalette = browser->palette();
		transparentPalette.setColor(QPalette::Base, Qt::transparent);
		transparentPalette.setColor(QPalette::Window, Qt::transparent);
		transparentPalette.setColor(QPalette::AlternateBase, Qt::transparent);
		browser->setPalette(transparentPalette);
		browser->resetViewportChrome();
		browser->setStyleSheet(QString::fromLatin1("background: transparent; border: none; margin: 0px; padding: 0px;"));
		if (QWidget *viewport = browser->viewport()) {
			viewport->setProperty("persistentChatEmbeddedBrowser", true);
			viewport->setAutoFillBackground(false);
			viewport->setPalette(transparentPalette);
			viewport->setStyleSheet(
				QString::fromLatin1("background: transparent; border: none; margin: 0px; padding: 0px;"));
		}
		configurePersistentChatDocument(document, baseStylesheet, width);
		document->setTextWidth(width);
		for (const auto &resource : imageResources) {
			if (resource.first.isValid() && !resource.second.isNull()) {
				document->addResource(QTextDocument::ImageResource, resource.first, resource.second);
			}
		}
		{
			mumble::chatperf::ScopedDuration setHtmlTrace("chat.group.browser.set_html");
			browser->setHtml(html);
		}
		const bool largeDocument = browser->document() && browser->document()->characterCount() > PersistentChatLargeDocumentThreshold;
		browser->setProperty("persistentChatLargeDocument", largeDocument);
		if (largeDocument) {
			mumble::chatperf::recordValue("chat.group.browser.large_document", 1);
		}
		QSize measuredSize;
		{
			mumble::chatperf::ScopedDuration measureTrace("chat.group.browser.measure");
			measuredSize = persistentChatDocumentSize(document, width, preferMaxWidth, singleLineBody, browser->font(),
													 singleLineMinimumWidth, singleLinePlainTextWidth);
		}
		browser->setProperty("persistentChatSingleLineBody", singleLineBody);
		browser->setProperty("persistentChatExplicitSizeHint", measuredSize);
		browser->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		browser->setFixedSize(measuredSize);
		return browser;
	}

	class PersistentChatPreviewWidget : public QFrame {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(PersistentChatPreviewWidget)

	public:
		PersistentChatPreviewWidget(const PersistentChatPreviewSpec &previewSpec,
								   const PersistentChatConversationLaneMetrics &laneMetrics,
								   PersistentChatDisplayMode displayMode,
								   QWidget *parent = nullptr)
			: QFrame(parent), m_actionUrl(previewSpec.actionUrl) {
			const bool compactTranscript = displayMode == PersistentChatDisplayMode::CompactTranscript;
			setAttribute(Qt::WA_StyledBackground, true);
			setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
			setProperty("compactTranscript", compactTranscript);
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
			QColor integratedCardBackground = cardBackground;
			integratedCardBackground.setAlphaF(std::max< qreal >(0.88, integratedCardBackground.alphaF()));
			QColor integratedAccentColor = accentColor;
			integratedAccentColor.setAlphaF(
				std::max< qreal >(0.36, std::min< qreal >(0.58, integratedAccentColor.alphaF())));
			QColor integratedBorderColor = placeholderColor;
			integratedBorderColor.setAlphaF(
				std::max< qreal >(0.42, std::min< qreal >(0.68, integratedBorderColor.alphaF())));
			const int bubbleWidth =
				std::max(1, laneMetrics.bubbleMaxWidth > 0 ? laneMetrics.bubbleMaxWidth : laneMetrics.conversationLaneWidth);
			const int previewMinWidth = std::max(1, std::min(laneMetrics.previewMinWidth, bubbleWidth));
			const int previewMaxWidth = std::max(previewMinWidth, std::min(laneMetrics.previewMaxWidth, bubbleWidth));
			const int inlinePreviewWidth = std::max(
				1, std::min(previewMaxWidth,
						 compactTranscript ? PersistentChatCompactTranscriptPreviewMaxWidth
										   : PersistentChatInlineImageMaxWidth));

			if (previewSpec.kind == PersistentChatPreviewKind::Image) {
				setObjectName(QLatin1String("qfPersistentChatImagePreview"));
				setStyleSheet(QString::fromLatin1("QFrame#qfPersistentChatImagePreview { background: transparent; border: none; }"));

				QVBoxLayout *layout = new QVBoxLayout(this);
				layout->setContentsMargins(0, 0, 0, 0);
				layout->setSpacing(1);
				layout->setSizeConstraint(QLayout::SetFixedSize);

				if (!previewSpec.thumbnailImage.isNull()) {
					QLabel *imageLabel = new QLabel(this);
					imageLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
					imageLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
					const QPixmap pixmap = QPixmap::fromImage(previewSpec.thumbnailImage)
											   .scaled(inlinePreviewWidth,
												   PersistentChatInlineImageMaxHeight, Qt::KeepAspectRatio,
												   Qt::SmoothTransformation);
					imageLabel->setPixmap(pixmap);
					imageLabel->setFixedSize(pixmap.size());
					imageLabel->setStyleSheet(QString::fromLatin1("background: transparent; border-radius: 6px;"));
					layout->addWidget(imageLabel, 0, Qt::AlignLeft);
				} else {
					QFrame *placeholder = new QFrame(this);
					placeholder->setAttribute(Qt::WA_TransparentForMouseEvents, true);
					placeholder->setFixedSize(std::min(inlinePreviewWidth, 240), 180);
					placeholder->setStyleSheet(
						QString::fromLatin1("background: %1; border-radius: 6px;").arg(placeholderColor.name(QColor::HexArgb)));
					layout->addWidget(placeholder, 0, Qt::AlignLeft);
				}

				if (!previewSpec.statusText.trimmed().isEmpty()) {
					QLabel *statusLabel = new QLabel(previewSpec.statusText, this);
					statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
					statusLabel->setWordWrap(true);
					statusLabel->setStyleSheet(
						QString::fromLatin1("color: %1; font-size: 10px;").arg(mutedColor.name()));
					statusLabel->setMaximumWidth(inlinePreviewWidth);
					layout->addWidget(statusLabel, 0, Qt::AlignLeft);
				}
			} else if (previewSpec.kind == PersistentChatPreviewKind::LinkCard) {
				const int cardWidth =
					std::max(previewMinWidth,
							 std::min(compactTranscript ? PersistentChatCompactTranscriptPreviewMaxWidth
													   : PersistentChatLinkPreviewMaxWidth,
									  previewMaxWidth));
				const bool showThumbnail = !previewSpec.thumbnailImage.isNull() || previewSpec.showThumbnailPlaceholder;
				const int thumbWidth =
					showThumbnail
						? qBound(PersistentChatLinkPreviewThumbMinWidth, cardWidth / 4,
								 PersistentChatLinkPreviewThumbMaxWidth)
						: 0;
				const int thumbHeight = thumbWidth > 0 ? std::max(42, (thumbWidth * 9) / 16) : 0;
				const int textColumnWidth =
					std::max(104, cardWidth - (PersistentChatLinkPreviewPadding * 2)
									  - (showThumbnail ? thumbWidth + PersistentChatLinkPreviewSpacing : 0));
				setObjectName(QLatin1String("qfPersistentChatLinkPreview"));
				setProperty("compactTranscript", compactTranscript);
				setFixedWidth(cardWidth);
				setStyleSheet(QString::fromLatin1(
								  "QFrame#qfPersistentChatLinkPreview {"
								  " background: %1;"
								  " border: 1px solid %3;"
								  " border-left: 1px solid %2;"
								  " border-radius: 5px;"
								  "}")
								  .arg((compactTranscript ? persistentChatAlphaColor(integratedCardBackground, 0.72)
														 : integratedCardBackground)
										   .name(QColor::HexArgb),
										   integratedAccentColor.name(QColor::HexArgb),
										   integratedBorderColor.name(QColor::HexArgb)));

				QHBoxLayout *layout = new QHBoxLayout(this);
				layout->setContentsMargins(PersistentChatLinkPreviewPadding, 2, PersistentChatLinkPreviewPadding, 2);
				layout->setSpacing(PersistentChatLinkPreviewSpacing);

				if (showThumbnail) {
					QFrame *thumbFrame = new QFrame(this);
					thumbFrame->setAttribute(Qt::WA_TransparentForMouseEvents, true);
					thumbFrame->setFixedSize(thumbWidth, thumbHeight);
					thumbFrame->setStyleSheet(QString::fromLatin1("background: %1; border-radius: 4px;")
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
						thumbLabel->setStyleSheet(QString::fromLatin1("background: transparent; border-radius: 4px;"));
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
				textLayout->setSpacing(1);

				QLabel *titleLabel = new QLabel(previewSpec.title, textColumn);
				titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
				titleLabel->setWordWrap(true);
				titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
				titleLabel->setMaximumWidth(textColumnWidth);
				titleLabel->setStyleSheet(
					QString::fromLatin1("color: %1; font-size: 10px; font-weight: 600; line-height: 1.14;")
						.arg(titleColor.name()));
				textLayout->addWidget(titleLabel);

				if (!previewSpec.description.trimmed().isEmpty()) {
					QLabel *descriptionLabel = new QLabel(previewSpec.description, textColumn);
					descriptionLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
					descriptionLabel->setWordWrap(true);
					descriptionLabel->setMaximumWidth(textColumnWidth);
					descriptionLabel->setStyleSheet(
						QString::fromLatin1("color: %1; font-size: 9px; line-height: 1.16;").arg(bodyColor.name()));
					textLayout->addWidget(descriptionLabel);
				}

				if (!previewSpec.subtitle.trimmed().isEmpty()) {
					QLabel *subtitleLabel = new QLabel(previewSpec.subtitle, textColumn);
					subtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
					subtitleLabel->setWordWrap(true);
					subtitleLabel->setMaximumWidth(textColumnWidth);
					subtitleLabel->setStyleSheet(
						QString::fromLatin1("color: %1; font-size: 8px; padding-top: 0px;").arg(mutedColor.name()));
					textLayout->addWidget(subtitleLabel);
				}

				if (!previewSpec.statusText.trimmed().isEmpty()) {
					QLabel *statusLabel = new QLabel(previewSpec.statusText, textColumn);
					statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
					statusLabel->setWordWrap(true);
					statusLabel->setMaximumWidth(textColumnWidth);
					statusLabel->setStyleSheet(
						QString::fromLatin1("color: %1; font-size: 8px; padding-top: 0px;").arg(mutedColor.name()));
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

	class PersistentChatAvatarWidget : public QWidget {
	public:
		explicit PersistentChatAvatarWidget(QWidget *parent = nullptr) : QWidget(parent) {
			setAttribute(Qt::WA_TranslucentBackground, true);
			setAutoFillBackground(false);
			setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
			setFixedSize(PersistentChatAvatarSize, PersistentChatAvatarSize);
		}

		void setAvatar(const QColor &backgroundColor, const QColor &foregroundColor, const QString &text) {
			m_backgroundColor = backgroundColor.isValid() ? backgroundColor : QColor(QLatin1String("#4b5565"));
			m_foregroundColor = foregroundColor.isValid() ? foregroundColor : QColor(QLatin1String("#f8fafc"));
			m_text            = text.trimmed().left(2).toUpper();
			update();
		}

	protected:
		void paintEvent(QPaintEvent *event) override {
			Q_UNUSED(event);

			QPainter painter(this);
			painter.setRenderHint(QPainter::Antialiasing, true);

			QRectF circleRect(0.5, 0.5, width() - 1.0, height() - 1.0);
			QColor borderColor = m_backgroundColor.lighter(118);
			borderColor.setAlphaF(0.72f);

			painter.setPen(QPen(borderColor, 1.0));
			painter.setBrush(m_backgroundColor);
			painter.drawEllipse(circleRect);

			QFont font = painter.font();
			font.setBold(true);
			font.setPixelSize(m_text.size() > 1 ? 9 : 11);
			painter.setFont(font);
			painter.setPen(m_foregroundColor);
			painter.drawText(rect(), Qt::AlignCenter, m_text);
		}

	private:
		QColor m_backgroundColor = QColor(QLatin1String("#4b5565"));
		QColor m_foregroundColor = QColor(QLatin1String("#f8fafc"));
		QString m_text;
	};

	class PersistentChatBubbleWidget : public QFrame {
	private:
		Q_OBJECT
		Q_DISABLE_COPY(PersistentChatBubbleWidget)

	public:
		PersistentChatBubbleWidget(const PersistentChatBubbleSpec &bubbleSpec,
								   const PersistentChatConversationLaneMetrics &laneMetrics,
								   const QString &baseStylesheet, QWidget *parent = nullptr)
			: QFrame(parent), m_messageID(bubbleSpec.messageID), m_readOnlyAction(bubbleSpec.readOnlyAction),
			  m_actionScope(bubbleSpec.actionScope), m_actionScopeID(bubbleSpec.actionScopeID),
			  m_baseStylesheet(baseStylesheet), m_laneMetrics(laneMetrics), m_displayMode(bubbleSpec.displayMode),
			  m_copyText(bubbleSpec.copyText), m_systemMessage(bubbleSpec.systemMessage),
			  m_actionText(bubbleSpec.actionText) {
			mumble::chatperf::ScopedDuration trace("chat.group.bubble_ctor");
			const bool compactTranscript =
				m_displayMode == PersistentChatDisplayMode::CompactTranscript && !bubbleSpec.systemMessage;
			m_contentWidth = std::max(1, m_laneMetrics.bubbleMaxWidth);
			mumble::chatperf::recordValue("chat.group.bubble.content_width", m_contentWidth);
			mumble::chatperf::recordValue("chat.group.bubble.body_chars", bubbleSpec.bodyHtml.size());
			if (bubbleSpec.previewSpec.kind != PersistentChatPreviewKind::None) {
				mumble::chatperf::recordValue("chat.group.bubble.preview_kind", static_cast< qint64 >(bubbleSpec.previewSpec.kind));
			}
			m_bubbleContentAlignment = persistentChatLaneHorizontalAlignment(m_laneMetrics.anchor);
			setObjectName(QLatin1String("qfPersistentChatBubbleContainer"));
			setAttribute(Qt::WA_StyledBackground, true);
			setProperty("compactTranscript", compactTranscript);
			setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
			const Qt::Alignment bubbleItemAlignment = m_bubbleContentAlignment | Qt::AlignTop;

			m_outerLayout = new QVBoxLayout(this);
			m_outerLayout->setContentsMargins(0, 0, 0, 0);
			m_outerLayout->setSpacing(0);
			m_outerLayout->setSizeConstraint(QLayout::SetFixedSize);

			m_surface = new QFrame(this);
			m_surface->setObjectName(QLatin1String("qfPersistentChatBubble"));
			m_surface->setProperty("persistentChatSystem", bubbleSpec.systemMessage);
			m_surface->setProperty("selfAuthored", bubbleSpec.selfAuthored);
			m_surface->setProperty("bubbleActive", false);
			m_surface->setProperty("compactTranscript", compactTranscript);
			m_surface->setToolTip(bubbleSpec.timeToolTip);
			m_surface->setAttribute(Qt::WA_StyledBackground, true);
			m_surface->setFrameShape(QFrame::NoFrame);
			m_surface->setFrameStyle(QFrame::NoFrame);
			m_surface->setLineWidth(0);
			m_surface->setMidLineWidth(0);
			m_surface->setSizePolicy(compactTranscript ? QSizePolicy::Fixed : QSizePolicy::Maximum, QSizePolicy::Fixed);

			m_surfaceLayout = new QVBoxLayout(m_surface);
			m_surfaceLayout->setContentsMargins(compactTranscript ? 0
																 : (bubbleSpec.systemMessage ? 0
																						 : PersistentChatBubbleHorizontalPadding),
											 compactTranscript ? 0
																 : (bubbleSpec.systemMessage ? 0
																						 : PersistentChatBubbleVerticalPadding),
											 compactTranscript ? 0
																 : (bubbleSpec.systemMessage ? 0
																						 : PersistentChatBubbleHorizontalPadding),
											 compactTranscript ? 0
																 : (bubbleSpec.systemMessage ? 0
																						 : PersistentChatBubbleVerticalPadding));
			m_surfaceLayout->setSpacing(compactTranscript ? 0 : (bubbleSpec.systemMessage ? 0 : PersistentChatBubbleContentSpacing));
			m_surfaceLayout->setSizeConstraint(QLayout::SetFixedSize);

			if (compactTranscript) {
				QColor timeColor = palette().color(QPalette::Mid);
				if (const std::optional< UiThemeTokens > tokens = activeUiThemeTokens(); tokens) {
					timeColor = tokens->overlay0;
				}

				m_transcriptRow = new QWidget(m_surface);
				m_transcriptRow->setObjectName(QLatin1String("qwPersistentChatCompactTranscriptRow"));
				m_transcriptRow->setAttribute(Qt::WA_StyledBackground, false);
				m_transcriptRow->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
				QHBoxLayout *transcriptLayout = new QHBoxLayout(m_transcriptRow);
				transcriptLayout->setContentsMargins(0, 0, 0, 0);
				transcriptLayout->setSpacing(PersistentChatCompactTranscriptColumnGap);
				transcriptLayout->setSizeConstraint(QLayout::SetFixedSize);

				m_transcriptTimeLabel = new QLabel(bubbleSpec.transcriptTimeLabel, m_transcriptRow);
				m_transcriptTimeLabel->setObjectName(QLatin1String("qlPersistentChatCompactTime"));
				m_transcriptTimeLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
				m_transcriptTimeLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
				m_transcriptTimeLabel->setFixedWidth(PersistentChatCompactTranscriptTimeColumnWidth);
				m_transcriptTimeLabel->setToolTip(bubbleSpec.timeToolTip);
				m_transcriptTimeLabel->setStyleSheet(
					QString::fromLatin1("color: %1; font-size: 0.72em; padding-top: 1px;")
						.arg(timeColor.name(QColor::HexArgb)));

				m_transcriptActorLabel = new QLabel(bubbleSpec.transcriptActorLabel, m_transcriptRow);
				m_transcriptActorLabel->setObjectName(QLatin1String("qlPersistentChatCompactActor"));
				m_transcriptActorLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
				m_transcriptActorLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
				m_transcriptActorLabel->setFixedWidth(PersistentChatCompactTranscriptActorColumnWidth);
				m_transcriptActorLabel->setToolTip(bubbleSpec.transcriptActorLabel);
				m_transcriptActorLabel->setStyleSheet(
					QString::fromLatin1("color: %1; font-size: 0.80em; font-weight: 600;")
						.arg((bubbleSpec.transcriptActorColor.isValid() ? bubbleSpec.transcriptActorColor
																	   : palette().color(QPalette::Text))
								 .name(QColor::HexArgb)));

				m_transcriptContentColumn = new QWidget(m_transcriptRow);
				m_transcriptContentColumn->setObjectName(QLatin1String("qwPersistentChatCompactTranscriptContent"));
				m_transcriptContentColumn->setAttribute(Qt::WA_StyledBackground, false);
				m_transcriptContentColumn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
				m_transcriptContentLayout = new QVBoxLayout(m_transcriptContentColumn);
				m_transcriptContentLayout->setContentsMargins(0, 0, 0, 0);
				m_transcriptContentLayout->setSpacing(2);
				m_transcriptContentLayout->setSizeConstraint(QLayout::SetFixedSize);

				transcriptLayout->addWidget(m_transcriptTimeLabel, 0, Qt::AlignTop);
				transcriptLayout->addWidget(m_transcriptActorLabel, 0, Qt::AlignTop);
				transcriptLayout->addWidget(m_transcriptContentColumn, 0, Qt::AlignTop);
				m_surfaceLayout->addWidget(m_transcriptRow, 0, Qt::AlignLeft | Qt::AlignTop);
			}

			QVBoxLayout *contentLayout = compactTranscript ? m_transcriptContentLayout : m_surfaceLayout;

			if (bubbleSpec.hasReply) {
				m_replyFrame = new QFrame(compactTranscript ? m_transcriptContentColumn : static_cast< QWidget * >(m_surface));
				m_replyFrame->setObjectName(QLatin1String("qfPersistentChatBubbleQuote"));
				m_replyFrame->setAttribute(Qt::WA_StyledBackground, true);
				m_replyFrame->setProperty("compactTranscript", compactTranscript);
				QVBoxLayout *replyLayout = new QVBoxLayout(m_replyFrame);
				replyLayout->setContentsMargins(compactTranscript ? 0 : 6, compactTranscript ? 0 : 3,
											 compactTranscript ? 0 : 6, compactTranscript ? 0 : 3);
				replyLayout->setSpacing(1);
				QLabel *replyActor = new QLabel(bubbleSpec.replyActor.toHtmlEscaped(), m_replyFrame);
				replyActor->setObjectName(QLatin1String("qlPersistentChatBubbleQuoteActor"));
				replyActor->setTextFormat(Qt::RichText);
				QLabel *replySnippet = new QLabel(bubbleSpec.replySnippet.toHtmlEscaped(), m_replyFrame);
				replySnippet->setObjectName(QLatin1String("qlPersistentChatBubbleQuoteSnippet"));
				replySnippet->setTextFormat(Qt::RichText);
				replySnippet->setWordWrap(true);
				replyLayout->addWidget(replyActor);
				replyLayout->addWidget(replySnippet);
				contentLayout->addWidget(m_replyFrame, 0, Qt::AlignLeft | Qt::AlignTop);
				registerTrackedObject(m_replyFrame);
				registerTrackedObject(replyActor);
				registerTrackedObject(replySnippet);
			}

			const bool previewSourceUrl = persistentChatHtmlIsPreviewSourceUrl(bubbleSpec.bodyHtml);
			int bodyContentWidth        = m_contentWidth;
			if (compactTranscript) {
				bodyContentWidth =
					std::max(PersistentChatCompactTranscriptMinBodyWidth,
							 m_contentWidth - PersistentChatCompactTranscriptTimeColumnWidth
								 - PersistentChatCompactTranscriptActorColumnWidth
								 - (PersistentChatCompactTranscriptColumnGap * 2));
				if (m_transcriptContentColumn) {
					m_transcriptContentColumn->setFixedWidth(bodyContentWidth);
				}
			}
			if (bubbleSpec.previewSpec.kind == PersistentChatPreviewKind::LinkCard) {
				bodyContentWidth =
					std::max(m_laneMetrics.previewMinWidth,
							 std::min(PersistentChatLinkPreviewMaxWidth, std::max(1, m_laneMetrics.previewMaxWidth)));
			} else if (previewSourceUrl && bubbleSpec.previewSpec.kind == PersistentChatPreviewKind::Image) {
				bodyContentWidth = std::min(m_contentWidth, std::max(1, m_laneMetrics.previewMaxWidth));
			}
			if (compactTranscript && m_transcriptContentColumn) {
				m_transcriptContentColumn->setFixedWidth(bodyContentWidth);
			}

			if (persistentChatHtmlHasVisibleContent(bubbleSpec.bodyHtml)) {
				mumble::chatperf::ScopedDuration bodyBrowserTrace("chat.group.bubble.create_body_browser");
				m_bodyBrowser = createEmbeddedBrowser(bubbleSpec.bodyHtml, bodyContentWidth, m_baseStylesheet,
													  bubbleSpec.imageResources, bubbleSpec.copyText);
				m_bodyWidget = m_bodyBrowser;
			}
			if (m_bodyBrowser) {
				m_bodyBrowser->setObjectName(QLatin1String("qtePersistentChatBodyBrowser"));
				connectBrowser(m_bodyBrowser);
			}
			if (m_bodyWidget) {
				registerTrackedObject(m_bodyWidget);
				contentLayout->addWidget(m_bodyWidget, 0, Qt::AlignLeft | Qt::AlignTop);
			}
			syncSurfaceShapeProperties();

			m_outerLayout->addWidget(m_surface, 0, compactTranscript ? (Qt::AlignLeft | Qt::AlignTop) : bubbleItemAlignment);

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

			{
				mumble::chatperf::ScopedDuration previewTrace("chat.group.bubble.set_preview");
				setPreviewContent(bubbleSpec.previewSpec);
			}
			registerTrackedObject(this);
			registerTrackedObject(m_surface);
		}

		bool isActive() const {
			return m_active;
		}

		void setClusterPosition(PersistentChatBubbleClusterPosition clusterPosition) {
			if (m_clusterPosition == clusterPosition) {
				return;
			}

			m_clusterPosition = clusterPosition;
			syncSurfaceShapeProperties();
		}

		bool setPreviewContent(const PersistentChatPreviewSpec &previewSpec) {
			const bool compactTranscript = m_displayMode == PersistentChatDisplayMode::CompactTranscript && !m_systemMessage;
			QVBoxLayout *contentLayout = compactTranscript && m_transcriptContentLayout ? m_transcriptContentLayout
																				 : m_surfaceLayout;
			const bool hadPreview = m_previewWidget != nullptr || m_previewContainer != nullptr;
			if (m_previewContainer) {
				if (contentLayout) {
					contentLayout->removeWidget(m_previewContainer);
				}
				delete m_previewContainer;
				m_previewContainer = nullptr;
				m_previewWidget = nullptr;
			}

			if (previewSpec.kind == PersistentChatPreviewKind::None) {
				syncSurfaceShapeProperties();
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

			m_previewWidget = new PersistentChatPreviewWidget(previewSpec, m_laneMetrics, m_displayMode, m_previewContainer);
			connect(m_previewWidget, SIGNAL(activated(QUrl)), this, SIGNAL(anchorClicked(QUrl)));
			const Qt::Alignment previewAlignment =
				compactTranscript ? (Qt::AlignLeft | Qt::AlignTop) : (m_bubbleContentAlignment | Qt::AlignTop);
			previewLayout->addWidget(m_previewWidget, 0, previewAlignment);
			registerWidgetTree(m_previewContainer);
			if (contentLayout) {
				contentLayout->addWidget(m_previewContainer, 0, previewAlignment);
			}
			syncSurfaceShapeProperties();
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
			if (!event) {
				return QFrame::eventFilter(watched, event);
			}

			switch (event->type()) {
				case QEvent::Resize:
					if (watched == m_surface) {
						updateSurfaceMask();
					}
					break;
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
		PersistentChatConversationLaneMetrics m_laneMetrics;
		PersistentChatDisplayMode m_displayMode = PersistentChatDisplayMode::Bubble;
		int m_contentWidth       = 0;
		QString m_copyText;
		bool m_systemMessage     = false;
		QString m_actionText;
		bool m_active            = false;
		QVBoxLayout *m_outerLayout = nullptr;
		QFrame *m_surface        = nullptr;
		QVBoxLayout *m_surfaceLayout = nullptr;
		QWidget *m_transcriptRow = nullptr;
		QLabel *m_transcriptTimeLabel = nullptr;
		QLabel *m_transcriptActorLabel = nullptr;
		QWidget *m_transcriptContentColumn = nullptr;
		QVBoxLayout *m_transcriptContentLayout = nullptr;
		QWidget *m_replyFrame    = nullptr;
		QWidget *m_bodyWidget    = nullptr;
		LogTextBrowser *m_bodyBrowser = nullptr;
		QWidget *m_previewContainer = nullptr;
		QWidget *m_previewWidget = nullptr;
		QWidget *m_actions       = nullptr;
		QToolButton *m_actionButton = nullptr;
		QToolButton *m_moreButton = nullptr;
		Qt::Alignment m_bubbleContentAlignment = Qt::AlignLeft;
		PersistentChatBubbleClusterPosition m_clusterPosition = PersistentChatBubbleClusterPosition::Single;
		bool m_embeddedBrowserCommitPending = false;
		QList< QPointer< QObject > > m_trackedObjects;

		void scheduleEmbeddedBrowserCommit(LogTextBrowser *browser) {
			if (!browser || m_embeddedBrowserCommitPending) {
				return;
			}

			m_embeddedBrowserCommitPending = true;
			QPointer< LogTextBrowser > guardedBrowser(browser);
			QTimer::singleShot(0, this, [this, guardedBrowser]() {
				m_embeddedBrowserCommitPending = false;
				if (!guardedBrowser) {
					return;
				}

				commitEmbeddedBrowserSize(guardedBrowser.data());
			});
		}

		void commitEmbeddedBrowserSize(LogTextBrowser *browser) {
			if (!browser) {
				return;
			}

			QSize explicitSize = browser->property("persistentChatExplicitSizeHint").toSize();
			if (!explicitSize.isValid()) {
				return;
			}
			const QSize previousExplicitSize = explicitSize;

			if (QTextDocument *document = browser->document()) {
				const bool singleLineBody = browser->property("persistentChatSingleLineBody").toBool();
				const int textWidth       = std::max(1, explicitSize.width());
				document->setTextWidth(textWidth);
				document->adjustSize();
				explicitSize = persistentChatDocumentSize(document, textWidth, true, singleLineBody, browser->font());
				browser->setProperty("persistentChatExplicitSizeHint", explicitSize);
			}

			const bool sizeChanged = explicitSize != previousExplicitSize || browser->minimumSize() != explicitSize
									 || browser->maximumSize() != explicitSize || browser->size() != explicitSize;
			if (!sizeChanged) {
				return;
			}

			browser->setMinimumSize(explicitSize);
			browser->setMaximumSize(explicitSize);
			browser->setFixedSize(explicitSize);
			browser->updateGeometry();
			if (m_surface) {
				m_surface->updateGeometry();
			}
			updateGeometry();
			repositionActions();
			emit contentSizeChanged();
		}

		void connectBrowser(LogTextBrowser *browser) {
			if (!browser) {
				return;
			}

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
			// Embedded chat browsers render static message content/resources. Re-listening to
			// document and scrollbar resize signals caused relayout churn during normal delegate
			// painting, which made large histories sluggish. Size once at creation instead.
			registerTrackedObject(browser);
			registerTrackedObject(browser->viewport());
			commitEmbeddedBrowserSize(browser);
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

		void syncSurfaceShapeProperties() {
			if (!m_surface) {
				return;
			}

			const bool compactTranscript = m_displayMode == PersistentChatDisplayMode::CompactTranscript && !m_systemMessage;
			if (m_surfaceLayout) {
				const int horizontalPadding =
					(compactTranscript || m_systemMessage) ? 0 : PersistentChatBubbleHorizontalPadding;
				const int verticalPadding =
					(compactTranscript || m_systemMessage) ? 0 : PersistentChatBubbleVerticalPadding;
				m_surfaceLayout->setContentsMargins(horizontalPadding, verticalPadding, horizontalPadding, verticalPadding);
			}
			const bool singleLineBubble =
				!compactTranscript && !m_systemMessage && !m_replyFrame && !m_previewContainer && m_bodyWidget
				&& m_bodyWidget->property("persistentChatSingleLineBody").toBool();
			m_surface->setProperty("singleLineBubble", singleLineBubble);
			m_surface->setProperty("clusterSingle", m_clusterPosition == PersistentChatBubbleClusterPosition::Single);
			m_surface->setProperty("clusterTop", m_clusterPosition == PersistentChatBubbleClusterPosition::Top);
			m_surface->setProperty("clusterMiddle", m_clusterPosition == PersistentChatBubbleClusterPosition::Middle);
			m_surface->setProperty("clusterBottom", m_clusterPosition == PersistentChatBubbleClusterPosition::Bottom);
			m_surface->setMinimumWidth(0);
			m_surface->setMinimumHeight(0);
			m_surface->style()->unpolish(m_surface);
			m_surface->style()->polish(m_surface);
			m_surface->update();
			updateSurfaceMask();
		}

		void updateSurfaceMask() {
			if (!m_surface) {
				return;
			}

			if (m_displayMode == PersistentChatDisplayMode::CompactTranscript || m_systemMessage || m_surface->width() <= 0
				|| m_surface->height() <= 0) {
				m_surface->clearMask();
				return;
			}

			const qreal roundRadius =
				std::min(PersistentChatBubbleCornerRadius, std::min(m_surface->width(), m_surface->height()) / 2.0);
			const qreal stackRadius =
				std::min(PersistentChatBubbleStackCornerRadius, std::min(m_surface->width(), m_surface->height()) / 2.0);

			qreal topLeftRadius     = roundRadius;
			qreal topRightRadius    = roundRadius;
			qreal bottomRightRadius = roundRadius;
			qreal bottomLeftRadius  = roundRadius;

			switch (m_clusterPosition) {
				case PersistentChatBubbleClusterPosition::Middle:
					if (persistentChatLaneIsTrailing(m_laneMetrics.anchor)) {
						topRightRadius    = stackRadius;
						bottomRightRadius = stackRadius;
					} else {
						topLeftRadius    = stackRadius;
						bottomLeftRadius = stackRadius;
					}
					break;
				case PersistentChatBubbleClusterPosition::Bottom:
					if (persistentChatLaneIsTrailing(m_laneMetrics.anchor)) {
						topRightRadius = stackRadius;
					} else {
						topLeftRadius = stackRadius;
					}
					break;
				case PersistentChatBubbleClusterPosition::Single:
				case PersistentChatBubbleClusterPosition::Top:
					break;
			}

			const QPainterPath clipPath =
				persistentChatRoundedRectPath(QRectF(m_surface->rect()), topLeftRadius, topRightRadius,
											  bottomRightRadius, bottomLeftRadius);
			m_surface->setMask(QRegion(clipPath.toFillPolygon().toPolygon()));
		}

		void repositionActions() {
			if (!m_actions || !m_surface) {
				return;
			}

			m_actions->adjustSize();
			const QSize actionSize = m_actions->sizeHint();
			const int horizontalInset = 12;
			const int x = persistentChatLaneIsTrailing(m_laneMetrics.anchor)
							  ? horizontalInset
							  : std::max(0, width() - actionSize.width() - horizontalInset);
			m_actions->move(x, 0);
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
	setProperty("compactTranscript", false);

	m_rowLayout = new QHBoxLayout(this);
	m_rowLayout->setContentsMargins(PersistentChatGroupHorizontalPadding, PersistentChatGroupVerticalPadding,
									PersistentChatGroupHorizontalPadding, PersistentChatGroupVerticalPadding);
	m_rowLayout->setSpacing(0);
	m_rowLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

	m_leadingSpacer = new QWidget(this);
	m_leadingSpacer->setObjectName(QLatin1String("qwPersistentChatLeadingSpacer"));
	m_leadingSpacer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	m_leadingSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	m_leadingSpacer->hide();

	m_leadingPresenceSlot = new QWidget(this);
	m_leadingPresenceSlot->setObjectName(QLatin1String("qwPersistentChatLeadingPresenceSlot"));
	m_leadingPresenceSlot->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	m_leadingPresenceSlot->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	QHBoxLayout *leadingPresenceLayout = new QHBoxLayout(m_leadingPresenceSlot);
	leadingPresenceLayout->setContentsMargins(0, 0, PersistentChatAvatarGap, 0);
	leadingPresenceLayout->setSpacing(0);

	m_leadingAvatarFrame = new PersistentChatAvatarWidget(m_leadingPresenceSlot);
	m_leadingAvatarFrame->setObjectName(QLatin1String("qfPersistentChatLeadingAvatarFrame"));
	m_leadingAvatarFrame->setFixedSize(PersistentChatAvatarSize, PersistentChatAvatarSize);
	leadingPresenceLayout->addWidget(m_leadingAvatarFrame, 0, Qt::AlignTop | Qt::AlignLeft);
	m_leadingPresenceSlot->hide();

	m_trailingSpacer = new QWidget(this);
	m_trailingSpacer->setObjectName(QLatin1String("qwPersistentChatTrailingSpacer"));
	m_trailingSpacer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	m_trailingSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	m_trailingSpacer->hide();

	m_trailingPresenceSlot = new QWidget(this);
	m_trailingPresenceSlot->setObjectName(QLatin1String("qwPersistentChatTrailingPresenceSlot"));
	m_trailingPresenceSlot->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	m_trailingPresenceSlot->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	QHBoxLayout *trailingPresenceLayout = new QHBoxLayout(m_trailingPresenceSlot);
	trailingPresenceLayout->setContentsMargins(PersistentChatAvatarGap, 0, 0, 0);
	trailingPresenceLayout->setSpacing(0);

	m_trailingAvatarFrame = new PersistentChatAvatarWidget(m_trailingPresenceSlot);
	m_trailingAvatarFrame->setObjectName(QLatin1String("qfPersistentChatTrailingAvatarFrame"));
	m_trailingAvatarFrame->setFixedSize(PersistentChatAvatarSize, PersistentChatAvatarSize);
	trailingPresenceLayout->addWidget(m_trailingAvatarFrame, 0, Qt::AlignTop | Qt::AlignLeft);
	m_trailingPresenceSlot->hide();

	m_contentColumn = new QWidget(this);
	m_contentColumn->setObjectName(QLatin1String("qwPersistentChatMessageColumn"));
	m_contentColumn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	QVBoxLayout *columnLayout = new QVBoxLayout(m_contentColumn);
	columnLayout->setContentsMargins(0, 0, 0, 0);
	columnLayout->setSpacing(1);
	columnLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

	m_headerWidget = new QWidget(m_contentColumn);
	m_headerWidget->setObjectName(QLatin1String("qwPersistentChatGroupHeader"));
	m_headerLayout = new QHBoxLayout(m_headerWidget);
	m_headerLayout->setContentsMargins(0, 0, 0, 0);
	m_headerLayout->setSpacing(1);

	m_headerLeadingSpacer = new QWidget(m_headerWidget);
	m_headerLeadingSpacer->setObjectName(QLatin1String("qwPersistentChatHeaderLeadingSpacer"));
	m_headerLeadingSpacer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	m_headerLeadingSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	m_headerLeadingSpacer->hide();

	m_actorLabel = new QLabel(m_headerWidget);
	m_actorLabel->setObjectName(QLatin1String("qlPersistentChatGroupActor"));
	m_actorLabel->setTextFormat(Qt::PlainText);

	m_timeLabel = new QLabel(m_headerWidget);
	m_timeLabel->setObjectName(QLatin1String("qlPersistentChatGroupTime"));

	m_scopeButton = new QToolButton(m_headerWidget);
	m_scopeButton->setObjectName(QLatin1String("qtbPersistentChatGroupScope"));
	m_scopeButton->setAutoRaise(true);
	m_scopeButton->hide();

	m_headerTrailingSpacer = new QWidget(m_headerWidget);
	m_headerTrailingSpacer->setObjectName(QLatin1String("qwPersistentChatHeaderTrailingSpacer"));
	m_headerTrailingSpacer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	m_headerTrailingSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	m_headerTrailingSpacer->hide();

	m_headerLayout->addWidget(m_headerLeadingSpacer, 1);
	m_headerLayout->addWidget(m_actorLabel);
	m_headerLayout->addWidget(m_timeLabel);
	m_headerLayout->addWidget(m_scopeButton);
	m_headerLayout->addWidget(m_headerTrailingSpacer, 1);

	columnLayout->addWidget(m_headerWidget);

	m_bubblesLayout = new QVBoxLayout();
	m_bubblesLayout->setContentsMargins(0, 0, 0, 0);
	m_bubblesLayout->setSpacing(PersistentChatGroupedBubbleSpacing);
	columnLayout->addLayout(m_bubblesLayout);

	m_rowLayout->addWidget(m_leadingSpacer, 1);
	m_rowLayout->addWidget(m_leadingPresenceSlot, 0, Qt::AlignTop);
	m_rowLayout->addWidget(m_contentColumn, 0, Qt::AlignTop);
	m_rowLayout->addWidget(m_trailingPresenceSlot, 0, Qt::AlignTop);
	m_rowLayout->addWidget(m_trailingSpacer, 1);
	m_rowLayout->setStretch(0, 0);
	m_rowLayout->setStretch(1, 0);
	m_rowLayout->setStretch(2, 0);
	m_rowLayout->setStretch(3, 0);
	m_rowLayout->setStretch(4, 0);

	applyConversationLaneMetrics(conversationLaneMetricsForWidth(m_availableWidth));
	syncMeasuredHeight();
}

void PersistentChatMessageGroupWidget::setHeader(const PersistentChatGroupHeaderSpec &headerSpec,
												 const QString &avatarFallbackText) {
	m_systemMessage = headerSpec.systemMessage;
	m_displayMode   = headerSpec.displayMode;
	m_selfAuthored  = headerSpec.selfAuthored && !headerSpec.systemMessage;
	const bool compactTranscript =
		m_displayMode == PersistentChatDisplayMode::CompactTranscript && !headerSpec.systemMessage;
	applyConversationLaneMetrics(
		conversationLaneMetricsForWidth(std::max(1, width() > 0 ? width() : m_availableWidth)));
	setProperty("selfAuthored", m_selfAuthored);
	setProperty("persistentChatSystem", headerSpec.systemMessage);
	setProperty("compactTranscript", compactTranscript);
	m_actorLabel->setText(headerSpec.actorLabel);
	const bool showActor =
		!compactTranscript && !headerSpec.systemMessage && !m_selfAuthored && !headerSpec.actorLabel.trimmed().isEmpty();
	const bool showTime  = !compactTranscript && !headerSpec.systemMessage && !headerSpec.timeLabel.trimmed().isEmpty();
	QColor timeColor = palette().color(QPalette::Mid);
	if (const std::optional< UiThemeTokens > tokens = activeUiThemeTokens(); tokens) {
		timeColor = tokens->overlay0;
	}
	m_actorLabel->setVisible(showActor);
	m_actorLabel->setStyleSheet(
		QString::fromLatin1("color: %1; font-size: 0.82em; font-weight: 600;")
			.arg(headerSpec.actorColor.isValid() ? headerSpec.actorColor.name() : QString()));
	m_actorLabel->setAlignment(persistentChatLaneTextAlignment(m_laneMetrics.anchor));

	m_timeLabel->setText(headerSpec.timeLabel.toHtmlEscaped());
	m_timeLabel->setToolTip(headerSpec.timeToolTip);
	m_timeLabel->setVisible(showTime);
	m_timeLabel->setStyleSheet(QString::fromLatin1("color: %1; font-size: 0.68em;")
								   .arg(timeColor.name(QColor::HexArgb)));
	m_timeLabel->setAlignment(persistentChatLaneTextAlignment(m_laneMetrics.anchor));

	bool showScope = false;
	if (!compactTranscript && headerSpec.aggregateScope && !headerSpec.scopeLabel.isEmpty()) {
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

	if (m_leadingAvatarFrame) {
		static_cast< PersistentChatAvatarWidget * >(m_leadingAvatarFrame)->setAvatar(
			headerSpec.avatarBackgroundColor, headerSpec.avatarForegroundColor,
			avatarFallbackText.isEmpty() ? QStringLiteral("?") : avatarFallbackText);
	}

	if (m_trailingAvatarFrame) {
		static_cast< PersistentChatAvatarWidget * >(m_trailingAvatarFrame)->setAvatar(
			headerSpec.avatarBackgroundColor, headerSpec.avatarForegroundColor,
			avatarFallbackText.isEmpty() ? QStringLiteral("?") : avatarFallbackText);
	}

	if (m_headerWidget) {
		m_headerWidget->setVisible(!compactTranscript && (showActor || showTime || showScope));
		m_headerWidget->setProperty("selfAuthored", m_selfAuthored);
	}

	if (m_contentColumn) {
		m_contentColumn->setProperty("selfAuthored", m_selfAuthored);
		m_contentColumn->setProperty("compactTranscript", compactTranscript);
	}

	setRowActive(false);
	syncMeasuredHeight();
}

void PersistentChatMessageGroupWidget::addBubble(const PersistentChatBubbleSpec &bubbleSpec) {
	mumble::chatperf::ScopedDuration trace("chat.group.add_bubble");
	applyConversationLaneMetrics(
		conversationLaneMetricsForWidth(std::max(1, width() > 0 ? width() : m_availableWidth)));
	auto *bubble = new PersistentChatBubbleWidget(bubbleSpec, m_laneMetrics, m_baseStylesheet, this);
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
		const int previousHeight = m_lastMeasuredHeight;
		syncMeasuredHeight();
		updateGeometry();
		if (m_lastMeasuredHeight != previousHeight) {
			emit contentUpdated();
		}
	});
	const Qt::Alignment bubbleAlignment = persistentChatLaneHorizontalAlignment(m_laneMetrics.anchor);
	m_bubblesLayout->addWidget(bubble, 0, bubbleAlignment | Qt::AlignTop);
	m_bubbleEntries.push_back(BubbleEntry { bubbleSpec.messageID, bubbleSpec.threadID, bubble });
	updateBubbleClusterShapes();
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
	const int hintWidth =
		std::max(1, width() > 0 ? width() : std::max(m_availableWidth, QWidget::sizeHint().width()));
	const int hintHeight = measuredHeightForWidth(hintWidth);
	return QSize(std::max(1, hintWidth), hintHeight);
}

QSize PersistentChatMessageGroupWidget::minimumSizeHint() const {
	const int hintWidth =
		std::max(1, width() > 0 ? width() : std::max(m_availableWidth, QWidget::minimumSizeHint().width()));
	const int hintHeight = measuredHeightForWidth(hintWidth);
	return QSize(hintWidth, hintHeight);
}

bool PersistentChatMessageGroupWidget::hasHeightForWidth() const {
	return true;
}

int PersistentChatMessageGroupWidget::heightForWidth(int width) const {
	return measuredHeightForWidth(width);
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
		case QEvent::LayoutRequest:
		case QEvent::PolishRequest:
		case QEvent::StyleChange:
		case QEvent::Show:
			syncMeasuredHeight();
			break;
		default:
			break;
	}

	return handled;
}

void PersistentChatMessageGroupWidget::resizeEvent(QResizeEvent *event) {
	QWidget::resizeEvent(event);
	m_availableWidth = std::max(1, event ? event->size().width() : m_availableWidth);
	applyConversationLaneMetrics(conversationLaneMetricsForWidth(m_availableWidth));
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
	const int referenceWidth =
		std::max(1, width() > 0 ? width() : std::max(m_availableWidth, QWidget::sizeHint().width()));
	return measuredHeightForWidth(referenceWidth);
}

int PersistentChatMessageGroupWidget::measuredHeightForWidth(int width) const {
	const int effectiveWidth = std::max(1, width);
	int heightHint = 0;
	if (QLayout *groupLayout = layout()) {
		groupLayout->activate();
		if (groupLayout->hasHeightForWidth()) {
			heightHint = std::max(heightHint, groupLayout->totalHeightForWidth(effectiveWidth));
		}
		heightHint = std::max(heightHint, groupLayout->sizeHint().height());
		heightHint = std::max(heightHint, groupLayout->minimumSize().height());
	}
	if (m_contentColumn) {
		if (QLayout *columnLayout = m_contentColumn->layout()) {
			columnLayout->activate();
			const PersistentChatConversationLaneMetrics laneMetrics = conversationLaneMetricsForWidth(effectiveWidth);
			const int columnWidth = std::max(1, laneMetrics.conversationLaneWidth);
			if (columnLayout->hasHeightForWidth()) {
				heightHint = std::max(heightHint, columnLayout->totalHeightForWidth(columnWidth));
			}
			heightHint = std::max(heightHint, columnLayout->sizeHint().height());
			heightHint = std::max(heightHint, columnLayout->minimumSize().height());
		}
		heightHint = std::max(heightHint, m_contentColumn->sizeHint().height());
		heightHint = std::max(heightHint, m_contentColumn->minimumSizeHint().height());
	}
	return std::max(1, heightHint);
}

PersistentChatConversationLaneMetrics PersistentChatMessageGroupWidget::conversationLaneMetricsForWidth(int width) const {
	PersistentChatConversationLaneMetrics metrics;
	const bool compactTranscript =
		m_displayMode == PersistentChatDisplayMode::CompactTranscript && !m_systemMessage;
	metrics.anchor =
		compactTranscript
			? PersistentChatConversationLaneAnchor::Leading
			: (m_systemMessage ? PersistentChatConversationLaneAnchor::Center
							   : (m_selfAuthored ? PersistentChatConversationLaneAnchor::Trailing
												 : PersistentChatConversationLaneAnchor::Leading));
	metrics.outerLeadingInset  = compactTranscript ? PersistentChatCompactTranscriptHorizontalInset
												  : PersistentChatGroupHorizontalPadding;
	metrics.outerTrailingInset = compactTranscript ? PersistentChatCompactTranscriptHorizontalInset
												   : PersistentChatGroupHorizontalPadding;
	metrics.verticalInset      = compactTranscript ? 0 : (m_systemMessage ? 1 : PersistentChatGroupVerticalPadding);
	metrics.showLeadingAvatar  = !compactTranscript && !m_systemMessage && !m_selfAuthored;
	metrics.showTrailingAvatar = !compactTranscript && !m_systemMessage && m_selfAuthored;
	metrics.leadingPresenceSlotWidth =
		(compactTranscript || m_systemMessage) ? 0 : PersistentChatConversationLaneReserve;
	metrics.trailingPresenceSlotWidth =
		(compactTranscript || m_systemMessage) ? 0 : PersistentChatConversationLaneReserve;

	const int effectiveWidth = std::max(1, width > 0 ? width : m_availableWidth);
	const int laneReserve =
		metrics.leadingPresenceSlotWidth + metrics.trailingPresenceSlotWidth;
	const int laneBudget  = std::max(
		1, effectiveWidth - metrics.outerLeadingInset - metrics.outerTrailingInset - laneReserve);
	const int laneFloor =
		compactTranscript ? std::min(PersistentChatCompactTranscriptMinBodyWidth, laneBudget)
						  : std::min(PersistentChatConversationLaneMinWidth, laneBudget);
	metrics.conversationLaneWidth =
		compactTranscript ? laneBudget : qBound(laneFloor, laneBudget, PersistentChatConversationLaneMaxWidth);
	metrics.bubbleMaxWidth        = metrics.conversationLaneWidth;
	if (compactTranscript) {
		metrics.bubbleMinWidth = 0;
		metrics.previewMaxWidth =
			std::min(metrics.bubbleMaxWidth, PersistentChatCompactTranscriptPreviewMaxWidth);
		metrics.previewMinWidth = std::min(metrics.previewMaxWidth, 160);
	} else {
		const int selfCompactBubbleWidth =
			std::max(PersistentChatSelfAuthoredSingleLineBubbleMinWidth,
					 static_cast< int >(std::lround(metrics.conversationLaneWidth * 0.60)));
		metrics.bubbleMinWidth =
			std::min(metrics.bubbleMaxWidth,
					 m_selfAuthored ? selfCompactBubbleWidth
									: PersistentChatSingleLineBubbleMinWidth);
		metrics.previewMaxWidth = metrics.bubbleMaxWidth;
		metrics.previewMinWidth = std::min(metrics.previewMaxWidth, PersistentChatLinkPreviewMinWidth);
	}
	return metrics;
}

void PersistentChatMessageGroupWidget::applyConversationLaneMetrics(
	const PersistentChatConversationLaneMetrics &metrics) {
	m_laneMetrics = metrics;

	if (m_rowLayout) {
		m_rowLayout->setDirection(QBoxLayout::LeftToRight);
		m_rowLayout->setContentsMargins(
			metrics.outerLeadingInset, metrics.verticalInset, metrics.outerTrailingInset, metrics.verticalInset);
		m_rowLayout->setSpacing(0);
		const bool leadingAnchor  = metrics.anchor == PersistentChatConversationLaneAnchor::Leading;
		const bool trailingAnchor = metrics.anchor == PersistentChatConversationLaneAnchor::Trailing;
		const bool centeredAnchor = metrics.anchor == PersistentChatConversationLaneAnchor::Center;
		m_rowLayout->setStretch(0, trailingAnchor || centeredAnchor ? 1 : 0);
		m_rowLayout->setStretch(1, 0);
		m_rowLayout->setStretch(2, 0);
		m_rowLayout->setStretch(3, 0);
		m_rowLayout->setStretch(4, leadingAnchor || centeredAnchor ? 1 : 0);
	}

	if (m_leadingSpacer) {
		m_leadingSpacer->setVisible(metrics.anchor != PersistentChatConversationLaneAnchor::Leading);
	}

	if (m_trailingSpacer) {
		m_trailingSpacer->setVisible(metrics.anchor != PersistentChatConversationLaneAnchor::Trailing);
	}

	if (m_leadingPresenceSlot) {
		m_leadingPresenceSlot->setVisible(metrics.leadingPresenceSlotWidth > 0);
		m_leadingPresenceSlot->setMinimumWidth(metrics.leadingPresenceSlotWidth);
		m_leadingPresenceSlot->setMaximumWidth(metrics.leadingPresenceSlotWidth);
	}

	if (m_trailingPresenceSlot) {
		m_trailingPresenceSlot->setVisible(metrics.trailingPresenceSlotWidth > 0);
		m_trailingPresenceSlot->setMinimumWidth(metrics.trailingPresenceSlotWidth);
		m_trailingPresenceSlot->setMaximumWidth(metrics.trailingPresenceSlotWidth);
	}

	if (m_leadingAvatarFrame) {
		m_leadingAvatarFrame->setVisible(metrics.showLeadingAvatar);
	}

	if (m_trailingAvatarFrame) {
		m_trailingAvatarFrame->setVisible(metrics.showTrailingAvatar);
	}

	if (m_contentColumn) {
		m_contentColumn->setMinimumWidth(metrics.conversationLaneWidth);
		m_contentColumn->setMaximumWidth(metrics.conversationLaneWidth);
	}

	if (m_headerLayout) {
		m_headerLayout->setDirection(QBoxLayout::LeftToRight);
		m_headerLayout->setStretch(0, metrics.anchor == PersistentChatConversationLaneAnchor::Leading ? 0 : 1);
		m_headerLayout->setStretch(1, 0);
		m_headerLayout->setStretch(2, 0);
		m_headerLayout->setStretch(3, 0);
		m_headerLayout->setStretch(4, metrics.anchor == PersistentChatConversationLaneAnchor::Trailing ? 0 : 1);
	}

	if (m_headerLeadingSpacer) {
		m_headerLeadingSpacer->setVisible(metrics.anchor != PersistentChatConversationLaneAnchor::Leading);
	}

	if (m_headerTrailingSpacer) {
		m_headerTrailingSpacer->setVisible(metrics.anchor != PersistentChatConversationLaneAnchor::Trailing);
	}
}

void PersistentChatMessageGroupWidget::updateBubbleClusterShapes() {
	QVector< PersistentChatBubbleWidget * > bubbles;
	bubbles.reserve(m_bubbleEntries.size());
	for (const BubbleEntry &entry : m_bubbleEntries) {
		if (auto *bubble = qobject_cast< PersistentChatBubbleWidget * >(entry.widget)) {
			bubbles.push_back(bubble);
		}
	}

	for (int i = 0; i < bubbles.size(); ++i) {
		PersistentChatBubbleClusterPosition clusterPosition = PersistentChatBubbleClusterPosition::Single;
		if (bubbles.size() > 1) {
			if (i == 0) {
				clusterPosition = PersistentChatBubbleClusterPosition::Top;
			} else if (i == bubbles.size() - 1) {
				clusterPosition = PersistentChatBubbleClusterPosition::Bottom;
			} else {
				clusterPosition = PersistentChatBubbleClusterPosition::Middle;
			}
		}

		bubbles.at(i)->setClusterPosition(clusterPosition);
	}
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
