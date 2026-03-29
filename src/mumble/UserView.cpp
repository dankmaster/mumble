// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "UserView.h"

#include "Channel.h"
#include "ClientUser.h"
#include "Log.h"
#include "MainWindow.h"
#include "ServerHandler.h"
#include "Settings.h"
#include "UserModel.h"
#include "Global.h"

#include <QtGui/QDesktopServices>
#include <QtGui/QHelpEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtWidgets/QWhatsThis>

namespace {
	QColor mixRowColors(const QColor &baseColor, const QColor &overlayColor, qreal overlayRatio) {
		const qreal clampedRatio = qBound< qreal >(0.0, overlayRatio, 1.0);
		const qreal baseRatio    = 1.0 - clampedRatio;
		return QColor::fromRgbF(baseColor.redF() * baseRatio + overlayColor.redF() * clampedRatio,
								baseColor.greenF() * baseRatio + overlayColor.greenF() * clampedRatio,
								baseColor.blueF() * baseRatio + overlayColor.blueF() * clampedRatio, 1.0);
	}

	bool isDarkRowPalette(const QPalette &palette) {
		return palette.color(QPalette::WindowText).lightness() > palette.color(QPalette::Window).lightness();
	}

	struct NavigatorRowPalette {
		QColor surfaceColor;
		QColor hoverColor;
		QColor selectedColor;
		QColor selectedOutlineColor;
		QColor currentColor;
		QColor linkedColor;
		QColor textColor;
		QColor mutedTextColor;
		QColor accentColor;
		QColor avatarFillColor;
		QColor avatarTextColor;
		QColor chipColor;
		QColor chipTextColor;
		QColor iconTintColor;
		QColor speakingColor;
		QColor mutedSpeakingColor;
	};

	NavigatorRowPalette buildNavigatorRowPalette(const QPalette &palette) {
		NavigatorRowPalette colors;
		const bool darkTheme        = isDarkRowPalette(palette);
		const QColor windowColor    = palette.color(QPalette::Window);
		const QColor baseColor      = palette.color(QPalette::Base);
		const QColor alternateColor = palette.color(QPalette::AlternateBase);
		const QColor highlightColor = palette.color(QPalette::Highlight);
		const QColor textColor      = palette.color(QPalette::WindowText);

		colors.surfaceColor         = mixRowColors(baseColor, alternateColor, darkTheme ? 0.74 : 0.14);
		colors.hoverColor           = mixRowColors(colors.surfaceColor, textColor, darkTheme ? 0.04 : 0.03);
		colors.selectedColor        = mixRowColors(alternateColor, highlightColor, darkTheme ? 0.28 : 0.14);
		colors.selectedOutlineColor = mixRowColors(highlightColor, textColor, darkTheme ? 0.20 : 0.10);
		colors.currentColor         = mixRowColors(alternateColor, highlightColor, darkTheme ? 0.14 : 0.10);
		colors.linkedColor          = mixRowColors(baseColor, alternateColor, darkTheme ? 0.76 : 0.06);
		colors.textColor            = textColor;
		colors.mutedTextColor       = mixRowColors(textColor, windowColor, darkTheme ? 0.38 : 0.28);
		colors.accentColor          = mixRowColors(textColor, highlightColor, darkTheme ? 0.18 : 0.12);
		colors.avatarFillColor      = mixRowColors(colors.surfaceColor, textColor, darkTheme ? 0.10 : 0.05);
		colors.avatarTextColor      = colors.textColor;
		colors.chipColor            = mixRowColors(colors.surfaceColor, textColor, darkTheme ? 0.10 : 0.06);
		colors.chipTextColor        = mixRowColors(colors.textColor, highlightColor, darkTheme ? 0.02 : 0.01);
		colors.iconTintColor        = mixRowColors(colors.textColor, windowColor, darkTheme ? 0.20 : 0.10);
		colors.speakingColor        = QColor::fromRgb(darkTheme ? 114 : 54, darkTheme ? 217 : 168, darkTheme ? 153 : 97);
		colors.mutedSpeakingColor   = QColor::fromRgb(darkTheme ? 230 : 190, darkTheme ? 176 : 120, darkTheme ? 96 : 70);
		return colors;
	}

