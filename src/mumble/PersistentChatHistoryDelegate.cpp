// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatHistoryDelegate.h"

#include "ChatPerfTrace.h"
#include "UiTheme.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QPersistentModelIndex>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyle>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>

namespace {
	constexpr int PersistentChatMaxLiveWidgetRows = 64;

	QString applicationStyleSheet() {
		QApplication *application = qobject_cast< QApplication * >(QCoreApplication::instance());
		return application ? application->styleSheet() : QString();
	}

	QWidget *optionViewport(const QStyleOptionViewItem &option) {
		QWidget *widget = const_cast< QWidget * >(option.widget);
		if (!widget) {
			return nullptr;
		}

		if (auto *itemView = qobject_cast< QAbstractItemView * >(widget)) {
			return itemView->viewport() ? itemView->viewport() : itemView;
		}

		if (widget->parentWidget() && qobject_cast< QAbstractItemView * >(widget->parentWidget())) {
			return widget;
		}

		return widget;
	}

	int viewContentWidth(const QStyleOptionViewItem &option, int fallbackWidth = 420) {
		if (QWidget *viewport = optionViewport(option); viewport && viewport->width() > 0) {
			return viewport->width();
		}

		if (option.rect.width() > 0) {
			return option.rect.width();
		}

		return fallbackWidth;
	}

	QSize measuredItemHint(QWidget *widget, int itemWidth) {
		if (!widget) {
			return QSize(std::max(0, itemWidth), 1);
		}

		const int measuredWidth = std::max(0, itemWidth);
		const QVariant explicitHeight = widget->property("persistentChatItemHeight");
		int measuredHeight = explicitHeight.isValid() ? explicitHeight.toInt() : 0;
		if (widget->hasHeightForWidth()) {
			measuredHeight = std::max(measuredHeight, widget->heightForWidth(measuredWidth));
		}
		measuredHeight = std::max(measuredHeight, widget->sizeHint().height());
		measuredHeight = std::max(measuredHeight, widget->minimumSizeHint().height());
		if (QLayout *layout = widget->layout()) {
			if (layout->hasHeightForWidth()) {
				measuredHeight = std::max(measuredHeight, layout->totalHeightForWidth(measuredWidth));
			}
			measuredHeight = std::max(measuredHeight, layout->sizeHint().height());
			measuredHeight = std::max(measuredHeight, layout->minimumSize().height());
		}

		return QSize(measuredWidth, std::max(1, measuredHeight));
	}

	QWidget *deepestInteractiveChildAt(QWidget *rootWidget, const QPoint &localPos) {
		if (!rootWidget) {
			return nullptr;
		}

		QWidget *targetWidget = rootWidget;
		QPoint targetLocalPos = localPos;
		while (QWidget *childWidget = targetWidget->childAt(targetLocalPos)) {
			if (childWidget->testAttribute(Qt::WA_TransparentForMouseEvents)) {
				break;
			}

			targetLocalPos = childWidget->mapFrom(targetWidget, targetLocalPos);
			targetWidget   = childWidget;
		}

		return targetWidget;
	}

	QWidget *createStateWidget(const PersistentChatStateRowSpec &stateRow, QWidget *parent, int width) {
		QWidget *widget = new QWidget(parent);
		widget->setAttribute(Qt::WA_StyledBackground, true);
		widget->setProperty("persistentChatItemHeight", std::max(180, stateRow.minimumHeight));

		QVBoxLayout *layout = new QVBoxLayout(widget);
		layout->setContentsMargins(20, 12, 20, 12);
		layout->setSpacing(0);
		layout->addStretch(1);

		QFrame *card = new QFrame(widget);
		card->setObjectName(QLatin1String("qfPersistentChatBanner"));
		card->setAttribute(Qt::WA_StyledBackground, true);
		card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
		card->setMaximumWidth(std::max(320, std::min(440, width - 40)));

		QVBoxLayout *cardLayout = new QVBoxLayout(card);
		cardLayout->setContentsMargins(18, 18, 18, 18);
		cardLayout->setSpacing(8);

		QLabel *eyebrowLabel = new QLabel(stateRow.eyebrow, card);
		eyebrowLabel->setObjectName(QLatin1String("qlPersistentChatBannerEyebrow"));
		eyebrowLabel->setTextFormat(Qt::PlainText);
		QFont eyebrowFont = eyebrowLabel->font();
		eyebrowFont.setCapitalization(QFont::AllUppercase);
		eyebrowFont.setBold(true);
		eyebrowFont.setPointSizeF(std::max(eyebrowFont.pointSizeF() - 1.0, 8.0));
		eyebrowLabel->setFont(eyebrowFont);

		QLabel *titleLabel = new QLabel(stateRow.title, card);
		titleLabel->setObjectName(QLatin1String("qlPersistentChatBannerTitle"));
		titleLabel->setTextFormat(Qt::PlainText);
		titleLabel->setWordWrap(true);
		QFont titleFont = titleLabel->font();
		titleFont.setBold(true);
		titleFont.setPointSizeF(titleFont.pointSizeF() + 2.0);
		titleLabel->setFont(titleFont);

		QLabel *bodyLabel = new QLabel(stateRow.body, card);
		bodyLabel->setObjectName(QLatin1String("qlPersistentChatBannerBody"));
		bodyLabel->setTextFormat(Qt::PlainText);
		bodyLabel->setWordWrap(true);
		bodyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);

