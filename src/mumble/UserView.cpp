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
#include "UiTheme.h"
#include "UserModel.h"
#include "Global.h"

#include <QtGui/QDesktopServices>
#include <QtGui/QHelpEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPolygonF>
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
		QColor currentRoomTintColor;
		QColor currentRoomBorderColor;
		QColor textColor;
		QColor mutedTextColor;
		QColor channelTextColor;
		QColor avatarFillColor;
		QColor avatarTextColor;
		QColor avatarBorderColor;
		QColor iconTintColor;
		QColor speakingColor;
		QColor idleColor;
		QColor dangerColor;
		QColor presenceCutoutColor;
		QColor branchColor;
	};

	NavigatorRowPalette buildNavigatorRowPalette(const QPalette &palette) {
		NavigatorRowPalette colors;
		const bool darkTheme        = isDarkRowPalette(palette);
		const QColor windowColor    = palette.color(QPalette::Window);
		const QColor baseColor      = palette.color(QPalette::Base);
		const QColor alternateColor = palette.color(QPalette::AlternateBase);
		const QColor highlightColor = palette.color(QPalette::Highlight);
		const QColor textColor      = palette.color(QPalette::WindowText);

		if (const std::optional< UiThemeTokens > tokens = activeUiThemeTokens(); tokens) {
			colors.surfaceColor         = tokens->mantle;
			colors.hoverColor           = tokens->surface0;
			colors.currentRoomTintColor = uiThemeColorWithAlpha(tokens->green, 0.06);
			colors.currentRoomBorderColor = tokens->green;
			colors.textColor            = tokens->text;
			colors.mutedTextColor       = tokens->overlay0;
			colors.channelTextColor     = tokens->subtext0;
			colors.avatarFillColor      = tokens->surface0;
			colors.avatarTextColor      = tokens->text;
			colors.avatarBorderColor    = tokens->surface1;
			colors.iconTintColor        = tokens->overlay0;
			colors.speakingColor        = tokens->green;
			colors.idleColor            = tokens->yellow;
			colors.dangerColor          = tokens->red;
			colors.presenceCutoutColor  = tokens->mantle;
			colors.branchColor          = tokens->subtext0;
			return colors;
		}

		colors.surfaceColor         = mixRowColors(baseColor, alternateColor, darkTheme ? 0.74 : 0.14);
		colors.hoverColor           = mixRowColors(colors.surfaceColor, textColor, darkTheme ? 0.04 : 0.03);
		colors.currentRoomTintColor =
			QColor::fromRgbF(0.25f, 0.68f, 0.46f, darkTheme ? 0.07f : 0.09f);
		colors.currentRoomBorderColor = QColor::fromRgb(darkTheme ? 114 : 54, darkTheme ? 217 : 168, darkTheme ? 153 : 97);
		colors.textColor            = textColor;
		colors.mutedTextColor       = mixRowColors(textColor, windowColor, darkTheme ? 0.38 : 0.28);
		colors.channelTextColor     = mixRowColors(textColor, windowColor, darkTheme ? 0.12 : 0.08);
		colors.avatarFillColor      = mixRowColors(colors.surfaceColor, textColor, darkTheme ? 0.10 : 0.05);
		colors.avatarTextColor      = colors.textColor;
		colors.avatarBorderColor    = mixRowColors(colors.surfaceColor, textColor, darkTheme ? 0.18 : 0.10);
		colors.iconTintColor        = mixRowColors(colors.textColor, windowColor, darkTheme ? 0.20 : 0.10);
		colors.speakingColor        = QColor::fromRgb(darkTheme ? 114 : 54, darkTheme ? 217 : 168, darkTheme ? 153 : 97);
		colors.idleColor            = QColor::fromRgb(darkTheme ? 230 : 190, darkTheme ? 176 : 120, darkTheme ? 96 : 70);
		colors.dangerColor          = QColor::fromRgb(darkTheme ? 231 : 188, darkTheme ? 92 : 64, darkTheme ? 101 : 72);
		colors.presenceCutoutColor  = colors.surfaceColor;
		colors.branchColor          = colors.mutedTextColor;
		return colors;
	}

	QString normalizedNavigatorText(const QString &text) {
		return text.simplified();
	}

	QString occupantLabel(int count) {
		if (count <= 0) {
			return QString();
		}

		return QString::fromLatin1("(%1)").arg(count);
	}

	QPolygonF branchIndicatorPolygon(const QRectF &rect, bool expanded) {
		const qreal left    = rect.left() + 0.5;
		const qreal right   = rect.right() - 0.5;
		const qreal top     = rect.top() + 0.5;
		const qreal bottom  = rect.bottom() - 0.5;
		const qreal centerX = rect.center().x();
		const qreal centerY = rect.center().y();
		QPolygonF polygon;
		if (expanded) {
			polygon << QPointF(left, top + 1.0) << QPointF(right, top + 1.0) << QPointF(centerX, bottom);
			return polygon;
		}

		polygon << QPointF(left + 1.0, top) << QPointF(right, centerY) << QPointF(left + 1.0, bottom);
		return polygon;
	}

	QModelIndex resolveTreeRowIndex(const QTreeView *view, const QPoint &position) {
		if (!view || !view->viewport() || !view->model() || position.y() < 0
			|| position.y() >= view->viewport()->height()) {
			return QModelIndex();
		}

		QModelIndex idx = view->indexAt(position);
		if (idx.isValid()) {
			return idx;
		}

		const int viewportWidth = view->viewport()->width();
		if (viewportWidth <= 0) {
			return QModelIndex();
		}

		const int probeXs[] = { qBound(0, position.x(), viewportWidth - 1),
								qBound(0, viewportWidth / 2, viewportWidth - 1),
								qBound(0, viewportWidth - 12, viewportWidth - 1),
								qBound(0, 12, viewportWidth - 1) };
		for (const int probeX : probeXs) {
			idx = view->indexAt(QPoint(probeX, position.y()));
			if (idx.isValid()) {
				return idx;
			}
		}

		for (int probeX = 4; probeX < viewportWidth; probeX += 24) {
			idx = view->indexAt(QPoint(probeX, position.y()));
			if (idx.isValid()) {
				return idx;
			}
		}

		return QModelIndex();
	}

	QColor alphaColor(const QColor &color, qreal alpha) {
		QColor adjusted = color;
		adjusted.setAlphaF(qBound< qreal >(0.0, alpha, 1.0));
		return adjusted;
	}

	QPixmap tintedIconPixmap(const QIcon &icon, const QSize &size, const QColor &color) {
		QPixmap pixmap = icon.pixmap(size);
		if (pixmap.isNull()) {
			return pixmap;
		}

		QPainter painter(&pixmap);
		painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
		painter.fillRect(pixmap.rect(), color);
		return pixmap;
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
	const QList< QVariant > statusIcons = index.data(UserModel::NavigatorStatusIconsRole).toList();
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
	const bool idle = index.data(UserModel::NavigatorIdleRole).toBool();
	const bool muted = index.data(UserModel::NavigatorMutedRole).toBool();
	const bool deafened = index.data(UserModel::NavigatorDeafenedRole).toBool();
	const bool isChannel  = itemKind == UserModel::NavigatorChannelItem;
	const bool isListener = itemKind == UserModel::NavigatorListenerItem;
	const bool hasChildren = index.model() && index.model()->hasChildren(index);
	const bool isRoot = isChannel && !index.parent().isValid();
	const bool talking =
		!isChannel && !isListener && static_cast< Settings::TalkState >(talkState) != Settings::Passive;
	const QColor primaryTextColor =
		isChannel
			? ((currentLocation || isRoot || linkedLocation || occupancy > 0 || hasChildren) ? colors.textColor
																							  : colors.channelTextColor)
			: (idle ? colors.mutedTextColor : colors.textColor);
	const QColor secondaryTextColor = colors.mutedTextColor;
	const UserView *view = qobject_cast< const UserView * >(opt.widget);

	QRect contentRect = option.rect.adjusted(isChannel ? 10 : 18, 1, -10, -1);
	const int statusIconsWidth = static_cast< int >(statusIcons.size() * m_iconTotalDimension);
	int iconRight = contentRect.right();

	QString occupancyText;
	if (itemKind == UserModel::NavigatorChannelItem && Global::get().s.bShowUserCount) {
		occupancyText = occupantLabel(occupancy);
	}
	const bool showInlineStatus = !isChannel && !isListener && (muted || deafened);
	const int inlineStatusWidth = showInlineStatus ? 14 : 0;
	static const QIcon s_voiceRoomIcon(QLatin1String("skin:priority_speaker.svg"));

	QFont secondaryFont(opt.font);
	secondaryFont.setPointSizeF(std::max(secondaryFont.pointSizeF() - 0.5, 8.0));
	QFontMetrics secondaryMetrics(secondaryFont);
	int occupancyWidth = 0;
	if (!occupancyText.isEmpty()) {
		occupancyWidth = secondaryMetrics.horizontalAdvance(occupancyText);
	}
	int textRight = contentRect.right();
	if (statusIconsWidth > 0) {
		textRight -= statusIconsWidth + 4;
	}
	if (occupancyWidth > 0) {
		textRight -= occupancyWidth + 8;
	}
	if (inlineStatusWidth > 0) {
		textRight -= inlineStatusWidth + 6;
	}

	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);

	int x = contentRect.left();
	if (isChannel) {
		if (!isRoot) {
			const QRect voiceIconRect(x, contentRect.center().y() - 6, 12, 12);
			const QColor voiceIconColor = currentLocation ? colors.currentRoomBorderColor : colors.iconTintColor;
			painter->drawPixmap(voiceIconRect.topLeft(),
								tintedIconPixmap(s_voiceRoomIcon, voiceIconRect.size(), voiceIconColor));
			x = voiceIconRect.right() + 7;
		}
	} else {
		const int avatarSize = 20;
		const QRect avatarRect(x, contentRect.center().y() - (avatarSize / 2), avatarSize, avatarSize);
		if (talking) {
			const qreal glowOutset = view && view->presencePulseExpanded() ? 3.0 : 1.0;
			const QColor glowColor =
				alphaColor(colors.speakingColor, view && view->presencePulseExpanded() ? 0.14 : 0.26);
			painter->setPen(QPen(glowColor, view && view->presencePulseExpanded() ? 2.0 : 3.0));
			painter->setBrush(Qt::NoBrush);
			const QRectF glowRect = QRectF(avatarRect).adjusted(-glowOutset, -glowOutset, glowOutset, glowOutset);
			painter->drawEllipse(glowRect);
		}

		painter->setPen(QPen(talking ? colors.speakingColor : colors.avatarBorderColor, talking ? 2.0 : 1.0));
		painter->setBrush(colors.avatarFillColor);
		painter->drawEllipse(avatarRect);
		if (!avatarImage.isNull()) {
			const QRect imageRect = avatarRect.adjusted(1, 1, -1, -1);
			QPainterPath clipPath;
			clipPath.addEllipse(imageRect);
			painter->save();
			painter->setClipPath(clipPath);
			const QImage scaledImage =
				avatarImage.scaled(imageRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			const QPoint imageTopLeft(
				imageRect.center().x() - scaledImage.width() / 2,
				imageRect.center().y() - scaledImage.height() / 2);
			painter->drawImage(imageTopLeft, scaledImage);
			painter->restore();
		} else {
			painter->setPen(colors.avatarTextColor);
			QFont avatarFont(opt.font);
			avatarFont.setBold(true);
			avatarFont.setPointSizeF(std::max(avatarFont.pointSizeF() - 1.5, 8.0));
			painter->setFont(avatarFont);
			painter->drawText(avatarRect, Qt::AlignCenter,
							  avatarFallback.isEmpty() ? QStringLiteral("?") : avatarFallback);
		}

		if (!talking) {
			const QRect statusDotRect(avatarRect.right() - 6, avatarRect.bottom() - 6, 8, 8);
			painter->setPen(QPen(colors.presenceCutoutColor, 2.0));
			painter->setBrush(idle ? colors.idleColor : colors.speakingColor);
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
	titleFont.setBold(isRoot);
	if (isChannel && currentLocation && !isRoot) {
		titleFont.setWeight(QFont::DemiBold);
	}
	if (isListener) {
		titleFont.setItalic(true);
	}
	const QFontMetrics titleMetrics(titleFont);
	painter->setFont(titleFont);
	painter->setPen(primaryTextColor);
	const QString titleText = titleMetrics.elidedText(title, Qt::ElideRight, textRect.width());
	painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, titleText);

	const int titleWidth = titleMetrics.horizontalAdvance(titleText);
	int titleRight = textRect.left() + titleWidth;
	if (showInlineStatus) {
		static const QIcon muteIcon(QLatin1String("skin:muted_server.svg"));
		static const QIcon deafIcon(QLatin1String("skin:deafened_server.svg"));
		const QRect inlineStatusRect(titleRight + 6, contentRect.center().y() - 6, 12, 12);
		const QIcon &audioStateIcon = deafened ? deafIcon : muteIcon;
		painter->drawPixmap(inlineStatusRect.topLeft(),
							tintedIconPixmap(audioStateIcon, inlineStatusRect.size(), colors.dangerColor));
		titleRight = inlineStatusRect.right();
	}
	if (!occupancyText.isEmpty()) {
		const int occupancyLeft = std::min(textRect.right() - occupancyWidth, titleRight + 8);
		if (occupancyLeft > titleRight) {
			const QRect occupancyRect(occupancyLeft, contentRect.top(), occupancyWidth, contentRect.height());
			painter->setFont(secondaryFont);
			painter->setPen(secondaryTextColor);
			painter->drawText(occupancyRect, Qt::AlignVCenter | Qt::AlignLeft, occupancyText);
			painter->setFont(titleFont);
		}
	}

	if (!statusIcons.isEmpty()) {
		const int iconPosY = contentRect.center().y() - (m_iconIconDimension / 2);
		for (int i = static_cast< int >(statusIcons.size()) - 1; i >= 0; --i) {
			iconRight -= m_iconTotalDimension;
			const QRect iconRect(iconRight + m_iconIconPadding, iconPosY, m_iconIconDimension, m_iconIconDimension);
			const QIcon icon = qvariant_cast< QIcon >(statusIcons.at(i));
			icon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal, QIcon::On);
		}
	}

	painter->restore();
}