	QString normalizedNavigatorText(const QString &text) {
		return text.simplified();
	}

	QString occupantLabel(int count) {
		if (count <= 0) {
			return QString();
		}

		return QString::number(count);
	}

	QColor talkStateColor(int talkState, const NavigatorRowPalette &colors) {
		switch (static_cast< Settings::TalkState >(talkState)) {
			case Settings::Talking:
			case Settings::Whispering:
			case Settings::Shouting:
				return colors.speakingColor;
			case Settings::MutedTalking:
				return colors.mutedSpeakingColor;
			case Settings::Passive:
			default:
				return QColor();
		}
	}
}

UserDelegate::UserDelegate(QObject *p) : QStyledItemDelegate(p) {
}

void UserDelegate::adjustIcons(int iconTotalDimension, int iconIconPadding, int iconIconDimension) {
	m_iconTotalDimension = iconTotalDimension;
	m_iconIconPadding    = iconIconPadding;
	m_iconIconDimension  = iconIconDimension;
}

void UserDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
	if (!index.isValid() || index.column() != 0) {
		QStyledItemDelegate::paint(painter, option, index);
		return;
	}

	QStyleOptionViewItem opt(option);
	initStyleOption(&opt, index);

	const QPalette effectivePalette = opt.widget ? opt.widget->palette() : opt.palette;
	const NavigatorRowPalette colors = buildNavigatorRowPalette(effectivePalette);
	const QList< QVariant > statusIcons =
		index.data(UserModel::NavigatorStatusIconsRole).toList();
	const QString title =
		normalizedNavigatorText(index.data(UserModel::NavigatorTitleRole).toString());
	const int itemKind = index.data(UserModel::NavigatorItemKindRole).toInt();
	const int occupancy = index.data(UserModel::NavigatorOccupancyRole).toInt();
	const bool currentLocation = index.data(UserModel::NavigatorCurrentLocationRole).toBool();
	const bool linkedLocation = index.data(UserModel::NavigatorLinkedLocationRole).toBool();
	const QImage avatarImage =
		qvariant_cast< QImage >(index.data(UserModel::NavigatorAvatarImageRole));
	const QString avatarFallback = index.data(UserModel::NavigatorAvatarFallbackRole).toString();
	const int talkState = index.data(UserModel::NavigatorTalkStateRole).toInt();
	const bool isSelected = opt.state.testFlag(QStyle::State_Selected);
	const bool isListener = itemKind == UserModel::NavigatorListenerItem;
	const QColor primaryTextColor = isSelected ? opt.palette.color(QPalette::HighlightedText) : colors.textColor;
	const QColor secondaryTextColor = isSelected ? opt.palette.color(QPalette::HighlightedText) : colors.mutedTextColor;

	QRect contentRect = option.rect.adjusted(10, 1, -10, -1);
	int trailingIconsWidth = 0;
	if (!statusIcons.isEmpty()) {
		trailingIconsWidth = static_cast< int >(statusIcons.size() * m_iconTotalDimension);
	}

	QString occupancyText;
	if (itemKind == UserModel::NavigatorChannelItem) {
		occupancyText = occupantLabel(occupancy);
	}

	QFont chipFont(opt.font);
	chipFont.setBold(true);
	chipFont.setPointSizeF(std::max(chipFont.pointSizeF() - 1.0, 8.0));
	QFontMetrics chipMetrics(chipFont);
	int occupancyWidth = 0;
	if (!occupancyText.isEmpty()) {
		occupancyWidth = chipMetrics.horizontalAdvance(occupancyText) + 14;
	}

	int textRight = contentRect.right() - trailingIconsWidth;
	if (trailingIconsWidth > 0) {
		textRight -= 4;
	}
	if (occupancyWidth > 0) {
		textRight -= occupancyWidth + 6;
	}

	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);

	int x = contentRect.left();
	if (itemKind == UserModel::NavigatorChannelItem) {
		const QIcon channelIcon = qvariant_cast< QIcon >(index.data(Qt::DecorationRole));
		const QRect iconRect(x, contentRect.center().y() - 8, 16, 16);
		if (!channelIcon.isNull()) {
			channelIcon.paint(painter, iconRect, Qt::AlignCenter,
							  isSelected ? QIcon::Selected : QIcon::Normal, QIcon::On);
		}
		if (currentLocation || linkedLocation) {
			painter->setPen(Qt::NoPen);
			painter->setBrush(currentLocation ? colors.selectedOutlineColor : colors.linkedColor);
			painter->drawEllipse(QRect(iconRect.right() - 4, iconRect.top() - 1, 7, 7));
		}
		x = iconRect.right() + 8;
	} else {
		const QRect avatarRect(x, contentRect.center().y() - 11, 22, 22);
		painter->setPen(Qt::NoPen);
		painter->setBrush(isSelected ? opt.palette.color(QPalette::Highlight) : colors.avatarFillColor);
		painter->drawEllipse(avatarRect);
		if (!avatarImage.isNull()) {
			QPainterPath clipPath;
			clipPath.addEllipse(avatarRect);
			painter->save();
			painter->setClipPath(clipPath);
			const QImage scaledImage =
				avatarImage.scaled(avatarRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			const QPoint imageTopLeft(
				avatarRect.center().x() - scaledImage.width() / 2,
				avatarRect.center().y() - scaledImage.height() / 2);
			painter->drawImage(imageTopLeft, scaledImage);
			painter->restore();
		} else {
			painter->setPen(isSelected ? opt.palette.color(QPalette::HighlightedText) : colors.avatarTextColor);
			painter->setFont(chipFont);
			painter->drawText(avatarRect, Qt::AlignCenter,
							  avatarFallback.isEmpty() ? QStringLiteral("?") : avatarFallback);
		}

		if (const QColor speakingColor = talkStateColor(talkState, colors); speakingColor.isValid()) {
			const QRect statusDotRect(avatarRect.right() - 4, avatarRect.bottom() - 5, 7, 7);
			painter->setPen(QPen(colors.surfaceColor, 1.5));
			painter->setBrush(speakingColor);
			painter->drawEllipse(statusDotRect);
		} else if (currentLocation) {
			const QRect statusDotRect(avatarRect.right() - 4, avatarRect.bottom() - 5, 7, 7);
			painter->setPen(QPen(colors.surfaceColor, 1.5));
			painter->setBrush(colors.selectedOutlineColor);
			painter->drawEllipse(statusDotRect);
		}

		if (isListener) {
			const QIcon listenerIcon = qvariant_cast< QIcon >(index.data(Qt::DecorationRole));
			const QRect listenerRect(avatarRect.right() - 7, avatarRect.top() - 1, 10, 10);
			if (!listenerIcon.isNull()) {
				listenerIcon.paint(painter, listenerRect, Qt::AlignCenter, QIcon::Normal, QIcon::On);
			}
		}

		x = avatarRect.right() + 8;
	}

	const QRect textRect(x, contentRect.top(), std::max(20, textRight - x), contentRect.height());
	QFont titleFont(opt.font);
	titleFont.setBold(currentLocation || isSelected || itemKind == UserModel::NavigatorChannelItem);
	painter->setFont(titleFont);
	painter->setPen(primaryTextColor);
	painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
					  QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, textRect.width()));

	int iconRight = contentRect.right();
	if (!statusIcons.isEmpty()) {
		const int iconPosY = contentRect.center().y() - (m_iconIconDimension / 2);
		for (int i = static_cast< int >(statusIcons.size()) - 1; i >= 0; --i) {
			iconRight -= m_iconTotalDimension;
			const QRect iconRect(iconRight + m_iconIconPadding, iconPosY, m_iconIconDimension, m_iconIconDimension);
			const QIcon icon = qvariant_cast< QIcon >(statusIcons.at(i));
			icon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal, QIcon::On);
		}
	}

	if (!occupancyText.isEmpty()) {
		const QRect chipRect(iconRight - occupancyWidth - 4, contentRect.center().y() - 10, occupancyWidth, 20);
		painter->setPen(Qt::NoPen);
		painter->setBrush(isSelected ? opt.palette.color(QPalette::Highlight) : colors.chipColor);
		painter->drawRoundedRect(chipRect, 10.0f, 10.0f);
		painter->setFont(chipFont);
		painter->setPen(isSelected ? opt.palette.color(QPalette::HighlightedText) : secondaryTextColor);
		painter->drawText(chipRect, Qt::AlignCenter, occupancyText);
	}

	painter->restore();
}