		cardLayout->addWidget(eyebrowLabel);
		cardLayout->addWidget(titleLabel);
		cardLayout->addWidget(bodyLabel);

		if (!stateRow.hints.isEmpty()) {
			QWidget *hintRow = new QWidget(card);
			hintRow->setObjectName(QLatin1String("qwPersistentChatBannerHints"));
			QHBoxLayout *hintLayout = new QHBoxLayout(hintRow);
			hintLayout->setContentsMargins(0, 4, 0, 0);
			hintLayout->setSpacing(6);
			hintLayout->addStretch(1);
			for (const QString &hint : stateRow.hints) {
				if (hint.trimmed().isEmpty()) {
					continue;
				}

				QLabel *hintLabel = new QLabel(hint, hintRow);
				hintLabel->setObjectName(QLatin1String("qlPersistentChatBannerHint"));
				hintLabel->setAttribute(Qt::WA_StyledBackground, true);
				hintLabel->setTextFormat(Qt::PlainText);
				hintLayout->addWidget(hintLabel);
			}
			hintLayout->addStretch(1);
			cardLayout->addWidget(hintRow);
		}

		layout->addWidget(card, 0, Qt::AlignHCenter);
		layout->addStretch(1);
		return widget;
	}

	QWidget *createPillWidget(const QString &text, const QString &objectName, QWidget *parent) {
		QLabel *label = new QLabel(parent);
		label->setObjectName(objectName);
		label->setAttribute(Qt::WA_StyledBackground, true);
		label->setTextFormat(Qt::RichText);
		label->setWordWrap(true);
		label->setAlignment(Qt::AlignCenter);
		label->setText(text);
		label->setContentsMargins(20, 6, 20, 6);
		return label;
	}

	QWidget *createDateDividerWidget(const PersistentChatTextRowSpec &textRow, QWidget *parent) {
		const bool compactTranscript = textRow.displayMode == PersistentChatDisplayMode::CompactTranscript;
		QWidget *divider = new QWidget(parent);
		divider->setObjectName(QLatin1String("qwPersistentChatDateDivider"));
		divider->setAttribute(Qt::WA_StyledBackground, true);
		divider->setProperty("compactTranscript", compactTranscript);
		QHBoxLayout *layout = new QHBoxLayout(divider);
		layout->setContentsMargins(compactTranscript ? 96 : 188, compactTranscript ? 8 : 1,
								   compactTranscript ? 96 : 188, compactTranscript ? 8 : 1);
		layout->setSpacing(compactTranscript ? 10 : 6);

		QFrame *leftLine = new QFrame(divider);
		leftLine->setObjectName(QLatin1String("qfPersistentChatDateDividerLine"));
		leftLine->setProperty("compactTranscript", compactTranscript);
		leftLine->setFixedHeight(compactTranscript ? 2 : 1);
		leftLine->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

		QLabel *label = new QLabel(textRow.text.toUpper(), divider);
		label->setObjectName(QLatin1String("qlPersistentChatDateDividerText"));
		label->setProperty("compactTranscript", compactTranscript);
		label->setAlignment(Qt::AlignCenter);
		QFont labelFont = label->font();
		labelFont.setBold(compactTranscript);
		labelFont.setPointSizeF(
			std::max(labelFont.pointSizeF() - (compactTranscript ? 1.4 : 3.8), compactTranscript ? 8.5 : 6.5));
		label->setFont(labelFont);
		label->setContentsMargins(0, 0, 0, 0);
		label->setStyleSheet(compactTranscript
								 ? QStringLiteral("font-size: 8px; font-weight: 700; letter-spacing: 0.22em;")
								 : QStringLiteral("font-size: 6px; font-weight: 400; letter-spacing: 0.18em;"));

		QFrame *rightLine = new QFrame(divider);
		rightLine->setObjectName(QLatin1String("qfPersistentChatDateDividerLine"));
		rightLine->setProperty("compactTranscript", compactTranscript);
		rightLine->setFixedHeight(compactTranscript ? 2 : 1);
		rightLine->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

		layout->addWidget(leftLine);
		layout->addWidget(label);
		layout->addWidget(rightLine);
		return divider;
	}
}