QSize UserDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
	QSize hint = QStyledItemDelegate::sizeHint(option, index);
	if (!index.isValid() || index.column() != 0) {
		return hint;
	}

	const int itemKind = index.data(UserModel::NavigatorItemKindRole).toInt();
	const int minimumHeight =
		itemKind == UserModel::NavigatorChannelItem ? 28 : (itemKind == UserModel::NavigatorUserItem ? 24 : 24);
	return QSize(hint.width(), std::max(hint.height(), minimumHeight));
}

bool UserDelegate::helpEvent(QHelpEvent *evt, QAbstractItemView *view, const QStyleOptionViewItem &option,
							 const QModelIndex &index) {
	if (index.isValid()) {
		const QAbstractItemModel *m      = index.model();
		const QModelIndex firstColumnIdx = index.sibling(index.row(), 1);
		QVariant data                    = m->data(firstColumnIdx);
		QList< QVariant > iconList       = data.toList();
		const auto offset                = static_cast< int >(iconList.size() * -m_iconTotalDimension);
		const int itemKind = index.data(UserModel::NavigatorItemKindRole).toInt();
		const QRect contentRect = option.rect.adjusted(itemKind == UserModel::NavigatorChannelItem ? 12 : 24, 1, -12, -1);
		const int firstIconPos  = contentRect.topRight().x() + offset;

		if (evt->pos().x() >= firstIconPos) {
			return QStyledItemDelegate::helpEvent(evt, view, option, firstColumnIdx);
		}
	}
	return QStyledItemDelegate::helpEvent(evt, view, option, index);
}