QSize UserDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
	QSize hint = QStyledItemDelegate::sizeHint(option, index);
	if (!index.isValid() || index.column() != 0) {
		return hint;
	}

	return QSize(hint.width(), std::max(hint.height(), QFontMetrics(option.font).height() + 10));
}

bool UserDelegate::helpEvent(QHelpEvent *evt, QAbstractItemView *view, const QStyleOptionViewItem &option,
							 const QModelIndex &index) {
	if (index.isValid()) {
		const QAbstractItemModel *m      = index.model();
		const QModelIndex firstColumnIdx = index.sibling(index.row(), 1);
		QVariant data                    = m->data(firstColumnIdx);
		QList< QVariant > iconList       = data.toList();
		const auto offset                = static_cast< int >(iconList.size() * -m_iconTotalDimension);
		const int firstIconPos           = option.rect.topRight().x() + offset;

		if (evt->pos().x() >= firstIconPos) {
			return QStyledItemDelegate::helpEvent(evt, view, option, firstColumnIdx);
		}
	}
	return QStyledItemDelegate::helpEvent(evt, view, option, index);
}

UserView::UserView(QWidget *p) : QTreeView(p), m_userDelegate(make_qt_unique< UserDelegate >(this)) {
	adjustIcons();
	setItemDelegate(m_userDelegate.get());

	// Because in Qt fonts take some time to initialize properly, we have to delay the call
	// to adjustIcons a bit in order to give the fonts the necessary time (so we can read out
	// the actual font details).
	QTimer::singleShot(0, [this]() { adjustIcons(); });

	connect(this, SIGNAL(doubleClicked(const QModelIndex &)), this, SLOT(nodeActivated(const QModelIndex &)));
}