PersistentChatHistoryDelegate::PersistentChatHistoryDelegate(QObject *parent) : QStyledItemDelegate(parent) {
}

PersistentChatHistoryDelegate::~PersistentChatHistoryDelegate() {
	clearCache();
}

void PersistentChatHistoryDelegate::clearCache() {
	for (auto it = m_widgetCache.begin(); it != m_widgetCache.end(); ++it) {
		delete it.value().widget;
	}
	m_widgetCache.clear();
}

bool PersistentChatHistoryDelegate::updateBubblePreview(const PersistentChatHistoryModel *model, unsigned int messageID,
														unsigned int threadID,
														const PersistentChatPreviewSpec &previewSpec) {
	for (auto it = m_widgetCache.begin(); it != m_widgetCache.end(); ++it) {
		WidgetCacheEntry &cacheEntry = it.value();
		touchCacheEntry(cacheEntry);
		auto *groupWidget = qobject_cast< PersistentChatMessageGroupWidget * >(cacheEntry.widget);
		if (!groupWidget || !groupWidget->updateBubblePreview(messageID, threadID, previewSpec)) {
			continue;
		}

		const QVariant explicitHeight = groupWidget->property("persistentChatItemHeight");
		const int height = explicitHeight.isValid() ? explicitHeight.toInt() : groupWidget->sizeHint().height();
		updateCachedWidgetHeight(it.key(), height);
		invalidateCachedRendering(it.key());

		if (model) {
			const int row = model->rowForId(it.key());
			if (row >= 0) {
				emit sizeHintChanged(model->index(row, 0));
			}
		}

		return true;
	}

	return false;
}

void PersistentChatHistoryDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
										  const QModelIndex &index) const {
	mumble::chatperf::ScopedDuration trace("chat.delegate.paint");
	QWidget *widget = widgetForIndex(index, viewContentWidth(option), optionViewport(option));
	if (!widget || !painter) {
		return;
	}

	const auto *model = qobject_cast< const PersistentChatHistoryModel * >(index.model());
	const PersistentChatHistoryRow *row = model ? model->rowAt(index.row()) : nullptr;
	QPixmap cachedPixmap;
	if (row && !row->rowId.isEmpty()) {
		auto it = m_widgetCache.find(row->rowId);
		if (it != m_widgetCache.end() && it->widget) {
			WidgetCacheEntry &cacheEntry = it.value();
			touchCacheEntry(cacheEntry);
			const QSize paintSize =
				cacheEntry.measuredSize.isValid() ? cacheEntry.measuredSize : measuredItemHint(widget, viewContentWidth(option));
			const qreal devicePixelRatio = painter->device() ? painter->device()->devicePixelRatioF() : qApp->devicePixelRatio();
			const QSize deviceSize = QSize(std::max(1, qRound(paintSize.width() * devicePixelRatio)),
										   std::max(1, qRound(paintSize.height() * devicePixelRatio)));
			if (cacheEntry.pixmapDirty || cacheEntry.renderedPixmap.isNull()
				|| cacheEntry.renderedPixmap.deviceIndependentSize().toSize() != paintSize) {
				QPixmap renderedPixmap(deviceSize);
				renderedPixmap.setDevicePixelRatio(devicePixelRatio);
				renderedPixmap.fill(Qt::transparent);
				QPainter pixmapPainter(&renderedPixmap);
				cacheEntry.widget->render(&pixmapPainter, QPoint(0, 0), QRegion(), QWidget::DrawChildren);
				cacheEntry.renderedPixmap = renderedPixmap;
				cacheEntry.pixmapDirty    = false;
			}
			cachedPixmap = cacheEntry.renderedPixmap;
		}
	}

	painter->save();
	painter->translate(option.rect.topLeft());
	if (!cachedPixmap.isNull()) {
		mumble::chatperf::recordValue("chat.delegate.paint.cache_hit", 1);
		painter->drawPixmap(0, 0, cachedPixmap);
	} else {
		mumble::chatperf::recordValue("chat.delegate.paint.cache_miss", 1);
		widget->render(painter, QPoint(0, 0), QRegion(), QWidget::DrawChildren);
	}
	painter->restore();
}