UserView::UserView(QWidget *p) : QTreeView(p), m_userDelegate(make_qt_unique< UserDelegate >(this)) {
	adjustIcons();
	setItemDelegate(m_userDelegate.get());
	m_presencePulseTimer = new QTimer(this);
	m_presencePulseTimer->setInterval(400);
	connect(m_presencePulseTimer, &QTimer::timeout, this, [this]() {
		m_presencePulseExpanded = !m_presencePulseExpanded;
		viewport()->update();
	});
	m_presencePulseTimer->start();

	// Because in Qt fonts take some time to initialize properly, we have to delay the call
	// to adjustIcons a bit in order to give the fonts the necessary time (so we can read out
	// the actual font details).
	QTimer::singleShot(0, [this]() { adjustIcons(); });
}

void UserView::drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const {
	if (!index.isValid() || !model() || !model()->hasChildren(index)) {
		return;
	}

	const int itemKind = index.data(UserModel::NavigatorItemKindRole).toInt();
	if (itemKind != UserModel::NavigatorChannelItem || !index.parent().isValid()) {
		return;
	}

	const NavigatorRowPalette colors = buildNavigatorRowPalette(viewport()->palette());
	const qreal indicatorSize = 10.0;
	const QRectF indicatorRect(rect.center().x() - (indicatorSize / 2.0),
							   rect.center().y() - (indicatorSize / 2.0), indicatorSize, indicatorSize);

	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setPen(Qt::NoPen);
	painter->setBrush(colors.branchColor);
	painter->drawPolygon(branchIndicatorPolygon(indicatorRect, isExpanded(index)));
	painter->restore();
}