void UserView::drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
	const bool isSelected = option.state & QStyle::State_Selected;
	const bool isHovered  = index == m_hoveredIndex;
	const bool isCurrentLocation = index.data(UserModel::NavigatorCurrentLocationRole).toBool();
	const bool isLinkedLocation  = index.data(UserModel::NavigatorLinkedLocationRole).toBool();
	if (isSelected || isHovered || isCurrentLocation || isLinkedLocation) {
		const NavigatorRowPalette colors = buildNavigatorRowPalette(viewport()->palette());
		QColor rowFillColor;
		QColor borderColor(Qt::transparent);
		if (isSelected) {
			rowFillColor = colors.selectedColor;
			borderColor  = colors.selectedOutlineColor;
		} else if (isCurrentLocation) {
			rowFillColor = colors.currentColor;
			borderColor  = colors.selectedOutlineColor;
		} else if (isHovered) {
			rowFillColor = colors.hoverColor;
		} else {
			rowFillColor = colors.linkedColor;
		}

		const QRect rowRect = QRect(5, option.rect.top() + 1, viewport()->width() - 10, option.rect.height() - 2);
		if (rowRect.isValid()) {
			painter->save();
			painter->setRenderHint(QPainter::Antialiasing, true);
			painter->setPen(borderColor.alpha() == 0 ? Qt::NoPen : QPen(borderColor, 1.0));
			painter->setBrush(rowFillColor);
			painter->drawRoundedRect(rowRect, 9.0f, 9.0f);
			if (isCurrentLocation && !isSelected) {
				const QRect accentRect(rowRect.left() + 4, rowRect.top() + 4, 3, rowRect.height() - 8);
				painter->setPen(Qt::NoPen);
				painter->setBrush(colors.selectedOutlineColor);
				painter->drawRoundedRect(accentRect, 1.5f, 1.5f);
			}
			painter->restore();
		}
	}

	QTreeView::drawRow(painter, option, index);
}