QSize PersistentChatHistoryDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
	const auto *model = qobject_cast< const PersistentChatHistoryModel * >(index.model());
	const PersistentChatHistoryRow *row = model ? model->rowAt(index.row()) : nullptr;
	int measuredWidth = viewContentWidth(option);
	if (measuredWidth <= 0 && row && !row->rowId.isEmpty()) {
		const auto it = m_widgetCache.constFind(row->rowId);
		if (it != m_widgetCache.cend() && it.value().width > 0) {
			measuredWidth = it.value().width;
		}
	}
	measuredWidth = std::max(1, measuredWidth > 0 ? measuredWidth : 420);

	mumble::chatperf::ScopedDuration trace("chat.delegate.size_hint");
	if (!row || row->rowId.isEmpty()) {
		QWidget *widget = widgetForIndex(index, measuredWidth);
		return widget ? measuredItemHint(widget, measuredWidth) : QSize(measuredWidth, 1);
	}

	const auto it = m_widgetCache.constFind(row->rowId);
	if (it != m_widgetCache.cend() && it.value().widget && it.value().width == measuredWidth
		&& it.value().measuredSize.isValid()) {
		return it.value().measuredSize;
	}

	QWidget *widget = widgetForIndex(index, measuredWidth, optionViewport(option));
	return widget ? measuredItemHint(widget, measuredWidth) : QSize(measuredWidth, 1);
}

bool PersistentChatHistoryDelegate::editorEvent(QEvent *event, QAbstractItemModel *, const QStyleOptionViewItem &option,
												const QModelIndex &index) {
	mumble::chatperf::ScopedDuration trace("chat.delegate.editor_event");
	QWidget *widget = widgetForIndex(index, viewContentWidth(option), optionViewport(option));
	if (!widget) {
		return false;
	}

	switch (event ? event->type() : QEvent::None) {
		case QEvent::MouseMove:
			mumble::chatperf::recordValue("chat.delegate.editor_event.mouse_move", 1);
			break;
		case QEvent::MouseButtonPress:
			mumble::chatperf::recordValue("chat.delegate.editor_event.mouse_press", 1);
			break;
		case QEvent::MouseButtonRelease:
			mumble::chatperf::recordValue("chat.delegate.editor_event.mouse_release", 1);
			break;
		case QEvent::MouseButtonDblClick:
			mumble::chatperf::recordValue("chat.delegate.editor_event.mouse_double_click", 1);
			break;
		default:
			break;
	}

	const auto *model = qobject_cast< const PersistentChatHistoryModel * >(index.model());
	const PersistentChatHistoryRow *row = model ? model->rowAt(index.row()) : nullptr;
	const bool handled = forwardEditorEvent(widget, event, option);
	if (row && !row->rowId.isEmpty()) {
		invalidateCachedRendering(row->rowId);
	}

	if (QWidget *viewport = optionViewport(option)) {
		viewport->update(option.rect);
	}

	return handled;
}

