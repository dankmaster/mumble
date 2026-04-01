// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatHistoryDelegate.h"

#include "UiTheme.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyle>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>

namespace {
	QString applicationStyleSheet() {
		QApplication *application = qobject_cast< QApplication * >(QCoreApplication::instance());
		return application ? application->styleSheet() : QString();
	}

	QSize measuredItemHint(QWidget *widget, int itemWidth) {
		if (!widget) {
			return QSize(std::max(0, itemWidth), 1);
		}

		const int measuredWidth = std::max(0, itemWidth);
		widget->ensurePolished();
		widget->setFixedWidth(measuredWidth);
		if (QLayout *layout = widget->layout()) {
			layout->activate();
		}
		widget->updateGeometry();
		widget->adjustSize();

		const QVariant explicitHeight = widget->property("persistentChatItemHeight");
		int measuredHeight            = explicitHeight.isValid() ? explicitHeight.toInt() : widget->sizeHint().height();
		if (QLayout *layout = widget->layout()) {
			measuredHeight = std::max(measuredHeight, layout->sizeHint().height());
		}

		measuredHeight = std::max(measuredHeight, widget->minimumSizeHint().height());
		return QSize(measuredWidth, std::max(1, measuredHeight));
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

	QWidget *createDateDividerWidget(const QString &text, QWidget *parent) {
		QWidget *divider = new QWidget(parent);
		divider->setObjectName(QLatin1String("qwPersistentChatDateDivider"));
		divider->setAttribute(Qt::WA_StyledBackground, true);
		QHBoxLayout *layout = new QHBoxLayout(divider);
		layout->setContentsMargins(16, 8, 16, 8);
		layout->setSpacing(12);

		QFrame *leftLine = new QFrame(divider);
		leftLine->setObjectName(QLatin1String("qfPersistentChatDateDividerLine"));
		leftLine->setFixedHeight(1);
		leftLine->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

		QLabel *label = new QLabel(text.toUpper(), divider);
		label->setObjectName(QLatin1String("qlPersistentChatDateDividerText"));
		label->setAlignment(Qt::AlignCenter);

		QFrame *rightLine = new QFrame(divider);
		rightLine->setObjectName(QLatin1String("qfPersistentChatDateDividerLine"));
		rightLine->setFixedHeight(1);
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

void PersistentChatHistoryDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
										  const QModelIndex &index) const {
	QWidget *widget = widgetForIndex(index, option.rect.width());
	if (!widget || !painter) {
		return;
	}

	painter->save();
	painter->translate(option.rect.topLeft());
	widget->render(painter, QPoint(0, 0), QRegion(), QWidget::DrawChildren);
	painter->restore();
}

QSize PersistentChatHistoryDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
	QWidget *widget = widgetForIndex(index, option.rect.width() > 0 ? option.rect.width() : 420);
	return measuredItemHint(widget, option.rect.width() > 0 ? option.rect.width() : 420);
}

bool PersistentChatHistoryDelegate::editorEvent(QEvent *event, QAbstractItemModel *, const QStyleOptionViewItem &option,
												const QModelIndex &index) {
	QWidget *widget = widgetForIndex(index, option.rect.width());
	return widget ? forwardEditorEvent(widget, event, option) : false;
}

QWidget *PersistentChatHistoryDelegate::widgetForIndex(const QModelIndex &index, int width) const {
	const auto *model = qobject_cast< const PersistentChatHistoryModel * >(index.model());
	const PersistentChatHistoryRow *row = model ? model->rowAt(index.row()) : nullptr;
	if (!row || row->rowId.isEmpty()) {
		return nullptr;
	}

	WidgetCacheEntry &cacheEntry = m_widgetCache[row->rowId];
	const QString stylesheet     = applicationStyleSheet();
	if (cacheEntry.widget && (cacheEntry.width != width || cacheEntry.signature != row->signature
							  || cacheEntry.widget->property("persistentChatStylesheet").toString() != stylesheet)) {
		delete cacheEntry.widget;
		cacheEntry.widget = nullptr;
	}

	if (!cacheEntry.widget) {
		cacheEntry.rowId     = row->rowId;
		cacheEntry.signature = row->signature;
		cacheEntry.width     = width;
		cacheEntry.widget    = createWidgetForRow(*row, std::max(1, width));
		if (!cacheEntry.widget) {
			return nullptr;
		}

		cacheEntry.widget->setProperty("persistentChatStylesheet", stylesheet);
		cacheEntry.widget->setAttribute(Qt::WA_DontShowOnScreen, true);
		cacheEntry.widget->show();
	}

	cacheEntry.widget->setFixedWidth(std::max(1, width));
	cacheEntry.widget->ensurePolished();
	if (QLayout *layout = cacheEntry.widget->layout()) {
		layout->activate();
	}
	cacheEntry.widget->adjustSize();
	return cacheEntry.widget;
}

QWidget *PersistentChatHistoryDelegate::createWidgetForRow(const PersistentChatHistoryRow &row, int width) const {
	switch (row.kind) {
		case PersistentChatHistoryRowKind::State:
			return row.state ? createStateWidget(*row.state, nullptr, width) : nullptr;
		case PersistentChatHistoryRowKind::LoadOlder: {
			if (!row.loadOlder) {
				return nullptr;
			}

			QPushButton *button = new QPushButton(row.loadOlder->text);
			button->setObjectName(QLatin1String("qpbPersistentChatLoadOlder"));
			button->setFlat(true);
			button->setEnabled(row.loadOlder->enabled);
			connect(button, &QPushButton::clicked, this, &PersistentChatHistoryDelegate::loadOlderRequested);
			return button;
		}
		case PersistentChatHistoryRowKind::DateDivider:
			return row.text ? createDateDividerWidget(row.text->text, nullptr) : nullptr;
		case PersistentChatHistoryRowKind::UnreadDivider:
			return row.text
					   ? createPillWidget(QString::fromLatin1("<strong>%1</strong>").arg(row.text->text.toHtmlEscaped()),
										  QLatin1String("qlPersistentChatUnreadPill"), nullptr)
					   : nullptr;
		case PersistentChatHistoryRowKind::MessageGroup: {
			if (!row.messageGroup) {
				return nullptr;
			}

			auto *groupWidget = new PersistentChatMessageGroupWidget(std::max(240, width - 18), applicationStyleSheet(),
																	 nullptr);
			groupWidget->setHeader(row.messageGroup->header, row.messageGroup->avatarFallbackText);
			for (const PersistentChatBubbleSpec &bubble : row.messageGroup->bubbles) {
				groupWidget->addBubble(bubble);
			}

			for (QWidget *actionsWidget : groupWidget->findChildren< QWidget * >(QLatin1String("qwPersistentChatBubbleActions"))) {
				actionsWidget->show();
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

bool PersistentChatHistoryDelegate::forwardEditorEvent(QWidget *rootWidget, QEvent *event,
													   const QStyleOptionViewItem &option) const {
	if (!rootWidget || !event) {
		return false;
	}

	rootWidget->setGeometry(0, 0, option.rect.width(), std::max(option.rect.height(), rootWidget->sizeHint().height()));
	rootWidget->ensurePolished();
	if (QLayout *layout = rootWidget->layout()) {
		layout->activate();
	}

	switch (event->type()) {
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::MouseButtonDblClick:
		case QEvent::MouseMove: {
			const auto *mouseEvent = static_cast< const QMouseEvent * >(event);
			const QPoint localPos  = mouseEvent->pos() - option.rect.topLeft();
			QWidget *targetWidget  = rootWidget->childAt(localPos);
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
			QWidget *targetWidget    = rootWidget->childAt(localPos);
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