void UserView::mouseMoveEvent(QMouseEvent *event) {
	const QModelIndex hoveredIndex = indexAt(event->pos());
	if (hoveredIndex != m_hoveredIndex) {
		const QModelIndex previousHoveredIndex = m_hoveredIndex;
		m_hoveredIndex                         = hoveredIndex;
		if (previousHoveredIndex.isValid()) {
			update(visualRect(previousHoveredIndex));
		}
		if (m_hoveredIndex.isValid()) {
			update(visualRect(m_hoveredIndex));
		}
	}

	QTreeView::mouseMoveEvent(event);
}

void UserView::leaveEvent(QEvent *event) {
	if (m_hoveredIndex.isValid()) {
		const QModelIndex previousHoveredIndex = m_hoveredIndex;
		m_hoveredIndex                         = QModelIndex();
		update(visualRect(previousHoveredIndex));
	}

	QTreeView::leaveEvent(event);
}

void UserView::adjustIcons() {
	// Calculate the icon size for status icons based on font size
	// This should automaticially adjust size when the user has
	// display scaling enabled
	const int baseIconTotalDimension = QFontMetrics(font()).height();
	m_iconTotalDimension             = qMax(14, baseIconTotalDimension - 1);
	int iconIconPadding   = 1;
	int iconIconDimension = m_iconTotalDimension - (2 * iconIconPadding);
	m_userDelegate->adjustIcons(m_iconTotalDimension, iconIconPadding, iconIconDimension);
	viewport()->update();
}

/**
 * This implementation contains a special handler to display
 * custom what's this entries for items. All other events are
 * passed on.
 */
bool UserView::event(QEvent *evt) {
	if (evt->type() == QEvent::WhatsThisClicked) {
		QWhatsThisClickedEvent *qwtce = static_cast< QWhatsThisClickedEvent * >(evt);
		QDesktopServices::openUrl(qwtce->href());
		evt->accept();
		return true;
	}
	return QTreeView::event(evt);
}

/**
 * This function is used to create custom behaviour when clicking
 * on user/channel icons (e.Global::get(). showing the comment)
 */