QWidget *PersistentChatHistoryDelegate::widgetForIndex(const QModelIndex &index, int width, QWidget *host) const {
	mumble::chatperf::ScopedDuration trace("chat.delegate.widget_for_index");
	const auto *model = qobject_cast< const PersistentChatHistoryModel * >(index.model());
	const PersistentChatHistoryRow *row = model ? model->rowAt(index.row()) : nullptr;
	if (!row || row->rowId.isEmpty()) {
		return nullptr;
	}

	if (host) {
		m_cacheHost = host;
	} else {
		host = m_cacheHost;
	}

	WidgetCacheEntry &cacheEntry = m_widgetCache[row->rowId];
	touchCacheEntry(cacheEntry);
	const QString stylesheet     = applicationStyleSheet();
	const int measuredWidth      = std::max(1, width);
	const bool hostChanged       = cacheEntry.widget && host && cacheEntry.widget->parentWidget() != host;
	if (cacheEntry.widget && (cacheEntry.width != measuredWidth || cacheEntry.signature != row->signature
							  || cacheEntry.widget->property("persistentChatStylesheet").toString() != stylesheet
							  || hostChanged)) {
		if (cacheEntry.width != measuredWidth) {
			mumble::chatperf::recordValue("chat.delegate.widget_recreate.width", 1);
		} else if (cacheEntry.signature != row->signature) {
			mumble::chatperf::recordValue("chat.delegate.widget_recreate.signature", 1);
		} else if (hostChanged) {
			mumble::chatperf::recordValue("chat.delegate.widget_recreate.host", 1);
		} else {
			mumble::chatperf::recordValue("chat.delegate.widget_recreate.stylesheet", 1);
		}
		delete cacheEntry.widget;
		cacheEntry.widget = nullptr;
		cacheEntry.renderedPixmap = QPixmap();
		cacheEntry.pixmapDirty    = true;
		if (cacheEntry.width != measuredWidth || cacheEntry.signature != row->signature) {
			cacheEntry.measuredSize = QSize();
		}
	}

	if (!cacheEntry.widget) {
		mumble::chatperf::recordValue("chat.delegate.widget_create", 1);
		cacheEntry.rowId     = row->rowId;
		cacheEntry.signature = row->signature;
		cacheEntry.width     = measuredWidth;
		cacheEntry.widget    = createWidgetForRow(*row, measuredWidth, host);
		if (!cacheEntry.widget) {
			return nullptr;
		}

		if (auto *groupWidget = qobject_cast< PersistentChatMessageGroupWidget * >(cacheEntry.widget)) {
			const QPersistentModelIndex persistentIndex(index);
			auto *delegate = const_cast< PersistentChatHistoryDelegate * >(this);
			const QString rowId = row->rowId;
			connect(groupWidget, &PersistentChatMessageGroupWidget::measuredHeightChanged, this,
					[delegate, persistentIndex, rowId](int height) {
						delegate->updateCachedWidgetHeight(rowId, height);
						delegate->invalidateCachedRendering(rowId);
						if (persistentIndex.isValid()) {
							emit delegate->sizeHintChanged(persistentIndex);
						}
					});
			connect(groupWidget, &PersistentChatMessageGroupWidget::contentUpdated, this,
					[delegate, persistentIndex, rowId]() {
						delegate->invalidateCachedRendering(rowId);
						if (persistentIndex.isValid()) {
							emit delegate->sizeHintChanged(persistentIndex);
						}
					});
		}

		cacheEntry.widget->setProperty("persistentChatStylesheet", stylesheet);
		cacheEntry.widget->setAttribute(Qt::WA_DontShowOnScreen, true);
		cacheEntry.widget->move(-4096, -4096);
		cacheEntry.widget->show();
	}

	syncWidgetLayout(cacheEntry, measuredWidth);
	pruneCachedWidgets(row->rowId);
	return cacheEntry.widget;
}