void UserView::drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
	const bool isHovered = index == m_hoveredIndex;
	const bool isCurrentLocation = index.data(UserModel::NavigatorCurrentLocationRole).toBool();
	const int itemKind = index.data(UserModel::NavigatorItemKindRole).toInt();
	const bool isChannel = itemKind == UserModel::NavigatorChannelItem;
	const bool shouldDrawCurrentRoom = isChannel && isCurrentLocation;
	const bool shouldDrawHover = isHovered && !shouldDrawCurrentRoom;
	if (shouldDrawCurrentRoom || shouldDrawHover) {
		const NavigatorRowPalette colors = buildNavigatorRowPalette(viewport()->palette());
		const QColor rowFillColor = shouldDrawCurrentRoom ? colors.currentRoomTintColor : colors.hoverColor;
		const QRect rowRect = option.rect.adjusted(0, 2, -4, -2);
		if (rowRect.isValid()) {
			painter->save();
			painter->setRenderHint(QPainter::Antialiasing, true);
			painter->setPen(Qt::NoPen);
			painter->setBrush(rowFillColor);
			painter->drawRoundedRect(rowRect, 8.0, 8.0);
			if (shouldDrawCurrentRoom) {
				const QRect accentRect(rowRect.left(), rowRect.top() + 2, 3, rowRect.height() - 4);
				painter->setPen(Qt::NoPen);
				painter->setBrush(colors.currentRoomBorderColor);
				painter->drawRoundedRect(accentRect, 1.5, 1.5);
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

void UserView::mouseDoubleClickEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton) {
		QTreeView::mouseDoubleClickEvent(event);
		return;
	}

	const QModelIndex idx = resolveTreeRowIndex(this, event->pos());
	if (!idx.isValid()) {
		QTreeView::mouseDoubleClickEvent(event);
		return;
	}

	setCurrentIndex(idx);
	nodeActivated(idx);
	event->accept();
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

	QModelIndex idx = resolveTreeRowIndex(this, clickPosition);
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
			const int itemKind         = idx.data(UserModel::NavigatorItemKindRole).toInt();
			const QRect contentRect = r.adjusted(itemKind == UserModel::NavigatorChannelItem ? 12 : 24, 1, -12, -1);
			const int commentIconPxPos = contentRect.topRight().x() + commentIconPxOffset;

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
	if (!idx.isValid()) {
		return;
	}

	UserModel *um = static_cast< UserModel * >(model());
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