void UserView::mouseReleaseEvent(QMouseEvent *evt) {
	QPoint clickPosition = evt->pos();

	QModelIndex idx = indexAt(clickPosition);
	if ((evt->button() == Qt::LeftButton) && idx.isValid()) {
		UserModel *userModel         = qobject_cast< UserModel * >(model());
		const ClientUser *clientUser = userModel->getUser(idx);
		const Channel *channel       = userModel->getChannel(idx);

		// This is the x offset of the _beginning_ of the comment icon starting from the
		// right.
		// Thus if the comment icon is the last icon that is displayed, this is equal to
		// the negative width of a icon's width (which it is initialized to here). For
		// every icon that is displayed to the right of the comment icon, we have to subtract
		// m_iconTotalDimension once.
		int commentIconPxOffset = -m_iconTotalDimension;
		bool hasComment         = false;

		if (clientUser && !clientUser->qbaCommentHash.isEmpty()) {
			hasComment = true;

			if (clientUser->bLocalIgnore)
				commentIconPxOffset -= m_iconTotalDimension;
			if (clientUser->bRecording)
				commentIconPxOffset -= m_iconTotalDimension;
			if (clientUser->bPrioritySpeaker)
				commentIconPxOffset -= m_iconTotalDimension;
			if (clientUser->bMute)
				commentIconPxOffset -= m_iconTotalDimension;
			if (clientUser->bSuppress)
				commentIconPxOffset -= m_iconTotalDimension;
			if (clientUser->bSelfMute)
				commentIconPxOffset -= m_iconTotalDimension;
			if (clientUser->bLocalMute)
				commentIconPxOffset -= m_iconTotalDimension;
			if (clientUser->bSelfDeaf)
				commentIconPxOffset -= m_iconTotalDimension;
			if (clientUser->bDeaf)
				commentIconPxOffset -= m_iconTotalDimension;
			if (!clientUser->qsFriendName.isEmpty())
				commentIconPxOffset -= m_iconTotalDimension;
			if (clientUser->iId >= 0)
				commentIconPxOffset -= m_iconTotalDimension;

		} else if (channel && !channel->qbaDescHash.isEmpty()) {
			hasComment = true;

			switch (channel->m_filterMode) {
				case ChannelFilterMode::PIN:
				case ChannelFilterMode::HIDE:
					commentIconPxOffset -= m_iconTotalDimension;
					break;
				case ChannelFilterMode::NORMAL:
					// NOOP
					break;
			}

			if (channel->hasEnterRestrictions) {
				commentIconPxOffset -= m_iconTotalDimension;
			}
		}

		if (hasComment) {
			QRect r                    = visualRect(idx);
			const int commentIconPxPos = r.topRight().x() + commentIconPxOffset;

			if ((clickPosition.x() >= commentIconPxPos)
				&& (clickPosition.x() <= (commentIconPxPos + m_iconTotalDimension))) {
				// Clicked comment icon
				QString str = userModel->data(idx, Qt::ToolTipRole).toString();
				if (str.isEmpty()) {
					userModel->bClicked = true;
				} else {
					QWhatsThis::showText(viewport()->mapToGlobal(r.bottomRight()), str, this);
					userModel->seenComment(idx);
				}
				return;
			}
		}
	}
	QTreeView::mouseReleaseEvent(evt);
}

void UserView::keyPressEvent(QKeyEvent *ev) {
	if (ev->key() == Qt::Key_Return || ev->key() == Qt::Key_Enter)
		UserView::nodeActivated(currentIndex());
	QTreeView::keyPressEvent(ev);
}

void UserView::nodeActivated(const QModelIndex &idx) {
	UserModel *um = static_cast< UserModel * >(model());
	ClientUser *p = um->getUser(idx);
	if (p) {
		Global::get().mw->openTextMessageDialog(p);
		return;
	}

	Channel *c = um->getChannel(idx);
	if (c) {
		// if a channel is activated join it
		Global::get().sh->joinChannel(Global::get().uiSession, c->iId);
	}
}

void UserView::keyboardSearch(const QString &) {
	// Disable keyboard search for the UserView in order to prevent jumping wildly through the
	// UI just because the user has accidentally typed something on their keyboard.
	return;
}

void UserView::updateChannel(const QModelIndex &idx) {
	UserModel *um = static_cast< UserModel * >(model());

	if (!idx.isValid()) {
		return;
	}

	Channel *c = um->getChannel(idx);

	for (int i = 0; idx.model()->index(i, 0, idx).isValid(); ++i) {
		updateChannel(idx.model()->index(i, 0, idx));
	}

	if (c && idx.parent().isValid()) {
		setRowHidden(idx.row(), idx.parent(), c->isFiltered());
	}
}

void UserView::dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector< int > &) {
	UserModel *um = static_cast< UserModel * >(model());
	int nRowCount = um->rowCount();
	int i;
	for (i = 0; i < nRowCount; i++)
		updateChannel(um->index(i, 0));

	QTreeView::dataChanged(topLeft, bottomRight);
}