QWidget *PersistentChatHistoryDelegate::createWidgetForRow(const PersistentChatHistoryRow &row, int width,
														   QWidget *parent) const {
	mumble::chatperf::ScopedDuration trace("chat.delegate.create_widget");
	switch (row.kind) {
		case PersistentChatHistoryRowKind::State:
			return row.state ? createStateWidget(*row.state, parent, width) : nullptr;
		case PersistentChatHistoryRowKind::LoadOlder: {
			if (!row.loadOlder) {
				return nullptr;
			}

			QPushButton *button = new QPushButton(row.loadOlder->text, parent);
			button->setObjectName(QLatin1String("qpbPersistentChatLoadOlder"));
			button->setFlat(true);
			button->setEnabled(row.loadOlder->enabled);
			connect(button, &QPushButton::clicked, this, &PersistentChatHistoryDelegate::loadOlderRequested);
			return button;
		}
		case PersistentChatHistoryRowKind::DateDivider:
			return row.text ? createDateDividerWidget(*row.text, parent) : nullptr;
		case PersistentChatHistoryRowKind::UnreadDivider:
			return row.text
					   ? createPillWidget(QString::fromLatin1("<strong>%1</strong>").arg(row.text->text.toHtmlEscaped()),
										  QLatin1String("qlPersistentChatUnreadPill"), parent)
					   : nullptr;
		case PersistentChatHistoryRowKind::MessageGroup: {
			if (!row.messageGroup) {
				return nullptr;
			}

			auto *groupWidget = new PersistentChatMessageGroupWidget(std::max(232, width - 22), applicationStyleSheet(),
																	 parent);
			groupWidget->setHeader(row.messageGroup->header, row.messageGroup->avatarFallbackText);
			for (const PersistentChatBubbleSpec &bubble : row.messageGroup->bubbles) {
				if (!bubble.previewKey.isEmpty() && bubble.previewSpec.kind == PersistentChatPreviewKind::None) {
					emit const_cast< PersistentChatHistoryDelegate * >(this)->previewRequested(bubble.previewKey);
				}
				groupWidget->addBubble(bubble);
			}

			for (QWidget *actionsWidget : groupWidget->findChildren< QWidget * >(QLatin1String("qwPersistentChatBubbleActions"))) {
				actionsWidget->raise();
			}

			connect(groupWidget, &PersistentChatMessageGroupWidget::replyRequested, this,
					&PersistentChatHistoryDelegate::replyRequested);
			connect(groupWidget, &PersistentChatMessageGroupWidget::scopeJumpRequested, this,
					&PersistentChatHistoryDelegate::scopeJumpRequested);
			connect(groupWidget, &PersistentChatMessageGroupWidget::logContextMenuRequested, this,
					&PersistentChatHistoryDelegate::logContextMenuRequested);
			connect(groupWidget, &PersistentChatMessageGroupWidget::logImageActivated, this,
					&PersistentChatHistoryDelegate::logImageActivated);
			connect(groupWidget, &PersistentChatMessageGroupWidget::anchorClicked, this,
					&PersistentChatHistoryDelegate::anchorClicked);
			connect(groupWidget, &PersistentChatMessageGroupWidget::highlighted, this,
					&PersistentChatHistoryDelegate::highlighted);
			return groupWidget;
		}
	}

	return nullptr;
}

void PersistentChatHistoryDelegate::syncWidgetLayout(WidgetCacheEntry &cacheEntry, int width) const {
	mumble::chatperf::ScopedDuration trace("chat.delegate.sync_layout");
	if (!cacheEntry.widget) {
		return;
	}

	const int measuredWidth = std::max(1, width);
	const bool widthChanged =
		cacheEntry.width != measuredWidth || cacheEntry.widget->width() != measuredWidth;
	if (!widthChanged && cacheEntry.measuredSize.isValid()) {
		return;
	}

	cacheEntry.width = measuredWidth;
	cacheEntry.widget->setFixedWidth(measuredWidth);
	cacheEntry.widget->ensurePolished();
	QSize stabilizedSize(std::max(1, measuredWidth), 1);
	for (int pass = 0; pass < 3; ++pass) {
		if (QLayout *layout = cacheEntry.widget->layout()) {
			layout->activate();
		}
		cacheEntry.widget->updateGeometry();
		cacheEntry.widget->adjustSize();
		const QSize measuredSize = measuredItemHint(cacheEntry.widget, measuredWidth);
		cacheEntry.widget->resize(measuredSize);
		if (measuredSize == stabilizedSize && pass > 0) {
			break;
		}
		stabilizedSize = measuredSize;
	}
	cacheEntry.measuredSize = stabilizedSize;
	cacheEntry.widget->resize(cacheEntry.measuredSize);
	cacheEntry.renderedPixmap = QPixmap();
	cacheEntry.pixmapDirty    = true;
}

void PersistentChatHistoryDelegate::updateCachedWidgetHeight(const QString &rowId, int height) {
	auto it = m_widgetCache.find(rowId);
	if (it == m_widgetCache.end() || !it->widget) {
		return;
	}

	it->measuredSize = QSize(std::max(1, it->width), std::max(1, height));
	it->widget->resize(it->measuredSize);
	it->renderedPixmap = QPixmap();
	it->pixmapDirty    = true;
}

void PersistentChatHistoryDelegate::invalidateCachedRendering(const QString &rowId) {
	auto it = m_widgetCache.find(rowId);
	if (it == m_widgetCache.end()) {
		return;
	}

	it->renderedPixmap = QPixmap();
	it->pixmapDirty    = true;
}

void PersistentChatHistoryDelegate::invalidateAllCachedRendering() const {
	for (auto it = m_widgetCache.begin(); it != m_widgetCache.end(); ++it) {
		it->renderedPixmap = QPixmap();
		it->pixmapDirty    = true;
	}
}

void PersistentChatHistoryDelegate::touchCacheEntry(WidgetCacheEntry &cacheEntry) const {
	cacheEntry.lastAccessSerial = ++m_cacheAccessSerial;
}

void PersistentChatHistoryDelegate::pruneCachedWidgets(const QString &preserveRowId) const {
	int liveWidgetCount = 0;
	for (auto it = m_widgetCache.cbegin(); it != m_widgetCache.cend(); ++it) {
		if (it.value().widget) {
			++liveWidgetCount;
		}
	}

	while (liveWidgetCount > PersistentChatMaxLiveWidgetRows) {
		auto evictionIt = m_widgetCache.end();
		for (auto it = m_widgetCache.begin(); it != m_widgetCache.end(); ++it) {
			if (!it.value().widget || it.key() == preserveRowId) {
				continue;
			}

			if (evictionIt == m_widgetCache.end()
				|| it.value().lastAccessSerial < evictionIt.value().lastAccessSerial) {
				evictionIt = it;
			}
		}

		if (evictionIt == m_widgetCache.end()) {
			return;
		}

		delete evictionIt.value().widget;
		evictionIt->widget = nullptr;
		evictionIt->renderedPixmap = QPixmap();
		evictionIt->pixmapDirty    = true;
		--liveWidgetCount;
	}
}

bool PersistentChatHistoryDelegate::forwardEditorEvent(QWidget *rootWidget, QEvent *event,
													   const QStyleOptionViewItem &option) const {
	mumble::chatperf::ScopedDuration trace("chat.delegate.forward_editor_event");
	if (!rootWidget || !event) {
		return false;
	}

	const QSize desiredSize(viewContentWidth(option), std::max(option.rect.height(), rootWidget->height()));
	if (rootWidget->size() != desiredSize) {
		rootWidget->resize(desiredSize);
		if (QLayout *layout = rootWidget->layout()) {
			layout->activate();
		}
	}

	switch (event->type()) {
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::MouseButtonDblClick:
		case QEvent::MouseMove: {
			const auto *mouseEvent = static_cast< const QMouseEvent * >(event);
			const QPoint localPos  = mouseEvent->pos() - option.rect.topLeft();
			QWidget *targetWidget  = deepestInteractiveChildAt(rootWidget, localPos);
			if (!targetWidget) {
				targetWidget = rootWidget;
			}

			const QPoint targetLocalPos = targetWidget->mapFrom(rootWidget, localPos);
			QMouseEvent translatedEvent(mouseEvent->type(), QPointF(targetLocalPos), mouseEvent->globalPosition(),
										mouseEvent->button(), mouseEvent->buttons(), mouseEvent->modifiers(),
										mouseEvent->pointingDevice());
			QCoreApplication::sendEvent(targetWidget, &translatedEvent);
			return translatedEvent.isAccepted();
		}
		case QEvent::ContextMenu: {
			const auto *contextEvent = static_cast< const QContextMenuEvent * >(event);
			const QPoint localPos    = contextEvent->pos() - option.rect.topLeft();
			QWidget *targetWidget    = deepestInteractiveChildAt(rootWidget, localPos);
			if (!targetWidget) {
				targetWidget = rootWidget;
			}

			const QPoint targetLocalPos = targetWidget->mapFrom(rootWidget, localPos);
			QContextMenuEvent translatedEvent(contextEvent->reason(), targetLocalPos, contextEvent->globalPos(),
											  contextEvent->modifiers());
			QCoreApplication::sendEvent(targetWidget, &translatedEvent);
			return translatedEvent.isAccepted();
		}
		default:
			break;
	}

	return false;
}
