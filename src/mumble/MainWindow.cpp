// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "MainWindow.h"

#include "ACL.h"
#include "ACLEditor.h"
#include "About.h"
#include "AudioInput.h"
#include "AudioStats.h"
#include "AudioWizard.h"
#include "BanEditor.h"
#include "Cert.h"
#include "Channel.h"
#include "ConnectDialog.h"
#include "Connection.h"
#include "Database.h"
#include "DeveloperConsole.h"
#include "Log.h"
#include "MumbleConstants.h"
#include "Net.h"
#include "NetworkConfig.h"
#include "GlobalShortcut.h"
#include "GlobalShortcutTypes.h"
#ifdef USE_OVERLAY
#	include "OverlayClient.h"
#endif
#include "../SignalCurry.h"
#include "ChannelListenerManager.h"
#include "FailedConnectionDialog.h"
#include "ListenerVolumeSlider.h"
#include "Markdown.h"
#include "MenuLabel.h"
#include "PersistentChatRender.h"
#include "PTTButtonWidget.h"
#include "PluginManager.h"
#include "PositionalAudioViewer.h"
#include "QtWidgetUtils.h"
#include "RichTextEditor.h"
#include "Screen.h"
#include "ScreenShareManager.h"
#include "SearchDialog.h"
#include "ServerHandler.h"
#include "ServerInformation.h"
#include "Settings.h"
#include "SvgIcon.h"
#include "TalkingUI.h"
#include "TextMessage.h"
#include "Themes.h"
#include "Tokens.h"
#include "User.h"
#include "UserEdit.h"
#include "UserInformation.h"
#include "UserLocalNicknameDialog.h"
#include "UserLocalVolumeSlider.h"
#include "UserModel.h"
#include "Utils.h"
#include "VersionCheck.h"
#include "ViewCert.h"
#include "VoiceRecorderDialog.h"
#include "Global.h"

#ifdef Q_OS_WIN
#	include "TaskList.h"
#endif

#ifdef Q_OS_MAC
#	include "AppNap.h"
#endif

#include <QAccessible>
#include <QtCore/QBuffer>
#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStandardPaths>
#include <QtCore/QUrlQuery>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtGui/QImageReader>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtGui/QTextDocument>
#include <QtGui/QTextDocumentFragment>
#include <QtGui/QTextFrame>
#include <QtGui/QWheelEvent>
#include <QtGui/QWindow>
#include <QtGui/QTextCursor>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QToolTip>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWhatsThis>

#include "widgets/BanDialog.h"
#include "widgets/PersistentChatListWidget.h"
#include "widgets/PersistentChatMessageGroupWidget.h"
#include "widgets/ResponsiveImageDialog.h"
#include "widgets/SemanticSlider.h"

#ifdef Q_OS_WIN
#	include <dbt.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

#include "widgets/EventFilters.h"
namespace {
	constexpr int PersistentChatScopeRole   = Qt::UserRole;
	constexpr int PersistentChatScopeIDRole = Qt::UserRole + 1;
	constexpr int PersistentChatThreadRole  = Qt::UserRole + 2;
	constexpr int PersistentChatMessageIDRole = Qt::UserRole + 3;
	constexpr int PersistentChatLabelRole   = Qt::UserRole + 4;
	constexpr int PersistentChatUnreadRole  = Qt::UserRole + 5;
	constexpr int LocalServerLogScope       = -1;
	constexpr int PersistentChatBottomInsetHeight = 18;

	bool chatMessageLessThan(const MumbleProto::ChatMessage &lhs, const MumbleProto::ChatMessage &rhs) {
		const quint64 lhsCreatedAt = lhs.has_created_at() ? lhs.created_at() : 0;
		const quint64 rhsCreatedAt = rhs.has_created_at() ? rhs.created_at() : 0;
		if (lhsCreatedAt != rhsCreatedAt) {
			return lhsCreatedAt < rhsCreatedAt;
		}
		if (lhs.thread_id() != rhs.thread_id()) {
			return lhs.thread_id() < rhs.thread_id();
		}

		return lhs.message_id() < rhs.message_id();
	}

	QString persistentChatScopeCacheKey(MumbleProto::ChatScope scope, unsigned int scopeID) {
		return QString::fromLatin1("%1:%2").arg(static_cast< int >(scope)).arg(scopeID);
	}

	QString persistentChatScopeJumpUrl(MumbleProto::ChatScope scope, unsigned int scopeID) {
		switch (scope) {
			case MumbleProto::TextChannel:
				return QString::fromLatin1("mumble-chat://scope/text/%1").arg(scopeID);
			case MumbleProto::Channel:
			case MumbleProto::ServerGlobal:
			case MumbleProto::Aggregate:
			default:
				return QString();
		}
	}

	std::size_t unreadMessagesAfter(const std::vector< MumbleProto::ChatMessage > &messages,
									unsigned int lastReadMessageID) {
		std::size_t unreadCount = 0;
		for (const MumbleProto::ChatMessage &message : messages) {
			if (message.message_id() > lastReadMessageID) {
				++unreadCount;
			}
		}

		return unreadCount;
	}

	QString persistentChatDateLabel(const QDate &date) {
		if (!date.isValid()) {
			return QObject::tr("Unknown date");
		}

		const QDate today = QDate::currentDate();
		if (date == today) {
			return QObject::tr("Today");
		}
		if (date == today.addDays(-1)) {
			return QObject::tr("Yesterday");
		}

		return QLocale().toString(date, QLocale::LongFormat);
	}

	QString persistentChatInitials(const QString &name) {
		const QString simplified = name.simplified();
		if (simplified.isEmpty()) {
			return QStringLiteral("?");
		}

		QString initials;
		const QStringList parts = simplified.split(QLatin1Char(' '), Qt::SkipEmptyParts);
		for (const QString &part : parts) {
			initials.append(part.left(1).toUpper());
			if (initials.size() >= 2) {
				break;
			}
		}

		if (initials.isEmpty()) {
			initials = simplified.left(1).toUpper();
		}

		return initials.left(2);
	}

	QSize persistentChatMeasuredItemHint(QWidget *widget, int itemWidth) {
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

	QWidget *createPersistentChatStateWidget(const QString &eyebrow, const QString &title, const QString &body,
											 const QStringList &hints, QWidget *parent, int minimumHeight) {
		QWidget *widget = new QWidget(parent);
		widget->setAttribute(Qt::WA_StyledBackground, true);
		widget->setProperty("persistentChatItemHeight", std::max(180, minimumHeight));

		QVBoxLayout *layout = new QVBoxLayout(widget);
		layout->setContentsMargins(20, 12, 20, 12);
		layout->setSpacing(0);
		layout->addStretch(1);

		QFrame *card = new QFrame(widget);
		card->setObjectName(QLatin1String("qfPersistentChatBanner"));
		card->setAttribute(Qt::WA_StyledBackground, true);
		card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
		card->setMaximumWidth(440);

		QVBoxLayout *cardLayout = new QVBoxLayout(card);
		cardLayout->setContentsMargins(18, 18, 18, 18);
		cardLayout->setSpacing(8);

		QLabel *eyebrowLabel = new QLabel(eyebrow, card);
		eyebrowLabel->setObjectName(QLatin1String("qlPersistentChatBannerEyebrow"));
		eyebrowLabel->setTextFormat(Qt::PlainText);
		QFont eyebrowFont = eyebrowLabel->font();
		eyebrowFont.setCapitalization(QFont::AllUppercase);
		eyebrowFont.setBold(true);
		eyebrowFont.setPointSizeF(std::max(eyebrowFont.pointSizeF() - 1.0, 8.0));
		eyebrowLabel->setFont(eyebrowFont);

		QLabel *titleLabel = new QLabel(title, card);
		titleLabel->setObjectName(QLatin1String("qlPersistentChatBannerTitle"));
		titleLabel->setTextFormat(Qt::PlainText);
		titleLabel->setWordWrap(true);
		QFont titleFont = titleLabel->font();
		titleFont.setBold(true);
		titleFont.setPointSizeF(titleFont.pointSizeF() + 2.0);
		titleLabel->setFont(titleFont);

		QLabel *bodyLabel = new QLabel(body, card);
		bodyLabel->setObjectName(QLatin1String("qlPersistentChatBannerBody"));
		bodyLabel->setTextFormat(Qt::PlainText);
		bodyLabel->setWordWrap(true);
		bodyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);

		cardLayout->addWidget(eyebrowLabel);
		cardLayout->addWidget(titleLabel);
		cardLayout->addWidget(bodyLabel);

		if (!hints.isEmpty()) {
			QWidget *hintRow = new QWidget(card);
			hintRow->setObjectName(QLatin1String("qwPersistentChatBannerHints"));
			QHBoxLayout *hintLayout = new QHBoxLayout(hintRow);
			hintLayout->setContentsMargins(0, 4, 0, 0);
			hintLayout->setSpacing(6);
			hintLayout->addStretch(1);
			for (const QString &hint : hints) {
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

	QImage persistentChatAvatarTexture(const ClientUser *user, int avatarSize) {
		if (!user || user->qbaTexture.isEmpty()) {
			return QImage();
		}

		QBuffer buffer(const_cast< QByteArray * >(&user->qbaTexture));
		buffer.open(QIODevice::ReadOnly);
		QImageReader reader(&buffer, user->qbaTextureFormat);
		QSize scaledSize = reader.size();
		scaledSize.scale(avatarSize, avatarSize, Qt::KeepAspectRatio);
		reader.setScaledSize(scaledSize);
		return reader.read();
	}

	bool isDarkPalette(const QPalette &palette) {
		return palette.color(QPalette::WindowText).lightness() > palette.color(QPalette::Window).lightness();
	}

	QColor mixColors(const QColor &baseColor, const QColor &overlayColor, qreal overlayRatio) {
		const qreal clampedRatio = qBound< qreal >(0.0, overlayRatio, 1.0);
		const qreal baseRatio    = 1.0 - clampedRatio;
		return QColor::fromRgbF(baseColor.redF() * baseRatio + overlayColor.redF() * clampedRatio,
								baseColor.greenF() * baseRatio + overlayColor.greenF() * clampedRatio,
								baseColor.blueF() * baseRatio + overlayColor.blueF() * clampedRatio, 1.0);
	}

	struct ChromePaletteColors {
		bool darkTheme = false;
		QColor railColor;
		QColor cardColor;
		QColor elevatedCardColor;
		QColor panelColor;
		QColor inputColor;
		QColor accentColor;
		QColor textColor;
		QColor mutedTextColor;
		QColor eyebrowColor;
		QColor borderColor;
		QColor dividerColor;
		QColor hoverColor;
		QColor selectedColor;
		QColor selectedTextColor;
		QColor scrollbarHandleColor;
		QColor scrollbarHandleHoverColor;
	};

	ChromePaletteColors buildChromePalette(const QPalette &palette) {
		ChromePaletteColors colors;
		colors.darkTheme = isDarkPalette(palette);

		const QColor windowColor      = palette.color(QPalette::Window);
		const QColor baseColor        = palette.color(QPalette::Base);
		const QColor alternateColor   = palette.color(QPalette::AlternateBase);
		const QColor highlightColor   = palette.color(QPalette::Highlight);
		const QColor highlightedText  = palette.color(QPalette::HighlightedText);
		const QColor textColor        = palette.color(QPalette::WindowText);

		colors.railColor        = windowColor;
		colors.textColor        = textColor;
		colors.selectedTextColor = highlightedText;
		colors.accentColor =
			colors.darkTheme ? mixColors(highlightColor, textColor, 0.10) : mixColors(highlightColor, baseColor, 0.08);

		if (colors.darkTheme) {
			colors.cardColor         = mixColors(baseColor, windowColor, 0.24);
			colors.elevatedCardColor = mixColors(colors.cardColor, colors.accentColor, 0.07);
			colors.panelColor        = mixColors(baseColor, colors.cardColor, 0.28);
			colors.inputColor        = mixColors(colors.panelColor, colors.cardColor, 0.26);
			colors.borderColor       = mixColors(colors.cardColor, colors.accentColor, 0.18);
			colors.dividerColor      = mixColors(colors.railColor, colors.borderColor, 0.68);
		} else {
			colors.cardColor         = mixColors(windowColor, alternateColor, 0.62);
			colors.elevatedCardColor = mixColors(colors.cardColor, colors.accentColor, 0.05);
			colors.panelColor        = mixColors(baseColor, alternateColor, 0.55);
			colors.inputColor        = mixColors(colors.panelColor, windowColor, 0.38);
			colors.borderColor       = mixColors(colors.cardColor, colors.accentColor, 0.18);
			colors.dividerColor      = mixColors(colors.cardColor, colors.borderColor, 0.78);
		}

		colors.mutedTextColor = mixColors(textColor, colors.railColor, colors.darkTheme ? 0.48 : 0.36);
		colors.eyebrowColor   = mixColors(colors.accentColor, textColor, colors.darkTheme ? 0.16 : 0.24);
		colors.hoverColor     = mixColors(colors.panelColor, colors.accentColor, colors.darkTheme ? 0.16 : 0.10);
		colors.selectedColor  = mixColors(colors.panelColor, colors.accentColor, colors.darkTheme ? 0.44 : 0.18);
		colors.scrollbarHandleColor =
			colors.darkTheme ? mixColors(colors.panelColor, textColor, 0.12) : mixColors(colors.panelColor, textColor, 0.10);
		colors.scrollbarHandleHoverColor =
			colors.darkTheme ? mixColors(colors.panelColor, textColor, 0.24) : mixColors(colors.panelColor, textColor, 0.20);

		return colors;
	}

	class PersistentChatScopeListDelegate : public QStyledItemDelegate {
	public:
		explicit PersistentChatScopeListDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {
		}

		QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const Q_DECL_OVERRIDE {
			QSize hint = QStyledItemDelegate::sizeHint(option, index);
			if (!index.isValid()) {
				return hint;
			}

			return QSize(hint.width(), std::max(hint.height(), QFontMetrics(option.font).height() + 12));
		}

		void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const Q_DECL_OVERRIDE {
			if (!index.isValid()) {
				return;
			}

			QStyleOptionViewItem opt(option);
			initStyleOption(&opt, index);

			const ChromePaletteColors chrome = buildChromePalette(opt.palette);
			const bool selected             = opt.state.testFlag(QStyle::State_Selected);
			const bool hovered              = opt.state.testFlag(QStyle::State_MouseOver);
			const QString label =
				index.data(PersistentChatLabelRole).toString().isEmpty() ? opt.text : index.data(PersistentChatLabelRole).toString();
			const qulonglong unreadCount = index.data(PersistentChatUnreadRole).toULongLong();
			const int scopeValue         = index.data(PersistentChatScopeRole).toInt();
			const bool utilityRow =
				scopeValue == LocalServerLogScope || scopeValue == static_cast< int >(MumbleProto::ServerGlobal);

			QString chipText = QStringLiteral("#");
			switch (scopeValue) {
				case LocalServerLogScope:
					chipText = QObject::tr("LOG");
					break;
				case static_cast< int >(MumbleProto::ServerGlobal):
					chipText = QObject::tr("ALL");
					break;
				case static_cast< int >(MumbleProto::Channel):
					chipText = QObject::tr("VC");
					break;
				case static_cast< int >(MumbleProto::TextChannel):
					chipText = QStringLiteral("#");
					break;
				default:
					break;
			}

			const QColor rowFillColor = selected
											 ? chrome.selectedColor
											 : (hovered ? chrome.hoverColor
														: (utilityRow ? mixColors(chrome.panelColor, chrome.elevatedCardColor, chrome.darkTheme ? 0.10 : 0.05)
																	  : QColor(Qt::transparent)));
			const QColor rowOutlineColor =
				selected ? mixColors(chrome.borderColor, chrome.accentColor, chrome.darkTheme ? 0.30 : 0.14) : QColor(Qt::transparent);
			const QColor textColor = selected ? chrome.selectedTextColor : chrome.textColor;
			const QColor mutedTextColor = selected ? chrome.selectedTextColor : chrome.mutedTextColor;
			const QColor chipFillColor =
				selected ? mixColors(chrome.selectedTextColor, chrome.selectedColor, chrome.darkTheme ? 0.14 : 0.08)
						 : mixColors(chrome.elevatedCardColor, chrome.selectedColor, utilityRow ? (chrome.darkTheme ? 0.18 : 0.08)
																								 : (chrome.darkTheme ? 0.24 : 0.10));
			const QColor chipTextColor = selected ? chrome.selectedTextColor : mutedTextColor;
			const QColor unreadFillColor =
				selected ? mixColors(chrome.selectedTextColor, chrome.selectedColor, chrome.darkTheme ? 0.16 : 0.10)
						 : mixColors(chrome.selectedColor, chrome.elevatedCardColor, chrome.darkTheme ? 0.34 : 0.14);
			const QColor unreadTextColor = selected ? chrome.selectedTextColor : chrome.textColor;

			QRect rowRect = option.rect.adjusted(4, 1, -4, -1);
			painter->save();
			painter->setRenderHint(QPainter::Antialiasing, true);
			if (rowRect.isValid() && (selected || hovered || utilityRow)) {
				painter->setPen(rowOutlineColor.alpha() == 0 ? Qt::NoPen : QPen(rowOutlineColor, 1.0));
				painter->setBrush(rowFillColor);
				painter->drawRoundedRect(rowRect, 10.0f, 10.0f);
			}

			QFont chipFont(opt.font);
			chipFont.setBold(true);
			chipFont.setPointSizeF(std::max(chipFont.pointSizeF() - 1.0, 8.0));
			const QFontMetrics chipMetrics(chipFont);

			int x = rowRect.left() + 8;
			const int chipWidth = std::max(20, chipMetrics.horizontalAdvance(chipText) + 10);
			const QRect chipRect(x, rowRect.center().y() - 9, chipWidth, 18);
			painter->setPen(Qt::NoPen);
			painter->setBrush(chipFillColor);
			painter->drawRoundedRect(chipRect, 9.0f, 9.0f);
			painter->setPen(chipTextColor);
			painter->setFont(chipFont);
			painter->drawText(chipRect, Qt::AlignCenter, chipText);
			x = chipRect.right() + 8;

			int textRight = rowRect.right() - 8;
			if (unreadCount > 0) {
				const QString unreadText = QString::number(unreadCount);
				const int unreadWidth = chipMetrics.horizontalAdvance(unreadText) + 12;
				const QRect unreadRect(textRight - unreadWidth, rowRect.center().y() - 9, unreadWidth, 18);
				painter->setPen(Qt::NoPen);
				painter->setBrush(unreadFillColor);
				painter->drawRoundedRect(unreadRect, 9.0f, 9.0f);
				painter->setPen(unreadTextColor);
				painter->drawText(unreadRect, Qt::AlignCenter, unreadText);
				textRight = unreadRect.left() - 6;
			}

			const QRect textRect(x, rowRect.top(), std::max(18, textRight - x), rowRect.height());
			QFont titleFont(opt.font);
			titleFont.setBold(selected || utilityRow);
			painter->setFont(titleFont);
			painter->setPen(textColor);
			painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
							  QFontMetrics(titleFont).elidedText(label.simplified(), Qt::ElideRight, textRect.width()));
			painter->restore();
		}
	};

#ifdef Q_OS_WIN
	using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);

	constexpr DWORD DwmUseImmersiveDarkModeAttribute       = 20;
	constexpr DWORD DwmUseImmersiveDarkModeLegacyAttribute = 19;
	constexpr DWORD DwmBorderColorAttribute                = 34;
	constexpr DWORD DwmCaptionColorAttribute               = 35;
	constexpr DWORD DwmTextColorAttribute                  = 36;

	COLORREF colorRefFromQColor(const QColor &color) {
		return RGB(color.red(), color.green(), color.blue());
	}

	void applyNativeTitleBarTheme(QWidget *widget) {
		if (!widget) {
			return;
		}

		const HWND hwnd = reinterpret_cast< HWND >(widget->winId());
		if (!hwnd) {
			return;
		}

		static const HMODULE dwmapiModule = GetModuleHandleW(L"dwmapi.dll");
		if (!dwmapiModule) {
			return;
		}

		static const DwmSetWindowAttributeFn setWindowAttribute =
			reinterpret_cast< DwmSetWindowAttributeFn >(GetProcAddress(dwmapiModule, "DwmSetWindowAttribute"));
		if (!setWindowAttribute) {
			return;
		}

		const QPalette palette      = widget->palette();
		const bool darkTheme        = isDarkPalette(palette);
		const QColor windowColor    = palette.color(QPalette::Window);
		const QColor baseColor      = palette.color(QPalette::Base);
		const QColor accentColor    = palette.color(QPalette::Highlight);
		const QColor titleTextColor = palette.color(QPalette::WindowText);
		const QColor captionColor =
			darkTheme ? mixColors(windowColor, baseColor, 0.22) : mixColors(windowColor, accentColor, 0.08);
		const QColor borderColor =
			darkTheme ? mixColors(captionColor, accentColor, 0.18) : mixColors(captionColor, accentColor, 0.26);

		const BOOL immersiveDarkMode = darkTheme ? TRUE : FALSE;
		HRESULT result =
			setWindowAttribute(hwnd, DwmUseImmersiveDarkModeAttribute, &immersiveDarkMode, sizeof(immersiveDarkMode));
		if (FAILED(result)) {
			setWindowAttribute(hwnd, DwmUseImmersiveDarkModeLegacyAttribute, &immersiveDarkMode,
							   sizeof(immersiveDarkMode));
		}

		const COLORREF captionColorRef = colorRefFromQColor(captionColor);
		const COLORREF textColorRef    = colorRefFromQColor(titleTextColor);
		const COLORREF borderColorRef  = colorRefFromQColor(borderColor);
		setWindowAttribute(hwnd, DwmCaptionColorAttribute, &captionColorRef, sizeof(captionColorRef));
		setWindowAttribute(hwnd, DwmTextColorAttribute, &textColorRef, sizeof(textColorRef));
		setWindowAttribute(hwnd, DwmBorderColorAttribute, &borderColorRef, sizeof(borderColorRef));
	}
#endif

	bool samePersistentChatActor(const MumbleProto::ChatMessage &lhs, const MumbleProto::ChatMessage &rhs) {
		if (lhs.has_actor() || rhs.has_actor()) {
			return lhs.has_actor() && rhs.has_actor() && lhs.actor() == rhs.actor();
		}

		if (lhs.has_actor_user_id() || rhs.has_actor_user_id()) {
			return lhs.has_actor_user_id() && rhs.has_actor_user_id() && lhs.actor_user_id() == rhs.actor_user_id();
		}

		if (lhs.has_actor_name() || rhs.has_actor_name()) {
			return lhs.has_actor_name() && rhs.has_actor_name() && u8(lhs.actor_name()) == u8(rhs.actor_name());
		}

		return true;
	}

	bool samePersistentChatScope(const MumbleProto::ChatMessage &lhs, const MumbleProto::ChatMessage &rhs) {
		const MumbleProto::ChatScope lhsScope = lhs.has_scope() ? lhs.scope() : MumbleProto::Channel;
		const MumbleProto::ChatScope rhsScope = rhs.has_scope() ? rhs.scope() : MumbleProto::Channel;
		const unsigned int lhsScopeID         = lhs.has_scope_id() ? lhs.scope_id() : 0;
		const unsigned int rhsScopeID         = rhs.has_scope_id() ? rhs.scope_id() : 0;
		return lhsScope == rhsScope && lhsScopeID == rhsScopeID;
	}

	bool startsPersistentChatGroup(const std::optional< MumbleProto::ChatMessage > &previousMessage,
								   const QDateTime &previousCreatedAt, const MumbleProto::ChatMessage &message,
								   const QDateTime &createdAt) {
		if (!previousMessage.has_value()) {
			return true;
		}

		if (previousCreatedAt.date() != createdAt.date()) {
			return true;
		}

		if (!samePersistentChatActor(previousMessage.value(), message)
			|| !samePersistentChatScope(previousMessage.value(), message)) {
			return true;
		}

		if (previousCreatedAt.isValid() && createdAt.isValid() && previousCreatedAt.secsTo(createdAt) > (5 * 60)) {
			return true;
		}

		return false;
	}

	QString persistentChatActorLabel(const MumbleProto::ChatMessage &msg) {
		if (msg.has_actor()) {
			ClientUser *user = ClientUser::get(msg.actor());
			if (user) {
				return msg.has_actor_name() ? Log::formatClientUser(user, Log::Source, u8(msg.actor_name()))
										   : Log::formatClientUser(user, Log::Source);
			}
		}

		if (msg.has_actor_name()) {
			return u8(msg.actor_name()).toHtmlEscaped();
		}

		if (msg.has_actor_user_id()) {
			return QObject::tr("User %1").arg(msg.actor_user_id()).toHtmlEscaped();
		}

		return QObject::tr("Unknown user").toHtmlEscaped();
	}

	QString normalizedPersistentChatText(QString text) {
		return text.replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1Char('\r'),
																			 QLatin1Char('\n'));
	}

	QString persistentChatContentHtml(const QString &content) {
		const QString normalizedContent = normalizedPersistentChatText(content);
		if (!Qt::mightBeRichText(normalizedContent)) {
			return normalizedContent.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>"));
		}

		const QString sanitizedHtml = Log::validHtml(normalizedContent);
		static const QRegularExpression bodyPattern(QLatin1String("<body[^>]*>(.*)</body>"),
													QRegularExpression::DotMatchesEverythingOption
														| QRegularExpression::CaseInsensitiveOption);
		const QRegularExpressionMatch bodyMatch = bodyPattern.match(sanitizedHtml);
		if (bodyMatch.hasMatch()) {
			return bodyMatch.captured(1);
		}

		return sanitizedHtml;
	}

	QString persistentChatPlainTextSummary(const QString &content, int maxLength = 160) {
		const QString plainText =
			QTextDocumentFragment::fromHtml(persistentChatContentHtml(content)).toPlainText().simplified();
		if (plainText.size() <= maxLength) {
			return plainText;
		}

		return plainText.left(std::max(0, maxLength - 3)).trimmed() + QLatin1String("...");
	}

	QString persistentChatMessageSourceText(const MumbleProto::ChatMessage &message) {
		if (message.has_body_text()) {
			return normalizedPersistentChatText(u8(message.body_text()));
		}

		return QTextDocumentFragment::fromHtml(u8(message.message())).toPlainText();
	}

	QString persistentChatMessageRawBody(const MumbleProto::ChatMessage &message) {
		if (message.has_body_text()) {
			return normalizedPersistentChatText(u8(message.body_text()));
		}

		return u8(message.message());
	}

	QString persistentChatMessageBodyHtml(const MumbleProto::ChatMessage &message) {
		if (message.has_body_text()) {
			const QString bodyText = normalizedPersistentChatText(u8(message.body_text()));
			if (message.has_body_format() && message.body_format() == MumbleProto::ChatBodyFormatMarkdownLite) {
				return persistentChatContentHtml(Markdown::markdownToHTML(bodyText));
			}

			return bodyText.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>"));
		}

		return persistentChatContentHtml(u8(message.message()));
	}

	QString mirroredServerLogHtml(const QString &html) {
		const QString fragmentHtml = persistentChatContentHtml(html);
		return QString::fromLatin1(
				   "<div style='margin:0; padding:0; border:none; background:transparent;'>%1</div>")
			.arg(fragmentHtml);
	}

	void insertPersistentChatContent(QTextCursor &cursor, const QString &content) {
		const QString fragmentHtml = persistentChatContentHtml(content);
		if (!fragmentHtml.isEmpty()) {
			cursor.insertHtml(fragmentHtml);
		}
	}

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

	QString persistentTextAclChannelLabel(const Channel *channel) {
		if (!channel) {
			return QObject::tr("Unknown channel");
		}

		QStringList segments;
		for (const Channel *current = channel; current; current = current->cParent) {
			segments.prepend(current->qsName);
		}

		return segments.join(QString::fromLatin1(" / "));
	}

	QString trimTrailingUrlPunctuation(QString url) {
		while (!url.isEmpty()) {
			const QChar last = url.back();
			if (QString::fromLatin1(".,!?;:)]}>").contains(last)) {
				url.chop(1);
			} else {
				break;
			}
		}

		return url;
	}

	void setDockSplitterHandleWidth(QWidget *root, int width) {
		if (!root) {
			return;
		}

		for (QSplitter *splitter : root->findChildren< QSplitter * >()) {
			splitter->setHandleWidth(width);
		}
	}

	std::optional< QString > extractYouTubeVideoId(const QUrl &url) {
		if (!url.isValid()) {
			return std::nullopt;
		}

		QString host = url.host().toLower();
		if (host.startsWith(QLatin1String("www."))) {
			host.remove(0, 4);
		}
		if (host.startsWith(QLatin1String("m."))) {
			host.remove(0, 2);
		}

		QString videoId;
		const QStringList pathSegments = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
		if (host == QLatin1String("youtu.be")) {
			if (!pathSegments.isEmpty()) {
				videoId = pathSegments.front();
			}
		} else if (host == QLatin1String("youtube.com") || host == QLatin1String("youtube-nocookie.com")) {
			const QString path = url.path();
			QUrlQuery query(url);
			if (path == QLatin1String("/watch")) {
				videoId = query.queryItemValue(QLatin1String("v"));
			} else if (!pathSegments.isEmpty()) {
				if (pathSegments.front() == QLatin1String("shorts") || pathSegments.front() == QLatin1String("embed")
					|| pathSegments.front() == QLatin1String("live")) {
					if (pathSegments.size() > 1) {
						videoId = pathSegments.at(1);
					}
				}
			}
		}

		static const QRegularExpression s_videoIdPattern(
			QRegularExpression::anchoredPattern(QLatin1String("[A-Za-z0-9_-]{11}")));
		if (!s_videoIdPattern.match(videoId).hasMatch()) {
			return std::nullopt;
		}

		return videoId;
	}

	bool isYouTubeHost(QString host) {
		host = host.toLower();
		if (host.startsWith(QLatin1String("www."))) {
			host.remove(0, 4);
		}
		if (host.startsWith(QLatin1String("m."))) {
			host.remove(0, 2);
		}

		return host == QLatin1String("youtube.com") || host == QLatin1String("youtube-nocookie.com")
			   || host == QLatin1String("youtu.be");
	}

	bool isDirectImageUrl(const QUrl &url) {
		if (!url.isValid()) {
			return false;
		}

		const QString path = url.path().toLower();
		return path.endsWith(QLatin1String(".png")) || path.endsWith(QLatin1String(".jpg"))
			   || path.endsWith(QLatin1String(".jpeg")) || path.endsWith(QLatin1String(".gif"))
			   || path.endsWith(QLatin1String(".webp")) || path.endsWith(QLatin1String(".bmp"));
	}

	QString normalizedPreviewUrl(const QUrl &url) {
		QUrl normalized = url.adjusted(QUrl::NormalizePathSegments | QUrl::RemoveFragment);
		return normalized.toString(QUrl::FullyEncoded);
	}

	QString previewDisplayHost(const QUrl &url) {
		QString host = url.host().toLower();
		if (host.startsWith(QLatin1String("www."))) {
			host.remove(0, 4);
		}
		return host;
	}

	bool isPrivateOrLocalAddress(const QHostAddress &address) {
		if (address.isNull() || address.isLoopback() || address.isMulticast()) {
			return true;
		}

		bool isIPv4 = false;
		const quint32 ipv4Address = address.toIPv4Address(&isIPv4);
		if (isIPv4) {
			const quint8 firstOctet  = static_cast< quint8 >((ipv4Address >> 24) & 0xff);
			const quint8 secondOctet = static_cast< quint8 >((ipv4Address >> 16) & 0xff);
			if (firstOctet == 0 || firstOctet == 10 || firstOctet == 127) {
				return true;
			}
			if (firstOctet == 169 && secondOctet == 254) {
				return true;
			}
			if (firstOctet == 172 && secondOctet >= 16 && secondOctet <= 31) {
				return true;
			}
			if (firstOctet == 192 && secondOctet == 168) {
				return true;
			}
			if (firstOctet == 100 && secondOctet >= 64 && secondOctet <= 127) {
				return true;
			}

			return false;
		}

		const Q_IPV6ADDR ipv6Address = address.toIPv6Address();
		if ((ipv6Address.c[0] & 0xfe) == 0xfc) {
			return true;
		}
		if (ipv6Address.c[0] == 0xfe && (ipv6Address.c[1] & 0xc0) == 0x80) {
			return true;
		}
		if (ipv6Address.c[0] == 0xfe && (ipv6Address.c[1] & 0xc0) == 0xc0) {
			return true;
		}

		return false;
	}

	bool isSafePreviewTarget(const QUrl &url) {
		if (!url.isValid()) {
			return false;
		}

		const QString scheme = url.scheme().toLower();
		if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
			return false;
		}
		if (!url.userName().isEmpty() || !url.password().isEmpty()) {
			return false;
		}

		const QString host = previewDisplayHost(url);
		if (host.isEmpty()) {
			return false;
		}
		if (host == QLatin1String("localhost") || host.endsWith(QLatin1String(".localhost"))
			|| host.endsWith(QLatin1String(".local")) || host.endsWith(QLatin1String(".lan"))
			|| host.endsWith(QLatin1String(".internal")) || host.endsWith(QLatin1String(".home.arpa"))) {
			return false;
		}

		QHostAddress literalAddress;
		if (literalAddress.setAddress(host)) {
			return !isPrivateOrLocalAddress(literalAddress);
		}

		if (!host.contains(QLatin1Char('.'))) {
			return false;
		}

		return true;
	}

	constexpr int PREVIEW_REQUEST_TIMEOUT_MSEC = 8000;
	constexpr qint64 PREVIEW_MAX_PAGE_BYTES    = 512 * 1024;
	constexpr qint64 PREVIEW_MAX_IMAGE_BYTES   = 4 * 1024 * 1024;
	constexpr int PERSISTENT_CHAT_RESIZE_RENDER_DELAY_MSEC = 120;
	constexpr int PERSISTENT_CHAT_PREVIEW_SOURCE_WIDTH   = 640;
	constexpr int PERSISTENT_CHAT_PREVIEW_SOURCE_HEIGHT  = 480;
	constexpr int PERSISTENT_CHAT_PREVIEW_DISPLAY_WIDTH  = 320;
	constexpr int PERSISTENT_CHAT_PREVIEW_DISPLAY_HEIGHT = 240;
	constexpr int PERSISTENT_CHAT_PREVIEW_CARD_MAX_WIDTH = 360;
	constexpr int PERSISTENT_CHAT_PREVIEW_WIDTH_STEP     = 24;

	void setPreviewAbortReason(QNetworkReply *reply, const QString &reason) {
		if (reply) {
			reply->setProperty("previewAbortReason", reason);
		}
	}

	QString previewAbortReason(const QNetworkReply *reply) {
		return reply ? reply->property("previewAbortReason").toString() : QString();
	}

	QString previewFailureText(const QNetworkReply *reply) {
		const QString abortReason = previewAbortReason(reply);
		if (abortReason == QLatin1String("timeout")) {
			return QObject::tr("Preview request timed out");
		}
		if (abortReason == QLatin1String("too_large")) {
			return QObject::tr("Preview exceeded size limit");
		}

		return QObject::tr("Preview unavailable");
	}

	void applyPreviewReplyGuards(QNetworkReply *reply, qint64 maxBytes) {
		if (!reply) {
			return;
		}

		QTimer *timeoutTimer = new QTimer(reply);
		timeoutTimer->setSingleShot(true);
		timeoutTimer->setInterval(PREVIEW_REQUEST_TIMEOUT_MSEC);
		QObject::connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
			if (!reply->isFinished()) {
				setPreviewAbortReason(reply, QLatin1String("timeout"));
				reply->abort();
			}
		});
		QObject::connect(reply, &QNetworkReply::finished, timeoutTimer, &QTimer::stop);
		timeoutTimer->start();

		if (maxBytes > 0) {
			QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, maxBytes]() {
				const qint64 contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
				if (contentLength > maxBytes && !reply->isFinished()) {
					setPreviewAbortReason(reply, QLatin1String("too_large"));
					reply->abort();
				}
			});
			QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 received, qint64) {
				if (received > maxBytes && !reply->isFinished()) {
					setPreviewAbortReason(reply, QLatin1String("too_large"));
					reply->abort();
				}
			});
		}
	}

	QImage persistentChatThumbnailImage(const QImage &image) {
		if (image.isNull()) {
			return QImage();
		}

		if (image.width() <= PERSISTENT_CHAT_PREVIEW_SOURCE_WIDTH
			&& image.height() <= PERSISTENT_CHAT_PREVIEW_SOURCE_HEIGHT) {
			return image;
		}

		return image.scaled(PERSISTENT_CHAT_PREVIEW_SOURCE_WIDTH, PERSISTENT_CHAT_PREVIEW_SOURCE_HEIGHT,
							Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}

	QImage decodePersistentChatThumbnailImage(const QByteArray &data) {
		if (data.isEmpty()) {
			return QImage();
		}

		QBuffer buffer;
		buffer.setData(data);
		if (!buffer.open(QIODevice::ReadOnly)) {
			return QImage();
		}

		QImageReader reader(&buffer);
		reader.setAutoTransform(true);
		const QSize sourceSize = reader.size();
		if (sourceSize.isValid() && (sourceSize.width() > PERSISTENT_CHAT_PREVIEW_SOURCE_WIDTH
									 || sourceSize.height() > PERSISTENT_CHAT_PREVIEW_SOURCE_HEIGHT)) {
			reader.setScaledSize(sourceSize.scaled(PERSISTENT_CHAT_PREVIEW_SOURCE_WIDTH,
												  PERSISTENT_CHAT_PREVIEW_SOURCE_HEIGHT, Qt::KeepAspectRatio));
		}

		return reader.read();
	}

	QUrl persistentChatThumbnailResourceUrl(const QString &previewKey) {
		QByteArray resourceUrl("mumble-preview:");
		resourceUrl.append(QUrl::toPercentEncoding(previewKey));
		return QUrl::fromEncoded(resourceUrl);
	}

	QString persistentChatThumbnailHtml(const QString &previewKey, const QImage &image, int maxWidth) {
		if (previewKey.isEmpty() || image.isNull() || maxWidth <= 0) {
			return QString();
		}

		const QSize displaySize =
			image.size().scaled(std::min(PERSISTENT_CHAT_PREVIEW_DISPLAY_WIDTH, maxWidth),
								PERSISTENT_CHAT_PREVIEW_DISPLAY_HEIGHT, Qt::KeepAspectRatio);
		if (!displaySize.isValid() || displaySize.isEmpty()) {
			return QString();
		}

		return QString::fromLatin1(
				   "<img src=\"%1\" width=\"%2\" height=\"%3\" "
				   "style=\"border:none; outline:none; display:block; margin:0;\" />")
			.arg(persistentChatThumbnailResourceUrl(previewKey).toString(QUrl::FullyEncoded).toHtmlEscaped())
			.arg(displaySize.width())
			.arg(displaySize.height());
	}

	QString decodedPreviewText(const QString &text) {
		return QTextDocumentFragment::fromHtml(text).toPlainText().simplified();
	}

	QString extractHtmlTitle(const QString &html) {
		static const QRegularExpression s_titleRegex(QLatin1String("<title[^>]*>(.*?)</title>"),
													 QRegularExpression::CaseInsensitiveOption
														 | QRegularExpression::DotMatchesEverythingOption);
		const QRegularExpressionMatch match = s_titleRegex.match(html);
		return match.hasMatch() ? decodedPreviewText(match.captured(1)) : QString();
	}

	QHash< QString, QString > extractMetaTags(const QString &html) {
		QHash< QString, QString > tags;

		static const QRegularExpression s_metaTagRegex(QLatin1String("<meta\\s+([^>]+)>"),
													   QRegularExpression::CaseInsensitiveOption);
		static const QRegularExpression s_attrRegex(
			QLatin1String("([A-Za-z_:][-A-Za-z0-9_:.]*)\\s*=\\s*([\"'])(.*?)\\2"),
			QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

		QRegularExpressionMatchIterator metaTags = s_metaTagRegex.globalMatch(html);
		while (metaTags.hasNext()) {
			const QString attrsString = metaTags.next().captured(1);
			QHash< QString, QString > attrs;
			QRegularExpressionMatchIterator attrsIt = s_attrRegex.globalMatch(attrsString);
			while (attrsIt.hasNext()) {
				const QRegularExpressionMatch attrMatch = attrsIt.next();
				attrs.insert(attrMatch.captured(1).toLower(), attrMatch.captured(3));
			}

			const QString key = attrs.value(QLatin1String("property"), attrs.value(QLatin1String("name"))).toLower();
			const QString content = attrs.value(QLatin1String("content"));
			if (!key.isEmpty() && !content.isEmpty() && !tags.contains(key)) {
				tags.insert(key, decodedPreviewText(content));
			}
		}

		return tags;
	}

	QList< QUrl > extractPreviewableUrls(const QString &messageHtml) {
		QList< QUrl > urls;
		QSet< QString > seen;

		auto addUrlCandidate = [&](QString candidate) {
			candidate = trimTrailingUrlPunctuation(candidate.trimmed());
			if (candidate.isEmpty()) {
				return;
			}

			const QUrl url = QUrl::fromUserInput(candidate);
			if (!url.isValid()) {
				return;
			}
			const QString scheme = url.scheme().toLower();
			if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
				return;
			}

			const QString normalized = url.toString(QUrl::FullyEncoded);
			if (seen.contains(normalized)) {
				return;
			}

			seen.insert(normalized);
			urls << url;
		};

		static const QRegularExpression s_hrefRegex(
			QLatin1String("<a\\s[^>]*href\\s*=\\s*[\"']([^\"']+)[\"']"),
			QRegularExpression::CaseInsensitiveOption);
		QRegularExpressionMatchIterator hrefMatches = s_hrefRegex.globalMatch(messageHtml);
		while (hrefMatches.hasNext()) {
			addUrlCandidate(hrefMatches.next().captured(1));
		}

		const QString plainText = QTextDocumentFragment::fromHtml(messageHtml).toPlainText();
		static const QRegularExpression s_urlRegex(QLatin1String("(https?://[^\\s<>\"]+)"),
												   QRegularExpression::CaseInsensitiveOption);
		QRegularExpressionMatchIterator urlMatches = s_urlRegex.globalMatch(plainText);
		while (urlMatches.hasNext()) {
			addUrlCandidate(urlMatches.next().captured(1));
		}

		return urls;
	}

	struct PersistentChatReplyReference {
		unsigned int messageID = 0;
		QString actor;
		QString snippet;
	};

	QString persistentChatMessageTextSnippet(const QString &messageHtml, int maxLength = 140) {
		QString snippet = Qt::mightBeRichText(messageHtml)
							  ? QTextDocumentFragment::fromHtml(messageHtml).toPlainText().simplified()
							  : normalizedPersistentChatText(messageHtml).simplified();
		if (snippet.size() > maxLength) {
			snippet = snippet.left(std::max(0, maxLength - 1)).trimmed() + QChar(0x2026);
		}
		return snippet;
	}

	QString buildPersistentChatReplyHtml(const MumbleProto::ChatMessage &replyTarget, const QString &bodyHtml) {
		QJsonObject metadata;
		metadata.insert(QStringLiteral("message_id"), static_cast< int >(replyTarget.message_id()));
		metadata.insert(QStringLiteral("actor"), persistentChatActorLabel(replyTarget));
		metadata.insert(QStringLiteral("snippet"), persistentChatMessageTextSnippet(persistentChatMessageSourceText(replyTarget)));

		const QString actor = persistentChatActorLabel(replyTarget).toHtmlEscaped();
		const QString snippet = persistentChatMessageTextSnippet(persistentChatMessageSourceText(replyTarget)).toHtmlEscaped();
		const QString metadataJson = QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
		return QString::fromLatin1(
				   "<!--mumble-reply:%1--><blockquote data-mumble-reply-quote=\"1\"><strong>%2</strong><br/>%3</blockquote>%4")
			.arg(metadataJson, actor, snippet, bodyHtml);
	}

	std::optional< PersistentChatReplyReference > extractPersistentChatReplyReference(const QString &messageHtml,
																							  QString *bodyHtml) {
		static const QRegularExpression s_replyRegex(
			QLatin1String(
				"^\\s*<!--mumble-reply:([^>]*)-->\\s*(?:<blockquote\\s+data-mumble-reply-quote=\"1\"[^>]*>.*?</blockquote>)?"),
			QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
		const QRegularExpressionMatch match = s_replyRegex.match(messageHtml);
		if (!match.hasMatch()) {
			if (bodyHtml) {
				*bodyHtml = messageHtml;
			}
			return std::nullopt;
		}

		PersistentChatReplyReference reference;
		QJsonParseError error;
		const QByteArray metadataJson = match.captured(1).toUtf8();
		const QJsonDocument metadata = QJsonDocument::fromJson(metadataJson, &error);
		if (error.error == QJsonParseError::NoError && metadata.isObject()) {
			const QJsonObject object = metadata.object();
			reference.messageID      = static_cast< unsigned int >(object.value(QStringLiteral("message_id")).toInt());
			reference.actor          = object.value(QStringLiteral("actor")).toString().trimmed();
			reference.snippet        = object.value(QStringLiteral("snippet")).toString().trimmed();
		}

		if (bodyHtml) {
			*bodyHtml = messageHtml.mid(match.capturedLength(0)).trimmed();
		}

		if (reference.actor.isEmpty() && reference.snippet.isEmpty() && reference.messageID == 0) {
			return std::nullopt;
		}

		return reference;
	}

	const MumbleProto::ChatMessage *findPersistentChatMessageByID(const std::vector< MumbleProto::ChatMessage > &messages,
																 unsigned int messageID) {
		for (const MumbleProto::ChatMessage &message : messages) {
			if (message.message_id() == messageID) {
				return &message;
			}
		}

		return nullptr;
	}
} // namespace

MessageBoxEvent::MessageBoxEvent(QString m) : QEvent(static_cast< QEvent::Type >(MB_QEVENT)) {
	msg = m;
}

OpenURLEvent::OpenURLEvent(QUrl u) : QEvent(static_cast< QEvent::Type >(OU_QEVENT)) {
	url = u;
}

MainWindow::MainWindow(QWidget *p)
	: QMainWindow(p), m_localVolumeLabel(make_qt_unique< MenuLabel >(tr("Local Volume Adjustment:"), this)),
	  m_userLocalVolumeSlider(make_qt_unique< UserLocalVolumeSlider >(this)),
	  m_listenerVolumeSlider(make_qt_unique< ListenerVolumeSlider >(this)) {
	SvgIcon::addSvgPixmapsToIcon(qiIconMuteSelf, QLatin1String("skin:muted_self.svg"));
	SvgIcon::addSvgPixmapsToIcon(qiIconMuteServer, QLatin1String("skin:muted_server.svg"));
	SvgIcon::addSvgPixmapsToIcon(qiIconMuteSuppressed, QLatin1String("skin:muted_suppressed.svg"));
	SvgIcon::addSvgPixmapsToIcon(qiIconMutePushToMute, QLatin1String("skin:muted_pushtomute.svg"));
	SvgIcon::addSvgPixmapsToIcon(qiIconDeafSelf, QLatin1String("skin:deafened_self.svg"));
	SvgIcon::addSvgPixmapsToIcon(qiIconDeafServer, QLatin1String("skin:deafened_server.svg"));
	SvgIcon::addSvgPixmapsToIcon(qiTalkingOff, QLatin1String("skin:talking_off.svg"));
	SvgIcon::addSvgPixmapsToIcon(qiTalkingOn, QLatin1String("skin:talking_on.svg"));
	SvgIcon::addSvgPixmapsToIcon(qiTalkingShout, QLatin1String("skin:talking_alt.svg"));
	SvgIcon::addSvgPixmapsToIcon(qiTalkingWhisper, QLatin1String("skin:talking_whisper.svg"));
	SvgIcon::addSvgPixmapsToIcon(m_iconInformation, QLatin1String("skin:Information_icon.svg"));

#ifdef Q_OS_MAC
	if (QFile::exists(QLatin1String("skin:mumble.icns")))
		qiIcon.addFile(QLatin1String("skin:mumble.icns"));
	else
		SvgIcon::addSvgPixmapsToIcon(qiIcon, QLatin1String("skin:mumble.svg"));
#else
	{ SvgIcon::addSvgPixmapsToIcon(qiIcon, QLatin1String("skin:mumble.svg")); }

	// Set application icon except on MacOSX, where the window-icon
	// shown in the title-bar usually serves as a draggable version of the
	// current open document (i.e. you can copy the open document anywhere
	// simply by dragging this icon).
	qApp->setWindowIcon(qiIcon);

	// Set the icon on the MainWindow directly. This fixes the icon not
	// being set on the MainWindow in certain environments (Ex: GTK+).
	setWindowIcon(qiIcon);
#endif

#ifdef Q_OS_WIN
	uiNewHardware = 1;
#endif
	forceQuit     = false;
	restartOnQuit = false;

	Channel::add(Mumble::ROOT_CHANNEL_ID, tr("Root"));

	aclEdit   = nullptr;
	banEdit   = nullptr;
	userEdit  = nullptr;
	tokenEdit = nullptr;

	voiceRecorderDialog = nullptr;

	qwPTTButtonWidget = nullptr;

	qtReconnect = new QTimer(this);
	qtReconnect->setInterval(10000);
	qtReconnect->setSingleShot(true);
	qtReconnect->setObjectName(QLatin1String("Reconnect"));

	qmUser     = new QMenu(tr("&User"), this);
	qmChannel  = new QMenu(tr("&Channel"), this);
	qmListener = new QMenu(tr("&Listener"), this);

	qmDeveloper = new QMenu(tr("&Developer"), this);

	qaEmpty = new QAction(tr("No action available..."), this);
	qaEmpty->setEnabled(false);

	createActions();
	setupUi(this);
	qaUserRemoteSpeechCleanup = new QAction(tr("Remote Speech Cleanup"), this);
	qaUserRemoteSpeechCleanup->setCheckable(true);
	qaUserRemoteSpeechCleanup->setToolTip(tr("Clean up this user's incoming speech locally"));
	qaUserRemoteSpeechCleanup->setWhatsThis(
		tr("Enable or disable receive-side speech cleanup for this user on this client only."));
	connect(qaUserRemoteSpeechCleanup, &QAction::triggered, this, &MainWindow::triggerUserRemoteSpeechCleanup);
	qaChannelScreenShareStart = new QAction(tr("Start Screen Share"), this);
	qaChannelScreenShareStop = new QAction(tr("Stop Screen Share"), this);
	qaChannelScreenShareWatch = new QAction(tr("Watch Screen Share"), this);
	qaChannelScreenShareStopWatching = new QAction(tr("Stop Watching Screen Share"), this);
	connect(qaChannelScreenShareStart, &QAction::triggered, this, &MainWindow::startChannelScreenShare);
	connect(qaChannelScreenShareStop, &QAction::triggered, this, &MainWindow::stopChannelScreenShare);
	connect(qaChannelScreenShareWatch, &QAction::triggered, this, &MainWindow::watchChannelScreenShare);
	connect(qaChannelScreenShareStopWatching, &QAction::triggered, this,
			&MainWindow::stopWatchingChannelScreenShare);
	setupGui();
	connect(qmUser, SIGNAL(aboutToShow()), this, SLOT(qmUser_aboutToShow()));
	connect(qmChannel, SIGNAL(aboutToShow()), this, SLOT(qmChannel_aboutToShow()));
	connect(qmListener, SIGNAL(aboutToShow()), this, SLOT(qmListener_aboutToShow()));
	connect(qteChat, SIGNAL(entered(QString)), this, SLOT(sendChatbarText(QString)));
	connect(qteChat, &ChatbarTextEdit::ctrlEnterPressed, [this](const QString &msg) { sendChatbarText(msg, true); });
	connect(qteChat, SIGNAL(pastedImage(QString)), this, SLOT(sendChatbarMessage(QString)));

	QObject::connect(qaServerAddToFavorites, &QAction::triggered, this, &MainWindow::addServerAsFavorite);

	QObject::connect(this, &MainWindow::transmissionModeChanged, this, &MainWindow::updateTransmitModeComboBox);

	// Explicitly add actions to mainwindow so their shortcuts are available
	// if only the main window is visible (e.g. Global::get(). minimal mode)
	addActions(findChildren< QAction * >());

	on_qmServer_aboutToShow();
	on_qmSelf_aboutToShow();
	qmChannel_aboutToShow();
	qmUser_aboutToShow();
	on_qmConfig_aboutToShow();

	qmDeveloper->addAction(qaDeveloperConsole);
	qmDeveloper->addAction(qaPositionalAudioViewer);

	setOnTop(Global::get().s.aotbAlwaysOnTop == Settings::OnTopAlways
			 || (Global::get().s.bMinimalView && Global::get().s.aotbAlwaysOnTop == Settings::OnTopInMinimal)
			 || (!Global::get().s.bMinimalView && Global::get().s.aotbAlwaysOnTop == Settings::OnTopInNormal));

	m_screenShareManager = std::make_unique< ScreenShareManager >(this);
	QObject::connect(this, &MainWindow::serverSynchronized, Global::get().pluginManager,
					 &PluginManager::on_serverSynchronized);
	QObject::connect(this, &MainWindow::disconnectedFromServer, m_screenShareManager.get(),
					 &ScreenShareManager::resetState);

	// Set up initial client side talking state without the need for the user to do anything.
	// This will, for example, make sure the correct status tray icon is used on connect.
	QObject::connect(this, &MainWindow::serverSynchronized, this, &MainWindow::userStateChanged);

	QObject::connect(this, &MainWindow::channelStateChanged, this, &MainWindow::on_channelStateChanged);

	QAccessible::installFactory(AccessibleSlider::semanticSliderFactory);
}

// Loading a state that was stored by a different version of Qt can lead to a crash.
// This function calculates the state version based on Qt's version and MainWindow.ui's hash (provided through CMake).
// That way we also avoid potentially causing bugs/glitches when there are changes to MainWindow's widgets.
constexpr int MainWindow::stateVersion() {
	return MUMBLE_MAINWINDOW_UI_HASH ^ QT_VERSION;
}

void MainWindow::createActions() {
	gsPushTalk = new GlobalShortcut(this, GlobalShortcutType::PushToTalk, tr("Push-to-Talk", "Global Shortcut"));
	gsPushTalk->setObjectName(QLatin1String("PushToTalk"));
	gsPushTalk->qsToolTip   = tr("Push and hold this button to send voice.", "Global Shortcut");
	gsPushTalk->qsWhatsThis = tr(
		"This configures the push-to-talk button, and as long as you hold this button down, you will transmit voice.",
		"Global Shortcut");


	gsResetAudio =
		new GlobalShortcut(this, GlobalShortcutType::ResetAudio, tr("Reset Audio Processor", "Global Shortcut"));
	gsResetAudio->setObjectName(QLatin1String("ResetAudio"));

	gsMuteSelf = new GlobalShortcut(this, GlobalShortcutType::MuteSelf, tr("Mute Self", "Global Shortcut"), 0);
	gsMuteSelf->setObjectName(QLatin1String("gsMuteSelf"));
	gsMuteSelf->qsToolTip = tr("Set self-mute status.", "Global Shortcut");
	gsMuteSelf->qsWhatsThis =
		tr("This will set or toggle your muted status. If you turn this off, you will also disable self-deafen.",
		   "Global Shortcut");

	gsDeafSelf = new GlobalShortcut(this, GlobalShortcutType::DeafenSelf, tr("Deafen Self", "Global Shortcut"), 0);
	gsDeafSelf->setObjectName(QLatin1String("gsDeafSelf"));
	gsDeafSelf->qsToolTip = tr("Set self-deafen status.", "Global Shortcut");
	gsDeafSelf->qsWhatsThis =
		tr("This will set or toggle your deafened status. If you turn this on, you will also enable self-mute.",
		   "Global Shortcut");

	gsUnlink = new GlobalShortcut(this, GlobalShortcutType::UnlinkPlugin, tr("Unlink Plugin", "Global Shortcut"));
	gsUnlink->setObjectName(QLatin1String("UnlinkPlugin"));

	gsPushMute = new GlobalShortcut(this, GlobalShortcutType::PushToMute, tr("Push-to-Mute", "Global Shortcut"));
	gsPushMute->setObjectName(QLatin1String("PushToMute"));

	gsJoinChannel = new GlobalShortcut(this, GlobalShortcutType::JoinChannel, tr("Join Channel", "Global Shortcut"));
	gsJoinChannel->setObjectName(QLatin1String("MetaChannel"));
	gsJoinChannel->qsToolTip = tr("Use in conjunction with Whisper to.", "Global Shortcut");

	gsListenChannel =
		new GlobalShortcut(this, GlobalShortcutType::ListenToChannel, tr("Listen to Channel", "Global Shortcut"),
						   QVariant::fromValue(ChannelTarget()));
	gsListenChannel->setObjectName(QLatin1String("gsListenChannel"));
	gsListenChannel->qsToolTip = tr("Toggles listening to the given channel.", "Global Shortcut");

#ifdef USE_OVERLAY
	gsToggleOverlay =
		new GlobalShortcut(this, GlobalShortcutType::ToggleOverlay, tr("Toggle Overlay", "Global Shortcut"));
	gsToggleOverlay->setObjectName(QLatin1String("ToggleOverlay"));
	gsToggleOverlay->qsToolTip   = tr("Toggle state of in-game overlay.", "Global Shortcut");
	gsToggleOverlay->qsWhatsThis = tr("This will switch the states of the in-game overlay.", "Global Shortcut");

	connect(gsToggleOverlay, SIGNAL(down(QVariant)), Global::get().o, SLOT(toggleShow()));
#endif

	gsMinimal =
		new GlobalShortcut(this, GlobalShortcutType::ToggleMinimalView, tr("Toggle Minimal", "Global Shortcut"));
	gsMinimal->setObjectName(QLatin1String("ToggleMinimal"));

	gsVolumeUp = new GlobalShortcut(this, GlobalShortcutType::VolumeUp, tr("Volume Up (+10%)", "Global Shortcut"));
	gsVolumeUp->setObjectName(QLatin1String("VolumeUp"));

	gsVolumeDown =
		new GlobalShortcut(this, GlobalShortcutType::VolumeDown, tr("Volume Down (-10%)", "Global Shortcut"));
	gsVolumeDown->setObjectName(QLatin1String("VolumeDown"));

	gsWhisper = new GlobalShortcut(this, GlobalShortcutType::Whisper_Shout, tr("Whisper/Shout"),
								   QVariant::fromValue(ShortcutTarget()));
	gsWhisper->setObjectName(QLatin1String("gsWhisper"));

	gsLinkChannel = new GlobalShortcut(this, GlobalShortcutType::LinkChannel, tr("Link Channel", "Global Shortcut"));
	gsLinkChannel->setObjectName(QLatin1String("MetaLink"));
	gsLinkChannel->qsToolTip = tr("Use in conjunction with Whisper to.", "Global Shortcut");

	gsCycleTransmitMode =
		new GlobalShortcut(this, GlobalShortcutType::CycleTransmitMode, tr("Cycle Transmit Mode", "Global Shortcut"));
	gsCycleTransmitMode->setObjectName(QLatin1String("gsCycleTransmitMode"));

	gsToggleMainWindowVisibility = new GlobalShortcut(this, GlobalShortcutType::ToggleMainWindowVisibility,
													  tr("Hide/show main window", "Global Shortcut"));
	gsToggleMainWindowVisibility->setObjectName(QLatin1String("gsToggleMainWindowVisibility"));

	gsTransmitModePushToTalk = new GlobalShortcut(this, GlobalShortcutType::UsePushToTalk,
												  tr("Set Transmit Mode to Push-To-Talk", "Global Shortcut"));
	gsTransmitModePushToTalk->setObjectName(QLatin1String("gsTransmitModePushToTalk"));

	gsTransmitModeContinuous = new GlobalShortcut(this, GlobalShortcutType::UseContinous,
												  tr("Set Transmit Mode to Continuous", "Global Shortcut"));
	gsTransmitModeContinuous->setObjectName(QLatin1String("gsTransmitModeContinuous"));

	gsTransmitModeVAD =
		new GlobalShortcut(this, GlobalShortcutType::UseVAD, tr("Set Transmit Mode to VAD", "Global Shortcut"));
	gsTransmitModeVAD->setObjectName(QLatin1String("gsTransmitModeVAD"));

	gsSendTextMessage = new GlobalShortcut(this, GlobalShortcutType::SendTextMessage,
										   tr("Send Text Message", "Global Shortcut"), QVariant(QString()));
	gsSendTextMessage->setObjectName(QLatin1String("gsSendTextMessage"));

	gsSendClipboardTextMessage = new GlobalShortcut(this, GlobalShortcutType::SendTextMessageClipboard,
													tr("Send Clipboard Text Message", "Global Shortcut"));
	gsSendClipboardTextMessage->setObjectName(QLatin1String("gsSendClipboardTextMessage"));
	gsSendClipboardTextMessage->qsWhatsThis =
		tr("This will send your Clipboard content to the channel you are currently in.", "Global Shortcut");

	gsToggleTalkingUI =
		new GlobalShortcut(this, GlobalShortcutType::ToggleTalkingUI, tr("Toggle TalkingUI", "Global shortcut"));
	gsToggleTalkingUI->setObjectName(QLatin1String("gsToggleTalkingUI"));
	gsToggleTalkingUI->qsWhatsThis = tr("Toggles the visibility of the TalkingUI.", "Global Shortcut");

	gsToggleSearch =
		new GlobalShortcut(this, GlobalShortcutType::ToggleSearch, tr("Toggle search dialog", "Global Shortcut"));
	gsToggleSearch->setObjectName(QLatin1String("gsToggleSearch"));
	gsToggleSearch->qsWhatsThis =
		tr("This will open or close the search dialog depending on whether it is currently opened already");

	gsServerConnect =
		new GlobalShortcut(this, GlobalShortcutType::ServerConnect, tr("Connect to a server", "Global Shortcut"));
	gsServerConnect->setObjectName(QLatin1String("gsServerConnect"));
	gsServerConnect->qsWhatsThis = tr("This will open the server connection dialog", "Global Shortcut");

	gsServerDisconnect =
		new GlobalShortcut(this, GlobalShortcutType::ServerDisconnect, tr("Disconnect from server", "Global Shortcut"));
	gsServerDisconnect->setObjectName(QLatin1String("gsServerDisconnect"));
	gsServerDisconnect->qsWhatsThis = tr("This will disconnect you from the server", "Global Shortcut");

	gsServerInformation = new GlobalShortcut(this, GlobalShortcutType::ServerInformation,
											 tr("Open server information", "Global Shortcut"));
	gsServerInformation->setObjectName(QLatin1String("gsServerInformation"));
	gsServerInformation->qsWhatsThis = tr("This will show information about the server connection", "Global Shortcut");

	gsServerTokens =
		new GlobalShortcut(this, GlobalShortcutType::ServerTokens, tr("Open server tokens", "Global Shortcut"));
	gsServerTokens->setObjectName(QLatin1String("gsServerTokens"));
	gsServerTokens->qsWhatsThis = tr("This will open the server tokens dialog", "Global Shortcut");

	gsServerUserList =
		new GlobalShortcut(this, GlobalShortcutType::ServerUserList, tr("Open server user list", "Global Shortcut"));
	gsServerUserList->setObjectName(QLatin1String("gsServerUserList"));
	gsServerUserList->qsWhatsThis = tr("This will open the server user list dialog", "Global Shortcut");

	gsServerBanList =
		new GlobalShortcut(this, GlobalShortcutType::ServerBanList, tr("Open server ban list", "Global Shortcut"));
	gsServerBanList->setObjectName(QLatin1String("gsServerBanList"));
	gsServerBanList->qsWhatsThis = tr("This will open the server ban list dialog", "Global Shortcut");

	gsSelfPrioritySpeaker = new GlobalShortcut(this, GlobalShortcutType::SelfPrioritySpeaker,
											   tr("Toggle priority speaker", "Global Shortcut"));
	gsSelfPrioritySpeaker->setObjectName(QLatin1String("gsSelfPrioritySpeaker"));
	gsSelfPrioritySpeaker->qsWhatsThis = tr("This will enable/disable the priority speaker", "Global Shortcut");

	gsRecording =
		new GlobalShortcut(this, GlobalShortcutType::Recording, tr("Open recording dialog", "Global Shortcut"));
	gsRecording->setObjectName(QLatin1String("gsRecording"));
	gsRecording->qsWhatsThis = tr("This will open the recording dialog");

	gsSelfComment = new GlobalShortcut(this, GlobalShortcutType::SelfComment, tr("Change comment", "Global Shortcut"));
	gsSelfComment->setObjectName(QLatin1String("gsSelfComment"));
	gsSelfComment->qsWhatsThis = tr("This will open the change comment dialog");

	gsServerTexture =
		new GlobalShortcut(this, GlobalShortcutType::ServerTexture, tr("Change avatar", "Global Shortcut"));
	gsServerTexture->setObjectName(QLatin1String("gsServerTexture"));
	gsServerTexture->qsWhatsThis = tr("This will open your file explorer to change your avatar image on this server");

	gsServerTextureRemove =
		new GlobalShortcut(this, GlobalShortcutType::ServerTextureRemove, tr("Remove avatar", "Global Shortcut"));
	gsServerTextureRemove->setObjectName(QLatin1String("gsServerTextureRemove"));
	gsServerTextureRemove->qsWhatsThis = tr("This will reset your avatar on the server");

	gsSelfRegister =
		new GlobalShortcut(this, GlobalShortcutType::SelfRegister, tr("Register on the server", "Global Shortcut"));
	gsSelfRegister->setObjectName(QLatin1String("gsSelfRegister"));
	gsSelfRegister->qsWhatsThis = tr("This will register you on the server");

	gsAudioStats = new GlobalShortcut(this, GlobalShortcutType::AudioStats, tr("Audio statistics", "Global Shortcut"));
	gsAudioStats->setObjectName(QLatin1String("gsAudioStats"));
	gsAudioStats->qsWhatsThis = tr("This will open the audio statistics dialog");

	gsConfigDialog = new GlobalShortcut(this, GlobalShortcutType::ConfigDialog, tr("Open settings", "Global Shortcut"));
	gsConfigDialog->setObjectName(QLatin1String("gsConfigDialog"));
	gsConfigDialog->qsWhatsThis = tr("This will open the settings dialog");

	gsAudioWizard =
		new GlobalShortcut(this, GlobalShortcutType::AudioWizard, tr("Start audio wizard", "Global Shortcut"));
	gsAudioWizard->setObjectName(QLatin1String("gsAudioWizard"));
	gsAudioWizard->qsWhatsThis = tr("This will open the audio wizard dialog");

	gsConfigCert =
		new GlobalShortcut(this, GlobalShortcutType::ConfigCert, tr("Start certificate wizard", "Global Shortcut"));
	gsConfigCert->setObjectName(QLatin1String("gsConfigCert"));
	gsConfigCert->qsWhatsThis = tr("This will open the certificate wizard dialog");

	gsAudioTTS = new GlobalShortcut(this, GlobalShortcutType::AudioTTS, tr("Toggle text to speech", "Global Shortcut"));
	gsAudioTTS->setObjectName(QLatin1String("gsAudioTTS"));
	gsAudioTTS->qsWhatsThis = tr("This will enable/disable the text to speech");

	gsHelpAbout = new GlobalShortcut(this, GlobalShortcutType::HelpAbout, tr("Open about dialog", "Global Shortcut"));
	gsHelpAbout->setObjectName(QLatin1String("gsHelpAbout"));
	gsHelpAbout->qsWhatsThis = tr("This will open the about dialog");

	gsHelpAboutQt =
		new GlobalShortcut(this, GlobalShortcutType::HelpAboutQt, tr("Open about Qt dialog", "Global Shortcut"));
	gsHelpAboutQt->setObjectName(QLatin1String("gsHelpAboutQt"));
	gsHelpAboutQt->qsWhatsThis = tr("This will open the about Qt dialog");

	gsHelpVersionCheck =
		new GlobalShortcut(this, GlobalShortcutType::HelpVersionCheck, tr("Check for update", "Global Shortcut"));
	gsHelpVersionCheck->setObjectName(QLatin1String("gsHelpVersionCheck"));
	gsHelpVersionCheck->qsWhatsThis = tr("This will check if mumble is up to date");

	gsTogglePositionalAudio = new GlobalShortcut(this, GlobalShortcutType::TogglePositionalAudio,
												 tr("Toggle positional audio", "Global Shortcut"));
	gsTogglePositionalAudio->setObjectName("gsTogglePositionalAudio");
	gsTogglePositionalAudio->qsWhatsThis = tr("This will toggle positional audio on/off");

	gsMoveBack = new GlobalShortcut(this, GlobalShortcutType::MoveBack, tr("Move back", "Global shortcut"));
	gsMoveBack->setObjectName("gsMoveBack");
	gsMoveBack->qsWhatsThis = tr("This will move you back into your previous channel");

	gsCycleListenerAttenuationMode = new GlobalShortcut(this, GlobalShortcutType::CycleListenerAttenuationMode,
														tr("Cycle listener attenuation mode", "Global shortcut"));
	gsCycleListenerAttenuationMode->setObjectName("gsCycleListenerAttenuationMode");
	gsCycleListenerAttenuationMode->qsWhatsThis =
		tr("This will cycle through the different attenuation modes for channel listeners");

	gsListenerAttenuationUp = new GlobalShortcut(this, GlobalShortcutType::ListenerAttenuationUp,
												 tr("Listener attenuation up (+10%)", "Global shortcut"));
	gsListenerAttenuationUp->setObjectName("gsListenerAttenuationUp");
	gsListenerAttenuationUp->qsWhatsThis =
		tr("This increases the attenuation of channel listeners by 10 percents points");

	gsListenerAttenuationDown = new GlobalShortcut(this, GlobalShortcutType::ListenerAttenuationDown,
												   tr("Listener attenuation down (-10%)", "Global shortcut"));
	gsListenerAttenuationDown->setObjectName("gsListenerAttenuationDown");
	gsListenerAttenuationDown->qsWhatsThis =
		tr("This decreases the attenuation of channel listeners by 10 percents points");

	gsAdaptivePush = new GlobalShortcut(this, GlobalShortcutType::AdaptivePush, tr("Adaptive Push", "Global Shortcut"));
	gsAdaptivePush->setObjectName("gsAdaptivePush");
	gsAdaptivePush->qsToolTip = tr("When using the push-to-talk transmission mode, this will act as the push-to-talk "
								   "action. Otherwise, it will act as a push-to-mute action.",
								   "Global Shortcut");
}

void MainWindow::setupGui() {
	updateWindowTitle();
	setupServerNavigator();
	setCentralWidget(m_serverNavigatorContainer);
	setAcceptDrops(true);

#ifdef Q_OS_MAC
	QMenu *qmWindow = new QMenu(tr("&Window"), this);
	menubar->insertMenu(qmHelp->menuAction(), qmWindow);
#	if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
	qmWindow->addAction(tr("Minimize"), QKeySequence(tr("Ctrl+M")), this, &MainWindow::showMinimized);
#	else
	qmWindow->addAction(tr("Minimize"), this, SLOT(showMinimized()), QKeySequence(tr("Ctrl+M")));
#	endif

	qtvUsers->setAttribute(Qt::WA_MacShowFocusRect, false);
	qteChat->setAttribute(Qt::WA_MacShowFocusRect, false);
	qteChat->setFrameShape(QFrame::NoFrame);
	qteLog->setFrameStyle(QFrame::NoFrame);
#endif

	qteLog->setFrameShape(QFrame::NoFrame);
	qteLog->setFrameStyle(QFrame::NoFrame);
	qteLog->setLineWidth(0);
	qteLog->setMidLineWidth(0);
	qteLog->setAttribute(Qt::WA_StyledBackground, true);
	qteLog->resetViewportChrome();
	if (QWidget *logViewport = qteLog->viewport()) {
		logViewport->setAttribute(Qt::WA_StyledBackground, true);
	}
	qteLog->setParent(nullptr);
	QWidget *logSurface = new QWidget(qdwLog);
	logSurface->setObjectName(QLatin1String("qwLogSurface"));
	logSurface->setAttribute(Qt::WA_StyledBackground, true);
	QVBoxLayout *logLayout = new QVBoxLayout(logSurface);
	logLayout->setContentsMargins(8, 8, 0, 4);
	logLayout->setSpacing(0);
	logLayout->addWidget(qteLog);
	qdwLog->setWidget(logSurface);
	if (QLayout *dockLayout = qdwLog->layout()) {
		dockLayout->setContentsMargins(0, 0, 0, 0);
		dockLayout->setSpacing(0);
	}
	LogDocument *ld = new LogDocument(qteLog);
	qteLog->setDocument(ld);
	qteLog->document()->setDocumentMargin(0);
	if (QTextFrame *rootFrame = qteLog->document()->rootFrame()) {
		QTextFrameFormat rootFrameFormat = rootFrame->frameFormat();
		rootFrameFormat.setBorder(0);
		rootFrameFormat.setMargin(0);
		rootFrameFormat.setPadding(0);
		rootFrame->setFrameFormat(rootFrameFormat);
	}
	connect(qteLog, &LogTextBrowser::imageActivated, this, [this](const QTextCursor &cursor) {
		openImageDialog(qteLog, cursor);
	});

	qteLog->document()->setMaximumBlockCount(Global::get().s.iMaxLogBlocks);
	connect(qteLog->document(), &QTextDocument::contentsChanged, this, [this]() {
		if (const PersistentChatTarget target = currentPersistentChatTarget();
			target.serverLog || target.legacyTextPath) {
			renderServerLogView(true);
		}
	});

	pmModel = new UserModel(qtvUsers);
	qtvUsers->setModel(pmModel);
	qtvUsers->setRowHidden(0, QModelIndex(), true);
	qtvUsers->ensurePolished();
	updateServerNavigatorChrome();

	QObject::connect(this, &MainWindow::userAddedChannelListener, pmModel, &UserModel::addChannelListener);
	QObject::connect(
		this, &MainWindow::userRemovedChannelListener, pmModel,
		static_cast< void (UserModel::*)(const ClientUser *, const Channel *) >(&UserModel::removeChannelListener));
	QObject::connect(Global::get().channelListenerManager.get(), &ChannelListenerManager::localVolumeAdjustmentsChanged,
					 pmModel, &UserModel::on_channelListenerLocalVolumeAdjustmentChanged);
	QObject::connect(pmModel, &UserModel::userMoved, this, &MainWindow::handleUserMoved);

	// connect slots to PluginManager
	QObject::connect(pmModel, &UserModel::userAdded, Global::get().pluginManager, &PluginManager::on_userAdded);
	QObject::connect(pmModel, &UserModel::userRemoved, Global::get().pluginManager, &PluginManager::on_userRemoved);
	QObject::connect(pmModel, &UserModel::channelAdded, Global::get().pluginManager, &PluginManager::on_channelAdded);
	QObject::connect(pmModel, &UserModel::channelRemoved, Global::get().pluginManager,
					 &PluginManager::on_channelRemoved);
	QObject::connect(pmModel, &UserModel::channelRenamed, Global::get().pluginManager,
					 &PluginManager::on_channelRenamed);

	qaAudioMute->setChecked(Global::get().s.bMute);
	qaAudioDeaf->setChecked(Global::get().s.bDeaf);

	updateAudioToolTips();

#ifdef USE_NO_TTS
	qaAudioTTS->setChecked(false);
	qaAudioTTS->setDisabled(true);
#else
	qaAudioTTS->setChecked(Global::get().s.bTTS);
#endif
	qaFilterToggle->setChecked(Global::get().s.bFilterActive);
	on_qaFilterToggle_triggered();

	qaHelpWhatsThis->setShortcuts(QKeySequence::WhatsThis);

	qaConfigMinimal->setChecked(Global::get().s.bMinimalView);
	qaConfigHideFrame->setChecked(Global::get().s.bHideFrame);

	connect(gsResetAudio, SIGNAL(down(QVariant)), qaAudioReset, SLOT(trigger()));
	connect(gsUnlink, SIGNAL(down(QVariant)), qaAudioUnlink, SLOT(trigger()));
	connect(gsMinimal, SIGNAL(down(QVariant)), qaConfigMinimal, SLOT(trigger()));

	dtbLogDockTitle = new DockTitleBar();
	qdwLog->setTitleBarWidget(dtbLogDockTitle);
	dtbLogDockTitle->setMinimumHeight(0);
	dtbLogDockTitle->setMaximumHeight(0);

	for (QWidget *w : qdwLog->findChildren< QWidget * >()) {
		w->installEventFilter(dtbLogDockTitle);
		w->setMouseTracking(true);
	}

	dtbChatDockTitle = new DockTitleBar();
	qdwChat->setTitleBarWidget(dtbChatDockTitle);
	dtbChatDockTitle->setMinimumHeight(0);
	dtbChatDockTitle->setMaximumHeight(0);
	qdwChat->installEventFilter(dtbChatDockTitle);
	setupPersistentChatDock();
	refreshTextDocumentStylesheets();
	qteChat->setDefaultText(tr("<center>Not connected</center>"), true);
	qteChat->setEnabled(false);

	QWidget *dummyTitlebar = new QWidget(qdwMinimalViewNote);
	qdwMinimalViewNote->setTitleBarWidget(dummyTitlebar);

	setShowDockTitleBars((Global::get().s.wlWindowLayout == Settings::LayoutCustom) && !Global::get().s.bLockLayout);

#ifdef Q_OS_MAC
	// Workaround for QTBUG-3116 -- using a unified toolbar on Mac OS X
	// and using restoreGeometry before the window has updated its frameStrut
	// causes the MainWindow to jump around on screen on launch.  Workaround
	// is to call show() to update the frameStrut and set the windowOpacity to
	// 0 to hide any graphical glitches that occur when we add stuff to the
	// window.
	setWindowOpacity(0.0f);
	show();
#endif

	connect(qtvUsers->selectionModel(), SIGNAL(currentChanged(const QModelIndex &, const QModelIndex &)),
			SLOT(qtvUserCurrentChanged(const QModelIndex &, const QModelIndex &)));

	// QtCreator and uic.exe do not allow adding arbitrary widgets
	// such as a MUComboBox to a QToolbar, even though they are supported.
	qcbTransmitMode = new MUComboBox(qtIconToolbar);
	qcbTransmitMode->setObjectName(QLatin1String("qcbTransmitMode"));
	qcbTransmitMode->addItem(tr("Continuous"));
	qcbTransmitMode->addItem(tr("Voice Activity"));
	qcbTransmitMode->addItem(tr("Push-to-Talk"));

	qaTransmitModeSeparator = qtIconToolbar->insertSeparator(qaConfigDialog);
	qaTransmitMode          = qtIconToolbar->insertWidget(qaTransmitModeSeparator, qcbTransmitMode);

	connect(qcbTransmitMode, SIGNAL(activated(int)), this, SLOT(qcbTransmitMode_activated(int)));

	updateTransmitModeComboBox(Global::get().s.atTransmit);

#ifdef Q_OS_WIN
	setupView(false);
#endif

	loadState(Global::get().s.bMinimalView);

	setupView(false);

#ifdef Q_OS_MAC
	setWindowOpacity(1.0f);
#endif
}

void MainWindow::setupServerNavigator() {
	m_serverNavigatorContainer = new QWidget(this);
	m_serverNavigatorContainer->setObjectName(QLatin1String("qwServerNavigator"));
	m_serverNavigatorContainer->setAttribute(Qt::WA_StyledBackground, true);
	m_serverNavigatorContainer->setMinimumWidth(196);

	QVBoxLayout *layout = new QVBoxLayout(m_serverNavigatorContainer);
	layout->setContentsMargins(0, 5, 4, 4);
	layout->setSpacing(5);

	m_serverNavigatorContentFrame = new QFrame(m_serverNavigatorContainer);
	m_serverNavigatorContentFrame->setObjectName(QLatin1String("qfServerNavigatorContent"));
	m_serverNavigatorContentFrame->setAttribute(Qt::WA_StyledBackground, true);
	QVBoxLayout *contentLayout = new QVBoxLayout(m_serverNavigatorContentFrame);
	contentLayout->setContentsMargins(7, 7, 7, 7);
	contentLayout->setSpacing(5);

	m_serverNavigatorHeaderFrame = new QFrame(m_serverNavigatorContentFrame);
	m_serverNavigatorHeaderFrame->setObjectName(QLatin1String("qfServerNavigatorHeader"));

	QVBoxLayout *headerLayout = new QVBoxLayout(m_serverNavigatorHeaderFrame);
	headerLayout->setContentsMargins(0, 0, 0, 1);
	headerLayout->setSpacing(2);

	m_serverNavigatorEyebrow = new QLabel(tr("Server"), m_serverNavigatorHeaderFrame);
	m_serverNavigatorEyebrow->setObjectName(QLatin1String("qlServerNavigatorEyebrow"));
	QFont eyebrowFont = m_serverNavigatorEyebrow->font();
	eyebrowFont.setCapitalization(QFont::AllUppercase);
	eyebrowFont.setBold(true);
	eyebrowFont.setPointSizeF(std::max(eyebrowFont.pointSizeF() - 1.0, 8.0));
	m_serverNavigatorEyebrow->setFont(eyebrowFont);

	m_serverNavigatorTitle = new QLabel(tr("Not connected"), m_serverNavigatorHeaderFrame);
	m_serverNavigatorTitle->setObjectName(QLatin1String("qlServerNavigatorTitle"));
	QFont titleFont = m_serverNavigatorTitle->font();
	titleFont.setBold(true);
	titleFont.setPointSizeF(titleFont.pointSizeF() + 0.5);
	m_serverNavigatorTitle->setFont(titleFont);
	m_serverNavigatorTitle->setTextFormat(Qt::PlainText);
	m_serverNavigatorTitle->setWordWrap(true);

	m_serverNavigatorSubtitle = new QLabel(tr("Browse channels and people here."), m_serverNavigatorHeaderFrame);
	m_serverNavigatorSubtitle->setObjectName(QLatin1String("qlServerNavigatorSubtitle"));
	m_serverNavigatorSubtitle->setWordWrap(true);
	m_serverNavigatorSubtitle->setTextFormat(Qt::PlainText);
	m_serverNavigatorSubtitle->setTextInteractionFlags(Qt::NoTextInteraction);

	headerLayout->addWidget(m_serverNavigatorEyebrow);
	headerLayout->addWidget(m_serverNavigatorTitle);
	headerLayout->addWidget(m_serverNavigatorSubtitle);
	m_serverNavigatorHeaderFrame->hide();

	m_serverNavigatorTextChannelsMotdFrame = new QFrame(m_serverNavigatorContentFrame);
	m_serverNavigatorTextChannelsMotdFrame->setObjectName(QLatin1String("qfServerNavigatorTextChannelsMotd"));
	m_serverNavigatorTextChannelsMotdFrame->setAttribute(Qt::WA_StyledBackground, true);
	m_serverNavigatorTextChannelsMotdFrame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
	QVBoxLayout *textChannelsMotdLayout = new QVBoxLayout(m_serverNavigatorTextChannelsMotdFrame);
	textChannelsMotdLayout->setContentsMargins(7, 6, 7, 6);
	textChannelsMotdLayout->setSpacing(3);

	QHBoxLayout *textChannelsMotdHeaderLayout = new QHBoxLayout();
	textChannelsMotdHeaderLayout->setContentsMargins(0, 0, 0, 0);
	textChannelsMotdHeaderLayout->setSpacing(3);

	m_serverNavigatorTextChannelsMotdTitle = new QLabel(tr("MOTD"), m_serverNavigatorTextChannelsMotdFrame);
	m_serverNavigatorTextChannelsMotdTitle->setObjectName(QLatin1String("qlServerNavigatorTextChannelsMotdTitle"));
	m_serverNavigatorTextChannelsMotdTitle->setTextFormat(Qt::PlainText);

	m_serverNavigatorTextChannelsMotdToggleButton = new QToolButton(m_serverNavigatorTextChannelsMotdFrame);
	m_serverNavigatorTextChannelsMotdToggleButton->setObjectName(
		QLatin1String("qtbServerNavigatorTextChannelsMotdToggle"));
	m_serverNavigatorTextChannelsMotdToggleButton->setAutoRaise(true);
	m_serverNavigatorTextChannelsMotdToggleButton->setText(tr("Hide"));

	textChannelsMotdHeaderLayout->addWidget(m_serverNavigatorTextChannelsMotdTitle);
	textChannelsMotdHeaderLayout->addStretch(1);
	textChannelsMotdHeaderLayout->addWidget(m_serverNavigatorTextChannelsMotdToggleButton);
	textChannelsMotdLayout->addLayout(textChannelsMotdHeaderLayout);

	m_serverNavigatorTextChannelsMotdBody = new QLabel(m_serverNavigatorTextChannelsMotdFrame);
	m_serverNavigatorTextChannelsMotdBody->setObjectName(QLatin1String("qlServerNavigatorTextChannelsMotdBody"));
	m_serverNavigatorTextChannelsMotdBody->setAttribute(Qt::WA_StyledBackground, true);
	m_serverNavigatorTextChannelsMotdBody->setTextFormat(Qt::RichText);
	m_serverNavigatorTextChannelsMotdBody->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	m_serverNavigatorTextChannelsMotdBody->setWordWrap(true);
	m_serverNavigatorTextChannelsMotdBody->setTextInteractionFlags(Qt::TextBrowserInteraction);
	m_serverNavigatorTextChannelsMotdBody->setOpenExternalLinks(false);
	m_serverNavigatorTextChannelsMotdBody->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
	textChannelsMotdLayout->addWidget(m_serverNavigatorTextChannelsMotdBody);
	m_serverNavigatorTextChannelsMotdFrame->hide();
	contentLayout->addWidget(m_serverNavigatorTextChannelsMotdFrame);
	contentLayout->addWidget(m_serverNavigatorHeaderFrame);

	m_serverNavigatorVoiceSectionEyebrow = new QLabel(tr("Rooms"), m_serverNavigatorContentFrame);
	m_serverNavigatorVoiceSectionEyebrow->setObjectName(QLatin1String("qlServerNavigatorVoiceSectionEyebrow"));
	QFont voiceEyebrowFont = m_serverNavigatorVoiceSectionEyebrow->font();
	voiceEyebrowFont.setCapitalization(QFont::AllUppercase);
	voiceEyebrowFont.setBold(true);
	voiceEyebrowFont.setPointSizeF(std::max(voiceEyebrowFont.pointSizeF() - 1.0, 8.0));
	m_serverNavigatorVoiceSectionEyebrow->setFont(voiceEyebrowFont);
	contentLayout->addWidget(m_serverNavigatorVoiceSectionEyebrow);

	m_serverNavigatorVoiceSectionSubtitle = new QLabel(tr("Rooms and people"), m_serverNavigatorContentFrame);
	m_serverNavigatorVoiceSectionSubtitle->setObjectName(QLatin1String("qlServerNavigatorVoiceSectionSubtitle"));
	m_serverNavigatorVoiceSectionSubtitle->setWordWrap(true);
	m_serverNavigatorVoiceSectionSubtitle->setTextFormat(Qt::PlainText);
	m_serverNavigatorVoiceSectionSubtitle->setTextInteractionFlags(Qt::NoTextInteraction);
	m_serverNavigatorVoiceSectionSubtitle->hide();
	contentLayout->addWidget(m_serverNavigatorVoiceSectionSubtitle);

	qtvUsers->setParent(m_serverNavigatorContentFrame);
	qtvUsers->setObjectName(QLatin1String("qtvUsers"));
	qtvUsers->setFrameShape(QFrame::NoFrame);
	qtvUsers->setUniformRowHeights(true);
	qtvUsers->setAnimated(true);
	qtvUsers->setIndentation(8);
	qtvUsers->setExpandsOnDoubleClick(false);
	qtvUsers->setAllColumnsShowFocus(false);
	qtvUsers->setMouseTracking(true);
	qtvUsers->setMinimumWidth(172);
	qtvUsers->setAttribute(Qt::WA_StyledBackground, true);
	qtvUsers->viewport()->setObjectName(QLatin1String("qtvUsersViewport"));
	qtvUsers->viewport()->setAttribute(Qt::WA_StyledBackground, true);
	qtvUsers->viewport()->setMouseTracking(true);
	qtvUsers->setStyleSheet(QString());
	qtvUsers->viewport()->setStyleSheet(QString());
	contentLayout->addWidget(qtvUsers, 1);

	m_serverNavigatorTextChannelsDivider = new QFrame(m_serverNavigatorContentFrame);
	m_serverNavigatorTextChannelsDivider->setObjectName(QLatin1String("qfServerNavigatorTextChannelsDivider"));
	m_serverNavigatorTextChannelsDivider->setFrameShape(QFrame::HLine);
	m_serverNavigatorTextChannelsDivider->setFrameShadow(QFrame::Plain);
	m_serverNavigatorTextChannelsDivider->hide();
	contentLayout->addWidget(m_serverNavigatorTextChannelsDivider);

	m_serverNavigatorTextChannelsFrame = new QFrame(m_serverNavigatorContentFrame);
	m_serverNavigatorTextChannelsFrame->setObjectName(QLatin1String("qfServerNavigatorTextChannels"));
	m_serverNavigatorTextChannelsFrame->setAttribute(Qt::WA_StyledBackground, true);
	m_serverNavigatorTextChannelsFrame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
	QVBoxLayout *textChannelsLayout = new QVBoxLayout(m_serverNavigatorTextChannelsFrame);
	textChannelsLayout->setContentsMargins(0, 0, 0, 0);
	textChannelsLayout->setSpacing(2);

	m_serverNavigatorTextChannelsEyebrow = new QLabel(tr("Text"), m_serverNavigatorTextChannelsFrame);
	m_serverNavigatorTextChannelsEyebrow->setObjectName(QLatin1String("qlServerNavigatorTextChannelsEyebrow"));
	QFont textChannelsEyebrowFont = m_serverNavigatorTextChannelsEyebrow->font();
	textChannelsEyebrowFont.setCapitalization(QFont::AllUppercase);
	textChannelsEyebrowFont.setBold(true);
	textChannelsEyebrowFont.setPointSizeF(std::max(textChannelsEyebrowFont.pointSizeF() - 1.0, 8.0));
	m_serverNavigatorTextChannelsEyebrow->setFont(textChannelsEyebrowFont);

	m_serverNavigatorTextChannelsTitle = new QLabel(tr("Conversations"), m_serverNavigatorTextChannelsFrame);
	m_serverNavigatorTextChannelsTitle->setObjectName(QLatin1String("qlServerNavigatorTextChannelsTitle"));
	QFont textChannelsTitleFont = m_serverNavigatorTextChannelsTitle->font();
	textChannelsTitleFont.setBold(true);
	textChannelsTitleFont.setPointSizeF(textChannelsTitleFont.pointSizeF() + 0.5);
	m_serverNavigatorTextChannelsTitle->setFont(textChannelsTitleFont);
	m_serverNavigatorTextChannelsTitle->setTextFormat(Qt::PlainText);
	m_serverNavigatorTextChannelsTitle->setWordWrap(true);

	m_serverNavigatorTextChannelsSubtitle =
		new QLabel(tr("Lobby, rooms, and shared activity."), m_serverNavigatorTextChannelsFrame);
	m_serverNavigatorTextChannelsSubtitle->setObjectName(QLatin1String("qlServerNavigatorTextChannelsSubtitle"));
	m_serverNavigatorTextChannelsSubtitle->setWordWrap(true);
	m_serverNavigatorTextChannelsSubtitle->setTextFormat(Qt::PlainText);
	m_serverNavigatorTextChannelsSubtitle->setTextInteractionFlags(Qt::NoTextInteraction);
	m_serverNavigatorTextChannelsSubtitle->hide();

	m_persistentChatChannelList = new QListWidget(m_serverNavigatorTextChannelsFrame);
	m_persistentChatChannelList->setObjectName(QLatin1String("qlwPersistentTextChannels"));
	m_persistentChatChannelList->setAccessibleName(tr("Conversations"));
	m_persistentChatChannelList->setFrameShape(QFrame::NoFrame);
	m_persistentChatChannelList->setAlternatingRowColors(false);
	m_persistentChatChannelList->setUniformItemSizes(true);
	m_persistentChatChannelList->setSpacing(0);
	m_persistentChatChannelList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_persistentChatChannelList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_persistentChatChannelList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	m_persistentChatChannelList->setSelectionMode(QAbstractItemView::SingleSelection);
	m_persistentChatChannelList->setContextMenuPolicy(Qt::CustomContextMenu);
	m_persistentChatChannelList->setMouseTracking(true);
	m_persistentChatChannelList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
	m_persistentChatChannelList->setMinimumHeight(0);
	m_persistentChatChannelList->setMaximumHeight(QWIDGETSIZE_MAX);
	m_persistentChatChannelList->setItemDelegate(new PersistentChatScopeListDelegate(m_persistentChatChannelList));

	textChannelsLayout->addWidget(m_serverNavigatorTextChannelsEyebrow);
	textChannelsLayout->addWidget(m_serverNavigatorTextChannelsTitle);
	textChannelsLayout->addWidget(m_serverNavigatorTextChannelsSubtitle);
	textChannelsLayout->addWidget(m_persistentChatChannelList);
	m_serverNavigatorTextChannelsFrame->hide();
	contentLayout->addWidget(m_serverNavigatorTextChannelsFrame);

	m_serverNavigatorFooter = new QLabel(tr("Ctrl+F searches the server tree."), m_serverNavigatorContainer);
	m_serverNavigatorFooter->setObjectName(QLatin1String("qlServerNavigatorFooter"));
	m_serverNavigatorFooter->setWordWrap(true);
	m_serverNavigatorFooter->setTextFormat(Qt::PlainText);
	m_serverNavigatorFooter->setTextInteractionFlags(Qt::NoTextInteraction);
	m_serverNavigatorFooter->hide();

	connect(m_serverNavigatorTextChannelsMotdToggleButton, &QToolButton::clicked, this, [this]() {
		m_persistentChatMotdHidden = !m_persistentChatMotdHidden;
		updatePersistentChatChrome(currentPersistentChatTarget());
	});
	connect(m_serverNavigatorTextChannelsMotdBody, &QLabel::linkActivated, this,
			[this](const QString &link) { on_qteLog_anchorClicked(QUrl(link)); });
	connect(qtvUsers, &QTreeView::clicked, this, [this](const QModelIndex &index) {
		if (!index.isValid()) {
			return;
		}

		setPersistentChatTargetUsesVoiceTree(true);
		updateChatBar();
		updateServerNavigatorChrome();
	});
	connect(qtvUsers, &QTreeView::activated, this, [this](const QModelIndex &index) {
		if (!index.isValid()) {
			return;
		}

		setPersistentChatTargetUsesVoiceTree(true);
		updateChatBar();
		updateServerNavigatorChrome();
	});
	m_serverNavigatorContentFrame->installEventFilter(this);
	m_serverNavigatorTextChannelsMotdFrame->installEventFilter(this);
	m_serverNavigatorTextChannelsMotdBody->installEventFilter(this);

	layout->addWidget(m_serverNavigatorContentFrame, 1);
	layout->addWidget(m_serverNavigatorFooter);

	refreshServerNavigatorStyles();
}

void MainWindow::setupPersistentChatDock() {
	qteChat->setParent(nullptr);

	m_persistentChatContainer = new QWidget(qdwChat);
	m_persistentChatContainer->setObjectName(QLatin1String("qwPersistentChat"));
	m_persistentChatContainer->setAttribute(Qt::WA_StyledBackground, true);
	QVBoxLayout *layout       = new QVBoxLayout(m_persistentChatContainer);
	layout->setContentsMargins(2, 4, 0, 2);
	layout->setSpacing(4);

	qdwChat->setWindowTitle(tr("Conversation"));
	qdwChat->setMinimumWidth(360);
	qdwLog->setWindowTitle(tr("Server log"));
	qdwLog->setMinimumWidth(180);

	m_persistentChatHeaderFrame = new QFrame(m_persistentChatContainer);
	m_persistentChatHeaderFrame->setObjectName(QLatin1String("qfPersistentChatHeader"));

	QVBoxLayout *headerLayout = new QVBoxLayout(m_persistentChatHeaderFrame);
	headerLayout->setContentsMargins(8, 4, 8, 4);
	headerLayout->setSpacing(0);

	m_persistentChatHeaderEyebrow = new QLabel(tr("Text"), m_persistentChatHeaderFrame);
	m_persistentChatHeaderEyebrow->setObjectName(QLatin1String("qlPersistentChatHeaderEyebrow"));
	QFont headerEyebrowFont = m_persistentChatHeaderEyebrow->font();
	headerEyebrowFont.setCapitalization(QFont::AllUppercase);
	headerEyebrowFont.setBold(true);
	headerEyebrowFont.setPointSizeF(std::max(headerEyebrowFont.pointSizeF() - 1.0, 8.0));
	m_persistentChatHeaderEyebrow->setFont(headerEyebrowFont);

	m_persistentChatHeaderTitle = new QLabel(tr("Conversation"), m_persistentChatHeaderFrame);
	m_persistentChatHeaderTitle->setObjectName(QLatin1String("qlPersistentChatHeaderTitle"));
	m_persistentChatHeaderTitle->setTextFormat(Qt::PlainText);
	QFont headerTitleFont = m_persistentChatHeaderTitle->font();
	headerTitleFont.setBold(true);
	headerTitleFont.setPointSizeF(headerTitleFont.pointSizeF() + 0.5);
	m_persistentChatHeaderTitle->setFont(headerTitleFont);

	m_persistentChatHeaderContext = new QLabel(m_persistentChatHeaderFrame);
	m_persistentChatHeaderContext->setObjectName(QLatin1String("qlPersistentChatHeaderContext"));
	m_persistentChatHeaderContext->setAttribute(Qt::WA_StyledBackground, true);
	m_persistentChatHeaderContext->setTextFormat(Qt::PlainText);
	m_persistentChatHeaderContext->setTextInteractionFlags(Qt::NoTextInteraction);
	m_persistentChatHeaderContext->hide();

	m_persistentChatHeaderSubtitle =
		new QLabel(tr("Catch up, then reply."),
				   m_persistentChatHeaderFrame);
	m_persistentChatHeaderSubtitle->setObjectName(QLatin1String("qlPersistentChatHeaderSubtitle"));
	m_persistentChatHeaderSubtitle->setTextFormat(Qt::PlainText);
	m_persistentChatHeaderSubtitle->setWordWrap(true);
	m_persistentChatHeaderSubtitle->setTextInteractionFlags(Qt::NoTextInteraction);

	headerLayout->addWidget(m_persistentChatHeaderEyebrow);
	headerLayout->addWidget(m_persistentChatHeaderTitle);
	headerLayout->addWidget(m_persistentChatHeaderContext);
	headerLayout->addWidget(m_persistentChatHeaderSubtitle);
	m_persistentChatHeaderFrame->hide();

	layout->addWidget(m_persistentChatHeaderFrame);

	m_persistentChatHistory = new PersistentChatListWidget(m_persistentChatContainer);
	m_persistentChatHistory->setObjectName(QLatin1String("qtePersistentChatHistory"));
	m_persistentChatHistory->setAccessibleName(tr("Persistent chat history"));
	m_persistentChatHistory->setFrameShape(QFrame::NoFrame);
	m_persistentChatHistory->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_persistentChatHistory->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	m_persistentChatHistory->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
	m_persistentChatHistory->setSelectionMode(QAbstractItemView::NoSelection);
	m_persistentChatHistory->setFocusPolicy(Qt::NoFocus);
	m_persistentChatHistory->setUniformItemSizes(false);
	m_persistentChatHistory->setWrapping(false);
	m_persistentChatHistory->setWordWrap(false);
	m_persistentChatHistory->setResizeMode(QListView::Adjust);
	m_persistentChatHistory->setSpacing(2);
	m_persistentChatHistory->setContextMenuPolicy(Qt::CustomContextMenu);
	m_persistentChatResizeRenderTimer = new QTimer(this);
	m_persistentChatResizeRenderTimer->setSingleShot(true);
	m_persistentChatResizeRenderTimer->setInterval(PERSISTENT_CHAT_RESIZE_RENDER_DELAY_MSEC);
	m_persistentChatScrollIdleTimer = new QTimer(this);
	m_persistentChatScrollIdleTimer->setSingleShot(true);
	m_persistentChatScrollIdleTimer->setInterval(180);
	m_persistentChatReplyFrame = new QFrame(m_persistentChatContainer);
	m_persistentChatReplyFrame->setObjectName(QLatin1String("qfPersistentChatReply"));
	m_persistentChatReplyFrame->setAttribute(Qt::WA_StyledBackground, true);
	QHBoxLayout *replyLayout = new QHBoxLayout(m_persistentChatReplyFrame);
	replyLayout->setContentsMargins(10, 6, 10, 6);
	replyLayout->setSpacing(6);
	QVBoxLayout *replyTextLayout = new QVBoxLayout();
	replyTextLayout->setContentsMargins(0, 0, 0, 0);
	replyTextLayout->setSpacing(1);
	m_persistentChatReplyLabel = new QLabel(m_persistentChatReplyFrame);
	m_persistentChatReplyLabel->setObjectName(QLatin1String("qlPersistentChatReplyLabel"));
	m_persistentChatReplySnippet = new QLabel(m_persistentChatReplyFrame);
	m_persistentChatReplySnippet->setObjectName(QLatin1String("qlPersistentChatReplySnippet"));
	m_persistentChatReplySnippet->setWordWrap(true);
	replyTextLayout->addWidget(m_persistentChatReplyLabel);
	replyTextLayout->addWidget(m_persistentChatReplySnippet);
	replyLayout->addLayout(replyTextLayout, 1);
	m_persistentChatReplyCancelButton = new QToolButton(m_persistentChatReplyFrame);
	m_persistentChatReplyCancelButton->setText(tr("Cancel"));
	replyLayout->addWidget(m_persistentChatReplyCancelButton, 0, Qt::AlignTop);
	m_persistentChatReplyFrame->hide();
	qteChat->setAttribute(Qt::WA_StyledBackground, true);
	if (QWidget *chatViewport = qteChat->viewport()) {
		chatViewport->setAttribute(Qt::WA_StyledBackground, true);
	}
	qteChat->setFrameShape(QFrame::NoFrame);
	qteChat->setFrameStyle(QFrame::NoFrame);
	qteChat->setLineWidth(0);
	qteChat->setMidLineWidth(0);
	qteChat->resetViewportChrome();
	qteChat->setMinimumHeight(34);
	qteChat->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);

	m_persistentChatComposerFrame = new QFrame(m_persistentChatContainer);
	m_persistentChatComposerFrame->setObjectName(QLatin1String("qfPersistentChatComposer"));
	m_persistentChatComposerFrame->setAttribute(Qt::WA_StyledBackground, true);
	QVBoxLayout *composerLayout = new QVBoxLayout(m_persistentChatComposerFrame);
	composerLayout->setContentsMargins(7, 7, 7, 7);
	composerLayout->setSpacing(5);
	composerLayout->addWidget(m_persistentChatReplyFrame);

	QWidget *composerInputRow = new QWidget(m_persistentChatComposerFrame);
	composerInputRow->setObjectName(QLatin1String("qwPersistentChatComposerInputRow"));
	QHBoxLayout *composerInputLayout = new QHBoxLayout(composerInputRow);
	composerInputLayout->setContentsMargins(0, 0, 0, 0);
	composerInputLayout->setSpacing(4);
	composerInputLayout->addWidget(qteChat, 1);

	m_persistentChatSendButton = new QToolButton(composerInputRow);
	m_persistentChatSendButton->setObjectName(QLatin1String("qtbPersistentChatSend"));
	m_persistentChatSendButton->setAutoRaise(false);
	m_persistentChatSendButton->setToolTip(tr("Send message"));
	m_persistentChatSendButton->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
	m_persistentChatSendButton->setIconSize(QSize(16, 16));
	m_persistentChatSendButton->setCursor(Qt::PointingHandCursor);
	composerInputLayout->addWidget(m_persistentChatSendButton, 0, Qt::AlignBottom);
	composerLayout->addWidget(composerInputRow);

	m_persistentChatChannelMenu = new QMenu(tr("Text rooms"), this);
	m_persistentChatAddRoomAction = m_persistentChatChannelMenu->addAction(tr("Create text room"));
	m_persistentChatEditRoomAction = m_persistentChatChannelMenu->addAction(tr("Edit text room"));
	m_persistentChatRemoveRoomAction = m_persistentChatChannelMenu->addAction(tr("Delete text room"));
	m_persistentChatChannelMenu->addSeparator();
	m_persistentChatAclRoomAction = m_persistentChatChannelMenu->addAction(tr("Edit ACL source for text room"));

	QWidget *chatPanel = new QWidget(m_persistentChatContainer);
	chatPanel->setObjectName(QLatin1String("qwPersistentChatPanel"));
	chatPanel->setAttribute(Qt::WA_StyledBackground, true);
	QVBoxLayout *chatLayout = new QVBoxLayout();
	chatLayout->setContentsMargins(0, 0, 0, 0);
	chatLayout->setSpacing(0);
	chatLayout->addWidget(m_persistentChatHistory, 1);
	chatLayout->addWidget(m_persistentChatComposerFrame);
	chatPanel->setLayout(chatLayout);

	QWidget *contentSurface = new QWidget(m_persistentChatContainer);
	contentSurface->setObjectName(QLatin1String("qwPersistentChatSurface"));
	contentSurface->setAttribute(Qt::WA_StyledBackground, true);
	QVBoxLayout *contentSurfaceLayout = new QVBoxLayout(contentSurface);
	contentSurfaceLayout->setContentsMargins(0, 0, 0, 0);
	contentSurfaceLayout->setSpacing(0);
	contentSurfaceLayout->addWidget(chatPanel, 1);

	layout->addWidget(contentSurface, 1);

	qdwChat->setWidget(m_persistentChatContainer);
	if (QLayout *dockLayout = qdwChat->layout()) {
		dockLayout->setContentsMargins(0, 0, 0, 0);
		dockLayout->setSpacing(0);
	}

	refreshPersistentChatStyles();

	auto schedulePersistentChatScopeRefresh = [this](bool forceReload) {
		QPointer< MainWindow > guardedThis(this);
		QMetaObject::invokeMethod(
			this,
			[guardedThis, forceReload]() {
				if (!guardedThis) {
					return;
				}

				guardedThis->updatePersistentTextChannelControls();
				guardedThis->updateChatBar(forceReload);
			},
			Qt::QueuedConnection);
	};

	auto activatePersistentChatChannelItem = [this, schedulePersistentChatScopeRefresh](QListWidgetItem *item,
																						  bool forceReload) {
		if (!item || !m_persistentChatChannelList) {
			return;
		}

		if (m_persistentChatChannelList->currentItem() != item) {
			m_persistentChatChannelList->setCurrentItem(item);
		}

		schedulePersistentChatScopeRefresh(forceReload);
	};

	connect(m_persistentChatChannelList, SIGNAL(currentRowChanged(int)), this, SLOT(on_persistentChatScopeChanged(int)));
	connect(m_persistentChatChannelList, &QListWidget::itemSelectionChanged, this, [this]() {
		if (!m_persistentChatChannelList) {
			return;
		}

		const QList< QListWidgetItem * > selectedItems = m_persistentChatChannelList->selectedItems();
		if (!selectedItems.isEmpty()) {
			setPersistentChatTargetUsesVoiceTree(false);
		}
		if (selectedItems.size() == 1 && m_persistentChatChannelList->currentItem() != selectedItems.front()) {
			m_persistentChatChannelList->setCurrentItem(selectedItems.front());
		}
	});
	connect(m_persistentChatChannelList, &QListWidget::itemClicked, this,
			[this, activatePersistentChatChannelItem](QListWidgetItem *item) {
				if (!item) {
					return;
				}

				setPersistentChatTargetUsesVoiceTree(false);
				const int clickedScopeValue = item->data(PersistentChatScopeRole).toInt();
				const unsigned int clickedScopeID = item->data(PersistentChatScopeIDRole).toUInt();
				const bool clickedVisibleScope =
					m_visiblePersistentChatScope
					&& clickedScopeValue != LocalServerLogScope
					&& static_cast< int >(*m_visiblePersistentChatScope) == clickedScopeValue
					&& m_visiblePersistentChatScopeID == clickedScopeID;
				activatePersistentChatChannelItem(item, clickedVisibleScope);
			});
	connect(m_persistentChatChannelList, &QListWidget::customContextMenuRequested, this,
			&MainWindow::showPersistentTextChannelContextMenu);
	connect(m_persistentChatReplyCancelButton, &QToolButton::clicked, this,
			[this]() { setPersistentChatReplyTarget(std::nullopt); });
	connect(qteChat, &QTextEdit::textChanged, this, &MainWindow::updatePersistentChatSendButton);
	connect(m_persistentChatSendButton, &QToolButton::clicked, this, [this]() {
		if (!qteChat || !qteChat->isEnabled() || qteChat->isShowingDefaultText()) {
			return;
		}

		const QString message = qteChat->toPlainText().trimmed();
		if (message.isEmpty()) {
			return;
		}

		sendChatbarText(qteChat->toPlainText());
	});
	connect(m_persistentChatResizeRenderTimer, &QTimer::timeout, this, [this]() {
		if (m_pendingPersistentChatViewportWidth <= 0 || !m_persistentChatHistory || m_persistentChatMessages.empty()) {
			return;
		}

		if (m_lastPersistentChatViewportWidth >= 0
			&& std::abs(m_pendingPersistentChatViewportWidth - m_lastPersistentChatViewportWidth) < 8) {
			return;
		}

		m_lastPersistentChatViewportWidth = m_pendingPersistentChatViewportWidth;
		renderPersistentChatView(QString(), false, true);
	});
	connect(m_persistentChatHistory, &PersistentChatListWidget::contentWidthChanged, this, [this](int width) {
		if (width <= 0 || !m_persistentChatHistory || m_persistentChatMessages.empty()) {
			return;
		}

		if (m_lastPersistentChatViewportWidth >= 0 && std::abs(width - m_lastPersistentChatViewportWidth) < 8) {
			return;
		}

		m_pendingPersistentChatViewportWidth = width;
		m_persistentChatResizeRenderTimer->start();
	});
	connect(m_persistentChatHistory->verticalScrollBar(), &QScrollBar::valueChanged, this,
			[this](int) {
				if (m_persistentChatScrollIdleTimer) {
					m_persistentChatScrollIdleTimer->start();
				}
				markPersistentChatRead();
			});
	connect(m_persistentChatScrollIdleTimer, &QTimer::timeout, this, [this]() {
		if (!m_persistentChatPreviewRefreshPending || !m_persistentChatHistory) {
			return;
		}

		m_persistentChatPreviewRefreshPending = false;
		const bool wasAtBottom = m_persistentChatHistory->isScrolledToBottom();
		renderPersistentChatView(QString(), wasAtBottom, !wasAtBottom);
	});
	connect(qdwChat, &QDockWidget::visibilityChanged, this, [this](bool visible) {
		if (visible) {
			markPersistentChatRead();
		}
	});
	connect(this, &MainWindow::windowActivated, this, [this]() { markPersistentChatRead(); });
	connect(m_persistentChatChannelMenu, &QMenu::aboutToShow, this, &MainWindow::qmPersistentTextChannel_aboutToShow);
	connect(m_persistentChatAddRoomAction, &QAction::triggered, this, [this]() { createPersistentTextChannel(); });
	connect(m_persistentChatEditRoomAction, &QAction::triggered, this, [this]() { editPersistentTextChannel(); });
	connect(m_persistentChatRemoveRoomAction, &QAction::triggered, this, [this]() { removePersistentTextChannel(); });
	connect(m_persistentChatAclRoomAction, &QAction::triggered, this, [this]() { editPersistentTextChannelACL(); });

	rebuildPersistentChatChannelList();
	updatePersistentChatScopeSelectorLabels();
	updatePersistentTextChannelControls();
	clearPersistentChatView(tr("Connect to a server to load conversations and history."), tr("Start a conversation"),
							{ tr("Open Server to connect"), tr("Room chat and history appear here") });
}

void MainWindow::refreshCustomChromeStyles() {
	refreshServerNavigatorStyles();
	refreshPersistentChatStyles();
#ifdef Q_OS_WIN
	applyNativeTitleBarTheme(this);
#endif
}

void MainWindow::refreshServerNavigatorStyles() {
	if (!m_serverNavigatorContainer || !qtvUsers) {
		return;
	}

	const QPalette navPalette = m_serverNavigatorContainer->palette();
	const ChromePaletteColors chrome = buildChromePalette(navPalette);

	if (m_persistentChatChannelList) {
		QPalette listPalette = m_persistentChatChannelList->palette();
		listPalette.setColor(QPalette::Base, chrome.panelColor);
		listPalette.setColor(QPalette::AlternateBase, chrome.panelColor);
		listPalette.setColor(QPalette::Window, chrome.panelColor);
		listPalette.setColor(QPalette::Text, chrome.textColor);
		listPalette.setColor(QPalette::Highlight, chrome.selectedColor);
		listPalette.setColor(QPalette::HighlightedText, chrome.selectedTextColor);
		m_persistentChatChannelList->setAutoFillBackground(true);
		m_persistentChatChannelList->setPalette(listPalette);
		m_persistentChatChannelList->viewport()->setAutoFillBackground(true);
		m_persistentChatChannelList->viewport()->setPalette(listPalette);
		m_persistentChatChannelList->viewport()->setStyleSheet(
			QString::fromLatin1("background-color: %1;").arg(chrome.panelColor.name()));
	}

	QPalette treePalette = qtvUsers->palette();
	treePalette.setColor(QPalette::Base, chrome.panelColor);
	treePalette.setColor(QPalette::AlternateBase, chrome.panelColor);
	treePalette.setColor(QPalette::Window, chrome.panelColor);
	treePalette.setColor(QPalette::Text, chrome.textColor);
	treePalette.setColor(QPalette::Highlight, chrome.selectedColor);
	treePalette.setColor(QPalette::HighlightedText, chrome.selectedTextColor);
	qtvUsers->setAutoFillBackground(true);
	qtvUsers->setPalette(treePalette);
	qtvUsers->setProperty("rowHoverColor", chrome.hoverColor);
	qtvUsers->viewport()->setAutoFillBackground(true);
	qtvUsers->viewport()->setPalette(treePalette);
	qtvUsers->viewport()->setStyleSheet(QString::fromLatin1("background-color: %1;").arg(chrome.panelColor.name()));

	m_serverNavigatorContainer->setAutoFillBackground(true);
	QPalette navContainerPalette = m_serverNavigatorContainer->palette();
	navContainerPalette.setColor(QPalette::Window, chrome.panelColor);
	m_serverNavigatorContainer->setPalette(navContainerPalette);
	QString serverNavigatorStyle = QString::fromLatin1(
			"QWidget#qwServerNavigator {"
			" background-color: %1;"
			"}"
			"QFrame#qfServerNavigatorHeader {"
			" background-color: transparent;"
			" border: none;"
			" border-radius: 0px;"
			" margin: 0px;"
			"}"
			"QFrame#qfServerNavigatorContent {"
			" background-color: %2;"
			" border: 1px solid %6;"
			" border-radius: 12px;"
			" margin: 0px 2px 0px 0px;"
			"}"
			"QLabel#qlServerNavigatorEyebrow {"
			" color: %3;"
			" letter-spacing: 0.12em;"
			"}"
			"QLabel#qlServerNavigatorTitle {"
			" color: %4;"
			" font-weight: 700;"
			"}"
			"QLabel#qlServerNavigatorSubtitle {"
			" color: %5;"
			" line-height: 1.3em;"
			" padding-bottom: 0px;"
			"}"
			"QLabel#qlServerNavigatorFooter {"
			" color: %5;"
			" line-height: 1.3em;"
			" padding: 0px 2px;"
			"}"
			"QLabel#qlServerNavigatorVoiceSectionEyebrow, QLabel#qlServerNavigatorTextChannelsEyebrow {"
			" color: %3;"
			" letter-spacing: 0.12em;"
			"}"
			"QLabel#qlServerNavigatorVoiceSectionSubtitle, QLabel#qlServerNavigatorTextChannelsSubtitle {"
			" color: %5;"
			" line-height: 1.35em;"
			"}"
			"QFrame#qfServerNavigatorTextChannels {"
			" background-color: transparent;"
			" border: none;"
			"}"
			"QFrame#qfServerNavigatorTextChannelsDivider {"
			" background-color: %6;"
			" min-height: 0px;"
			" max-height: 1px;"
			" margin: 1px 0px;"
			" border: none;"
			"}"
			"QLabel#qlServerNavigatorTextChannelsTitle {"
			" color: %4;"
			" font-weight: 700;"
			"}"
			"QFrame#qfServerNavigatorTextChannelsMotd {"
			" background-color: %2;"
			" border: 1px solid %6;"
			" border-radius: 10px;"
			"}"
			"QLabel#qlServerNavigatorTextChannelsMotdTitle {"
			" color: %4;"
			" font-weight: 600;"
			"}"
			"QLabel#qlServerNavigatorTextChannelsMotdBody {"
			" color: %4;"
			" background: transparent;"
			" line-height: 1.35em;"
			"}"
			"QToolButton#qtbServerNavigatorTextChannelsMotdToggle {"
			" border: 1px solid transparent;"
			" border-radius: 8px;"
			" background: transparent;"
			" color: %5;"
			" padding: 1px 6px;"
			"}"
			"QToolButton#qtbServerNavigatorTextChannelsMotdToggle:hover {"
			" border-color: %6;"
			" background-color: %8;"
			" color: %4;"
			"}"
			"QTreeView#qtvUsers {"
			" border: none;"
			" border-radius: 0px;"
			" background-color: transparent;"
			" alternate-background-color: transparent;"
			" color: %4;"
			" padding: 0px;"
			" outline: none;"
			" show-decoration-selected: 0;"
			"}"
			"QWidget#qtvUsersViewport {"
			" background-color: transparent;"
			"}"
			"QTreeView#qtvUsers::branch {"
			" background: transparent;"
			"}"
			"QTreeView#qtvUsers::branch:hover, QTreeView#qtvUsers::branch:selected {"
			" background: transparent;"
			"}"
			"QTreeView#qtvUsers::branch:has-siblings:!adjoins-item, "
			"QTreeView#qtvUsers::branch:has-siblings:adjoins-item, "
			"QTreeView#qtvUsers::branch:!has-children:!has-siblings:adjoins-item, "
			"QTreeView#qtvUsers::branch:closed:has-children:has-siblings, "
			"QTreeView#qtvUsers::branch:open:has-children:has-siblings, "
			"QTreeView#qtvUsers::branch:closed:has-children:!has-siblings, "
			"QTreeView#qtvUsers::branch:open:has-children:!has-siblings {"
			" margin-left: 4px;"
			" }"
			"QTreeView#qtvUsers::item {"
			" min-height: 26px;"
			" padding: 0px 7px;"
			" border: none;"
			" border-radius: 0px;"
			" margin: 0px;"
			"}"
			"QTreeView#qtvUsers::item:hover {"
			" background-color: transparent;"
			"}"
			"QTreeView#qtvUsers::item:selected {"
			" border-radius: 0px;"
			" background-color: transparent;"
			" color: %9;"
			"}"
			"QTreeView#qtvUsers::item:selected:active {"
			" background-color: transparent;"
			" color: %9;"
			"}"
			"QTreeView#qtvUsers::item:selected:!active {"
			" background-color: transparent;"
			" color: %9;"
			"}"
			"QListWidget#qlwPersistentTextChannels {"
			" border: none;"
			" border-radius: 0px;"
			" background-color: transparent;"
			" alternate-background-color: transparent;"
			" color: %4;"
			" padding: 0px;"
			" outline: none;"
			"}"
			"QListWidget#qlwPersistentTextChannels::item {"
			" min-height: 26px;"
			" padding: 0px;"
			" border: none;"
			" border-radius: 0px;"
			" margin: 0px;"
			"}"
			"QListWidget#qlwPersistentTextChannels::item:hover {"
			" background-color: transparent;"
			"}"
			"QListWidget#qlwPersistentTextChannels::item:selected {"
			" background-color: transparent;"
			" color: %4;"
			"}"
			"QListWidget#qlwPersistentTextChannels::item:selected:active {"
			" background-color: transparent;"
			" color: %4;"
			"}"
			"QListWidget#qlwPersistentTextChannels::item:selected:!active {"
			" background-color: transparent;"
			" color: %4;"
			"}");
	serverNavigatorStyle.replace(QStringLiteral("%9"), chrome.selectedTextColor.name());
	serverNavigatorStyle.replace(QStringLiteral("%8"), chrome.hoverColor.name());
	serverNavigatorStyle.replace(QStringLiteral("%6"), chrome.dividerColor.name());
	serverNavigatorStyle.replace(QStringLiteral("%5"), chrome.mutedTextColor.name());
	serverNavigatorStyle.replace(QStringLiteral("%4"), chrome.textColor.name());
	serverNavigatorStyle.replace(QStringLiteral("%3"), chrome.eyebrowColor.name());
	serverNavigatorStyle.replace(QStringLiteral("%2"), chrome.elevatedCardColor.name());
	serverNavigatorStyle.replace(QStringLiteral("%1"), chrome.cardColor.name());
	m_serverNavigatorContainer->setStyleSheet(serverNavigatorStyle);
	refreshServerNavigatorMotdHeight();
}

void MainWindow::refreshServerNavigatorMotdHeight() {
	if (!m_serverNavigatorTextChannelsMotdFrame || !m_serverNavigatorTextChannelsMotdBody) {
		return;
	}

	static const int minimumExpandedMotdHeight = 96;

	if (m_persistentChatWelcomeText.trimmed().isEmpty() || m_persistentChatMotdHidden
		|| !m_serverNavigatorTextChannelsMotdFrame->isVisible() || !m_serverNavigatorTextChannelsMotdBody->isVisible()) {
		m_serverNavigatorTextChannelsMotdBody->setMinimumHeight(0);
		return;
	}

	int availableWidth = m_serverNavigatorTextChannelsMotdBody->width();
	if (availableWidth <= 0 && m_serverNavigatorTextChannelsMotdFrame->layout()) {
		const QMargins margins = m_serverNavigatorTextChannelsMotdFrame->layout()->contentsMargins();
		availableWidth = m_serverNavigatorTextChannelsMotdFrame->contentsRect().width() - margins.left() - margins.right();
	}
	if (availableWidth <= 0 && m_serverNavigatorContentFrame) {
		availableWidth = m_serverNavigatorContentFrame->contentsRect().width() - 48;
	}
	if (availableWidth <= 0) {
		return;
	}

	QTextDocument document;
	document.setDocumentMargin(0);
	document.setDefaultFont(m_serverNavigatorTextChannelsMotdBody->font());
	document.setHtml(m_serverNavigatorTextChannelsMotdBody->text());
	document.setTextWidth(availableWidth);

	const int desiredHeight =
		std::max(static_cast< int >(std::ceil(document.size().height())),
				 std::max(m_serverNavigatorTextChannelsMotdBody->heightForWidth(availableWidth),
						  m_serverNavigatorTextChannelsMotdBody->sizeHint().height()));
	if (desiredHeight > 0) {
		m_serverNavigatorTextChannelsMotdBody->setMinimumHeight(std::max(desiredHeight, minimumExpandedMotdHeight));
	}
}

void MainWindow::refreshPersistentChatStyles() {
	if (!m_persistentChatContainer) {
		return;
	}

	const QPalette chatPalette = m_persistentChatContainer->palette();
	const ChromePaletteColors chrome = buildChromePalette(chatPalette);
	const QColor headerSurfaceColor  = chrome.elevatedCardColor;
	const QColor historyColor        = chrome.panelColor;
	const QColor inputColor          = chrome.inputColor;
	const QColor seamColor           = chrome.dividerColor;
	const QColor bubbleColor         = mixColors(chrome.elevatedCardColor, chrome.selectedColor, chrome.darkTheme ? 0.08 : 0.04);
	const QColor selfBubbleColor     = mixColors(chrome.selectedColor, chrome.elevatedCardColor, chrome.darkTheme ? 0.26 : 0.14);
	const QColor quoteColor          = mixColors(chrome.elevatedCardColor, chrome.selectedColor, chrome.darkTheme ? 0.18 : 0.08);
	const QColor pillColor           = mixColors(chrome.elevatedCardColor, chrome.selectedColor, chrome.darkTheme ? 0.06 : 0.03);
	const QColor avatarBadgeColor    = mixColors(chrome.selectedColor, chrome.elevatedCardColor, chrome.darkTheme ? 0.10 : 0.04);
	const QColor sendButtonColor     = mixColors(chrome.selectedColor, chrome.elevatedCardColor, chrome.darkTheme ? 0.02 : 0.00);

	m_persistentChatContainer->setAutoFillBackground(true);
	QPalette containerPalette = m_persistentChatContainer->palette();
	containerPalette.setColor(QPalette::Window, historyColor);
	m_persistentChatContainer->setPalette(containerPalette);

	qdwChat->setAutoFillBackground(true);
	QPalette chatDockPalette = qdwChat->palette();
	chatDockPalette.setColor(QPalette::Window, historyColor);
	qdwChat->setPalette(chatDockPalette);
	qdwChat->setStyleSheet(QString::fromLatin1("QDockWidget#qdwChat { background-color: %1; border: none; }")
						   .arg(historyColor.name()));
	qdwLog->setAutoFillBackground(true);
	QPalette logDockPalette = qdwLog->palette();
	logDockPalette.setColor(QPalette::Window, chrome.railColor);
	qdwLog->setPalette(logDockPalette);
	qdwLog->setStyleSheet(
		QString::fromLatin1(
			"QDockWidget#qdwLog { background-color: %1; border: none; }"
			"QWidget#qwLogSurface { background-color: %2; border: none; }")
			.arg(chrome.railColor.name(), chrome.railColor.name()));
	const QString dockTitleBarStyle = QString::fromLatin1(
										 "background-color: %1; color: %1; border: none; margin: 0px; padding: 0px;")
										 .arg(chrome.railColor.name());
	if (dtbLogDockTitle) {
		dtbLogDockTitle->setAttribute(Qt::WA_StyledBackground, true);
		dtbLogDockTitle->setContentsMargins(0, 0, 0, 0);
		dtbLogDockTitle->setStyleSheet(dockTitleBarStyle);
	}
	if (dtbChatDockTitle) {
		dtbChatDockTitle->setAttribute(Qt::WA_StyledBackground, true);
		dtbChatDockTitle->setContentsMargins(0, 0, 0, 0);
		dtbChatDockTitle->setStyleSheet(dockTitleBarStyle);
	}
	if (menubar) {
		menubar->setStyleSheet(QString::fromLatin1(
								  "QMenuBar {"
								  " background-color: %1;"
								  " border: none;"
								  " padding: 0px 4px 0px 0px;"
								  "}"
								  "QMenuBar::item {"
								  " background: transparent;"
								  " border-radius: 8px;"
								  " margin: 0px 1px;"
								  " padding: 3px 6px;"
								  "}"
								  "QMenuBar::item:selected {"
								  " background: %2;"
								  " color: %4;"
								  "}"
								  "QMenuBar::item:pressed {"
								  " background: %3;"
								  " color: %4;"
								  "}")
								  .arg(chrome.railColor.name(), chrome.hoverColor.name(), chrome.selectedColor.name(),
									   chrome.selectedTextColor.name()));
	}
	if (qtIconToolbar) {
		qtIconToolbar->setIconSize(QSize(16, 16));
		qtIconToolbar->setStyleSheet(
			QString::fromLatin1(
				"QToolBar { background-color: %1; border: none; spacing: 2px; padding: 0px 1px 0px 0px; }"
				"QToolBar::separator { background: transparent; width: 2px; }"
				"QToolBar QToolButton {"
				" border: none;"
				" border-radius: 8px;"
				" background: transparent;"
				" padding: 2px;"
				" margin: 0px;"
				"}"
				"QToolBar QToolButton:hover { background: %2; }"
				"QToolBar QToolButton:pressed, QToolBar QToolButton:checked { background: %3; }"
				"QToolBar QComboBox {"
				" background: %4;"
				" border: 1px solid %5;"
				" border-radius: 9px;"
				" color: %6;"
				" min-height: 20px;"
				" padding: 0px 7px;"
				" margin-left: 3px;"
				"}"
				"QToolBar QComboBox:hover { border-color: %2; }"
				"QToolBar QComboBox::drop-down { border: none; width: 18px; }")
				.arg(chrome.railColor.name(), chrome.hoverColor.name(), chrome.selectedColor.name(),
					 chrome.elevatedCardColor.name(), chrome.dividerColor.name(), chrome.textColor.name()));
	}
	setStyleSheet(QString::fromLatin1(
					  "QMainWindow::separator:vertical {"
					  " background: transparent;"
					  " width: 1px;"
					  " margin: 8px 0px 4px 0px;"
					  "}"
					  "QMainWindow::separator:vertical:hover {"
					  " background: transparent;"
					  " border-radius: 0px;"
					  " margin: 8px 0px 4px 0px;"
					  "}"
					  "QMainWindow::separator:horizontal {"
					  " background: transparent;"
					  " height: 8px;"
					  " margin: 0px 8px 4px 8px;"
					  "}"
					  "QMainWindow::separator:horizontal:hover {"
					  " background: %1;"
					  " border-radius: 4px;"
					  " margin: 2px 12px 2px 12px;"
					  "}"
					  "QTextBrowser#qteLog QScrollBar:vertical,"
					  "QListWidget#qtePersistentChatHistory QScrollBar:vertical,"
					  "ChatbarTextEdit#qteChat QScrollBar:vertical,"
					  "QListWidget#qlwPersistentTextChannels QScrollBar:vertical,"
					  "QTreeView#qtvUsers QScrollBar:vertical {"
					  " background: transparent;"
					  " width: 8px;"
					  " margin: 3px 1px 3px 0px;"
					  " border: none;"
					  "}"
					  "QTextBrowser#qteLog QScrollBar::handle:vertical,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::handle:vertical,"
					  "ChatbarTextEdit#qteChat QScrollBar::handle:vertical,"
					  "QListWidget#qlwPersistentTextChannels QScrollBar::handle:vertical,"
					  "QTreeView#qtvUsers QScrollBar::handle:vertical {"
					  " background: %2;"
					  " min-height: 24px;"
					  " border-radius: 4px;"
					  "}"
					  "QTextBrowser#qteLog QScrollBar:horizontal,"
					  "QListWidget#qtePersistentChatHistory QScrollBar:horizontal,"
					  "ChatbarTextEdit#qteChat QScrollBar:horizontal {"
					  " background: transparent;"
					  " height: 6px;"
					  " margin: 0px 1px 1px 1px;"
					  " border: none;"
					  "}"
					  "QTextBrowser#qteLog QScrollBar::handle:vertical:hover,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::handle:vertical:hover,"
					  "ChatbarTextEdit#qteChat QScrollBar::handle:vertical:hover,"
					  "QListWidget#qlwPersistentTextChannels QScrollBar::handle:vertical:hover,"
					  "QTreeView#qtvUsers QScrollBar::handle:vertical:hover,"
					  "QTextBrowser#qteLog QScrollBar::handle:vertical:pressed,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::handle:vertical:pressed,"
					  "ChatbarTextEdit#qteChat QScrollBar::handle:vertical:pressed,"
					  "QListWidget#qlwPersistentTextChannels QScrollBar::handle:vertical:pressed,"
					  "QTreeView#qtvUsers QScrollBar::handle:vertical:pressed {"
					  " background: %3;"
					  "}"
					  "QTextBrowser#qteLog QScrollBar::handle:horizontal,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::handle:horizontal,"
					  "ChatbarTextEdit#qteChat QScrollBar::handle:horizontal {"
					  " background: %2;"
					  " min-width: 24px;"
					  " border-radius: 3px;"
					  "}"
					  "QTextBrowser#qteLog QScrollBar::handle:horizontal:hover,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::handle:horizontal:hover,"
					  "ChatbarTextEdit#qteChat QScrollBar::handle:horizontal:hover,"
					  "QTextBrowser#qteLog QScrollBar::handle:horizontal:pressed,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::handle:horizontal:pressed,"
					  "ChatbarTextEdit#qteChat QScrollBar::handle:horizontal:pressed {"
					  " background: %3;"
					  "}"
					  "QTextBrowser#qteLog QScrollBar::add-line:vertical,"
					  "QTextBrowser#qteLog QScrollBar::sub-line:vertical,"
					  "QTextBrowser#qteLog QScrollBar::add-page:vertical,"
					  "QTextBrowser#qteLog QScrollBar::sub-page:vertical,"
					  "QTextBrowser#qteLog QScrollBar::add-line:horizontal,"
					  "QTextBrowser#qteLog QScrollBar::sub-line:horizontal,"
					  "QTextBrowser#qteLog QScrollBar::add-page:horizontal,"
					  "QTextBrowser#qteLog QScrollBar::sub-page:horizontal,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::add-line:vertical,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::sub-line:vertical,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::add-page:vertical,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::sub-page:vertical,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::add-line:horizontal,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::sub-line:horizontal,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::add-page:horizontal,"
					  "QListWidget#qtePersistentChatHistory QScrollBar::sub-page:horizontal,"
					  "ChatbarTextEdit#qteChat QScrollBar::add-line:vertical,"
					  "ChatbarTextEdit#qteChat QScrollBar::sub-line:vertical,"
					  "ChatbarTextEdit#qteChat QScrollBar::add-page:vertical,"
					  "ChatbarTextEdit#qteChat QScrollBar::sub-page:vertical,"
					  "ChatbarTextEdit#qteChat QScrollBar::add-line:horizontal,"
					  "ChatbarTextEdit#qteChat QScrollBar::sub-line:horizontal,"
					  "ChatbarTextEdit#qteChat QScrollBar::add-page:horizontal,"
					  "ChatbarTextEdit#qteChat QScrollBar::sub-page:horizontal,"
					  "QListWidget#qlwPersistentTextChannels QScrollBar::add-line:vertical,"
					  "QListWidget#qlwPersistentTextChannels QScrollBar::sub-line:vertical,"
					  "QListWidget#qlwPersistentTextChannels QScrollBar::add-page:vertical,"
					  "QListWidget#qlwPersistentTextChannels QScrollBar::sub-page:vertical,"
					  "QTreeView#qtvUsers QScrollBar::add-line:vertical,"
					  "QTreeView#qtvUsers QScrollBar::sub-line:vertical,"
					  "QTreeView#qtvUsers QScrollBar::add-page:vertical,"
					  "QTreeView#qtvUsers QScrollBar::sub-page:vertical {"
					  " background: transparent;"
					  " border: none;"
					  " height: 0px;"
					  "}"
					  "QListWidget#qtePersistentChatHistory QScrollBar:vertical {"
					  " width: 6px;"
					  " margin: 0px;"
					  "}"
					  "QListWidget#qtePersistentChatHistory QScrollBar::handle:vertical {"
					  " border-radius: 3px;"
					  "}")
					  .arg(chrome.dividerColor.name(), chrome.scrollbarHandleColor.name(),
						   chrome.scrollbarHandleHoverColor.name()));

	if (m_persistentChatHistory) {
		QPalette historyPalette = m_persistentChatHistory->palette();
		historyPalette.setColor(QPalette::Base, historyColor);
		historyPalette.setColor(QPalette::AlternateBase, historyColor);
		historyPalette.setColor(QPalette::Window, historyColor);
		historyPalette.setColor(QPalette::Text, chrome.textColor);
		historyPalette.setColor(QPalette::Highlight, chrome.selectedColor);
		historyPalette.setColor(QPalette::HighlightedText, chrome.selectedTextColor);
		m_persistentChatHistory->setAutoFillBackground(true);
		m_persistentChatHistory->setPalette(historyPalette);
		m_persistentChatHistory->viewport()->setAutoFillBackground(true);
		m_persistentChatHistory->viewport()->setPalette(historyPalette);
		m_persistentChatHistory->viewport()->setStyleSheet(
			QString::fromLatin1("background-color: %1; border: none; outline: none;").arg(historyColor.name()));
	}

	if (qteLog) {
		QPalette logPalette = qteLog->palette();
		logPalette.setColor(QPalette::Base, historyColor);
		logPalette.setColor(QPalette::AlternateBase, historyColor);
		logPalette.setColor(QPalette::Window, historyColor);
		logPalette.setColor(QPalette::Text, chrome.textColor);
		logPalette.setColor(QPalette::Highlight, chrome.selectedColor);
		logPalette.setColor(QPalette::HighlightedText, chrome.selectedTextColor);
		qteLog->setAutoFillBackground(true);
		qteLog->setPalette(logPalette);
		qteLog->viewport()->setAutoFillBackground(true);
		qteLog->viewport()->setPalette(logPalette);
		qteLog->setStyleSheet(
			QString::fromLatin1(
				"border-style: solid;"
				"border-color: %1;"
				"border-width: 1px 0px 1px 1px;"
				"border-top-left-radius: 12px;"
				"border-bottom-left-radius: 12px;"
				"border-top-right-radius: 0px;"
				"border-bottom-right-radius: 0px;"
				"background-color: %2; padding: 0px; outline: none;")
				.arg(chrome.borderColor.name(), historyColor.name()));
		qteLog->viewport()->setStyleSheet(
			QString::fromLatin1("background-color: %1; border: none; outline: none;").arg(historyColor.name()));
	}

	if (QWidget *logSurface = qdwLog->widget()) {
		QPalette logSurfacePalette = logSurface->palette();
		logSurfacePalette.setColor(QPalette::Window, chrome.railColor);
		logSurface->setAutoFillBackground(true);
		logSurface->setPalette(logSurfacePalette);
		logSurface->setStyleSheet(
			QString::fromLatin1("QWidget#qwLogSurface { background-color: %1; border: none; }")
				.arg(chrome.railColor.name()));
	}

	if (qteChat) {
		QPalette inputPalette = qteChat->palette();
		inputPalette.setColor(QPalette::Base, inputColor);
		inputPalette.setColor(QPalette::AlternateBase, inputColor);
		inputPalette.setColor(QPalette::Window, inputColor);
		inputPalette.setColor(QPalette::Text, chrome.textColor);
		inputPalette.setColor(QPalette::Highlight, chrome.selectedColor);
		inputPalette.setColor(QPalette::HighlightedText, chrome.selectedTextColor);
		qteChat->setAutoFillBackground(true);
		qteChat->setPalette(inputPalette);
		qteChat->viewport()->setAutoFillBackground(true);
		qteChat->viewport()->setPalette(inputPalette);
		qteChat->setStyleSheet(
			QString::fromLatin1("border: none; background-color: transparent; padding: 6px 4px; outline: none;"));
		qteChat->viewport()->setStyleSheet(
			QString::fromLatin1("background-color: transparent; border: none; outline: none;"));
	}

	const QString persistentChatStyle =
		QString::fromLatin1(
			"QWidget#qwPersistentChat {"
			" background-color: %2;"
			"}"
			"QFrame#qfPersistentChatHeader {"
			" border: none;"
			" border-bottom: 1px solid %1;"
			" border-radius: 0px;"
			" background-color: %8;"
			"}"
			"QWidget#qwPersistentChatSurface {"
			" border: none;"
			" border-radius: 0px;"
			" background-color: transparent;"
			"}"
			"QWidget#qwPersistentChatPanel {"
			" border: none;"
			" border-radius: 0px;"
			" background-color: %2;"
			"}"
			"QLabel#qlPersistentChatHeaderEyebrow {"
			" color: %5;"
			" letter-spacing: 0.08em;"
			"}"
			"QLabel#qlPersistentChatHeaderSubtitle {"
			" color: %3;"
			" line-height: 1.3em;"
			"}"
			"QLabel#qlPersistentChatHeaderContext {"
			" color: %6;"
			" background-color: %12;"
			" border-radius: 999px;"
			" padding: 2px 8px;"
			"}"
			"QListWidget#qtePersistentChatHistory {"
			" border: none;"
			" border-radius: 0px;"
			" background-color: %2;"
			" padding: 0px;"
			" outline: none;"
			"}"
			"QListWidget#qtePersistentChatHistory:focus {"
			" border: none;"
			" outline: none;"
			"}"
			"QListWidget#qtePersistentChatHistory::item {"
			" border: none;"
			" margin: 0px;"
			" padding: 0px;"
			"}"
			"QFrame#qfPersistentChatComposer {"
			" border: none;"
			" border-top: 1px solid %1;"
			" background-color: %2;"
			" border-bottom-left-radius: 0px;"
			" border-bottom-right-radius: 0px;"
			"}"
			"QWidget#qwPersistentChatComposerInputRow {"
			" background: %4;"
			" border: 1px solid %1;"
			" border-radius: 14px;"
			" padding: 2px 5px;"
			"}"
			"QFrame#qfPersistentChatReply {"
			" border: none;"
			" border-radius: 11px;"
			" background-color: %10;"
			"}"
			"QLabel#qlPersistentChatReplyLabel {"
			" color: %6;"
			" font-weight: 600;"
			"}"
			"QLabel#qlPersistentChatReplySnippet {"
			" color: %3;"
			"}"
			"QToolButton#qtbPersistentChatSend {"
			" border: none;"
			" border-radius: 14px;"
			" background-color: %14;"
			" min-width: 28px;"
			" min-height: 28px;"
			" padding: 4px;"
			"}"
			"QToolButton#qtbPersistentChatSend:hover {"
			" background-color: %9;"
			"}"
			"QToolButton#qtbPersistentChatSend:disabled {"
			" background-color: %7;"
			"}"
			"QFrame#qfPersistentChatBanner {"
			" border: 1px solid %1;"
			" border-radius: 18px;"
			" background-color: %8;"
			"}"
			"QLabel#qlPersistentChatBannerEyebrow {"
			" color: %5;"
			" letter-spacing: 0.12em;"
			"}"
			"QLabel#qlPersistentChatBannerTitle {"
			" color: %6;"
			" font-weight: 700;"
			"}"
			"QLabel#qlPersistentChatBannerBody {"
			" color: %3;"
			" line-height: 1.45em;"
			"}"
			"QWidget#qwPersistentChatBannerHints {"
			" background: transparent;"
			" border: none;"
			"}"
			"QLabel#qlPersistentChatBannerHint {"
			" color: %6;"
			" background-color: %12;"
			" border-radius: 999px;"
			" padding: 4px 10px;"
			"}"
			"QLabel#qlPersistentChatStatusPill,"
			"QLabel#qlPersistentChatLoadingPill,"
			"QLabel#qlPersistentChatInfoPill,"
			"QLabel#qlPersistentChatUnreadPill,"
			"QLabel#qlPersistentChatDateDivider,"
			"QLabel#qlPersistentChatAggregateNotice,"
			"QLabel#qlPersistentChatEmptyPill,"
			"QPushButton#qpbPersistentChatLoadOlder {"
			" border: none;"
			" border-radius: 999px;"
			" background: %12;"
			" color: %3;"
			" padding: 5px 12px;"
			"}"
			"QPushButton#qpbPersistentChatLoadOlder:hover {"
			" color: %6;"
			"}"
			"QWidget#qwPersistentChatMessageGroup,"
			"QWidget#qwPersistentChatMessageColumn,"
			"QWidget#qwPersistentChatGroupHeader,"
			"QWidget#qwPersistentChatBubbleActions {"
			" background: transparent;"
			" border: none;"
			"}"
			"QFrame#qfPersistentChatAvatarFrame {"
			" border: none;"
			" border-radius: 14px;"
			" background-color: %13;"
			"}"
			"QLabel#qlPersistentChatAvatarFallback {"
			" border: none;"
			" border-radius: 14px;"
			" background-color: transparent;"
			" color: %6;"
			" font-weight: 700;"
			"}"
			"QLabel#qlPersistentChatAvatar {"
			" background: transparent;"
			"}"
			"QLabel#qlPersistentChatGroupActor {"
			" color: %6;"
			" font-weight: 600;"
			"}"
			"QLabel#qlPersistentChatGroupTime {"
			" color: %3;"
			"}"
			"QToolButton#qtbPersistentChatGroupScope,"
			"QToolButton#qtbPersistentChatBubbleAction {"
			" border: none;"
			" background: transparent;"
			" color: %3;"
			" padding: 1px 5px;"
			"}"
			"QToolButton#qtbPersistentChatGroupScope:hover,"
			"QToolButton#qtbPersistentChatBubbleAction:hover {"
			" color: %6;"
			"}"
			"QFrame#qfPersistentChatBubble {"
			" border: 1px solid %1;"
			" border-radius: 16px;"
			" background-color: %11;"
			"}"
			"QFrame#qfPersistentChatBubble[bubbleSelf=\"true\"] {"
			" background-color: %9;"
			" border-color: %9;"
			"}"
			"QFrame#qfPersistentChatBubble[bubbleActive=\"true\"] {"
			" border-color: %13;"
			"}"
			"QFrame#qfPersistentChatBubbleQuote {"
			" border: 1px solid transparent;"
			" border-left: 3px solid %13;"
			" border-radius: 9px;"
			" background-color: %10;"
			"}"
			"QLabel#qlPersistentChatBubbleQuoteActor {"
			" color: %6;"
			"}"
			"QLabel#qlPersistentChatBubbleQuoteSnippet {"
			" color: %3;"
			"}"
			"ChatbarTextEdit#qteChat {"
			" border: none;"
			" border-radius: 0px;"
			" background-color: transparent;"
			" padding: 6px 5px;"
			"}"
			"ChatbarTextEdit#qteChat > QWidget {"
			" border: none;"
			" border-radius: 0px;"
			" background-color: transparent;"
			"}"
			"QLabel#qlPersistentChatHeaderTitle {"
			" color: %6;"
			"}")
			.arg(chrome.borderColor.name(), historyColor.name(), chrome.mutedTextColor.name(), inputColor.name(),
				 chrome.eyebrowColor.name(), chrome.textColor.name(), chrome.cardColor.name(),
				 headerSurfaceColor.name(), selfBubbleColor.name(), quoteColor.name(), bubbleColor.name(),
				 pillColor.name(), avatarBadgeColor.name(), sendButtonColor.name());
	m_persistentChatContainer->setStyleSheet(persistentChatStyle);
	if (m_persistentChatHistory) {
		m_persistentChatHistory->setStyleSheet(persistentChatStyle);
		if (QWidget *viewport = m_persistentChatHistory->viewport()) {
			viewport->setStyleSheet(persistentChatStyle);
		}
		for (int i = 0; i < m_persistentChatHistory->count(); ++i) {
			if (QListWidgetItem *item = m_persistentChatHistory->item(i)) {
				if (QWidget *itemWidget = m_persistentChatHistory->itemWidget(item)) {
					itemWidget->setStyleSheet(persistentChatStyle);
					itemWidget->style()->unpolish(itemWidget);
					itemWidget->style()->polish(itemWidget);
					itemWidget->update();
				}
			}
		}
	}
}

void MainWindow::refreshTextDocumentStylesheets() {
	const QString stylesheet = qApp->styleSheet();

	if (qteLog && qteLog->document()) {
		qteLog->document()->setDefaultStyleSheet(stylesheet);
	}

	if (qteChat && qteChat->document()) {
		qteChat->document()->setDefaultStyleSheet(stylesheet);
	}
}

void MainWindow::setPersistentChatWelcomeText(const QString &message) {
	if (m_persistentChatWelcomeText.trimmed().isEmpty() && !message.trimmed().isEmpty()) {
		m_persistentChatMotdHidden = true;
	}
	m_persistentChatWelcomeText = message;
	updatePersistentChatWelcome();
}

void MainWindow::updatePersistentChatWelcome() {
	if (!m_persistentChatHeaderFrame) {
		return;
	}

	updatePersistentChatChrome(currentPersistentChatTarget());
}

void MainWindow::setPersistentChatReplyTarget(const std::optional< MumbleProto::ChatMessage > &message) {
	if (!m_persistentChatReplyFrame || !m_persistentChatReplyLabel || !m_persistentChatReplySnippet) {
		m_pendingPersistentChatReply = message;
		return;
	}

	if (!message) {
		clearPersistentChatReplyTarget(true);
		return;
	}

	m_pendingPersistentChatReply = message;
	m_persistentChatReplyLabel->setText(tr("Replying to %1").arg(persistentChatActorLabel(*message).toHtmlEscaped()));
	m_persistentChatReplySnippet->setText(
		persistentChatMessageTextSnippet(persistentChatMessageSourceText(*message)).toHtmlEscaped());
	m_persistentChatReplyFrame->show();
	updateChatBar();
}

void MainWindow::clearPersistentChatReplyTarget(bool refreshChatBar) {
	m_pendingPersistentChatReply.reset();

	if (m_persistentChatReplyFrame) {
		m_persistentChatReplyFrame->hide();
	}

	if (refreshChatBar) {
		updateChatBar();
	}
}

void MainWindow::markPersistentChatAvailable(bool refreshUi) {
	if (m_hasPersistentChatSupport) {
		return;
	}

	m_hasPersistentChatSupport = true;
	if (!refreshUi) {
		return;
	}

	rebuildPersistentChatChannelList();
	updateChatBar();
}

QString MainWindow::persistentChatScopeLabel(MumbleProto::ChatScope scope, unsigned int scopeID) const {
	switch (scope) {
		case MumbleProto::Channel: {
			Channel *channel = Channel::get(scopeID);
			if (channel) {
				return Log::formatChannel(channel);
			}

			return tr("Channel %1").arg(scopeID).toHtmlEscaped();
		}
		case MumbleProto::ServerGlobal:
			return tr("Lobby").toHtmlEscaped();
		case MumbleProto::Aggregate:
			return tr("Feed").toHtmlEscaped();
		case MumbleProto::TextChannel: {
			const auto it = m_persistentTextChannels.constFind(scopeID);
			if (it != m_persistentTextChannels.cend()) {
				return tr("#%1").arg(it->name).toHtmlEscaped();
			}

			return tr("#text-%1").arg(scopeID).toHtmlEscaped();
		}
	}

	return tr("Unknown chat").toHtmlEscaped();
}

void MainWindow::rebuildPersistentChatChannelList() {
	if (!m_persistentChatChannelList) {
		return;
	}

	int previousScopeValue      = static_cast< int >(MumbleProto::TextChannel);
	unsigned int previousScopeID = m_defaultPersistentTextChannelID;
	if (const QListWidgetItem *currentItem = m_persistentChatChannelList->currentItem(); currentItem) {
		previousScopeValue = currentItem->data(PersistentChatScopeRole).toInt();
		previousScopeID = currentItem->data(PersistentChatScopeIDRole).toUInt();
	}

	const bool oldSignalState = m_persistentChatChannelList->blockSignals(true);
	m_persistentChatChannelList->clear();

	if (!hasPersistentChatCapabilities()) {
		m_persistentChatChannelList->blockSignals(oldSignalState);
		updatePersistentChatChrome(currentPersistentChatTarget());
		return;
	}

	QListWidgetItem *serverLogItem = new QListWidgetItem(m_persistentChatChannelList);
	serverLogItem->setData(PersistentChatScopeRole, LocalServerLogScope);
	serverLogItem->setData(PersistentChatScopeIDRole, 0U);
	serverLogItem->setToolTip(tr("Connection status, notices, and client diagnostics."));

	if (Global::get().bPersistentGlobalChatEnabled) {
		QListWidgetItem *globalItem = new QListWidgetItem(m_persistentChatChannelList);
		globalItem->setData(PersistentChatScopeRole, static_cast< int >(MumbleProto::ServerGlobal));
		globalItem->setData(PersistentChatScopeIDRole, 0U);
		globalItem->setToolTip(tr("Shared server chat for everyone here."));
	}

	QList< PersistentTextChannel > textChannels = m_persistentTextChannels.values();
	std::sort(textChannels.begin(), textChannels.end(),
			  [](const PersistentTextChannel &lhs, const PersistentTextChannel &rhs) {
				  if (lhs.position != rhs.position) {
					  return lhs.position < rhs.position;
				  }
				  if (lhs.name != rhs.name) {
					  return lhs.name.localeAwareCompare(rhs.name) < 0;
				  }

				  return lhs.textChannelID < rhs.textChannelID;
			  });

	for (const PersistentTextChannel &textChannel : textChannels) {
		QListWidgetItem *item = new QListWidgetItem(m_persistentChatChannelList);
		item->setData(PersistentChatScopeRole, static_cast< int >(MumbleProto::TextChannel));
		item->setData(PersistentChatScopeIDRole, textChannel.textChannelID);
		item->setToolTip(textChannel.description.isEmpty() ? tr("Persistent text channel")
														 : textChannel.description);
	}

	updatePersistentChatScopeSelectorLabels();

	QListWidgetItem *selectionItem = nullptr;
	for (int i = 0; i < m_persistentChatChannelList->count(); ++i) {
		QListWidgetItem *candidate = m_persistentChatChannelList->item(i);
		if (!candidate) {
			continue;
		}

		const int candidateScopeValue = candidate->data(PersistentChatScopeRole).toInt();
		const unsigned int candidateScopeID = candidate->data(PersistentChatScopeIDRole).toUInt();
		if (candidateScopeValue == previousScopeValue && candidateScopeID == previousScopeID) {
			selectionItem = candidate;
			break;
		}
	}

	if (!selectionItem && m_persistentTextChannels.contains(m_defaultPersistentTextChannelID)) {
		for (int i = 0; i < m_persistentChatChannelList->count(); ++i) {
			QListWidgetItem *candidate = m_persistentChatChannelList->item(i);
			if (!candidate) {
				continue;
			}

			const int candidateScopeValue = candidate->data(PersistentChatScopeRole).toInt();
			if (candidateScopeValue == static_cast< int >(MumbleProto::TextChannel)
				&& candidate->data(PersistentChatScopeIDRole).toUInt() == m_defaultPersistentTextChannelID) {
				selectionItem = candidate;
				break;
			}
		}
	}

	if (!selectionItem) {
		for (int i = 0; i < m_persistentChatChannelList->count(); ++i) {
			QListWidgetItem *candidate = m_persistentChatChannelList->item(i);
			if (!candidate) {
				continue;
			}

			if (candidate->data(PersistentChatScopeRole).toInt() == static_cast< int >(MumbleProto::TextChannel)) {
				selectionItem = candidate;
				break;
			}
		}
	}

	if (!selectionItem && m_persistentChatChannelList->count() > 1) {
		selectionItem = m_persistentChatChannelList->item(1);
	}
	if (!selectionItem && m_persistentChatChannelList->count() > 0) {
		selectionItem = m_persistentChatChannelList->item(0);
	}

	if (selectionItem) {
		m_persistentChatChannelList->setCurrentItem(selectionItem);
		if (!m_persistentChatTargetUsesVoiceTree) {
			selectionItem->setSelected(true);
		}
	}
	if (m_persistentChatTargetUsesVoiceTree) {
		m_persistentChatChannelList->clearSelection();
	}
	updatePersistentChatChannelListHeight();
	m_persistentChatChannelList->blockSignals(oldSignalState);
	updatePersistentChatChrome(currentPersistentChatTarget());
}

void MainWindow::handlePersistentTextChannelSync(const MumbleProto::TextChannelSync &msg) {
	const bool hadExistingTextChannels = !m_persistentTextChannels.isEmpty();
	m_persistentTextChannels.clear();
	m_defaultPersistentTextChannelID = msg.has_default_text_channel_id() ? msg.default_text_channel_id() : 0;

	for (const MumbleProto::TextChannelInfo &protoChannel : msg.channels()) {
		if (!protoChannel.has_text_channel_id() || !protoChannel.has_name()) {
			continue;
		}

		PersistentTextChannel textChannel;
		textChannel.textChannelID = protoChannel.text_channel_id();
		textChannel.aclChannelID  = protoChannel.has_acl_channel_id() ? protoChannel.acl_channel_id() : 0;
		textChannel.position      = protoChannel.has_position() ? protoChannel.position() : 0;
		textChannel.name          = u8(protoChannel.name());
		textChannel.description   = protoChannel.has_description() ? u8(protoChannel.description()) : QString();

		m_persistentTextChannels.insert(textChannel.textChannelID, textChannel);
	}

	rebuildPersistentChatChannelList();
	if (!hadExistingTextChannels && !m_persistentTextChannels.isEmpty()) {
		unsigned int preferredTextChannelID = 0;
		if (m_persistentTextChannels.contains(m_defaultPersistentTextChannelID)) {
			preferredTextChannelID = m_defaultPersistentTextChannelID;
		} else {
			QList< PersistentTextChannel > textChannels = m_persistentTextChannels.values();
			std::sort(textChannels.begin(), textChannels.end(),
					  [](const PersistentTextChannel &lhs, const PersistentTextChannel &rhs) {
						  if (lhs.position != rhs.position) {
							  return lhs.position < rhs.position;
						  }
						  if (lhs.name != rhs.name) {
							  return lhs.name.localeAwareCompare(rhs.name) < 0;
						  }

						  return lhs.textChannelID < rhs.textChannelID;
					  });
			if (!textChannels.isEmpty()) {
				preferredTextChannelID = textChannels.front().textChannelID;
			}
		}

		if (preferredTextChannelID != 0) {
			navigateToPersistentChatScope(MumbleProto::TextChannel, preferredTextChannelID);
		}
	}
	updatePersistentTextChannelControls();
	updateChatBar();
}

void MainWindow::setPersistentChatTargetUsesVoiceTree(bool useVoiceTree) {
	if (!m_persistentChatChannelList) {
		m_persistentChatTargetUsesVoiceTree = useVoiceTree;
		return;
	}

	if (m_persistentChatTargetUsesVoiceTree == useVoiceTree) {
		return;
	}

	m_persistentChatTargetUsesVoiceTree = useVoiceTree;

	const QSignalBlocker signalBlocker(m_persistentChatChannelList);
	if (useVoiceTree) {
		m_persistentChatChannelList->clearSelection();
	} else if (QListWidgetItem *currentItem = m_persistentChatChannelList->currentItem(); currentItem) {
		currentItem->setSelected(true);
	}

	m_persistentChatChannelList->viewport()->update();
}

void MainWindow::updatePersistentChatChannelListHeight() {
	if (!m_persistentChatChannelList) {
		return;
	}

	const int itemCount = m_persistentChatChannelList->count();
	if (itemCount <= 0) {
		m_persistentChatChannelList->setMinimumHeight(0);
		m_persistentChatChannelList->setMaximumHeight(0);
		m_persistentChatChannelList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		return;
	}

	int rowHeight = m_persistentChatChannelList->sizeHintForRow(0);
	if (rowHeight <= 0) {
		rowHeight = std::max(m_persistentChatChannelList->fontMetrics().height() + 12, 24);
	}

	const int desiredHeight = (itemCount * rowHeight)
							  + std::max(0, itemCount - 1) * m_persistentChatChannelList->spacing()
							  + (m_persistentChatChannelList->frameWidth() * 2);
	const int maximumHeight = 176;
	const int clampedHeight = std::min(desiredHeight, maximumHeight);

	m_persistentChatChannelList->setVerticalScrollBarPolicy(desiredHeight > maximumHeight ? Qt::ScrollBarAsNeeded
																						  : Qt::ScrollBarAlwaysOff);
	m_persistentChatChannelList->setMinimumHeight(clampedHeight);
	m_persistentChatChannelList->setMaximumHeight(clampedHeight);
	m_persistentChatChannelList->updateGeometry();
}

void MainWindow::updatePersistentChatScopeSelectorLabels() {
	if (!m_persistentChatChannelList) {
		return;
	}

	for (int i = 0; i < m_persistentChatChannelList->count(); ++i) {
		QListWidgetItem *item = m_persistentChatChannelList->item(i);
		if (!item) {
			continue;
		}

		const int scopeValue = item->data(PersistentChatScopeRole).toInt();
		const unsigned int scopeID = item->data(PersistentChatScopeIDRole).toUInt();

		QString scopeLabel;
		std::size_t unreadCount = 0;
		if (scopeValue == LocalServerLogScope) {
			scopeLabel = tr("Server log");
		} else {
			const MumbleProto::ChatScope scope = static_cast< MumbleProto::ChatScope >(scopeValue);
			switch (scope) {
				case MumbleProto::Aggregate:
					scopeLabel = tr("Feed");
					break;
				case MumbleProto::ServerGlobal:
					scopeLabel = tr("Lobby");
					break;
				case MumbleProto::TextChannel: {
					const auto it = m_persistentTextChannels.constFind(scopeID);
					scopeLabel    = it == m_persistentTextChannels.cend() ? tr("#text-%1").arg(scopeID)
																		  : tr("#%1").arg(it->name);
					break;
				}
				case MumbleProto::Channel: {
					Channel *channel = Channel::get(scopeID);
					scopeLabel       = channel ? persistentTextAclChannelLabel(channel) : tr("Channel %1").arg(scopeID);
					break;
				}
				default:
					scopeLabel = persistentChatScopeLabel(scope, scopeID);
					break;
			}
			unreadCount = scope == MumbleProto::Aggregate ? totalCachedPersistentChatUnreadCount()
														  : cachedPersistentChatUnreadCount(scope, scopeID);
		}

		item->setData(PersistentChatLabelRole, scopeLabel);
		item->setData(PersistentChatUnreadRole, static_cast< qulonglong >(unreadCount));
		item->setText(scopeLabel);
	}
}

bool MainWindow::canManagePersistentTextChannels() const {
	return Global::get().sh && Global::get().sh->isRunning() && (Global::get().pPermissions & ChanACL::Write);
}

std::optional< MainWindow::PersistentTextChannel > MainWindow::selectedPersistentTextChannel() const {
	if (!m_persistentChatChannelList || !m_persistentChatChannelList->currentItem()) {
		return std::nullopt;
	}

	const QListWidgetItem *item = m_persistentChatChannelList->currentItem();
	if (item->data(PersistentChatScopeRole).toInt() != static_cast< int >(MumbleProto::TextChannel)) {
		return std::nullopt;
	}

	const auto it = m_persistentTextChannels.constFind(item->data(PersistentChatScopeIDRole).toUInt());
	if (it == m_persistentTextChannels.cend()) {
		return std::nullopt;
	}

	return *it;
}

bool MainWindow::promptForPersistentTextChannel(PersistentTextChannel &textChannel, bool isNew) {
	QDialog dialog(this);
	dialog.setWindowTitle(isNew ? tr("Create text room") : tr("Edit text room"));

	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QFormLayout *formLayout = new QFormLayout();
	layout->addLayout(formLayout);

	QLineEdit *nameEdit = new QLineEdit(textChannel.name, &dialog);
	nameEdit->setPlaceholderText(tr("links"));
	formLayout->addRow(tr("Name"), nameEdit);

	QLineEdit *descriptionEdit = new QLineEdit(textChannel.description, &dialog);
	descriptionEdit->setPlaceholderText(tr("What this room is for"));
	formLayout->addRow(tr("Description"), descriptionEdit);

	QComboBox *aclChannelCombo = new QComboBox(&dialog);
	QList< Channel * > channels = Channel::c_qhChannels.values();
	std::sort(channels.begin(), channels.end(), [](const Channel *lhs, const Channel *rhs) {
		if (lhs == rhs) {
			return false;
		}
		if (lhs->iId == Mumble::ROOT_CHANNEL_ID || rhs->iId == Mumble::ROOT_CHANNEL_ID) {
			return lhs->iId == Mumble::ROOT_CHANNEL_ID;
		}
		return persistentTextAclChannelLabel(lhs).localeAwareCompare(persistentTextAclChannelLabel(rhs)) < 0;
	});

	int currentAclIndex = -1;
	for (Channel *channel : channels) {
		if (!channel) {
			continue;
		}

		const QString channelLabel = persistentTextAclChannelLabel(channel);
		aclChannelCombo->addItem(channelLabel, channel->iId);
		if (channel->iId == textChannel.aclChannelID) {
			currentAclIndex = aclChannelCombo->count() - 1;
		}
	}

	if (currentAclIndex < 0) {
		currentAclIndex = std::max(0, aclChannelCombo->findData(Mumble::ROOT_CHANNEL_ID));
	}
	aclChannelCombo->setCurrentIndex(currentAclIndex);
	formLayout->addRow(tr("ACL source"), aclChannelCombo);

	QSpinBox *positionSpin = new QSpinBox(&dialog);
	positionSpin->setRange(0, 9999);
	positionSpin->setValue(static_cast< int >(textChannel.position));
	formLayout->addRow(tr("Order"), positionSpin);

	QDialogButtonBox *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	if (dialog.exec() != QDialog::Accepted) {
		return false;
	}

	const QString name = nameEdit->text().trimmed();
	if (name.isEmpty()) {
		QMessageBox::warning(this, tr("Text room"), tr("A text room needs a name."));
		return false;
	}

	textChannel.name          = name;
	textChannel.description   = descriptionEdit->text().trimmed();
	textChannel.aclChannelID  = aclChannelCombo->currentData().toUInt();
	textChannel.position      = static_cast< unsigned int >(positionSpin->value());
	return true;
}

void MainWindow::createPersistentTextChannel() {
	if (!canManagePersistentTextChannels()) {
		return;
	}

	PersistentTextChannel textChannel;
	textChannel.aclChannelID = Mumble::ROOT_CHANNEL_ID;
	textChannel.position     = static_cast< unsigned int >(m_persistentTextChannels.size());
	if (!promptForPersistentTextChannel(textChannel, true)) {
		return;
	}

	Global::get().sh->upsertTextChannel(0, textChannel.name, textChannel.description, textChannel.aclChannelID,
										 textChannel.position, true);
}

void MainWindow::editPersistentTextChannel() {
	if (!canManagePersistentTextChannels()) {
		return;
	}

	std::optional< PersistentTextChannel > textChannel = selectedPersistentTextChannel();
	if (!textChannel || textChannel->textChannelID == 0) {
		return;
	}

	PersistentTextChannel updated = *textChannel;
	if (!promptForPersistentTextChannel(updated, false)) {
		return;
	}

	Global::get().sh->upsertTextChannel(updated.textChannelID, updated.name, updated.description, updated.aclChannelID,
										 updated.position, false);
}

void MainWindow::removePersistentTextChannel() {
	if (!canManagePersistentTextChannels()) {
		return;
	}

	std::optional< PersistentTextChannel > textChannel = selectedPersistentTextChannel();
	if (!textChannel || textChannel->textChannelID == 0) {
		return;
	}

	const int result =
		QMessageBox::question(this, tr("Delete text room"),
							  tr("Delete text room #%1? Existing history will no longer be visible in this room.")
								  .arg(textChannel->name.toHtmlEscaped()),
							  QMessageBox::Yes, QMessageBox::No);
	if (result != QMessageBox::Yes) {
		return;
	}

	Global::get().sh->removeTextChannel(textChannel->textChannelID);
}

void MainWindow::editPersistentTextChannelACL() {
	std::optional< PersistentTextChannel > textChannel = selectedPersistentTextChannel();
	if (!textChannel) {
		return;
	}

	Channel *channel = Channel::get(textChannel->aclChannelID);
	if (!channel) {
		return;
	}

	cContextChannel = channel;
	on_qaChannelACL_triggered();
	cContextChannel.clear();
}

void MainWindow::showPersistentTextChannelContextMenu(const QPoint &position) {
	if (!m_persistentChatChannelList || !m_persistentChatChannelMenu) {
		return;
	}

	if (QListWidgetItem *item = m_persistentChatChannelList->itemAt(position)) {
		m_persistentChatChannelList->setCurrentItem(item);
	}

	updatePersistentTextChannelControls();
	m_persistentChatChannelMenu->exec(m_persistentChatChannelList->viewport()->mapToGlobal(position), nullptr);
}

void MainWindow::updatePersistentTextChannelControls() {
	if (!m_persistentChatAddRoomAction) {
		return;
	}

	const bool canManage = canManagePersistentTextChannels();
	const bool hasSelectedTextChannel = selectedPersistentTextChannel().has_value();
	m_persistentChatAddRoomAction->setEnabled(canManage);
	m_persistentChatEditRoomAction->setEnabled(canManage && hasSelectedTextChannel);
	m_persistentChatRemoveRoomAction->setEnabled(canManage && hasSelectedTextChannel);
	m_persistentChatAclRoomAction->setEnabled(canManage && hasSelectedTextChannel);
}

std::size_t MainWindow::cachedPersistentChatUnreadCount(MumbleProto::ChatScope scope, unsigned int scopeID) const {
	const QString key = persistentChatScopeCacheKey(scope, scopeID);
	const auto it     = m_persistentChatUnreadByScope.constFind(key);
	return it == m_persistentChatUnreadByScope.cend() ? 0 : static_cast< std::size_t >(*it);
}

void MainWindow::setCachedPersistentChatUnreadCount(MumbleProto::ChatScope scope, unsigned int scopeID,
													unsigned int lastReadMessageID, std::size_t unreadCount) {
	const QString key = persistentChatScopeCacheKey(scope, scopeID);
	m_persistentChatLastReadByScope.insert(key, lastReadMessageID);
	m_persistentChatUnreadByScope.insert(key, static_cast< int >(unreadCount));
	updatePersistentChatScopeSelectorLabels();
}

std::size_t MainWindow::totalCachedPersistentChatUnreadCount() const {
	std::size_t unreadCount = 0;
	for (auto it = m_persistentChatUnreadByScope.cbegin(); it != m_persistentChatUnreadByScope.cend(); ++it) {
		unreadCount += static_cast< std::size_t >(it.value());
	}

	return unreadCount;
}

bool MainWindow::navigateToPersistentChatScope(MumbleProto::ChatScope scope, unsigned int scopeID) {
	if (!m_persistentChatChannelList) {
		return false;
	}

	for (int i = 0; i < m_persistentChatChannelList->count(); ++i) {
		QListWidgetItem *item = m_persistentChatChannelList->item(i);
		if (!item) {
			continue;
		}

		const MumbleProto::ChatScope itemScope =
			static_cast< MumbleProto::ChatScope >(item->data(PersistentChatScopeRole).toInt());
		const unsigned int itemScopeID = item->data(PersistentChatScopeIDRole).toUInt();
		if (itemScope == scope && itemScopeID == scopeID) {
			setPersistentChatTargetUsesVoiceTree(false);
			if (m_persistentChatChannelList->currentItem() == item) {
				updateChatBar();
				refreshPersistentChatView(true);
			} else {
				m_persistentChatChannelList->setCurrentItem(item);
			}
			return true;
		}
	}

	return false;
}

bool MainWindow::hasPersistentChatCapabilities() const {
	return m_hasPersistentChatSupport || Global::get().bPersistentGlobalChatEnabled
		   || !m_persistentTextChannels.isEmpty();
}

MainWindow::PersistentChatTarget MainWindow::legacyChatTarget() const {
	PersistentChatTarget target;

	if (Global::get().uiSession == 0 || !Global::get().sh || !Global::get().sh->isRunning()) {
		target.label = tr("Not connected");
		return target;
	}

	const ClientUser *self = ClientUser::get(Global::get().uiSession);
	ClientUser *selectedUser = pmModel ? pmModel->getSelectedUser() : nullptr;
	if (selectedUser && selectedUser->uiSession != Global::get().uiSession) {
		target.valid          = true;
		target.directMessage  = true;
		target.legacyTextPath = true;
		target.user           = selectedUser;
		target.label          = selectedUser->qsName;
		target.description    = tr("Classic direct message");
		target.statusMessage =
			tr("This server uses classic direct messages. Incoming messages still appear in the Activity panel.");
		return target;
	}

	Channel *selectedChannel = nullptr;
	if (pmModel) {
		selectedChannel = pmModel->getSelectedChannel();
		if (!selectedChannel && qtvUsers) {
			selectedChannel = pmModel->getChannel(qtvUsers->currentIndex());
		}
	}
	if (!selectedChannel && self) {
		selectedChannel = self->cChannel;
	}

	if (!selectedChannel) {
		target.label = tr("No channel selected");
		return target;
	}

	target.valid          = true;
	target.legacyTextPath = true;
	target.channel        = selectedChannel;
	target.scope          = MumbleProto::Channel;
	target.scopeID        = selectedChannel->iId;
	target.label          = selectedChannel->qsName;
	target.description    = tr("Classic channel chat");
	target.statusMessage =
		tr("This server uses classic channel chat. New messages are still mirrored in the Activity panel.");

	return target;
}

MainWindow::PersistentChatTarget MainWindow::currentPersistentChatTarget() const {
	PersistentChatTarget target;

	if (Global::get().uiSession != 0 && Global::get().sh && Global::get().sh->isRunning()
		&& !hasPersistentChatCapabilities()) {
		return legacyChatTarget();
	}

	if (!m_persistentChatChannelList) {
		target.label = tr("Not connected");
		if (Global::get().uiSession != 0 && Global::get().sh && Global::get().sh->isRunning()) {
			target.label = tr("No conversation selected");
		}
		return target;
	}

	if (m_persistentChatTargetUsesVoiceTree && Global::get().uiSession != 0 && Global::get().sh
		&& Global::get().sh->isRunning()) {
		Channel *selectedChannel = nullptr;
		if (pmModel) {
			selectedChannel = pmModel->getSelectedChannel();
			if (!selectedChannel && qtvUsers) {
				const QModelIndex currentIndex = qtvUsers->currentIndex();
				selectedChannel                = pmModel->getChannel(currentIndex);
				if (!selectedChannel) {
					if (ClientUser *selectedUser = pmModel->getUser(currentIndex); selectedUser) {
						selectedChannel = selectedUser->cChannel;
					}
				}
			}
		}
		if (!selectedChannel) {
			if (const ClientUser *self = ClientUser::get(Global::get().uiSession); self) {
				selectedChannel = self->cChannel;
			}
		}

		if (!selectedChannel) {
			target.label = tr("No room selected");
			return target;
		}

		target.valid       = true;
		target.scope       = MumbleProto::Channel;
		target.scopeID     = selectedChannel->iId;
		target.channel     = selectedChannel;
		target.label       = selectedChannel->qsName;
		target.description = tr("Persistent history for this voice room.");
		return target;
	}

	const QList< QListWidgetItem * > selectedItems = m_persistentChatChannelList->selectedItems();
	const QListWidgetItem *currentItem = m_persistentChatChannelList->currentItem();
	if (selectedItems.size() == 1 && selectedItems.front() != currentItem) {
		currentItem = selectedItems.front();
	}

	if (!currentItem) {
		target.label = tr("Not connected");
		if (Global::get().uiSession != 0 && Global::get().sh && Global::get().sh->isRunning()) {
			target.label = tr("No conversation selected");
		}
		return target;
	}

	const int scopeValue = currentItem->data(PersistentChatScopeRole).toInt();
	if (scopeValue == LocalServerLogScope) {
		target.valid       = true;
		target.readOnly    = true;
		target.serverLog   = true;
		target.label       = tr("Server log");
		target.description = tr("Connection status, notices, and client diagnostics.");
		return target;
	}

	if (Global::get().uiSession == 0 || !Global::get().sh || !Global::get().sh->isRunning()) {
		target.label = tr("Not connected");
		return target;
	}

	const MumbleProto::ChatScope scope = static_cast< MumbleProto::ChatScope >(scopeValue);
	const unsigned int scopeID = currentItem->data(PersistentChatScopeIDRole).toUInt();

	switch (scope) {
		case MumbleProto::TextChannel: {
			const auto it = m_persistentTextChannels.constFind(scopeID);
			if (it == m_persistentTextChannels.cend()) {
				target.label = tr("Text channel is unavailable");
				return target;
			}

			target.valid       = true;
			target.scope       = MumbleProto::TextChannel;
			target.scopeID     = scopeID;
			target.channel     = Channel::get(it->aclChannelID);
			target.label       = tr("#%1").arg(it->name);
			target.description = it->description;
			if (!target.channel) {
				target.readOnly      = true;
				target.statusMessage = tr("This text channel is linked to an unavailable ACL channel.");
			}
			return target;
		}
		case MumbleProto::Channel: {
			target.valid   = true;
			target.scope   = MumbleProto::Channel;
			target.scopeID = scopeID;
			target.channel = Channel::get(scopeID);
			if (target.channel) {
				target.label       = target.channel->qsName;
				target.description = tr("Persistent history for this voice room.");
			} else {
				target.readOnly      = true;
				target.label         = tr("Channel %1").arg(scopeID);
				target.statusMessage = tr("This channel is unavailable.");
			}
			return target;
		}
		case MumbleProto::Aggregate:
			target.valid    = true;
			target.readOnly = true;
			target.scope    = MumbleProto::Aggregate;
			target.scopeID  = 0;
			target.label    = tr("All chats");
			return target;
		case MumbleProto::ServerGlobal:
			target.valid    = true;
			target.scope    = MumbleProto::ServerGlobal;
			target.scopeID  = 0;
			target.label    = tr("Server-wide");
			target.description = tr("Server-wide conversation shared across the instance.");
			if (!Global::get().bPersistentGlobalChatEnabled) {
				target.readOnly      = true;
				target.statusMessage = tr("Global chat is disabled by this server.");
			}
			return target;
		default:
			break;
	}

	target.label = tr("Unknown chat");
	return target;
}

void MainWindow::clearPersistentChatView(const QString &message, const QString &title, const QStringList &hints) {
	const PersistentChatTarget target = currentPersistentChatTarget();
	QString resolvedEyebrow           = tr("Text");
	QString resolvedTitle             = title.trimmed();
	QString resolvedBody              = message.trimmed();
	QStringList resolvedHints         = hints;
	if (resolvedBody.isEmpty()) {
		resolvedBody = tr("Nothing to show yet.");
	}

	if (resolvedTitle.isEmpty()) {
		if (Global::get().uiSession == 0 || !Global::get().sh || !target.valid) {
			resolvedTitle = tr("Start a conversation");
			if (resolvedHints.isEmpty()) {
				resolvedHints << tr("Open Server to connect") << tr("Room chat and history appear here");
			}
		} else if (target.directMessage) {
			resolvedEyebrow = tr("Direct");
			resolvedTitle   = tr("Direct messages");
			if (resolvedHints.isEmpty()) {
				resolvedHints << tr("Classic chat still handles direct messages");
			}
		} else if (target.readOnly) {
			resolvedTitle = tr("Read-only conversation");
			if (resolvedHints.isEmpty()) {
				resolvedHints << tr("Choose another room to reply");
			}
		} else {
			if (target.scope == MumbleProto::ServerGlobal) {
				resolvedEyebrow = tr("Lobby");
			} else if (target.scope == MumbleProto::TextChannel) {
				resolvedEyebrow = tr("Room");
			} else if (target.scope == MumbleProto::Aggregate) {
				resolvedEyebrow = tr("All chats");
			}

			resolvedTitle = target.label.isEmpty() ? tr("Conversation") : target.label;
			if (resolvedHints.isEmpty()) {
				resolvedHints << tr("Messages stay with this room");
			}
		}
	}

	m_persistentChatMessages.clear();
	m_pendingPersistentChatRender.reset();
	m_visiblePersistentChatScope.reset();
	m_visiblePersistentChatScopeID         = 0;
	m_visiblePersistentChatLastReadMessageID = 0;
	m_visiblePersistentChatHasMore         = false;
	m_persistentChatLoadingOlder           = false;
	m_persistentChatPreviewRefreshPending  = false;
	if (m_persistentChatScrollIdleTimer) {
		m_persistentChatScrollIdleTimer->stop();
	}
	clearPersistentChatReplyTarget(false);
	updatePersistentChatScopeSelectorLabels();

	if (!m_persistentChatHistory) {
		return;
	}

	const QSignalBlocker historySignalBlocker(m_persistentChatHistory);
	const QSignalBlocker historyScrollSignalBlocker(m_persistentChatHistory->verticalScrollBar());
	m_persistentChatHistory->clear();

	const int viewportHeight = std::max(220, m_persistentChatHistory->viewport()->height() - 4);
	QWidget *widget         = createPersistentChatStateWidget(resolvedEyebrow, resolvedTitle, resolvedBody, resolvedHints,
															  m_persistentChatHistory, viewportHeight);

	QListWidgetItem *item = new QListWidgetItem(m_persistentChatHistory);
	item->setFlags(Qt::NoItemFlags);
	m_persistentChatHistory->addItem(item);
	m_persistentChatHistory->setItemWidget(item, widget);
	item->setSizeHint(
		persistentChatMeasuredItemHint(widget, std::max(0, m_persistentChatHistory->viewport()->width())));
	m_persistentChatHistory->verticalScrollBar()->setValue(0);
}

std::optional< QString > MainWindow::persistentChatPreviewKey(const MumbleProto::ChatMessage &message) const {
	if (!Global::get().s.bEnableLinkPreviews) {
		return std::nullopt;
	}

	if (const std::optional< MumbleProto::ChatEmbedRef > embed = persistentChatPrimaryEmbed(message); embed) {
		return QString::fromLatin1("embed:%1")
			.arg(QString::fromUtf8(QUrl::toPercentEncoding(u8(embed->canonical_url()))));
	}

	QString messageContent =
		message.has_body_text() ? persistentChatMessageBodyHtml(message) : persistentChatMessageRawBody(message);
	if (!message.has_body_text()) {
		extractPersistentChatReplyReference(messageContent, &messageContent);
	}
	const QList< QUrl > urls  = extractPreviewableUrls(messageContent);
	for (const QUrl &url : urls) {
		if (const std::optional< QString > videoId = extractYouTubeVideoId(url); videoId) {
			return QString::fromLatin1("youtube:%1").arg(*videoId);
		}
		if (isYouTubeHost(url.host())) {
			// Only create custom previews for actual videos. Generic previews for YouTube
			// landing pages are noisy, expensive, and were unstable in Windows testing.
			continue;
		}

		if (isDirectImageUrl(url)) {
			return QString::fromLatin1("image:%1").arg(normalizedPreviewUrl(url));
		}

		return QString::fromLatin1("url:%1").arg(normalizedPreviewUrl(url));
	}

	return std::nullopt;
}

std::optional< MumbleProto::ChatEmbedRef > MainWindow::persistentChatPrimaryEmbed(const MumbleProto::ChatMessage &message) const {
	for (int i = 0; i < message.embeds_size(); ++i) {
		const MumbleProto::ChatEmbedRef &embed = message.embeds(i);
		if (embed.has_canonical_url() && !u8(embed.canonical_url()).trimmed().isEmpty()) {
			return embed;
		}
	}

	return std::nullopt;
}

void MainWindow::ensurePersistentChatPreview(const QString &previewKey) {
	if (previewKey.isEmpty()) {
		return;
	}

	if (m_persistentChatPreviews.contains(previewKey)) {
		return;
	}

	const auto renderIfVisible = [this, previewKey]() {
		QPointer< MainWindow > guardedThis(this);
		QMetaObject::invokeMethod(
			this,
			[guardedThis, previewKey]() {
				if (!guardedThis) {
					return;
				}

				guardedThis->updatePersistentChatPreviewViewIfVisible(previewKey);
			},
			Qt::QueuedConnection);
	};

	PersistentChatPreview preview;
	preview.openLabel = tr("Open link");

	if (previewKey.startsWith(QLatin1String("embed:"))) {
		const QString canonicalUrl =
			QUrl::fromPercentEncoding(previewKey.mid(QStringLiteral("embed:").size()).toUtf8());
		const QUrl previewUrl(canonicalUrl);
		if (!previewUrl.isValid()) {
			return;
		}

		std::optional< MumbleProto::ChatEmbedRef > embed;
		for (const MumbleProto::ChatMessage &message : m_persistentChatMessages) {
			for (int i = 0; i < message.embeds_size(); ++i) {
				const MumbleProto::ChatEmbedRef &currentEmbed = message.embeds(i);
				if (currentEmbed.has_canonical_url() && u8(currentEmbed.canonical_url()) == canonicalUrl) {
					embed = currentEmbed;
					break;
				}
			}
			if (embed) {
				break;
			}
		}

		if (!embed) {
			return;
		}

		const MumbleProto::ChatEmbedRef &resolvedEmbed = *embed;
		const MumbleProto::ChatEmbedStatus status =
			resolvedEmbed.has_status() ? resolvedEmbed.status() : MumbleProto::ChatEmbedStatusPending;
		preview.canonicalUrl = canonicalUrl;
		preview.title =
			resolvedEmbed.has_title() && !u8(resolvedEmbed.title()).trimmed().isEmpty()
				? u8(resolvedEmbed.title())
				: previewDisplayHost(previewUrl);
		preview.subtitle =
			resolvedEmbed.has_site_name() && !u8(resolvedEmbed.site_name()).trimmed().isEmpty()
				? u8(resolvedEmbed.site_name())
				: previewDisplayHost(previewUrl);
		preview.description =
			resolvedEmbed.has_description() ? u8(resolvedEmbed.description()) : QString();
		preview.previewAssetID = resolvedEmbed.has_preview_asset_id() ? resolvedEmbed.preview_asset_id() : 0;
		preview.metadataFinished = status != MumbleProto::ChatEmbedStatusPending;
		preview.thumbnailFinished = preview.previewAssetID == 0;
		preview.failed = status == MumbleProto::ChatEmbedStatusBlocked || status == MumbleProto::ChatEmbedStatusFailed;

		if (status == MumbleProto::ChatEmbedStatusBlocked && preview.description.isEmpty()) {
			preview.description =
				tr("Automatic previews are disabled for localhost and private-network targets.");
		} else if (status == MumbleProto::ChatEmbedStatusFailed && preview.description.isEmpty()) {
			preview.description = tr("Preview unavailable");
		}

		m_persistentChatPreviews.insert(previewKey, preview);
		if (preview.previewAssetID > 0) {
			ensurePersistentChatPreviewAssetDownload(preview.previewAssetID, previewKey);
		} else {
			renderIfVisible();
		}
		return;
	}

	if (previewKey.startsWith(QLatin1String("youtube:"))) {
		const QString videoId = previewKey.mid(QStringLiteral("youtube:").size());
		preview.canonicalUrl  = QString::fromLatin1("https://www.youtube.com/watch?v=%1").arg(videoId);
		preview.title         = tr("Loading YouTube preview...");
		preview.subtitle      = tr("Fetching title and thumbnail");
		preview.openLabel     = tr("Open on YouTube");
		m_persistentChatPreviews.insert(previewKey, preview);

		QUrl oembedUrl(QLatin1String("https://www.youtube.com/oembed"));
		QUrlQuery oembedQuery;
		oembedQuery.addQueryItem(QLatin1String("url"),
								 QString::fromLatin1("https://www.youtube.com/watch?v=%1").arg(videoId));
		oembedQuery.addQueryItem(QLatin1String("format"), QLatin1String("json"));
		oembedUrl.setQuery(oembedQuery);

		QNetworkRequest oembedRequest(oembedUrl);
		Network::prepareRequest(oembedRequest);
		QNetworkReply *oembedReply = Global::get().nam->get(oembedRequest);
		applyPreviewReplyGuards(oembedReply, PREVIEW_MAX_PAGE_BYTES);
		connect(oembedReply, &QNetworkReply::finished, this, [this, oembedReply, previewKey, renderIfVisible]() {
			const QByteArray response = oembedReply->readAll();
			const bool success        = oembedReply->error() == QNetworkReply::NoError;
			const QString failureText = previewFailureText(oembedReply);
			oembedReply->deleteLater();

			auto it = m_persistentChatPreviews.find(previewKey);
			if (it == m_persistentChatPreviews.end()) {
				return;
			}

			it->metadataFinished = true;

			if (success) {
				QJsonParseError error;
				const QJsonDocument document = QJsonDocument::fromJson(response, &error);
				if (error.error == QJsonParseError::NoError && document.isObject()) {
					const QJsonObject object = document.object();
					const QString title      = object.value(QLatin1String("title")).toString().trimmed();
					const QString author     = object.value(QLatin1String("author_name")).toString().trimmed();
					it->title = title.isEmpty() ? tr("YouTube video") : title;
					it->subtitle = author.isEmpty() ? tr("YouTube") : tr("YouTube by %1").arg(author);
					renderIfVisible();
					return;
				}
			}

			if (it->title == tr("Loading YouTube preview...")) {
				it->title = tr("YouTube video");
			}
			if (it->subtitle.isEmpty() || it->subtitle == tr("Fetching title and thumbnail")) {
				it->subtitle = failureText;
			}

			if (it->thumbnailFinished && it->thumbnailImage.isNull()) {
				it->failed = true;
			}
			renderIfVisible();
		});

		QUrl thumbnailUrl(QString::fromLatin1("https://i.ytimg.com/vi/%1/hqdefault.jpg").arg(videoId));
		QNetworkRequest thumbnailRequest(thumbnailUrl);
		Network::prepareRequest(thumbnailRequest);
		QNetworkReply *thumbnailReply = Global::get().nam->get(thumbnailRequest);
		applyPreviewReplyGuards(thumbnailReply, PREVIEW_MAX_IMAGE_BYTES);
		connect(thumbnailReply, &QNetworkReply::finished, this,
				[this, thumbnailReply, previewKey, renderIfVisible]() {
					const QByteArray data = thumbnailReply->readAll();
					const bool success    = thumbnailReply->error() == QNetworkReply::NoError;
					const QString failureText = previewFailureText(thumbnailReply);
					thumbnailReply->deleteLater();

					auto it = m_persistentChatPreviews.find(previewKey);
					if (it == m_persistentChatPreviews.end()) {
						return;
					}

					it->thumbnailFinished = true;

					if (success) {
						const QImage image = decodePersistentChatThumbnailImage(data);
						if (!image.isNull()) {
							it->thumbnailImage = persistentChatThumbnailImage(image);
							renderIfVisible();
							return;
						}
					}

					if (it->metadataFinished && it->title == tr("Loading YouTube preview...")) {
						it->title = tr("YouTube video");
					}
					if (it->metadataFinished && it->subtitle.isEmpty()) {
						it->subtitle = failureText;
					}
					if (it->metadataFinished && it->thumbnailImage.isNull()) {
						it->failed = true;
					}
					renderIfVisible();
				});
		return;
	}

	const bool isImagePreview = previewKey.startsWith(QLatin1String("image:"));
	const QString encodedUrl =
		previewKey.mid(isImagePreview ? QStringLiteral("image:").size() : QStringLiteral("url:").size());
	const QUrl previewUrl = QUrl::fromEncoded(encodedUrl.toUtf8());
	if (!previewUrl.isValid()) {
		return;
	}

	preview.canonicalUrl = previewUrl.toString();
	preview.subtitle     = previewDisplayHost(previewUrl);
	preview.openLabel    = isImagePreview ? tr("Open image") : tr("Open link");

	if (!isSafePreviewTarget(previewUrl)) {
		preview.title            = isImagePreview ? tr("Image preview blocked") : tr("Link preview blocked");
		preview.description      = tr("Automatic previews are disabled for localhost and private-network targets.");
		preview.metadataFinished = true;
		preview.thumbnailFinished = true;
		m_persistentChatPreviews.insert(previewKey, preview);
		renderIfVisible();
		return;
	}

	if (isImagePreview) {
		const QString fileName = QFileInfo(previewUrl.path()).fileName();
		preview.title          = fileName.isEmpty() ? tr("Image preview") : fileName;
		preview.description    = tr("Direct image preview");
		m_persistentChatPreviews.insert(previewKey, preview);

		QNetworkRequest imageRequest(previewUrl);
		Network::prepareRequest(imageRequest);
		QNetworkReply *imageReply = Global::get().nam->get(imageRequest);
		applyPreviewReplyGuards(imageReply, PREVIEW_MAX_IMAGE_BYTES);
		connect(imageReply, &QNetworkReply::finished, this, [this, imageReply, previewKey, renderIfVisible]() {
			const QByteArray data = imageReply->readAll();
			const bool success    = imageReply->error() == QNetworkReply::NoError;
			const QString failureText = previewFailureText(imageReply);
			imageReply->deleteLater();

			auto it = m_persistentChatPreviews.find(previewKey);
			if (it == m_persistentChatPreviews.end()) {
				return;
			}

			it->metadataFinished  = true;
			it->thumbnailFinished = true;

			if (success) {
				const QImage image = decodePersistentChatThumbnailImage(data);
				if (!image.isNull()) {
					it->thumbnailImage = persistentChatThumbnailImage(image);
					renderIfVisible();
					return;
				}
			}

			it->failed = true;
			it->description = failureText;
			renderIfVisible();
		});
		return;
	}

	preview.title       = tr("Loading link preview...");
	preview.description = tr("Fetching page metadata");
	m_persistentChatPreviews.insert(previewKey, preview);

	QNetworkRequest pageRequest(previewUrl);
	Network::prepareRequest(pageRequest);
	QNetworkReply *pageReply = Global::get().nam->get(pageRequest);
	applyPreviewReplyGuards(pageReply, PREVIEW_MAX_PAGE_BYTES);
	connect(pageReply, &QNetworkReply::finished, this, [this, pageReply, previewKey, previewUrl, renderIfVisible]() {
		const QByteArray data = pageReply->readAll();
		const bool success    = pageReply->error() == QNetworkReply::NoError;
		const QString contentType = pageReply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
		const QString failureText = previewFailureText(pageReply);
		pageReply->deleteLater();

		auto it = m_persistentChatPreviews.find(previewKey);
		if (it == m_persistentChatPreviews.end()) {
			return;
		}

		it->metadataFinished = true;

		if (!success || (!contentType.contains(QLatin1String("html")) && !contentType.isEmpty()
						 && !contentType.contains(QLatin1String("xml")))) {
			it->thumbnailFinished = true;
			if (it->title == tr("Loading link preview...")) {
				it->title = previewDisplayHost(previewUrl);
			}
			it->description = success ? tr("Preview unavailable") : failureText;
			it->failed      = !success;
			renderIfVisible();
			return;
		}

		const QString html = QString::fromUtf8(data);
		const QHash< QString, QString > metaTags = extractMetaTags(html);
		const QString title = metaTags.value(QLatin1String("og:title"),
											 metaTags.value(QLatin1String("twitter:title"), extractHtmlTitle(html)));
		const QString description =
			metaTags.value(QLatin1String("og:description"),
						   metaTags.value(QLatin1String("twitter:description"),
										  metaTags.value(QLatin1String("description"))));
		const QString siteName =
			metaTags.value(QLatin1String("og:site_name"), previewDisplayHost(previewUrl));
		const QString imageUrlString =
			metaTags.value(QLatin1String("og:image"), metaTags.value(QLatin1String("twitter:image")));

		it->title = title.isEmpty() ? previewDisplayHost(previewUrl) : title;
		it->subtitle = siteName.isEmpty() ? previewDisplayHost(previewUrl) : siteName;
		it->description = description;

		if (imageUrlString.isEmpty()) {
			it->thumbnailFinished = true;
			renderIfVisible();
			return;
		}

		const QUrl imageUrl = previewUrl.resolved(QUrl(imageUrlString));
		if (!isSafePreviewTarget(imageUrl)) {
			it->thumbnailFinished = true;
			renderIfVisible();
			return;
		}

		QNetworkRequest imageRequest(imageUrl);
		Network::prepareRequest(imageRequest);
		QNetworkReply *imageReply = Global::get().nam->get(imageRequest);
		applyPreviewReplyGuards(imageReply, PREVIEW_MAX_IMAGE_BYTES);
		connect(imageReply, &QNetworkReply::finished, this, [this, imageReply, previewKey, renderIfVisible]() {
			const QByteArray imageData = imageReply->readAll();
			const bool imageSuccess    = imageReply->error() == QNetworkReply::NoError;
			const QString failureText = previewFailureText(imageReply);
			imageReply->deleteLater();

			auto it = m_persistentChatPreviews.find(previewKey);
			if (it == m_persistentChatPreviews.end()) {
				return;
			}

			it->thumbnailFinished = true;

			if (imageSuccess) {
				const QImage image = decodePersistentChatThumbnailImage(imageData);
				if (!image.isNull()) {
					it->thumbnailImage = persistentChatThumbnailImage(image);
					renderIfVisible();
					return;
				}
			}

			if (it->description.isEmpty() || it->description == tr("Fetching page metadata")) {
				it->description = failureText;
			}
			renderIfVisible();
		});
		renderIfVisible();
	});
}

void MainWindow::ensurePersistentChatPreviewAssetDownload(unsigned int assetID, const QString &previewKey) {
	if (assetID == 0 || previewKey.isEmpty() || !Global::get().sh || !Global::get().sh->isRunning()) {
		return;
	}

	auto it = m_persistentChatAssetDownloads.find(assetID);
	if (it == m_persistentChatAssetDownloads.end()) {
		PersistentChatAssetDownload download;
		download.assetID = assetID;
		download.previewKeys.insert(previewKey);
		it = m_persistentChatAssetDownloads.insert(assetID, download);
	} else {
		it->previewKeys.insert(previewKey);
		if (it->totalSize > 0 && static_cast< quint64 >(it->bytes.size()) >= it->totalSize) {
			return;
		}
	}

	MumbleProto::ChatAssetRequest request;
	request.set_asset_id(assetID);
	request.set_offset(it->nextOffset);
	request.set_max_bytes(262144);
	Global::get().sh->sendMessage(request);
}

int MainWindow::persistentChatPreviewContentWidth(int leftPadding) const {
	if (!m_persistentChatHistory) {
		return 320;
	}

	const int viewportWidth      = m_persistentChatHistory->viewport()->width();
	const int previewSpacerWidth = 28;
	const int cardPadding        = 12;
	const int rightSafetyMargin  = 12;
	int availableWidth = viewportWidth - leftPadding - previewSpacerWidth - cardPadding - rightSafetyMargin;
	availableWidth     = std::max(160, std::min(PERSISTENT_CHAT_PREVIEW_CARD_MAX_WIDTH, availableWidth));
	if (availableWidth <= 160) {
		return 160;
	}

	const int quantizedWidth =
		160 + (((availableWidth - 160) / PERSISTENT_CHAT_PREVIEW_WIDTH_STEP) * PERSISTENT_CHAT_PREVIEW_WIDTH_STEP);
	return std::max(160, quantizedWidth);
}

QString MainWindow::persistentChatPreviewHtml(const QString &previewKey, int availableWidth) const {
	const auto it = m_persistentChatPreviews.constFind(previewKey);
	if (it == m_persistentChatPreviews.cend()) {
		return QString();
	}

	const PersistentChatPreview &preview = it.value();
	const QString cardUrl                = preview.canonicalUrl.toHtmlEscaped();
	const QString title =
		(preview.title.isEmpty() ? tr("Link preview") : preview.title).toHtmlEscaped();
	const QString subtitle = preview.subtitle.toHtmlEscaped();
	const QString description = preview.description.toHtmlEscaped();
	const QString openLabel =
		(preview.openLabel.isEmpty() ? tr("Open link") : preview.openLabel).toHtmlEscaped();
	QString detailsHtml;
	const int cardWidth = std::max(160, availableWidth);

	if (!preview.thumbnailImage.isNull()) {
		detailsHtml +=
			persistentChatThumbnailHtml(previewKey, preview.thumbnailImage, std::max(96, cardWidth - 12));
		detailsHtml += QString::fromLatin1("<br/>");
	}

	if (!subtitle.isEmpty()) {
		detailsHtml +=
			QString::fromLatin1("<span class='persistent-chat-preview-subtitle'>%1</span><br/>").arg(subtitle);
	}

	if (!description.isEmpty()) {
		detailsHtml += QString::fromLatin1("%1<br/>").arg(description);
	}

	if (!preview.metadataFinished || !preview.thumbnailFinished) {
		detailsHtml += QString::fromLatin1("<span class='persistent-chat-preview-status'>%1</span><br/>")
						   .arg(tr("Loading preview...").toHtmlEscaped());
	} else if (preview.failed && description.isEmpty()) {
		detailsHtml += QString::fromLatin1("<span class='persistent-chat-preview-status'>%1</span><br/>")
						   .arg(tr("Preview unavailable").toHtmlEscaped());
	}

	return QString::fromLatin1(
			   "<table cellspacing='0' cellpadding='0' style='border-collapse:collapse; border:none; background:transparent;'>"
			   "<tr><td width='28' style='border:none; background:transparent;'></td>"
			   "<td style='border:none; background:transparent;'>"
			   "<table cellspacing='0' cellpadding='0' width='%5' style='border-collapse:collapse; border:none; background:transparent;'>"
			   "<tr><td class='persistent-chat-preview-card' width='%5' "
			   "style='border:none; background:transparent; padding:6px;'>"
			   "<a href=\"%1\"><strong>%2</strong></a><br/>%3"
			   "<a href=\"%1\">%4</a></td></tr></table>"
			   "</td></tr></table>")
		.arg(cardUrl, title, detailsHtml, openLabel)
		.arg(cardWidth);
}

void MainWindow::updatePersistentChatPreviewViewIfVisible(const QString &previewKey) {
	for (const MumbleProto::ChatMessage &message : m_persistentChatMessages) {
		if (const std::optional< QString > messagePreviewKey = persistentChatPreviewKey(message);
			messagePreviewKey && *messagePreviewKey == previewKey) {
			if (m_persistentChatScrollIdleTimer && m_persistentChatScrollIdleTimer->isActive()) {
				m_persistentChatPreviewRefreshPending = true;
				return;
			}
			const bool wasAtBottom = m_persistentChatHistory ? m_persistentChatHistory->isScrolledToBottom() : true;
			renderPersistentChatView(QString(), wasAtBottom, !wasAtBottom);
			return;
		}
	}
}

void MainWindow::renderPersistentChatView(const QString &statusMessage, bool scrollToBottom,
										  bool preserveScrollPosition) {
	m_pendingPersistentChatRender = PersistentChatRenderRequest { statusMessage, scrollToBottom, preserveScrollPosition };
	if (m_persistentChatRenderQueued) {
		return;
	}

	m_persistentChatRenderQueued = true;
	QPointer< MainWindow > guardedThis(this);
	QMetaObject::invokeMethod(
		this,
		[guardedThis]() {
			if (!guardedThis) {
				return;
			}

			guardedThis->flushPersistentChatRender();
		},
		Qt::QueuedConnection);
}

void MainWindow::flushPersistentChatRender() {
	m_persistentChatRenderQueued = false;
	if (!m_pendingPersistentChatRender) {
		return;
	}

	const PersistentChatRenderRequest request = *m_pendingPersistentChatRender;
	m_pendingPersistentChatRender.reset();
	renderPersistentChatViewImmediately(request.statusMessage, request.scrollToBottom,
										request.preserveScrollPosition);
}

void MainWindow::renderPersistentChatViewImmediately(const QString &statusMessage, bool scrollToBottom,
													 bool preserveScrollPosition) {
	if (!m_persistentChatHistory) {
		return;
	}
	m_lastPersistentChatViewportWidth = m_persistentChatHistory->viewport()->width();
	const bool enforceBottomLock =
		m_persistentChatBottomLockRendersRemaining > 0 && !m_persistentChatLoadingOlder;
	if (enforceBottomLock) {
		scrollToBottom = true;
		preserveScrollPosition = false;
	}

	const QSignalBlocker historySignalBlocker(m_persistentChatHistory);
	const QSignalBlocker historyScrollSignalBlocker(m_persistentChatHistory->verticalScrollBar());
	const PersistentChatTarget target = currentPersistentChatTarget();
	const bool showInlinePreviews =
		Global::get().s.bEnableLinkPreviews && target.scope != MumbleProto::Aggregate;
	QSet< QString > previewKeysToEnsure;
	for (const MumbleProto::ChatMessage &message : m_persistentChatMessages) {
		if ((message.has_deleted_at() && message.deleted_at() > 0) || !showInlinePreviews) {
			continue;
		}

		if (const std::optional< QString > previewKey = persistentChatPreviewKey(message);
			previewKey && !m_persistentChatPreviews.contains(*previewKey)) {
			previewKeysToEnsure.insert(*previewKey);
		}
	}

	for (auto it = previewKeysToEnsure.cbegin(); it != previewKeysToEnsure.cend(); ++it) {
		ensurePersistentChatPreview(*it);
	}

	const int oldScrollValue = m_persistentChatHistory->verticalScrollBar()->value();
	const int oldScrollMax   = m_persistentChatHistory->verticalScrollBar()->maximum();
	auto historyItemContentRect = [this](QListWidgetItem *item) {
		if (!item || !m_persistentChatHistory) {
			return QRect();
		}

		if (QWidget *itemWidget = m_persistentChatHistory->itemWidget(item)) {
			const QRect widgetRect = itemWidget->geometry();
			if (widgetRect.isValid() && !widgetRect.isEmpty()) {
				return widgetRect;
			}
		}

		return m_persistentChatHistory->visualItemRect(item);
	};
	unsigned int anchorThreadID  = 0;
	unsigned int anchorMessageID = 0;
	int anchorTopOffset          = 0;
	bool hasMessageAnchor        = false;
	if (preserveScrollPosition) {
		for (int i = 0; i < m_persistentChatHistory->count(); ++i) {
			QListWidgetItem *item = m_persistentChatHistory->item(i);
			if (!item) {
				continue;
			}

			const QRect itemRect = historyItemContentRect(item);
			if (itemRect.bottom() <= 0) {
				continue;
			}

			if (auto *groupWidget =
					qobject_cast< PersistentChatMessageGroupWidget * >(m_persistentChatHistory->itemWidget(item))) {
				unsigned int messageID = 0;
				unsigned int threadID  = 0;
				int bubbleTopOffset    = 0;
				if (!groupWidget->bubbleAnchorAtOffset(std::max(0, -itemRect.top()), messageID, threadID, bubbleTopOffset)
					|| threadID == 0 || messageID == 0) {
					continue;
				}

				anchorThreadID   = threadID;
				anchorMessageID  = messageID;
				anchorTopOffset  = itemRect.top() + bubbleTopOffset;
				hasMessageAnchor = true;
				break;
			}

			const unsigned int threadID  = item->data(PersistentChatThreadRole).toUInt();
			const unsigned int messageID = item->data(PersistentChatMessageIDRole).toUInt();
			if (threadID == 0 || messageID == 0) {
				continue;
			}

			anchorThreadID   = threadID;
			anchorMessageID  = messageID;
			anchorTopOffset  = itemRect.top();
			hasMessageAnchor = true;
			break;
		}
	}

	m_persistentChatHistory->clear();
	const int contentWidth = std::max(240, m_persistentChatHistory->viewport()->width() - 18);

	auto addListWidget = [this](QWidget *widget) {
		if (!widget) {
			return;
		}

		widget->setParent(m_persistentChatHistory);
		widget->setAttribute(Qt::WA_StyledBackground, true);
		if (m_persistentChatContainer) {
			widget->setStyleSheet(m_persistentChatContainer->styleSheet());
			widget->style()->unpolish(widget);
			widget->style()->polish(widget);
		}
		widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		QListWidgetItem *item = new QListWidgetItem();
		item->setFlags(Qt::NoItemFlags);
		const int itemWidth = std::max(0, m_persistentChatHistory->viewport()->width());
		const QVariant threadID = widget->property("persistentChatThreadID");
		if (threadID.isValid()) {
			item->setData(PersistentChatThreadRole, threadID);
		}
		const QVariant messageID = widget->property("persistentChatMessageID");
		if (messageID.isValid()) {
			item->setData(PersistentChatMessageIDRole, messageID);
		}
		m_persistentChatHistory->addItem(item);
		m_persistentChatHistory->setItemWidget(item, widget);
		item->setSizeHint(persistentChatMeasuredItemHint(widget, itemWidth));
		if (auto *groupWidget = qobject_cast< PersistentChatMessageGroupWidget * >(widget)) {
			connect(groupWidget, &PersistentChatMessageGroupWidget::measuredHeightChanged, this,
					[this, groupWidget](int) {
						if (!m_persistentChatHistory || !groupWidget) {
							return;
						}

						const int currentItemWidth = std::max(0, m_persistentChatHistory->viewport()->width());
						for (int i = 0; i < m_persistentChatHistory->count(); ++i) {
							QListWidgetItem *existingItem = m_persistentChatHistory->item(i);
							if (!existingItem || m_persistentChatHistory->itemWidget(existingItem) != groupWidget) {
								continue;
							}

							existingItem->setSizeHint(persistentChatMeasuredItemHint(groupWidget, currentItemWidth));
							break;
						}

						m_persistentChatHistory->doItemsLayout();
						m_persistentChatHistory->stabilizeVisibleContent();
					});
		}
	};

	auto createTextBrowser =
		[this](const QString &html, int width, const QVector< QPair< QUrl, QImage > > &imageResources = {}) {
		auto *browser = new LogTextBrowser();
		auto *document = new LogDocument(browser);
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
		browser->resetViewportChrome();
		browser->installEventFilter(this);
		if (QWidget *viewport = browser->viewport()) {
			viewport->setProperty("persistentChatEmbeddedBrowser", true);
			viewport->installEventFilter(this);
		}
		configurePersistentChatDocument(document, qApp->styleSheet());
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
		connect(browser, &LogTextBrowser::anchorClicked, this, &MainWindow::on_qteLog_anchorClicked);
		connect(browser, QOverload< const QUrl & >::of(&QTextBrowser::highlighted), this,
				&MainWindow::on_qteLog_highlighted);
		connect(browser, &LogTextBrowser::customContextMenuRequested, this,
				[this, browser](const QPoint &position) { showLogContextMenu(browser, position); });
		connect(browser, &LogTextBrowser::imageActivated, this,
				[this, browser](const QTextCursor &cursor) { openImageDialog(browser, cursor); });
		return browser;
	};

	auto createPill = [&](const QString &html, const QString &objectName) {
		QLabel *label = new QLabel();
		label->setObjectName(objectName);
		label->setAttribute(Qt::WA_StyledBackground, true);
		label->setTextFormat(Qt::RichText);
		label->setWordWrap(true);
		label->setAlignment(Qt::AlignCenter);
		label->setText(html);
		label->setContentsMargins(20, 6, 20, 6);
		return label;
	};

	auto addStateCard = [&](const QString &eyebrow, const QString &title, const QString &body, const QStringList &hints) {
		const int viewportHeight = std::max(220, m_persistentChatHistory->viewport()->height() - 4);
		addListWidget(createPersistentChatStateWidget(eyebrow, title, body, hints, m_persistentChatHistory, viewportHeight));
	};

	auto buildRenderGroups = [&](std::size_t beginIndex, std::size_t endIndex,
								 const PersistentChatRender::SelfIdentity &selfIdentity) {
		std::vector< MumbleProto::ChatMessage > slice(m_persistentChatMessages.begin() + beginIndex,
													  m_persistentChatMessages.begin() + endIndex);
		auto groups = PersistentChatRender::buildGroups(slice, selfIdentity);
		for (PersistentChatRender::PersistentChatRenderGroup &group : groups) {
			for (PersistentChatRender::PersistentChatRenderBubble &bubble : group.bubbles) {
				bubble.messageIndex += static_cast< int >(beginIndex);
			}
		}
		return groups;
	};

	auto connectGroupWidget = [this](PersistentChatMessageGroupWidget *groupWidget) {
		connect(groupWidget, &PersistentChatMessageGroupWidget::replyRequested, this, [this](unsigned int messageID) {
			if (const MumbleProto::ChatMessage *message = findPersistentChatMessageByID(m_persistentChatMessages, messageID)) {
				setPersistentChatReplyTarget(*message);
			}
		});
		connect(groupWidget, &PersistentChatMessageGroupWidget::scopeJumpRequested, this,
				[this](MumbleProto::ChatScope scope, unsigned int scopeID) { navigateToPersistentChatScope(scope, scopeID); });
		connect(groupWidget, &PersistentChatMessageGroupWidget::logContextMenuRequested, this,
				[this](LogTextBrowser *browser, const QPoint &position) { showLogContextMenu(browser, position); });
		connect(groupWidget, &PersistentChatMessageGroupWidget::logImageActivated, this,
				[this](LogTextBrowser *browser, const QTextCursor &cursor) { openImageDialog(browser, cursor); });
		connect(groupWidget, &PersistentChatMessageGroupWidget::anchorClicked, this, &MainWindow::on_qteLog_anchorClicked);
		connect(groupWidget, &PersistentChatMessageGroupWidget::highlighted, this,
				QOverload< const QUrl & >::of(&MainWindow::on_qteLog_highlighted));
	};

	if (m_persistentChatMessages.empty()) {
		QString eyebrow = tr("Text");
		QString title;
		QString body;
		QStringList hints;
		if (target.scope == MumbleProto::ServerGlobal) {
			eyebrow = tr("Lobby");
		} else if (target.scope == MumbleProto::TextChannel) {
			eyebrow = tr("Room");
		} else if (target.scope == MumbleProto::Aggregate) {
			eyebrow = tr("All chats");
		}

		if (!statusMessage.isEmpty()) {
			title = target.label.isEmpty() ? tr("Loading conversation") : tr("Loading %1").arg(target.label);
			body  = statusMessage;
			hints << tr("Fetching recent messages and read state");
		} else if (target.readOnly && !target.statusMessage.trimmed().isEmpty()) {
			title = tr("Read-only conversation");
			body  = target.statusMessage;
			hints << tr("Choose another room to reply");
		} else if (target.scope == MumbleProto::Aggregate) {
			title = tr("Nothing here yet");
			body  = tr("All rooms only shows conversations you can currently read.");
			hints << tr("Unread state stays with each room");
		} else {
			title = tr("Nothing here yet");
			body  = target.readOnly ? tr("No accessible messages yet.") : tr("No messages in %1 yet.").arg(target.label);
			if (target.readOnly) {
				hints << tr("Choose another room to reply");
			} else {
				hints << tr("Start the first message below");
				hints << tr("Messages stay with this room");
			}
		}

		addStateCard(eyebrow, title, body, hints);
	} else {
		if (!statusMessage.isEmpty()) {
			addListWidget(createPill(QString::fromLatin1("<em>%1</em>").arg(statusMessage.toHtmlEscaped()),
									 QLatin1String("qlPersistentChatStatusPill")));
		}

		if (m_persistentChatLoadingOlder) {
			addListWidget(createPill(QString::fromLatin1("<em>%1</em>")
										 .arg(tr("Loading older messages...").toHtmlEscaped()),
									 QLatin1String("qlPersistentChatLoadingPill")));
		} else if (m_visiblePersistentChatHasMore) {
			QPushButton *button = new QPushButton(tr("Load older messages"));
			button->setObjectName(QLatin1String("qpbPersistentChatLoadOlder"));
			button->setFlat(true);
			connect(button, &QPushButton::clicked, this, &MainWindow::requestOlderPersistentChatHistory);
			addListWidget(button);
		}

		if (target.readOnly && !target.statusMessage.isEmpty()) {
			addListWidget(createPill(QString::fromLatin1("<em>%1</em>").arg(target.statusMessage.toHtmlEscaped()),
									 QLatin1String("qlPersistentChatInfoPill")));
		}

		if (target.scope == MumbleProto::Aggregate) {
			addListWidget(createPill(
				QString::fromLatin1("<em>%1</em>")
					.arg(tr("All chats is read-only and does not track per-thread read state.").toHtmlEscaped()),
				QLatin1String("qlPersistentChatAggregateNotice")));
		}

		const std::size_t unreadCount = persistentChatUnreadCount();
		std::optional< std::size_t > firstUnreadIndex;
		if (unreadCount > 0 && target.scope != MumbleProto::Aggregate) {
			for (std::size_t i = 0; i < m_persistentChatMessages.size(); ++i) {
				if (m_persistentChatMessages[i].message_id() > m_visiblePersistentChatLastReadMessageID) {
					firstUnreadIndex = i;
					break;
				}
			}
		}

		PersistentChatRender::SelfIdentity selfIdentity;
		selfIdentity.session = Global::get().uiSession;
		if (const ClientUser *self = ClientUser::get(Global::get().uiSession)) {
			selfIdentity.userID = self->iId;
			selfIdentity.name   = self->qsName;
		}

		std::vector< PersistentChatRender::PersistentChatRenderGroup > renderGroups;
		std::optional< std::size_t > unreadGroupBoundary;
		if (firstUnreadIndex && *firstUnreadIndex > 0 && *firstUnreadIndex < m_persistentChatMessages.size()) {
			auto readGroups = buildRenderGroups(0, *firstUnreadIndex, selfIdentity);
			renderGroups.insert(renderGroups.end(), readGroups.begin(), readGroups.end());
			unreadGroupBoundary = renderGroups.size();
			auto unreadGroups = buildRenderGroups(*firstUnreadIndex, m_persistentChatMessages.size(), selfIdentity);
			renderGroups.insert(renderGroups.end(), unreadGroups.begin(), unreadGroups.end());
		} else {
			renderGroups = buildRenderGroups(0, m_persistentChatMessages.size(), selfIdentity);
		}

		QDate previousDate;
		bool hasRenderedDateSeparator = false;
		for (std::size_t groupIndex = 0; groupIndex < renderGroups.size(); ++groupIndex) {
			const PersistentChatRender::PersistentChatRenderGroup &group = renderGroups[groupIndex];
			if (unreadGroupBoundary && *unreadGroupBoundary == groupIndex) {
				addListWidget(createPill(
					QString::fromLatin1("<strong>%1</strong>").arg(tr("New since last read").toHtmlEscaped()),
					QLatin1String("qlPersistentChatUnreadPill")));
			}

			const QDate messageDate = group.date;
			if (!hasRenderedDateSeparator || previousDate != messageDate) {
				addListWidget(createPill(
					QString::fromLatin1("<strong>%1</strong>").arg(persistentChatDateLabel(messageDate).toHtmlEscaped()),
					QLatin1String("qlPersistentChatDateDivider")));
				previousDate = messageDate;
				hasRenderedDateSeparator = true;
			}

			const MumbleProto::ChatMessage &firstMessage = m_persistentChatMessages[group.bubbles.front().messageIndex];
			PersistentChatGroupHeaderSpec headerSpec;
			headerSpec.selfAuthored   = group.selfAuthored;
			headerSpec.aggregateScope = target.scope == MumbleProto::Aggregate;
			headerSpec.actorLabelHtml = group.selfAuthored ? QString() : persistentChatActorLabel(firstMessage);
			headerSpec.timeLabel = group.startedAt.isValid() ? group.startedAt.time().toString(QLatin1String("HH:mm"))
														 : tr("Unknown time");
			headerSpec.timeToolTip = group.startedAt.isValid() ? QLocale().toString(group.startedAt, QLocale::LongFormat)
															 : tr("Unknown time");
			headerSpec.scope    = group.scope;
			headerSpec.scopeID  = group.scopeID;
			headerSpec.scopeLabel = target.scope == MumbleProto::Aggregate ? persistentChatScopeLabel(group.scope, group.scopeID)
																		   : QString();

			QImage avatarImage;
			QString avatarFallbackText;
			if (!group.selfAuthored) {
				const ClientUser *actorUser =
					group.actor.session ? ClientUser::get(*group.actor.session) : nullptr;
				avatarImage = persistentChatAvatarTexture(actorUser, 32);
				if (!actorUser && !group.actor.name.isEmpty()) {
					avatarFallbackText = persistentChatInitials(group.actor.name);
				} else if (actorUser) {
					avatarFallbackText = persistentChatInitials(actorUser->qsName);
				} else {
					avatarFallbackText = QStringLiteral("?");
				}
			}

			auto *groupWidget = new PersistentChatMessageGroupWidget(contentWidth, qApp->styleSheet());
			groupWidget->setHeader(headerSpec, avatarImage, avatarFallbackText);
			connectGroupWidget(groupWidget);

			for (const PersistentChatRender::PersistentChatRenderBubble &bubble : group.bubbles) {
				const MumbleProto::ChatMessage &message = m_persistentChatMessages[bubble.messageIndex];
				QString bodyHtml;
				std::optional< PersistentChatReplyReference > replyReference;
				if (message.has_deleted_at() && message.deleted_at() > 0) {
					bodyHtml = QString::fromLatin1("<em>%1</em>").arg(tr("[message deleted]").toHtmlEscaped());
				} else {
					QString rawBodyHtml = persistentChatMessageRawBody(message);
					if (message.has_reply_to_message_id()) {
						if (const MumbleProto::ChatMessage *replyTarget =
								findPersistentChatMessageByID(m_persistentChatMessages, message.reply_to_message_id())) {
							PersistentChatReplyReference reference;
							reference.messageID = replyTarget->message_id();
							reference.actor     = persistentChatActorLabel(*replyTarget);
							reference.snippet   = persistentChatMessageTextSnippet(persistentChatMessageSourceText(*replyTarget));
							replyReference      = std::move(reference);
						}
					}
					if (!replyReference && !message.has_body_text()) {
						replyReference = extractPersistentChatReplyReference(rawBodyHtml, &rawBodyHtml);
					}
					bodyHtml = message.has_body_text() ? persistentChatMessageBodyHtml(message)
													   : persistentChatContentHtml(rawBodyHtml);
				}

				if (message.has_edited_at() && message.edited_at() > 0) {
					bodyHtml += QString::fromLatin1(" <em>%1</em>").arg(tr("(edited)").toHtmlEscaped());
				}

				PersistentChatBubbleSpec bubbleSpec;
				bubbleSpec.messageID    = message.message_id();
				bubbleSpec.threadID     = message.thread_id();
				bubbleSpec.bodyHtml     = bodyHtml;
				bubbleSpec.timeToolTip  = bubble.createdAt.isValid() ? QLocale().toString(bubble.createdAt, QLocale::LongFormat)
																	 : tr("Unknown time");
				bubbleSpec.replyEnabled =
					target.scope == MumbleProto::Aggregate || !message.has_deleted_at() || message.deleted_at() == 0;
				bubbleSpec.readOnlyAction = target.scope == MumbleProto::Aggregate;
				bubbleSpec.actionText   = target.scope == MumbleProto::Aggregate ? tr("Open room") : tr("Reply");
				bubbleSpec.actionScope  = message.has_scope() ? message.scope() : MumbleProto::Channel;
				bubbleSpec.actionScopeID = message.has_scope_id() ? message.scope_id() : 0;

				if (replyReference) {
					bubbleSpec.hasReply       = true;
					bubbleSpec.replyMessageID = replyReference->messageID;
					bubbleSpec.replyActor = Qt::mightBeRichText(replyReference->actor)
											 ? QTextDocumentFragment::fromHtml(replyReference->actor).toPlainText()
											 : replyReference->actor;
					bubbleSpec.replySnippet = replyReference->snippet;
				}

				if ((!message.has_deleted_at() || message.deleted_at() == 0) && showInlinePreviews) {
					if (const std::optional< QString > previewKey = persistentChatPreviewKey(message); previewKey) {
						bubbleSpec.previewHtml =
							persistentChatPreviewHtml(*previewKey, persistentChatPreviewContentWidth(group.selfAuthored ? 18 : 38));
						const auto previewIt = m_persistentChatPreviews.constFind(*previewKey);
						if (previewIt != m_persistentChatPreviews.cend() && !previewIt->thumbnailImage.isNull()) {
							bubbleSpec.previewResources.append(
								qMakePair(persistentChatThumbnailResourceUrl(*previewKey), previewIt->thumbnailImage));
						}
					}
				}

				groupWidget->addBubble(bubbleSpec);
			}

			groupWidget->setProperty("persistentChatThreadID", group.lastThreadID);
			groupWidget->setProperty("persistentChatMessageID", group.lastMessageID);
			addListWidget(groupWidget);
		}

		QWidget *bottomInset = new QWidget();
		bottomInset->setObjectName(QLatin1String("qwPersistentChatBottomInset"));
		bottomInset->setAttribute(Qt::WA_StyledBackground, false);
		bottomInset->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
		bottomInset->setFixedHeight(PersistentChatBottomInsetHeight);
		bottomInset->setProperty("persistentChatItemHeight", PersistentChatBottomInsetHeight);
		addListWidget(bottomInset);
	}

	m_persistentChatHistory->doItemsLayout();
	m_persistentChatHistory->viewport()->updateGeometry();
	m_persistentChatHistory->stabilizeVisibleContent();

	if (preserveScrollPosition) {
		QScrollBar *scrollBar = m_persistentChatHistory->verticalScrollBar();
		bool restoredByAnchor = false;
		if (hasMessageAnchor) {
			for (int i = 0; i < m_persistentChatHistory->count(); ++i) {
				QListWidgetItem *item = m_persistentChatHistory->item(i);
				if (!item) {
					continue;
				}

				int newTopOffset = 0;
				const QRect itemRect = historyItemContentRect(item);
				if (auto *groupWidget =
						qobject_cast< PersistentChatMessageGroupWidget * >(m_persistentChatHistory->itemWidget(item))) {
					int bubbleTopOffset = 0;
					if (!groupWidget->bubbleTopOffset(anchorMessageID, anchorThreadID, bubbleTopOffset)) {
						continue;
					}
					newTopOffset = itemRect.top() + bubbleTopOffset;
				} else {
					if (item->data(PersistentChatMessageIDRole).toUInt() != anchorMessageID
						|| item->data(PersistentChatThreadRole).toUInt() != anchorThreadID) {
						continue;
					}

					newTopOffset = itemRect.top();
				}

				const int restoredValue =
					qBound(scrollBar->minimum(), oldScrollValue + (newTopOffset - anchorTopOffset), scrollBar->maximum());
				scrollBar->setValue(restoredValue);
				restoredByAnchor = true;
				break;
			}
		}

		if (!restoredByAnchor) {
			const int newScrollMax = scrollBar->maximum();
			if (oldScrollValue <= scrollBar->minimum()) {
				scrollBar->setValue(scrollBar->minimum());
			} else {
				scrollBar->setValue(
					qBound(scrollBar->minimum(), oldScrollValue + (newScrollMax - oldScrollMax), newScrollMax));
			}
		}

		QPointer< PersistentChatListWidget > history(m_persistentChatHistory);
		const unsigned int queuedAnchorThreadID  = anchorThreadID;
		const unsigned int queuedAnchorMessageID = anchorMessageID;
		const int queuedAnchorTopOffset          = anchorTopOffset;
		const bool queuedHasMessageAnchor        = hasMessageAnchor;
		QMetaObject::invokeMethod(
			this,
			[history, oldScrollValue, oldScrollMax, queuedAnchorThreadID, queuedAnchorMessageID, queuedAnchorTopOffset,
			 queuedHasMessageAnchor]() {
				if (!history) {
					return;
				}

				auto itemContentRectForItem = [history](QListWidgetItem *item) {
					if (!item || !history) {
						return QRect();
					}

					if (QWidget *itemWidget = history->itemWidget(item)) {
						const QRect widgetRect = itemWidget->geometry();
						if (widgetRect.isValid() && !widgetRect.isEmpty()) {
							return widgetRect;
						}
					}

					return history->visualItemRect(item);
				};

				history->doItemsLayout();
				history->stabilizeVisibleContent();

				QScrollBar *scrollBar = history->verticalScrollBar();
				bool restoredByAnchor = false;
				if (queuedHasMessageAnchor) {
					for (int i = 0; i < history->count(); ++i) {
						QListWidgetItem *item = history->item(i);
						if (!item) {
							continue;
						}

						int newTopOffset = 0;
						const QRect itemRect = itemContentRectForItem(item);
						if (auto *groupWidget =
								qobject_cast< PersistentChatMessageGroupWidget * >(history->itemWidget(item))) {
							int bubbleTopOffset = 0;
							if (!groupWidget->bubbleTopOffset(queuedAnchorMessageID, queuedAnchorThreadID, bubbleTopOffset)) {
								continue;
							}
							newTopOffset = itemRect.top() + bubbleTopOffset;
						} else {
							if (item->data(PersistentChatMessageIDRole).toUInt() != queuedAnchorMessageID
								|| item->data(PersistentChatThreadRole).toUInt() != queuedAnchorThreadID) {
								continue;
							}

							newTopOffset = itemRect.top();
						}

						const int restoredValue = qBound(scrollBar->minimum(),
														 oldScrollValue + (newTopOffset - queuedAnchorTopOffset),
														 scrollBar->maximum());
						scrollBar->setValue(restoredValue);
						restoredByAnchor = true;
						break;
					}
				}

				if (!restoredByAnchor) {
					const int newScrollMax = scrollBar->maximum();
					if (oldScrollValue <= scrollBar->minimum()) {
						scrollBar->setValue(scrollBar->minimum());
					} else {
						scrollBar->setValue(
							qBound(scrollBar->minimum(), oldScrollValue + (newScrollMax - oldScrollMax), newScrollMax));
					}
				}

				history->stabilizeVisibleContent();
			},
			Qt::QueuedConnection);
	} else if (scrollToBottom) {
		m_persistentChatHistory->verticalScrollBar()->setValue(m_persistentChatHistory->verticalScrollBar()->maximum());
		QPointer< PersistentChatListWidget > history(m_persistentChatHistory);
		QMetaObject::invokeMethod(
			this,
			[history]() {
				if (!history) {
					return;
				}
				history->doItemsLayout();
				history->stabilizeVisibleContent();
				history->verticalScrollBar()->setValue(history->verticalScrollBar()->maximum());
			},
			Qt::QueuedConnection);
	} else {
		m_persistentChatHistory->verticalScrollBar()->setValue(0);
	}

	if (enforceBottomLock && m_persistentChatBottomLockRendersRemaining > 0) {
		--m_persistentChatBottomLockRendersRemaining;
	}
}

bool MainWindow::canMarkPersistentChatRead() const {
	return m_visiblePersistentChatScope && *m_visiblePersistentChatScope != MumbleProto::Aggregate
		   && !m_persistentChatMessages.empty() && Global::get().sh && Global::get().sh->isRunning()
		   && isActiveWindow() && qdwChat->isVisible() && m_persistentChatHistory
		   && m_persistentChatHistory->isScrolledToBottom();
}

std::size_t MainWindow::persistentChatUnreadCount() const {
	if (!m_visiblePersistentChatScope || *m_visiblePersistentChatScope == MumbleProto::Aggregate) {
		return 0;
	}

	return unreadMessagesAfter(m_persistentChatMessages, m_visiblePersistentChatLastReadMessageID);
}

void MainWindow::markPersistentChatRead() {
	if (!canMarkPersistentChatRead()) {
		return;
	}

	const unsigned int lastVisibleMessageID = m_persistentChatMessages.back().message_id();
	if (lastVisibleMessageID <= m_visiblePersistentChatLastReadMessageID) {
		return;
	}

	m_visiblePersistentChatLastReadMessageID = lastVisibleMessageID;
	setCachedPersistentChatUnreadCount(*m_visiblePersistentChatScope, m_visiblePersistentChatScopeID,
									   lastVisibleMessageID, 0);
	renderPersistentChatView(QString(), true, false);
	Global::get().sh->updateChatReadState(*m_visiblePersistentChatScope, m_visiblePersistentChatScopeID,
										   lastVisibleMessageID);
}

void MainWindow::refreshPersistentChatView(bool forceReload) {
	const PersistentChatTarget target = currentPersistentChatTarget();

	if (!target.valid) {
		clearPersistentChatView(target.label.isEmpty() ? tr("Not connected") : target.label, tr("Start a conversation"),
								{ tr("Open Server to connect"), tr("Room chat and history appear here") });
		return;
	}

	if (target.serverLog) {
		renderServerLogView(!forceReload);
		return;
	}

	if (target.legacyTextPath) {
		renderServerLogView(false);
		return;
	}

	if (target.directMessage) {
		clearPersistentChatView(tr("Direct messages still use the classic text message path and do not have persistent "
								   "history yet."),
							 tr("Direct messages"), { tr("Classic chat still handles direct messages") });
		return;
	}

	if (target.scope == MumbleProto::ServerGlobal && !Global::get().bPersistentGlobalChatEnabled) {
		clearPersistentChatView(target.statusMessage.isEmpty() ? tr("Global chat is disabled by this server.")
															  : target.statusMessage,
							 tr("Global chat is unavailable"), { tr("Choose a room conversation instead") });
		return;
	}

	const bool targetChanged =
		!m_visiblePersistentChatScope || *m_visiblePersistentChatScope != target.scope
		|| m_visiblePersistentChatScopeID != target.scopeID;
	if (!forceReload && !targetChanged) {
		return;
	}

	if (targetChanged) {
		clearPersistentChatReplyTarget(false);
	}

	m_visiblePersistentChatScope          = target.scope;
	m_visiblePersistentChatScopeID        = target.scopeID;
	m_visiblePersistentChatLastReadMessageID = 0;
	m_visiblePersistentChatOldestMessageID   = 0;
	m_visiblePersistentChatHasMore        = false;
	m_persistentChatLoadingOlder          = false;
	m_persistentChatBottomLockRendersRemaining = 3;
	m_persistentChatMessages.clear();

	renderPersistentChatView(tr("Loading %1...").arg(target.label));
	Global::get().sh->requestChatHistory(target.scope, target.scopeID, 0, 50, std::nullopt);
}

void MainWindow::renderServerLogView(bool preserveScrollPosition) {
	if (!m_persistentChatHistory || !qteLog) {
		return;
	}

	const bool wasAtBottom = m_persistentChatHistory->isScrolledToBottom();
	int previousScrollValue = 0;
	if (preserveScrollPosition) {
		previousScrollValue = m_persistentChatHistory->verticalScrollBar()->value();
	}

	m_persistentChatMessages.clear();
	m_pendingPersistentChatRender.reset();
	m_visiblePersistentChatScope.reset();
	m_visiblePersistentChatScopeID           = 0;
	m_visiblePersistentChatLastReadMessageID = 0;
	m_visiblePersistentChatOldestMessageID   = 0;
	m_visiblePersistentChatHasMore           = false;
	m_persistentChatLoadingOlder             = false;
	m_persistentChatBottomLockRendersRemaining = 0;
	clearPersistentChatReplyTarget(false);

	const QSignalBlocker historySignalBlocker(m_persistentChatHistory);
	const QSignalBlocker historyScrollSignalBlocker(m_persistentChatHistory->verticalScrollBar());
	m_persistentChatHistory->clear();

	auto *browser = new LogTextBrowser();
	browser->setFrameShape(QFrame::NoFrame);
	browser->setFrameStyle(QFrame::NoFrame);
	browser->setLineWidth(0);
	browser->setMidLineWidth(0);
	browser->setReadOnly(true);
	browser->setOpenLinks(false);
	browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	browser->setContextMenuPolicy(Qt::CustomContextMenu);
	browser->resetViewportChrome();
	configurePersistentChatDocument(browser->document(), qApp->styleSheet());
	const int contentWidth = std::max(240, m_persistentChatHistory->viewport()->width() - 18);
	browser->setFixedWidth(contentWidth);
	browser->document()->setTextWidth(contentWidth);
	browser->setHtml(mirroredServerLogHtml(qteLog->toHtml()));
	browser->document()->adjustSize();
	browser->setFixedHeight(std::max(40, static_cast< int >(std::ceil(browser->document()->size().height())) + 4));
	connect(browser, &LogTextBrowser::anchorClicked, this, &MainWindow::on_qteLog_anchorClicked);
	connect(browser, QOverload< const QUrl & >::of(&QTextBrowser::highlighted), this,
			&MainWindow::on_qteLog_highlighted);
	connect(browser, &LogTextBrowser::customContextMenuRequested, this,
			[this, browser](const QPoint &position) { showLogContextMenu(browser, position); });
	connect(browser, &LogTextBrowser::imageActivated, this,
			[this, browser](const QTextCursor &cursor) { openImageDialog(browser, cursor); });

	QListWidgetItem *item = new QListWidgetItem(m_persistentChatHistory);
	item->setFlags(Qt::NoItemFlags);
	item->setSizeHint(browser->sizeHint());
	m_persistentChatHistory->addItem(item);
	m_persistentChatHistory->setItemWidget(item, browser);
	if (preserveScrollPosition) {
		m_persistentChatHistory->verticalScrollBar()->setValue(
			wasAtBottom ? m_persistentChatHistory->verticalScrollBar()->maximum() : previousScrollValue);
	} else {
		m_persistentChatHistory->verticalScrollBar()->setValue(m_persistentChatHistory->verticalScrollBar()->maximum());
	}
}

void MainWindow::requestOlderPersistentChatHistory() {
	const PersistentChatTarget target = currentPersistentChatTarget();
	if (!target.valid || target.directMessage || !m_visiblePersistentChatScope || !m_visiblePersistentChatHasMore
		|| m_persistentChatLoadingOlder || !Global::get().sh || !Global::get().sh->isRunning()) {
		return;
	}

	m_persistentChatLoadingOlder = true;
	renderPersistentChatView(QString(), false, true);
	if (target.scope == MumbleProto::Aggregate) {
		Global::get().sh->requestChatHistory(
			target.scope, target.scopeID, static_cast< unsigned int >(m_persistentChatMessages.size()), 50, std::nullopt);
		return;
	}

	Global::get().sh->requestChatHistory(target.scope, target.scopeID, 0, 50,
										 m_visiblePersistentChatOldestMessageID > 0
											 ? std::optional< unsigned int >(m_visiblePersistentChatOldestMessageID)
											 : std::nullopt);
}

void MainWindow::handlePersistentChatMessage(const MumbleProto::ChatMessage &msg) {
	const PersistentChatTarget target = currentPersistentChatTarget();
	if (!target.valid) {
		return;
	}

	const MumbleProto::ChatScope messageScope =
		msg.has_scope() ? msg.scope() : MumbleProto::Channel;
	const unsigned int messageScopeID = msg.has_scope_id() ? msg.scope_id() : 0;
	const QString scopeKey            = persistentChatScopeCacheKey(messageScope, messageScopeID);
	const unsigned int lastReadMessageID = m_persistentChatLastReadByScope.value(scopeKey, 0);
	const bool messageAlreadyLoaded =
		target.scope == MumbleProto::Aggregate
		&& std::find_if(m_persistentChatMessages.cbegin(), m_persistentChatMessages.cend(),
						[&msg](const MumbleProto::ChatMessage &currentMessage) {
							return currentMessage.thread_id() == msg.thread_id()
								   && currentMessage.message_id() == msg.message_id();
						})
			   != m_persistentChatMessages.cend();

	const bool visibleScopeMatches =
		m_visiblePersistentChatScope && messageScope == *m_visiblePersistentChatScope
		&& messageScopeID == m_visiblePersistentChatScopeID;
	if (target.directMessage || !visibleScopeMatches) {
		if (!messageAlreadyLoaded && msg.message_id() > lastReadMessageID
			&& (!msg.has_edited_at() || msg.edited_at() == 0)
			&& (!msg.has_deleted_at() || msg.deleted_at() == 0)) {
			const std::size_t unreadCount = cachedPersistentChatUnreadCount(messageScope, messageScopeID) + 1;
			setCachedPersistentChatUnreadCount(messageScope, messageScopeID, lastReadMessageID, unreadCount);
		}
		if (target.scope != MumbleProto::Aggregate) {
			return;
		}
	}

	if (!m_visiblePersistentChatScope) {
		return;
	}

	auto existingMessage = std::find_if(m_persistentChatMessages.begin(), m_persistentChatMessages.end(),
										[&msg](const MumbleProto::ChatMessage &currentMessage) {
											return currentMessage.thread_id() == msg.thread_id()
												   && currentMessage.message_id() == msg.message_id();
										});

	const bool inserted = existingMessage == m_persistentChatMessages.end();
	if (inserted) {
		m_persistentChatMessages.push_back(msg);
	} else {
		*existingMessage = msg;
	}

	if (inserted && visibleScopeMatches) {
		const std::size_t unreadCount =
			msg.message_id() > m_visiblePersistentChatLastReadMessageID
				? unreadMessagesAfter(m_persistentChatMessages, m_visiblePersistentChatLastReadMessageID)
				: cachedPersistentChatUnreadCount(messageScope, messageScopeID);
		setCachedPersistentChatUnreadCount(messageScope, messageScopeID, m_visiblePersistentChatLastReadMessageID,
										   unreadCount);
	}

	const bool wasAtBottom = m_persistentChatHistory ? m_persistentChatHistory->isScrolledToBottom() : true;
	std::sort(m_persistentChatMessages.begin(), m_persistentChatMessages.end(), chatMessageLessThan);
	m_visiblePersistentChatOldestMessageID =
		m_persistentChatMessages.empty() ? 0U : m_persistentChatMessages.front().message_id();
	renderPersistentChatView(QString(), wasAtBottom, !wasAtBottom);
	markPersistentChatRead();
}

void MainWindow::handlePersistentChatHistory(const MumbleProto::ChatHistoryResponse &msg) {
	const PersistentChatTarget target = currentPersistentChatTarget();
	if (!target.valid || target.directMessage || target.legacyTextPath) {
		return;
	}

	const MumbleProto::ChatScope responseScope =
		msg.has_scope() ? msg.scope() : MumbleProto::Channel;
	const unsigned int responseScopeID = msg.has_scope_id() ? msg.scope_id() : 0;
	const bool requestedScopeMatches =
		m_visiblePersistentChatScope && responseScope == *m_visiblePersistentChatScope
		&& responseScopeID == m_visiblePersistentChatScopeID;
	if ((m_visiblePersistentChatScope && !requestedScopeMatches)
		|| (!m_visiblePersistentChatScope
			&& (responseScope != target.scope || responseScopeID != target.scopeID))) {
		return;
	}

	m_visiblePersistentChatScope          = responseScope;
	m_visiblePersistentChatScopeID        = responseScopeID;
	m_visiblePersistentChatHasMore        =
		msg.has_has_older() ? msg.has_older() : (msg.has_has_more() ? msg.has_more() : false);

	const bool loadingOlder = m_persistentChatLoadingOlder
							  || (msg.has_oldest_message_id() && m_visiblePersistentChatOldestMessageID > 0
								  && msg.oldest_message_id() < m_visiblePersistentChatOldestMessageID);
	m_persistentChatLoadingOlder          = false;
	if (!loadingOlder) {
		m_visiblePersistentChatLastReadMessageID =
			msg.has_last_read_message_id() ? msg.last_read_message_id() : 0;
		m_visiblePersistentChatOldestMessageID = 0;
		m_persistentChatMessages.clear();
		m_persistentChatMessages.reserve(static_cast< std::size_t >(msg.messages_size()));
	}

	for (const MumbleProto::ChatMessage &message : msg.messages()) {
		auto existingMessage = std::find_if(m_persistentChatMessages.begin(), m_persistentChatMessages.end(),
											[&message](const MumbleProto::ChatMessage &currentMessage) {
												return currentMessage.thread_id() == message.thread_id()
													   && currentMessage.message_id() == message.message_id();
											});
		if (existingMessage == m_persistentChatMessages.end()) {
			m_persistentChatMessages.push_back(message);
		} else {
			*existingMessage = message;
		}
	}

	std::sort(m_persistentChatMessages.begin(), m_persistentChatMessages.end(), chatMessageLessThan);
	m_visiblePersistentChatOldestMessageID =
		msg.has_oldest_message_id()
			? msg.oldest_message_id()
			: (m_persistentChatMessages.empty() ? 0U : m_persistentChatMessages.front().message_id());
	if (responseScope != MumbleProto::Aggregate) {
		setCachedPersistentChatUnreadCount(responseScope, responseScopeID, m_visiblePersistentChatLastReadMessageID,
										   unreadMessagesAfter(m_persistentChatMessages,
															  m_visiblePersistentChatLastReadMessageID));
	} else {
		updatePersistentChatScopeSelectorLabels();
	}
	renderPersistentChatView(QString(), !loadingOlder, loadingOlder);
	markPersistentChatRead();
}

void MainWindow::handlePersistentChatReadState(const MumbleProto::ChatReadStateUpdate &msg) {
	const MumbleProto::ChatScope scope = msg.has_scope() ? msg.scope() : MumbleProto::Channel;
	const unsigned int scopeID         = msg.has_scope_id() ? msg.scope_id() : 0;
	const unsigned int lastReadMessageID =
		msg.has_last_read_message_id() ? msg.last_read_message_id() : 0;

	if (scope == MumbleProto::Aggregate) {
		updatePersistentChatScopeSelectorLabels();
		return;
	}

	if (m_visiblePersistentChatScope && scope == *m_visiblePersistentChatScope && scopeID == m_visiblePersistentChatScopeID) {
		const bool wasAtBottom = m_persistentChatHistory ? m_persistentChatHistory->isScrolledToBottom() : true;
		m_visiblePersistentChatLastReadMessageID = lastReadMessageID;
		setCachedPersistentChatUnreadCount(scope, scopeID, lastReadMessageID,
										   unreadMessagesAfter(m_persistentChatMessages, lastReadMessageID));
		renderPersistentChatView(QString(), wasAtBottom, !wasAtBottom);
		return;
	}

	setCachedPersistentChatUnreadCount(scope, scopeID, lastReadMessageID,
									   cachedPersistentChatUnreadCount(scope, scopeID));
}

void MainWindow::handlePersistentChatEmbedState(const MumbleProto::ChatEmbedState &msg) {
	if (!msg.has_thread_id() || !msg.has_message_id()) {
		return;
	}

	auto existingMessage = std::find_if(m_persistentChatMessages.begin(), m_persistentChatMessages.end(),
										[&msg](const MumbleProto::ChatMessage &currentMessage) {
											return currentMessage.thread_id() == msg.thread_id()
												   && currentMessage.message_id() == msg.message_id();
										});
	if (existingMessage == m_persistentChatMessages.end()) {
		return;
	}

	QString oldPreviewKey;
	if (const std::optional< QString > key = persistentChatPreviewKey(*existingMessage); key) {
		oldPreviewKey = *key;
	}

	existingMessage->clear_embeds();
	for (const MumbleProto::ChatEmbedRef &embed : msg.embeds()) {
		*existingMessage->add_embeds() = embed;
	}

	QString newPreviewKey;
	if (const std::optional< QString > key = persistentChatPreviewKey(*existingMessage); key) {
		newPreviewKey = *key;
	}

	if (!oldPreviewKey.isEmpty() && oldPreviewKey != newPreviewKey) {
		m_persistentChatPreviews.remove(oldPreviewKey);
	}
	if (!newPreviewKey.isEmpty()) {
		m_persistentChatPreviews.remove(newPreviewKey);
		ensurePersistentChatPreview(newPreviewKey);
	}

	const bool wasAtBottom = m_persistentChatHistory ? m_persistentChatHistory->isScrolledToBottom() : true;
	renderPersistentChatView(QString(), wasAtBottom, !wasAtBottom);
}

void MainWindow::syncPersistentChatInputState(bool baseEnabled) {
	if (!qteChat) {
		return;
	}

	const PersistentChatTarget target = currentPersistentChatTarget();
	bool enableInput                  = baseEnabled && target.valid && !target.readOnly;
	if (target.valid && !target.readOnly && target.channel && !target.directMessage
		&& (target.scope == MumbleProto::TextChannel || target.legacyTextPath || target.scope == MumbleProto::Channel)) {
		ChanACL::Permissions textPermissions = static_cast< ChanACL::Permissions >(target.channel->uiPermissions);
		if (!textPermissions) {
			Global::get().sh->requestChannelPermissions(target.channel->iId);
			textPermissions = target.channel->iId == 0 ? Global::get().pPermissions : ChanACL::All;
			target.channel->uiPermissions = textPermissions;
		}

		enableInput = Global::get().uiSession
					  && (textPermissions & (ChanACL::Write | ChanACL::TextMessage));
	}
	if (target.valid && !target.readOnly && target.scope == MumbleProto::ServerGlobal) {
		enableInput = Global::get().bPersistentGlobalChatEnabled && Global::get().uiSession
					  && (Global::get().pPermissions & (ChanACL::Write | ChanACL::TextMessage));
	}
	qteChat->setEnabled(enableInput);
	updatePersistentChatSendButton();
}

void MainWindow::updatePersistentChatChrome(const PersistentChatTarget &target) {
	const bool showConversationList = hasPersistentChatCapabilities();
	const bool showMotd             = !m_persistentChatWelcomeText.trimmed().isEmpty();

	if (m_persistentChatHeaderEyebrow) {
		QString eyebrow = tr("Text");
		if (target.serverLog) {
			eyebrow = tr("Logs");
		} else if (target.directMessage) {
			eyebrow = tr("Direct");
		} else if (target.scope == MumbleProto::Aggregate) {
			eyebrow = tr("All chats");
		} else if (target.scope == MumbleProto::ServerGlobal) {
			eyebrow = tr("Lobby");
		} else if (target.scope == MumbleProto::TextChannel) {
			eyebrow = tr("Room");
		}
		m_persistentChatHeaderEyebrow->setText(eyebrow);
	}

	if (m_persistentChatHeaderTitle) {
		m_persistentChatHeaderTitle->setText(target.valid ? target.label : tr("Chat"));
	}

	if (m_persistentChatHeaderContext) {
		m_persistentChatHeaderContext->clear();
		m_persistentChatHeaderContext->setVisible(false);
	}

	QString subtitle;
	if (Global::get().uiSession == 0) {
		subtitle = tr("Join a server to chat.");
	} else if (!target.valid) {
		subtitle = target.label;
	} else if (target.serverLog) {
		subtitle = tr("Server events and connection diagnostics.");
	} else if (target.legacyTextPath) {
		subtitle = tr("Classic server chat in one shared flow.");
	} else if (target.directMessage) {
		subtitle = tr("Classic direct chat without persistent history yet.");
	} else if (target.scope == MumbleProto::Aggregate) {
		subtitle = tr("Across rooms and shared activity.");
	} else if (target.scope == MumbleProto::ServerGlobal) {
		if (!target.description.trimmed().isEmpty()) {
			subtitle = target.description;
		} else if (const ClientUser *self = ClientUser::get(Global::get().uiSession); self && self->cChannel) {
			subtitle = tr("Server-wide conversation from %1.").arg(persistentTextAclChannelLabel(self->cChannel));
		} else {
			subtitle = tr("Server-wide conversation shared across the instance.");
		}
	} else if (target.scope == MumbleProto::TextChannel && target.channel) {
		subtitle = !target.description.trimmed().isEmpty()
					   ? target.description
					   : tr("Linked to %1.").arg(persistentTextAclChannelLabel(target.channel));
	} else if (target.scope == MumbleProto::Channel && target.channel) {
		subtitle = tr("History shared with %1.").arg(persistentTextAclChannelLabel(target.channel));
	} else if (!target.statusMessage.trimmed().isEmpty()) {
		subtitle = target.statusMessage;
	} else if (!target.description.trimmed().isEmpty()) {
		subtitle = target.description;
	} else {
		subtitle = tr("Persistent chat with shared history and read state.");
	}

	if (m_persistentChatHeaderSubtitle) {
		m_persistentChatHeaderSubtitle->setText(subtitle);
	}
	if (m_persistentChatHeaderFrame) {
		m_persistentChatHeaderFrame->setVisible(true);
	}
	if (m_serverNavigatorTextChannelsMotdFrame && m_serverNavigatorTextChannelsMotdTitle
		&& m_serverNavigatorTextChannelsMotdBody && m_serverNavigatorTextChannelsMotdToggleButton) {
		const bool showMotdBody = showMotd && !m_persistentChatMotdHidden;
		m_serverNavigatorTextChannelsMotdFrame->setVisible(showMotd);
		if (showMotd) {
			const QString teaserText = persistentChatPlainTextSummary(m_persistentChatWelcomeText, 96);
			m_serverNavigatorTextChannelsMotdTitle->setText(tr("Server note"));
			m_serverNavigatorTextChannelsMotdBody->setVisible(true);
			m_serverNavigatorTextChannelsMotdBody->setText(showMotdBody
															 ? persistentChatContentHtml(m_persistentChatWelcomeText)
															 : QString::fromLatin1("<span>%1</span>")
																   .arg(teaserText.toHtmlEscaped()));
			m_serverNavigatorTextChannelsMotdBody->setTextInteractionFlags(showMotdBody ? Qt::TextBrowserInteraction
																					   : Qt::NoTextInteraction);
			m_serverNavigatorTextChannelsMotdBody->setToolTip(showMotdBody ? QString() : teaserText);
			m_serverNavigatorTextChannelsMotdBody->setMinimumHeight(showMotdBody ? 96 : 0);
			m_serverNavigatorTextChannelsMotdBody->setMaximumHeight(showMotdBody ? QWIDGETSIZE_MAX : 24);
			m_serverNavigatorTextChannelsMotdBody->updateGeometry();
			if (showMotdBody) {
				static const int motdRefreshDelaysMs[] = { 0, 150, 600, 1800, 4500 };
				for (const int delayMs : motdRefreshDelaysMs) {
					QTimer::singleShot(delayMs, this, [this]() { refreshServerNavigatorMotdHeight(); });
				}
			}
			m_serverNavigatorTextChannelsMotdToggleButton->setText(showMotdBody ? tr("Hide") : tr("Open"));
			m_serverNavigatorTextChannelsMotdToggleButton->setToolTip(showMotdBody ? tr("Hide message of the day")
																				   : tr("Open message of the day"));
		}
	}

	if (m_serverNavigatorVoiceSectionEyebrow) {
		m_serverNavigatorVoiceSectionEyebrow->setText(tr("Rooms"));
		m_serverNavigatorVoiceSectionEyebrow->setVisible(true);
	}
	if (m_serverNavigatorVoiceSectionSubtitle) {
		m_serverNavigatorVoiceSectionSubtitle->setVisible(false);
		m_serverNavigatorVoiceSectionSubtitle->setText(tr("Voice rooms, people, and text rooms"));
	}
	if (m_serverNavigatorTextChannelsEyebrow) {
		m_serverNavigatorTextChannelsEyebrow->setVisible(false);
	}
	if (m_serverNavigatorTextChannelsTitle) {
		m_serverNavigatorTextChannelsTitle->setVisible(false);
		m_serverNavigatorTextChannelsTitle->setText(tr("Text rooms"));
	}
	if (m_serverNavigatorTextChannelsSubtitle) {
		m_serverNavigatorTextChannelsSubtitle->setVisible(false);
		m_serverNavigatorTextChannelsSubtitle->setText(tr("Lobby, rooms, and shared activity"));
	}
	if (m_persistentChatChannelList) {
		m_persistentChatChannelList->setVisible(showConversationList);
		updatePersistentChatChannelListHeight();
	}
	if (m_serverNavigatorTextChannelsFrame) {
		m_serverNavigatorTextChannelsFrame->setVisible(showConversationList);
		if (m_serverNavigatorTextChannelsDivider) {
			m_serverNavigatorTextChannelsDivider->setVisible(false);
		}
	}

	qdwLog->setWindowTitle((showConversationList || Global::get().uiSession == 0) ? tr("Activity")
													   : tr("Activity & classic chat"));
}

void MainWindow::updateWindowTitle() {
	QString title;
	if (Global::get().s.bMinimalView) {
		title = tr("Mumble - Minimal View");
	} else {
		title = tr("Mumble");
	}

	if (!Global::get().windowTitlePostfix.isEmpty()) {
		title += QString::fromLatin1(" | %1").arg(Global::get().windowTitlePostfix);
	}

	setWindowTitle(title);
}

void MainWindow::updateServerNavigatorChrome() {
	if (!m_serverNavigatorHeaderFrame || !m_serverNavigatorTitle || !m_serverNavigatorSubtitle || !m_serverNavigatorFooter) {
		return;
	}

	if (Global::get().uiSession == 0 || !Global::get().sh) {
		m_serverNavigatorEyebrow->setText(tr("Voice"));
		m_serverNavigatorTitle->setText(tr("Join a server"));
		m_serverNavigatorSubtitle->setText(tr("Rooms, people, and live speaking state"));
		m_serverNavigatorFooter->setText(tr("Open Server to reconnect or browse saved favorites."));
		m_serverNavigatorHeaderFrame->setVisible(true);
		m_serverNavigatorFooter->setVisible(true);
		return;
	}

	const PersistentChatTarget chatTarget = currentPersistentChatTarget();
	QString host;
	QString username;
	QString password;
	unsigned short port = 0;
	Global::get().sh->getConnectionInfo(host, port, username, password);

	const QString serverLabel =
		port == 0 || port == DEFAULT_MUMBLE_PORT ? host : tr("%1:%2").arg(host).arg(port);

	QString eyebrow = tr("Voice");
	QString title = serverLabel;
	QString subtitle = serverLabel;
	QString footer = tr("Connected to %1").arg(serverLabel);

	if (hasPersistentChatCapabilities() && chatTarget.valid && !chatTarget.legacyTextPath) {
		eyebrow = tr("Live context");
		title   = chatTarget.label;
		if (chatTarget.serverLog) {
			eyebrow  = tr("Server log");
			subtitle = tr("Shared activity and diagnostics");
		} else if (chatTarget.directMessage && chatTarget.user) {
			eyebrow  = tr("Direct");
			subtitle = tr("With %1").arg(chatTarget.user->qsName);
		} else if (chatTarget.scope == MumbleProto::Aggregate) {
			eyebrow  = tr("All chats");
			subtitle = tr("Across rooms and shared activity");
		} else if (chatTarget.scope == MumbleProto::ServerGlobal) {
			eyebrow = tr("Lobby");
			if (const ClientUser *self = ClientUser::get(Global::get().uiSession); self && self->cChannel) {
				subtitle = tr("From %1").arg(persistentTextAclChannelLabel(self->cChannel));
			} else {
				subtitle = tr("Server-wide chat");
			}
		} else if (chatTarget.scope == MumbleProto::TextChannel && chatTarget.channel) {
			eyebrow  = tr("Room");
			subtitle = tr("Text room linked to %1").arg(persistentTextAclChannelLabel(chatTarget.channel));
		} else if (chatTarget.scope == MumbleProto::Channel && chatTarget.channel) {
			eyebrow  = tr("Voice");
			subtitle = tr("History for %1").arg(persistentTextAclChannelLabel(chatTarget.channel));
		} else if (!chatTarget.description.trimmed().isEmpty()) {
			subtitle = chatTarget.description;
		}
	} else if (ClientUser *selectedUser = pmModel ? pmModel->getUser(qtvUsers->currentIndex()) : nullptr) {
		eyebrow = selectedUser->uiSession == Global::get().uiSession ? tr("You") : tr("Member");
		title = selectedUser->qsName;
		subtitle = selectedUser->cChannel ? tr("In %1").arg(selectedUser->cChannel->qsName) : tr("Connected");
	} else if (Channel *selectedChannel = pmModel ? pmModel->getChannel(qtvUsers->currentIndex()) : nullptr) {
		const ClientUser *self = ClientUser::get(Global::get().uiSession);
		const bool currentRoom = self && self->cChannel == selectedChannel;
		eyebrow = currentRoom ? tr("Current room") : tr("Voice room");
		title = selectedChannel->qsName;
		subtitle = tr("%1 people here").arg(selectedChannel->qlUsers.count());
	} else if (ClientUser *self = ClientUser::get(Global::get().uiSession); self && self->cChannel) {
		eyebrow = tr("Current room");
		title = self->cChannel->qsName;
		subtitle = tr("%1 people here").arg(self->cChannel->qlUsers.count());
	} else {
		footer = tr("Connected to %1").arg(serverLabel);
	}

	m_serverNavigatorHeaderFrame->setVisible(true);
	m_serverNavigatorEyebrow->setText(eyebrow);
	m_serverNavigatorTitle->setText(title);
	m_serverNavigatorSubtitle->setText(subtitle);
	m_serverNavigatorFooter->setText(footer);
	m_serverNavigatorFooter->setVisible(!footer.isEmpty());
}

void MainWindow::updateToolbar() {
	bool layoutIsCustom = Global::get().s.wlWindowLayout == Settings::LayoutCustom;
	qtIconToolbar->setMovable(layoutIsCustom && !Global::get().s.bLockLayout);

	// Update the toolbar so the movable flag takes effect.
	if (layoutIsCustom) {
		// Update the toolbar, but keep it in place.
		addToolBar(toolBarArea(qtIconToolbar), qtIconToolbar);
	} else {
		// Update the toolbar, and ensure it is at the top of the window.
		addToolBar(Qt::TopToolBarArea, qtIconToolbar);
	}
}

void MainWindow::updatePersistentChatSendButton() {
	if (!m_persistentChatSendButton || !qteChat) {
		return;
	}

	const bool hasUserText =
		qteChat->isEnabled() && !qteChat->isShowingDefaultText() && !qteChat->toPlainText().trimmed().isEmpty();
	m_persistentChatSendButton->setEnabled(hasUserText);
}

void MainWindow::updateFavoriteButton() {
	if (Global::get().uiSession == 0) {
		qaServerAddToFavorites->setEnabled(false);
	} else {
		QString host, uname, pw;
		unsigned short port;
		Global::get().sh->getConnectionInfo(host, port, uname, pw);
		qaServerAddToFavorites->setEnabled(!Global::get().db->isFavorite(host, port));
	}
}

// Sets whether or not to show the title bars on the MainWindow's
// dock widgets.
void MainWindow::setShowDockTitleBars(bool doShow) {
	dtbLogDockTitle->setEnabled(doShow);
	dtbChatDockTitle->setEnabled(doShow);
}

MainWindow::~MainWindow() {
	delete qwPTTButtonWidget;
	delete qdwLog->titleBarWidget();
	delete pmModel;
	delete qtvUsers;
	delete Channel::get(Mumble::ROOT_CHANNEL_ID);
}

void MainWindow::msgBox(QString msg) {
	MessageBoxEvent *mbe = new MessageBoxEvent(msg);
	QApplication::postEvent(this, mbe);
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &, void *message, qintptr *) {
	MSG *msg = reinterpret_cast< MSG * >(message);
	if (msg->message == WM_DEVICECHANGE && msg->wParam == DBT_DEVNODES_CHANGED)
		uiNewHardware++;

	return false;
}
#endif

void MainWindow::closeEvent(QCloseEvent *e) {
	ServerHandlerPtr sh               = Global::get().sh;
	QuitBehavior quitBehavior         = Global::get().s.quitBehavior;
	const bool alwaysAsk              = quitBehavior == QuitBehavior::ALWAYS_ASK;
	const bool askDueToConnected      = sh && sh->isRunning() && quitBehavior == QuitBehavior::ASK_WHEN_CONNECTED;
	const bool alwaysMinimize         = quitBehavior == QuitBehavior::ALWAYS_MINIMIZE;
	const bool minimizeDueToConnected = sh && sh->isRunning() && quitBehavior == QuitBehavior::MINIMIZE_WHEN_CONNECTED;

	if (!forceQuit && (alwaysAsk || askDueToConnected)) {
		QMessageBox mb(QMessageBox::Warning, QLatin1String("Mumble"),
					   tr("Are you sure you want to close Mumble? Perhaps you prefer to minimize it instead?"),
					   QMessageBox::NoButton, this);
		QCheckBox *qcbRemember   = new QCheckBox(tr("Remember this setting"));
		QPushButton *qpbClose    = mb.addButton(tr("Close"), QMessageBox::YesRole);
		QPushButton *qpbMinimize = mb.addButton(tr("Minimize"), QMessageBox::NoRole);
		QPushButton *qpbCancel   = mb.addButton(tr("Cancel"), QMessageBox::RejectRole);
		mb.setDefaultButton(qpbClose);
		mb.setEscapeButton(qpbCancel);
		mb.setCheckBox(qcbRemember);
		mb.exec();
		if (mb.clickedButton() == qpbMinimize) {
			setWindowState(windowState() | Qt::WindowMinimized);
			e->ignore();

			// If checkbox is checked and not connected, always minimize
			// If checkbox is checked and connected, always minimize when connected
			if (qcbRemember->isChecked() && !(sh && sh->isRunning())) {
				Global::get().s.quitBehavior = QuitBehavior::ALWAYS_MINIMIZE;
			} else if (qcbRemember->isChecked()) {
				Global::get().s.quitBehavior = QuitBehavior::MINIMIZE_WHEN_CONNECTED;
			}

			return;
		} else if (mb.clickedButton() == qpbCancel) {
			e->ignore();
			return;
		}

		// If checkbox is checked, quit always
		if (qcbRemember->isChecked()) {
			Global::get().s.quitBehavior = QuitBehavior::ALWAYS_QUIT;
		}
	} else if (!forceQuit && (alwaysMinimize || minimizeDueToConnected)) {
		setWindowState(windowState() | Qt::WindowMinimized);
		e->ignore();
		return;
	}

	sh.reset();

	storeState(Global::get().s.bMinimalView);

	if (Global::get().talkingUI && Global::get().talkingUI->isVisible()) {
		// Save the TalkingUI's position if it is visible
		// Note that we explicitly don't save the whole geometry as the TalkingUI's size
		// is a flexible thing that'll adjust automatically anyways.
		Global::get().s.qpTalkingUI_Position = Global::get().talkingUI->pos();
	}

	if (m_searchDialog) {
		// Save position of search dialog
		Global::get().s.searchDialogPosition = { m_searchDialog->x(), m_searchDialog->y() };
	}

	if (qwPTTButtonWidget) {
		qwPTTButtonWidget->close();
		qwPTTButtonWidget->deleteLater();
		qwPTTButtonWidget = nullptr;
	}
	Global::get().bQuit = true;

	QMainWindow::closeEvent(e);

	qApp->exit(restartOnQuit ? MUMBLE_EXIT_CODE_RESTART : 0);
}

void MainWindow::hideEvent(QHideEvent *e) {
#ifdef USE_OVERLAY
	if (Global::get().ocIntercept) {
		QMetaObject::invokeMethod(Global::get().ocIntercept, "hideGui", Qt::QueuedConnection);
		e->ignore();
		return;
	}
#endif
	QMainWindow::hideEvent(e);
}

void MainWindow::showEvent(QShowEvent *e) {
	QMainWindow::showEvent(e);
#ifdef Q_OS_WIN
	applyNativeTitleBarTheme(this);
#endif
}

void MainWindow::changeEvent(QEvent *e) {
	// Parse minimize event
	if (e->type() == QEvent::WindowStateChange) {
		// This code block is not triggered on (X)Wayland due to a Qt bug we can do nothing about (QTBUG-74310)
		QWindowStateChangeEvent *windowStateEvent = static_cast< QWindowStateChangeEvent * >(e);
		if (windowStateEvent) {
			bool wasMinimizedState = (windowStateEvent->oldState() & Qt::WindowMinimized);
			bool isMinimizedState  = (windowState() & Qt::WindowMinimized);
			if (!wasMinimizedState && isMinimizedState) {
				emit windowMinimized();
			}
			return;
		}
	}

	// The window has just received focus after being in the background
	if (e->type() == QEvent::ActivationChange) {
		if (isActiveWindow()) {
			emit windowActivated();
		}
		return;
	}

	if (e->type() == QEvent::ThemeChange) {
		Themes::apply();
		refreshCustomChromeStyles();
	}

	QWidget::changeEvent(e);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
	if (event && watched
		&& (watched == m_serverNavigatorContentFrame || watched == m_serverNavigatorTextChannelsMotdFrame
			|| watched == m_serverNavigatorTextChannelsMotdBody)
		&& (event->type() == QEvent::Resize || event->type() == QEvent::Show
			|| event->type() == QEvent::LayoutRequest)) {
		QTimer::singleShot(0, this, [this]() { refreshServerNavigatorMotdHeight(); });
	}

	if (event && event->type() == QEvent::Wheel && watched
		&& watched->property("persistentChatEmbeddedBrowser").toBool() && m_persistentChatHistory) {
		QWheelEvent *wheelEvent = static_cast< QWheelEvent * >(event);
		if (QScrollBar *scrollBar = m_persistentChatHistory->verticalScrollBar()) {
			int delta = wheelEvent->pixelDelta().y();
			if (delta == 0 && !wheelEvent->angleDelta().isNull()) {
				delta = (wheelEvent->angleDelta().y() / 120) * std::max(scrollBar->singleStep(), 24);
			}
			if (delta != 0) {
				scrollBar->setValue(scrollBar->value() - delta);
				m_persistentChatHistory->stabilizeVisibleContent();
				wheelEvent->accept();
				return true;
			}
		}
	}

	return QMainWindow::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent *e) {
	// Pressing F6 switches between the main
	// window's main widgets, making it easier
	// to navigate Mumble's MainWindow with only
	// a keyboard.
	if (e->key() == Qt::Key_F6) {
		focusNextMainWidget();
	} else {
		QMainWindow::keyPressEvent(e);
	}
}

/// focusNextMainWidget switches the focus to the next main
/// widget of the MainWindow.
///
/// This is used to implement behavior where pressing F6
/// switches between major elements of an application.
/// This behavior is for example seen in Windows's (File) Explorer.
///
/// The main widgets are qteLog (the log view), the persistent chat controls
/// and qtvUsers (users tree view).
void MainWindow::focusNextMainWidget() {
	QWidget *mainFocusWidgets[] = {
		qteLog,
		m_persistentChatChannelList,
		m_persistentChatHistory,
		qteChat,
		qtvUsers,
	};
	const int numMainFocusWidgets = sizeof(mainFocusWidgets) / sizeof(mainFocusWidgets[0]);

	int currentMainFocusWidgetIndex = -1;

	QWidget *w = focusWidget();
	for (int i = 0; i < numMainFocusWidgets; i++) {
		QWidget *mainFocusWidget = mainFocusWidgets[i];
		if (mainFocusWidget && (w == mainFocusWidget || mainFocusWidget->isAncestorOf(w))) {
			currentMainFocusWidgetIndex = i;
			break;
		}
	}

	Q_ASSERT(currentMainFocusWidgetIndex != -1);

	int nextMainFocusWidgetIndex = (currentMainFocusWidgetIndex + 1) % numMainFocusWidgets;
	QWidget *nextMainFocusWidget = mainFocusWidgets[nextMainFocusWidgetIndex];
	nextMainFocusWidget->setFocus();
}

void MainWindow::updateAudioToolTips() {
	if (Global::get().s.bMute)
		qaAudioMute->setToolTip(tr("Unmute yourself"));
	else
		qaAudioMute->setToolTip(tr("Mute yourself"));

	if (Global::get().s.bDeaf)
		qaAudioDeaf->setToolTip(tr("Undeafen yourself"));
	else
		qaAudioDeaf->setToolTip(tr("Deafen yourself"));
}

void MainWindow::updateUserModel() {
	UserModel *um = static_cast< UserModel * >(qtvUsers->model());
	um->forceVisualUpdate();
}

void MainWindow::updateTransmitModeComboBox(Settings::AudioTransmit newMode) {
	switch (newMode) {
		case Settings::Continuous:
			qcbTransmitMode->setCurrentIndex(0);
			return;
		case Settings::VAD:
			qcbTransmitMode->setCurrentIndex(1);
			return;
		case Settings::PushToTalk:
			qcbTransmitMode->setCurrentIndex(2);
			return;
	}
}

QMenu *MainWindow::createPopupMenu() {
	if ((Global::get().s.wlWindowLayout == Settings::LayoutCustom) && !Global::get().s.bLockLayout) {
		// We have to explicitly create a menu here instead of simply referring to QMainWindow::createPopupMenu as we
		// don't want a toggle for showing/hiding the minimal view note (which is also present as a QDockWidget). Thus,
		// we have to explicitly add only those widgets that we really want to be toggleable.
		QMenu *menu = new QMenu(this);
		menu->addAction(qdwChat->toggleViewAction());
		menu->addAction(qdwLog->toggleViewAction());
		menu->addAction(qtIconToolbar->toggleViewAction());

		return menu;
	}

	return nullptr;
}

Channel *MainWindow::getContextMenuChannel() {
	if (cContextChannel)
		return cContextChannel.data();

	return nullptr;
}

ClientUser *MainWindow::getContextMenuUser() {
	if (cuContextUser)
		return cuContextUser.data();

	return nullptr;
}

ContextMenuTarget MainWindow::getContextMenuTargets() {
	ContextMenuTarget target;

	if (Global::get().uiSession != 0) {
		QModelIndex idx;

		if (!qpContextPosition.isNull())
			idx = qtvUsers->indexAt(qpContextPosition);

		if (!idx.isValid())
			idx = qtvUsers->currentIndex();

		target.user    = pmModel->getUser(idx);
		target.channel = pmModel->getChannel(idx);

		if (!target.user)
			target.user = getContextMenuUser();

		if (!target.channel)
			target.channel = getContextMenuChannel();
	}

	cuContextUser     = target.user;
	cContextChannel   = target.channel;
	qpContextPosition = QPoint();

	return target;
}

QString MainWindow::screenShareStreamForChannel(const Channel *channel) const {
	if (!channel || !m_screenShareManager) {
		return QString();
	}

	const QHash< QString, ScreenShareSession > &sessions = m_screenShareManager->sessions();
	for (auto it = sessions.cbegin(); it != sessions.cend(); ++it) {
		const ScreenShareSession &session = it.value();
		if (session.scope == MumbleProto::ScreenShareScopeChannel && session.scopeID == channel->iId) {
			return it.key();
		}
	}

	return QString();
}

bool MainWindow::handleSpecialContextMenu(const QUrl &url, const QPoint &pos_, bool focus) {
	// This method abuses QUrls for internal data serialization
	// The protocol, host and path parts of the URL may contain
	// special values which are only parsable by this method.

	if (url.scheme() == QString::fromLatin1("clientid")) {
		bool ok = false;
		QString maybeUserHash(url.host());
		if (maybeUserHash.length() == 40) {
			ClientUser *cu = pmModel->getUser(maybeUserHash);
			if (cu) {
				cuContextUser = cu;
				ok            = true;
			}
		} else {
			// We expect the host part of the URL to contain the user id in the format
			// id.<id>
			// where <id> is the user id as integer. This is necessary, because QUrl parses
			// plain integers in the host field as IP addresses
			QByteArray qbaServerDigest = QByteArray::fromBase64(url.path().remove(0, 1).toLatin1());
			QString id                 = url.host().split(".").value(1, "-1");
			cuContextUser              = ClientUser::get(id.toUInt(&ok, 10));
			ServerHandlerPtr sh        = Global::get().sh;
			ok                         = ok && sh && (qbaServerDigest == sh->qbaDigest);
		}
		if (ok && cuContextUser) {
			if (focus) {
				qtvUsers->setCurrentIndex(pmModel->index(cuContextUser.data()));
				qteChat->setFocus();
			} else {
				qpContextPosition = QPoint();
				qmUser->exec(pos_, nullptr);
			}
		}
		cuContextUser.clear();
	} else if (url.scheme() == QString::fromLatin1("channelid")) {
		// We expect the host part of the URL to contain the channel id in the format
		// id.<id>
		// where <id> is the channel id as integer. This is necessary, because QUrl parses
		// plain integers in the host field as IP addresses
		bool ok;
		QByteArray qbaServerDigest = QByteArray::fromBase64(url.path().remove(0, 1).toLatin1());
		QString id                 = url.host().split(".").value(1, "-1");
		cContextChannel            = Channel::get(id.toUInt(&ok, 10));
		ServerHandlerPtr sh        = Global::get().sh;
		ok                         = ok && sh && (qbaServerDigest == sh->qbaDigest);
		if (ok) {
			if (focus) {
				qtvUsers->setCurrentIndex(pmModel->index(cContextChannel.data()));
				qteChat->setFocus();
			} else {
				qpContextPosition = QPoint();
				qmChannel->exec(pos_, nullptr);
			}
		}
		cContextChannel.clear();
	} else {
		return false;
	}
	return true;
}

void MainWindow::showUsersContextMenu(const QPoint &mpos, bool usePositionForGettingContext) {
	QModelIndex idx = qtvUsers->indexAt(mpos);
	if (!idx.isValid() || !usePositionForGettingContext) {
		idx = qtvUsers->currentIndex();
	} else {
		qtvUsers->setCurrentIndex(idx);
	}

	ClientUser *p    = pmModel->getUser(idx);
	Channel *channel = pmModel->getChannel(idx);

	qpContextPosition = mpos;
	if (pmModel->isChannelListener(idx)) {
		// Have a separate context menu for listeners
		QModelIndex parent = idx.parent();

		if (parent.isValid()) {
			// Find the channel in which the action was triggered and set it
			// in order to be able to obtain it in the action itself
			cContextChannel = pmModel->getChannel(parent);
		}
		cuContextUser.clear();
		qmListener->exec(qtvUsers->mapToGlobal(mpos), nullptr);
		cuContextUser.clear();
		cContextChannel.clear();
	} else {
		if (p) {
			cuContextUser.clear();
			if (!usePositionForGettingContext) {
				cuContextUser = p;
			}

			qmUser->exec(qtvUsers->mapToGlobal(mpos), nullptr);
			cuContextUser.clear();
		} else {
			cContextChannel.clear();

			if (!usePositionForGettingContext && channel) {
				cContextChannel = channel;
			}

			qmChannel->exec(qtvUsers->mapToGlobal(mpos), nullptr);
			cContextChannel.clear();
		}
	}
	qpContextPosition = QPoint();
}

void MainWindow::showLogContextMenu(LogTextBrowser *browser, const QPoint &mpos) {
	if (!browser) {
		return;
	}

	m_selectedLogImage = QImage();

	QString link = browser->anchorAt(mpos);
	if (!link.isEmpty()) {
		QUrl l(link);

		if (handleSpecialContextMenu(l, browser->mapToGlobal(mpos)))
			return;
	}

	QPoint contentPosition =
		QPoint(QApplication::isRightToLeft()
				   ? (browser->horizontalScrollBar()->maximum() - browser->horizontalScrollBar()->value())
				   : browser->horizontalScrollBar()->value(),
			   browser->verticalScrollBar()->value());
	QMenu *menu = browser->createStandardContextMenu(mpos + contentPosition);

	QTextCursor cursor = browser->imageCursorAt(mpos);
	if (!cursor.isNull()) {
		m_selectedLogImage = imageFromLogBrowser(browser, cursor);
		if (!m_selectedLogImage.isNull()) {
			menu->addSeparator();
			menu->addAction(tr("Save Image As..."), this, SLOT(saveImageAs(void)));

			QAction *testItem = menu->addAction(tr("Open Image"));
			connect(testItem, &QAction::triggered, this, &MainWindow::showImageDialog);
		}
	}

	if (browser == qteLog) {
		menu->addSeparator();
		menu->addAction(tr("Clear"), browser, SLOT(clear(void)));
	}
	menu->exec(browser->mapToGlobal(mpos));
	delete menu;
}

void MainWindow::on_qteLog_customContextMenuRequested(const QPoint &mpos) {
	LogTextBrowser *browser = qobject_cast< LogTextBrowser * >(sender());
	if (!browser) {
		browser = qteLog;
	}

	showLogContextMenu(browser, mpos);
}

QImage MainWindow::imageFromLogBrowser(const LogTextBrowser *browser, const QTextCursor &cursor) const {
	if (!browser || cursor.isNull() || !cursor.charFormat().isImageFormat()) {
		return QImage();
	}

	const QString resourceName = cursor.charFormat().toImageFormat().name();
	const QVariant resource    = browser->document()->resource(QTextDocument::ImageResource, resourceName);
	return resource.value< QImage >();
}

void MainWindow::openImageDialog(const QImage &image) {
	if (image.isNull()) {
		QMessageBox::warning(this, tr("Error"), tr("Failed to decode image."));
		return;
	}

	const QPixmap pixmap = QPixmap::fromImage(image);
	ResponsiveImageDialog dialog(pixmap, this);
	dialog.exec();
}

void MainWindow::openImageDialog(LogTextBrowser *browser, const QTextCursor &cursor) {
	m_selectedLogImage = imageFromLogBrowser(browser, cursor);
	openImageDialog(m_selectedLogImage);
}

void MainWindow::saveImageAs() {
	QDateTime now = QDateTime::currentDateTime();
	QString defaultFname =
		QString::fromLatin1("Mumble-%1.jpg").arg(now.toString(QString::fromLatin1("yyyy-MM-dd-HHmmss")));

	QString fname = QFileDialog::getSaveFileName(this, tr("Save Image File"), getImagePath(defaultFname),
												 tr("Images (*.png *.jpg *.jpeg)"));
	if (fname.isNull()) {
		return;
	}

	const QImage img = m_selectedLogImage;
	if (img.isNull()) {
		QMessageBox::warning(this, tr("Error"), tr("Failed to decode image."));
		return;
	}
	bool ok = img.save(fname);
	if (!ok) {
		// In case fname did not contain a file extension, try saving with an
		// explicit format.
		ok = img.save(fname, "PNG");
	}

	updateImagePath(fname);

	if (!ok) {
		Global::get().l->log(Log::Warning, tr("Could not save image: %1").arg(fname.toHtmlEscaped()));
	}
}

QString MainWindow::getImagePath(QString filename) const {
	if (Global::get().s.qsImagePath.isEmpty() || !QDir(Global::get().s.qsImagePath).exists()) {
		Global::get().s.qsImagePath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
	}
	if (filename.isEmpty()) {
		return Global::get().s.qsImagePath;
	}
	return Global::get().s.qsImagePath + QDir::separator() + filename;
}

void MainWindow::updateImagePath(QString filepath) const {
	QFileInfo fi(filepath);
	Global::get().s.qsImagePath = fi.absolutePath();
}

void MainWindow::setTransmissionMode(Settings::AudioTransmit mode) {
	if (Global::get().s.atTransmit != mode) {
		Global::get().s.atTransmit = mode;

		switch (mode) {
			case Settings::Continuous:
				Global::get().l->log(Log::Information, tr("Transmit Mode set to Continuous"));
				break;
			case Settings::VAD:
				Global::get().l->log(Log::Information, tr("Transmit Mode set to Voice Activity"));
				break;
			case Settings::PushToTalk:
				Global::get().l->log(Log::Information, tr("Transmit Mode set to Push-to-Talk"));
				break;
		}

		emit transmissionModeChanged(mode);
	}
}

void MainWindow::on_qaSearch_triggered() {
	toggleSearchDialogVisibility();
}

void MainWindow::toggleSearchDialogVisibility() {
	if (!m_searchDialog) {
		m_searchDialog = new Search::SearchDialog(this);

		QPoint position = Global::get().s.searchDialogPosition;

		if (position == Settings::UNSPECIFIED_POSITION) {
			// Get MainWindow's position on screen
			position = mapToGlobal(QPoint(0, 0));
		}

		if (Mumble::QtUtils::positionIsOnScreen(position)) {
			// Move the search dialog to the same origin as the MainWindow is
			m_searchDialog->move(position);
		}
	}

	m_searchDialog->setVisible(!m_searchDialog->isVisible());
}

void MainWindow::enableRecording(bool recordingAllowed) {
	qaRecording->setEnabled(recordingAllowed);

	Global::get().recordingAllowed = recordingAllowed;

	if (!recordingAllowed && voiceRecorderDialog) {
		voiceRecorderDialog->reject();
	}
}

void MainWindow::handleUserMoved(unsigned int sessionID, const std::optional< unsigned int > &prevChannelID,
								 unsigned int newChannelID) {
	if (sessionID == Global::get().uiSession && prevChannelID.has_value()) {
		if (prevChannelID != m_movedBackFromChannel) {
			// Add to stack of previous channels
			m_previousChannels.push(prevChannelID.value());
			qaMoveBack->setEnabled(true);
		} else {
			m_movedBackFromChannel.reset();
		}

		updateChatBar();
	}

	(void) newChannelID;
}

void MainWindow::on_qaMoveBack_triggered() {
	if (m_previousChannels.empty()) {
		return;
	}

	Channel *prevChannel = Channel::get(m_previousChannels.top());
	m_previousChannels.pop();

	if (!prevChannel) {
		Global::get().l->log(Log::Warning,
							 tr("The channel you have been in previously no longer exists on this server."));
		qaMoveBack->setEnabled(false);
		return;
	}

	ClientUser *self = ClientUser::get(Global::get().uiSession);
	if (!self) {
		qaMoveBack->setEnabled(false);
		return;
	}

	ServerHandlerPtr handler = Global::get().sh;
	if (!handler) {
		qaMoveBack->setEnabled(false);
		return;
	}

	// Setting this prevents the user's current channel to be added to the stack
	// of last visited channels. If it was added, the user could only ever cycle
	// between the last channel and the current one.
	m_movedBackFromChannel = self->cChannel->iId;
	handler->joinChannel(Global::get().uiSession, prevChannel->iId);

	qaMoveBack->setEnabled(!m_previousChannels.empty());
}

static void recreateServerHandler() {
	ServerHandlerPtr sh = Global::get().sh;
	if (sh && sh->isRunning()) {
		Global::get().mw->on_qaServerDisconnect_triggered();
		sh->disconnect();
		sh->wait();
		QCoreApplication::instance()->processEvents();
	}

	Global::get().sh.reset();
	while (sh && sh.use_count() > 1) {
		QThread::yieldCurrentThread();
	}
	sh.reset();

	sh = ServerHandlerPtr(new ServerHandler());
	sh->moveToThread(sh.get());
	Global::get().sh = sh;
	Global::get().mw->connect(sh.get(), SIGNAL(connected()), Global::get().mw, SLOT(serverConnected()));
	Global::get().mw->connect(sh.get(), SIGNAL(disconnected(QAbstractSocket::SocketError, QString)), Global::get().mw,
							  SLOT(serverDisconnected(QAbstractSocket::SocketError, QString)));
	Global::get().mw->connect(sh.get(), SIGNAL(error(QAbstractSocket::SocketError, QString)), Global::get().mw,
							  SLOT(resolverError(QAbstractSocket::SocketError, QString)));

	QObject::connect(sh.get(), &ServerHandler::disconnected, Global::get().talkingUI,
					 &TalkingUI::on_serverDisconnected);

	// We have to use direct connections for these here as the PluginManager must be able to access the connection's ID
	// and in order for that to be possible the (dis)connection process must not proceed in the background.
	Global::get().pluginManager->connect(sh.get(), &ServerHandler::connected, Global::get().pluginManager,
										 &PluginManager::on_serverConnected, Qt::DirectConnection);
	// We connect the plugin manager to "aboutToDisconnect" instead of "disconnect" in order for the slot to be
	// guaranteed to be completed *before* the actual disconnect logic (e.g. MainWindow::serverDisconnected) kicks in.
	// In order for that to work it is ESSENTIAL to use a DIRECT CONNECTION!
	Global::get().pluginManager->connect(sh.get(), &ServerHandler::aboutToDisconnect, Global::get().pluginManager,
										 &PluginManager::on_serverDisconnected, Qt::DirectConnection);
}

void MainWindow::openUrl(const QUrl &url) {
	Global::get().l->log(Log::Information,
						 tr("Opening URL %1").arg(url.toString(QUrl::RemovePassword).toHtmlEscaped()));
	if (url.scheme() == QLatin1String("file")) {
		QFile f(url.toLocalFile());
		if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
			Global::get().l->log(Log::Warning, tr("File does not exist"));
			return;
		}
		f.close();

		try {
			Settings newSettings;
			newSettings.load(f.fileName());

			std::swap(newSettings, Global::get().s);

			Global::get().l->log(Log::Warning, tr("Settings merged from file."));
		} catch (const std::exception &) {
			Global::get().l->log(Log::Warning, tr("Invalid settings file encountered."));
		}

		return;
	}

	if (url.scheme() != QLatin1String("mumble")) {
		Global::get().l->log(Log::Warning, tr("URL scheme is not 'mumble'"));
		return;
	}

	Version::full_t thisVersion   = Version::get();
	Version::full_t targetVersion = Version::UNKNOWN;

	QUrlQuery query(url);
	QString version = query.queryItemValue(QLatin1String("version"));
	if (version.size() > 0) {
		targetVersion = Version::fromString(version);
		if (targetVersion == Version::UNKNOWN) {
			// The version format is invalid
			Global::get().l->log(Log::Warning,
								 QObject::tr("The provided URL uses an invalid version format: \"%1\"").arg(version));
			return;
		}
	}

	// With no version parameter given assume the link refers to our version
	if (targetVersion == Version::UNKNOWN) {
		targetVersion = thisVersion;
	}

	// We can't handle URLs for versions < 1.2.0
	const bool isPre_120 = targetVersion < Version::fromComponents(1, 2, 0);
	// We also can't handle URLs for versions newer than the running Mumble instance
	const bool isFuture = thisVersion < targetVersion;

	if (isPre_120 || isFuture) {
		Global::get().l->log(
			Log::Warning,
			tr("This version of Mumble can't handle URLs for Mumble version %1").arg(Version::toString(targetVersion)));
		return;
	}

	QString host        = url.host();
	unsigned short port = static_cast< unsigned short >(url.port(DEFAULT_MUMBLE_PORT));
	QString user        = url.userName();
	QString pw          = url.password();
	qsDesiredChannel    = url.path();
	QString name;

	if (query.hasQueryItem(QLatin1String("title")))
		name = query.queryItemValue(QLatin1String("title"));

	if (Global::get().sh && Global::get().sh->isRunning()) {
		QString oHost, oUser, oPw;
		unsigned short oport;
		Global::get().sh->getConnectionInfo(oHost, oport, oUser, oPw);
		ClientUser *p = ClientUser::get(Global::get().uiSession);

		if ((user.isEmpty() || (p && p->iId >= 0) || (user == oUser))
			&& (host.isEmpty() || ((host == oHost) && (port == oport)))) {
			findDesiredChannel();
			return;
		}
	}

	Global::get().db->fuzzyMatch(name, user, pw, host, port);

	if (user.isEmpty()) {
		bool ok;
		user = QInputDialog::getText(this, tr("Connecting to %1").arg(url.toString()), tr("Enter username"),
									 QLineEdit::Normal, Global::get().s.qsUsername, &ok);
		if (!ok || user.isEmpty())
			return;
		Global::get().s.qsUsername = user;
	}

	if (name.isEmpty())
		name = QString::fromLatin1("%1@%2").arg(user).arg(host);

	recreateServerHandler();

	Global::get().s.qsLastServer = name;
	rtLast                       = MumbleProto::Reject_RejectType_None;
	bRetryServer                 = true;
	qaServerDisconnect->setEnabled(true);
	Global::get().l->log(Log::Information,
						 tr("Connecting to server %1.").arg(Log::msgColor(host.toHtmlEscaped(), Log::Server)));
	Global::get().sh->setConnectionInfo(host, port, user, pw);
	Global::get().sh->start(QThread::TimeCriticalPriority);
}

/**
 * This function tries to join a desired channel on connect. It gets called
 * directly after server synchronization is completed.
 * @see void MainWindow::msgServerSync(const MumbleProto::ServerSync &msg)
 */
void MainWindow::findDesiredChannel() {
	bool found          = false;
	QStringList qlChans = qsDesiredChannel.split(QLatin1String("/"));
	Channel *chan       = Channel::get(Mumble::ROOT_CHANNEL_ID);
	QString str         = QString();
	while (chan && qlChans.count() > 0) {
		QString elem = qlChans.takeFirst().toLower();
		if (elem.isEmpty())
			continue;
		if (str.isNull())
			str = elem;
		else
			str = str + QLatin1String("/") + elem;
		for (Channel *c : chan->qlChannels) {
			if (c->qsName.toLower() == str) {
				str   = QString();
				found = true;
				chan  = c;
				break;
			}
		}
	}
	if (found) {
		if (chan != ClientUser::get(Global::get().uiSession)->cChannel) {
			Global::get().sh->joinChannel(Global::get().uiSession, chan->iId);
		}
		qtvUsers->setCurrentIndex(pmModel->index(chan));
	} else if (Global::get().uiSession) {
		qtvUsers->setCurrentIndex(pmModel->index(ClientUser::get(Global::get().uiSession)->cChannel));
	}
	updateMenuPermissions();
}

void MainWindow::setOnTop(bool top) {
	Qt::WindowFlags wf = windowFlags();
	if (wf.testFlag(Qt::WindowStaysOnTopHint) != top) {
		if (top)
			wf |= Qt::WindowStaysOnTopHint;
		else
			wf &= ~Qt::WindowStaysOnTopHint;
		setWindowFlags(wf);
		show();
	}
}

void MainWindow::loadState(const bool minimalView) {
	if (minimalView) {
		if (!Global::get().s.qbaMinimalViewGeometry.isNull()) {
			restoreGeometry(Global::get().s.qbaMinimalViewGeometry);
		}
		if (!Global::get().s.qbaMinimalViewState.isNull()) {
			restoreState(Global::get().s.qbaMinimalViewState, stateVersion());
		}
	} else {
		if (!Global::get().s.qbaMainWindowGeometry.isNull()) {
			restoreGeometry(Global::get().s.qbaMainWindowGeometry);
		}
		if (!Global::get().s.qbaMainWindowState.isNull()) {
			restoreState(Global::get().s.qbaMainWindowState, stateVersion());
		}
	}
}

void MainWindow::storeState(const bool minimalView) {
	if (minimalView) {
		Global::get().s.qbaMinimalViewGeometry = saveGeometry();
		Global::get().s.qbaMinimalViewState    = saveState(stateVersion());
	} else {
		Global::get().s.qbaMainWindowGeometry = saveGeometry();
		Global::get().s.qbaMainWindowState    = saveState(stateVersion());
	}
}

void MainWindow::setupView(bool toggle_minimize) {
	bool showit = !Global::get().s.bMinimalView;

	switch (Global::get().s.wlWindowLayout) {
		case Settings::LayoutClassic:
			removeDockWidget(qdwChat);
			removeDockWidget(qdwLog);
			addDockWidget(Qt::LeftDockWidgetArea, qdwChat);
			qdwChat->show();
			splitDockWidget(qdwChat, qdwLog, Qt::Vertical);
			qdwLog->show();
			break;
		case Settings::LayoutStacked:
			removeDockWidget(qdwChat);
			removeDockWidget(qdwLog);
			addDockWidget(Qt::BottomDockWidgetArea, qdwChat);
			qdwChat->show();
			splitDockWidget(qdwChat, qdwLog, Qt::Vertical);
			qdwLog->show();
			break;
		case Settings::LayoutHybrid:
			removeDockWidget(qdwChat);
			removeDockWidget(qdwLog);
			addDockWidget(Qt::LeftDockWidgetArea, qdwChat);
			qdwChat->show();
			splitDockWidget(qdwChat, qdwLog, Qt::Vertical);
			qdwLog->show();
			break;
		default:
			break;
	}

	if (Global::get().s.wlWindowLayout != Settings::LayoutCustom) {
		removeDockWidget(qdwLog);
		qdwLog->hide();
		const Qt::Orientation dockResizeOrientation =
			Global::get().s.wlWindowLayout == Settings::LayoutStacked ? Qt::Vertical : Qt::Horizontal;
		const int dockResizeTarget = dockResizeOrientation == Qt::Vertical ? 720 : 740;
		resizeDocks(QList< QDockWidget * >() << qdwChat, QList< int >() << dockResizeTarget,
					dockResizeOrientation);
	}

	setDockSplitterHandleWidth(this, 1);

	updateToolbar();

	qteChat->updateGeometry();

	QRect geom = frameGeometry();

	if (toggle_minimize) {
		storeState(showit);
	}

	Qt::WindowFlags f = Qt::Window;
	if (!showit) {
		if (Global::get().s.bHideFrame) {
			f |= Qt::FramelessWindowHint;
		}
	}

	if (Global::get().s.aotbAlwaysOnTop == Settings::OnTopAlways
		|| (Global::get().s.bMinimalView && Global::get().s.aotbAlwaysOnTop == Settings::OnTopInMinimal)
		|| (!Global::get().s.bMinimalView && Global::get().s.aotbAlwaysOnTop == Settings::OnTopInNormal)) {
		f |= Qt::WindowStaysOnTopHint;
	}

	if (!graphicsProxyWidget())
		setWindowFlags(f);

	if (Global::get().s.bShowContextMenuInMenuBar) {
		bool found = false;
		for (QAction *a : menuBar()->actions()) {
			if (a == qmUser->menuAction()) {
				found = true;
				break;
			}
		}

		if (!found) {
			menuBar()->insertMenu(qmConfig->menuAction(), qmUser);
			menuBar()->insertMenu(qmConfig->menuAction(), qmChannel);
		}
	} else {
		menuBar()->removeAction(qmUser->menuAction());
		menuBar()->removeAction(qmChannel->menuAction());
	}

	if (Global::get().s.bEnableDeveloperMenu) {
		bool found = false;
		for (QAction *a : menuBar()->actions()) {
			if (a == qmDeveloper->menuAction()) {
				found = true;
				break;
			}
		}

		if (!found) {
			menuBar()->insertMenu(qmHelp->menuAction(), qmDeveloper);
		}
	} else {
		menuBar()->removeAction(qmDeveloper->menuAction());
	}

	if (toggle_minimize) {
		loadState(!showit);
	} else {
		QRect newgeom = frameGeometry();
		resize(geometry().width() - newgeom.width() + geom.width(),
			   geometry().height() - newgeom.height() + geom.height());
		move(geom.x(), geom.y());
	}


	// Explicitly hide UI elements, if we're entering minimal view
	// Note that showing them again is handled above via restoreState/restoreGeometry calls
	if (!showit) {
		qdwLog->setVisible(false);
		qdwChat->setVisible(false);
		qtIconToolbar->setVisible(false);
	}
	menuBar()->setVisible(showit);

	if (showit) {
		qdwMinimalViewNote->hide();
	} else if (!Global::get().sh) {
		// Show the note, if we're not connected to a server
		qdwMinimalViewNote->show();
	}

	// Display the Transmit Mode Dropdown, if configured to do so, otherwise
	// hide it.
	if (Global::get().s.bShowTransmitModeComboBox) {
		qaTransmitMode->setVisible(true);
		qaTransmitModeSeparator->setVisible(true);
	} else {
		qaTransmitMode->setVisible(false);
		qaTransmitModeSeparator->setVisible(false);
	}

	// If activated show the PTT window
	if (Global::get().s.bShowPTTButtonWindow && Global::get().s.atTransmit == Settings::PushToTalk) {
		if (qwPTTButtonWidget) {
			qwPTTButtonWidget->show();
		} else {
			qwPTTButtonWidget = new PTTButtonWidget();
			qwPTTButtonWidget->show();
			connect(qwPTTButtonWidget, SIGNAL(triggered(bool, QVariant)),
					SLOT(on_PushToTalk_triggered(bool, QVariant)));
		}
	} else {
		if (qwPTTButtonWidget) {
			qwPTTButtonWidget->deleteLater();
			qwPTTButtonWidget = nullptr;
		}
	}
}

void MainWindow::on_qaServerConnect_triggered(bool autoconnect) {
	openServerConnectDialog(autoconnect);
}

void MainWindow::on_Reconnect_timeout() {
	if (Global::get().sh->isRunning()) {
		return;
	}

	if (!m_reconnectSoundBlocker) {
		m_reconnectSoundBlocker = std::make_unique< NotificationSoundBlocker >(Log::MsgType::ServerDisconnected);
	}

	Global::get().l->log(Log::Information, tr("Reconnecting."));
	Global::get().sh->start(QThread::TimeCriticalPriority);
}

void MainWindow::on_qmSelf_aboutToShow() {
	ClientUser *user = ClientUser::get(Global::get().uiSession);

	qaServerTexture->setEnabled(user != nullptr);
	qaSelfComment->setEnabled(user != nullptr);

	qaServerTextureRemove->setEnabled(user && !user->qbaTextureHash.isEmpty());

	qaSelfRegister->setEnabled(user && (user->iId < 0) && !user->qsHash.isEmpty()
							   && (Global::get().pPermissions & (ChanACL::SelfRegister | ChanACL::Write)));
	if (Global::get().sh && Global::get().sh->m_version >= Version::fromComponents(1, 2, 3)) {
		qaSelfPrioritySpeaker->setEnabled(user && Global::get().pPermissions & (ChanACL::Write | ChanACL::MuteDeafen));
		qaSelfPrioritySpeaker->setChecked(user && user->bPrioritySpeaker);
	} else {
		qaSelfPrioritySpeaker->setEnabled(false);
		qaSelfPrioritySpeaker->setChecked(false);
	}
}

void MainWindow::on_qaSelfComment_triggered() {
	openSelfCommentDialog();
}

void MainWindow::on_qaSelfRegister_triggered() {
	selfRegister();
}

void MainWindow::qcbTransmitMode_activated(int index) {
	switch (index) {
		case 0: // Continuous
			setTransmissionMode(Settings::Continuous);
			break;
		case 1: // Voice Activity
			setTransmissionMode(Settings::VAD);
			break;
		case 2: // Push-to-Talk
			setTransmissionMode(Settings::PushToTalk);
			break;
	}
}

void MainWindow::on_qmServer_aboutToShow() {
	qmServer->clear();
	qmServer->addAction(qaServerConnect);
	qmServer->addSeparator();
	qmServer->addAction(qaServerDisconnect);
	qmServer->addAction(qaServerInformation);
	qmServer->addAction(qaServerAddToFavorites);
	qmServer->addAction(qaSearch);
	qmServer->addAction(qaServerTokens);
	qmServer->addAction(qaServerUserList);
	qmServer->addAction(qaServerBanList);
	qmServer->addSeparator();
	qmServer->addAction(qaQuit);

	qaServerBanList->setEnabled(Global::get().pPermissions & (ChanACL::Ban | ChanACL::Write));
	qaServerUserList->setEnabled(Global::get().pPermissions & (ChanACL::Register | ChanACL::Write));
	qaServerInformation->setEnabled(Global::get().uiSession != 0);
	updateFavoriteButton();
	qaServerTokens->setEnabled(Global::get().uiSession != 0);

	if (!qlServerActions.isEmpty()) {
		qmServer->addSeparator();
		for (QAction *a : qlServerActions) {
			qmServer->addAction(a);
		}
	}
}

void MainWindow::on_qaServerDisconnect_triggered() {
	disconnectFromServer();
}

void MainWindow::on_qaServerBanList_triggered() {
	openServerBanListDialog();
}

void MainWindow::on_qaServerUserList_triggered() {
	openServerUserListDialog();
}

void MainWindow::on_qaServerInformation_triggered() {
	openServerInformationDialog();
}

void MainWindow::on_qaServerTexture_triggered() {
	changeServerTexture();
}

void MainWindow::on_qaServerTextureRemove_triggered() {
	removeServerTexture();
}

void MainWindow::on_qaServerTokens_triggered() {
	openServerTokensDialog();
}

void MainWindow::voiceRecorderDialog_finished(int) {
	voiceRecorderDialog->deleteLater();
	voiceRecorderDialog = nullptr;
}

void MainWindow::qmUser_aboutToShow() {
	ClientUser *p = getContextMenuTargets().user;

	const ClientUser *self = ClientUser::get(Global::get().uiSession);
	bool isSelf            = p == self;

	qmUser->clear();

	if (self && p && !isSelf && self->cChannel != p->cChannel) {
		qmUser->addAction(qaUserJoin);
		qmUser->addAction(qaUserMove);
		qmUser->addSeparator();
	}

	if (Global::get().pPermissions & (ChanACL::Kick | ChanACL::Ban | ChanACL::Write))
		qmUser->addAction(qaUserKick);
	if (Global::get().pPermissions & (ChanACL::Ban | ChanACL::Write))
		qmUser->addAction(qaUserBan);
	qmUser->addAction(qaUserMute);
	qmUser->addAction(qaUserDeaf);
	if (Global::get().sh && Global::get().sh->m_version >= Version::fromComponents(1, 2, 3))
		qmUser->addAction(qaUserPrioritySpeaker);
	qmUser->addAction(qaUserLocalMute);
#ifdef USE_RNNOISE
	qmUser->addAction(qaUserRemoteSpeechCleanup);
#endif
	qmUser->addAction(qaUserLocalIgnore);
	if (Global::get().s.bTTS)
		qmUser->addAction(qaUserLocalIgnoreTTS);

	if (p && !isSelf) {
		qmUser->addSeparator();
		qmUser->addAction(m_localVolumeLabel.get());
		m_userLocalVolumeSlider->setUser(p->uiSession);
		qmUser->addAction(m_userLocalVolumeSlider.get());
		qmUser->addSeparator();
	}

	qmUser->addAction(qaUserLocalNickname);

	if (isSelf)
		qmUser->addAction(qaSelfComment);
	else {
		qmUser->addAction(qaUserCommentView);
		qmUser->addAction(qaUserCommentReset);
		qmUser->addAction(qaUserTextureReset);
	}

	qmUser->addAction(qaUserTextMessage);
	if (Global::get().sh && Global::get().sh->m_version >= Version::fromComponents(1, 2, 2))
		qmUser->addAction(qaUserInformation);

	if (p && (p->iId < 0) && !p->qsHash.isEmpty()
		&& (Global::get().pPermissions & ((isSelf ? ChanACL::SelfRegister : ChanACL::Register) | ChanACL::Write))) {
		qmUser->addSeparator();
		qmUser->addAction(qaUserRegister);
	}

	if (p && !p->qsHash.isEmpty() && (!p->qsFriendName.isEmpty() || (p->uiSession != Global::get().uiSession))) {
		qmUser->addSeparator();
		if (p->qsFriendName.isEmpty())
			qmUser->addAction(qaUserFriendAdd);
		else {
			if (p->qsFriendName != p->qsName)
				qmUser->addAction(qaUserFriendUpdate);
			qmUser->addAction(qaUserFriendRemove);
		}
	}

	if (isSelf) {
		qmUser->addSeparator();
		qmUser->addAction(qaAudioMute);
		qmUser->addAction(qaAudioDeaf);
	}

#ifndef Q_OS_MAC
	if (Global::get().s.bMinimalView) {
		qmUser->addSeparator();
		qmUser->addMenu(qmServer);
		qmUser->addMenu(qmSelf);
		qmUser->addMenu(qmConfig);
		qmUser->addMenu(qmHelp);
	}
#endif

	if (!qlUserActions.isEmpty()) {
		qmUser->addSeparator();
		for (QAction *a : qlUserActions) {
			qmUser->addAction(a);
		}
	}

	if (!p) {
		qaUserKick->setEnabled(false);
		qaUserBan->setEnabled(false);
		qaUserTextMessage->setEnabled(false);
		qaUserLocalNickname->setEnabled(false);
		qaUserLocalMute->setEnabled(false);
		qaUserRemoteSpeechCleanup->setEnabled(false);
		qaUserRemoteSpeechCleanup->setChecked(false);
		qaUserLocalIgnore->setEnabled(false);
		qaUserLocalIgnoreTTS->setEnabled(false);
		qaUserCommentReset->setEnabled(false);
		qaUserTextureReset->setEnabled(false);
		qaUserCommentView->setEnabled(false);
	} else {
		qaUserKick->setEnabled(!isSelf);
		qaUserBan->setEnabled(!isSelf);
		qaUserTextMessage->setEnabled(true);
		qaUserLocalNickname->setEnabled(!isSelf);
		qaUserLocalMute->setEnabled(!isSelf);
		qaUserRemoteSpeechCleanup->setEnabled(!isSelf);
		qaUserRemoteSpeechCleanup->setChecked(!isSelf && p->isRemoteSpeechCleanupEnabled());
		qaUserLocalIgnore->setEnabled(!isSelf);
		qaUserLocalIgnoreTTS->setEnabled(!isSelf);
		// If the server's version is less than 1.4.0 it won't support the new permission to reset a comment/avatar, so
		// fall back to the old method
		if (Global::get().sh->m_version < Version::fromComponents(1, 4, 0)) {
			qaUserCommentReset->setEnabled(!p->qbaCommentHash.isEmpty()
										   && (Global::get().pPermissions & (ChanACL::Move | ChanACL::Write)));
			qaUserTextureReset->setEnabled(!p->qbaTextureHash.isEmpty()
										   && (Global::get().pPermissions & (ChanACL::Move | ChanACL::Write)));
		} else {
			qaUserCommentReset->setEnabled(
				!p->qbaCommentHash.isEmpty()
				&& (Global::get().pPermissions & (ChanACL::ResetUserContent | ChanACL::Write)));
			qaUserTextureReset->setEnabled(
				!p->qbaTextureHash.isEmpty()
				&& (Global::get().pPermissions & (ChanACL::ResetUserContent | ChanACL::Write)));
		}
		qaUserCommentView->setEnabled(!p->qbaCommentHash.isEmpty());

		qaUserMute->setChecked(p->bMute || p->bSuppress);
		qaUserDeaf->setChecked(p->bDeaf);
		qaUserPrioritySpeaker->setChecked(p->bPrioritySpeaker);
		qaUserLocalMute->setChecked(p->bLocalMute);
		qaUserLocalIgnore->setChecked(p->bLocalIgnore);
		qaUserLocalIgnoreTTS->setChecked(p->bLocalIgnoreTTS);
	}
	updateMenuPermissions();
}

void MainWindow::qmListener_aboutToShow() {
	ClientUser *p = getContextMenuTargets().user;

	bool self = p && (p->uiSession == Global::get().uiSession);

	qmListener->clear();

	if (self) {
		qmListener->addAction(m_localVolumeLabel.get());
		Channel *channel = getContextMenuChannel();
		if (channel) {
			m_listenerVolumeSlider->setListenedChannel(*channel);
			qmListener->addAction(m_listenerVolumeSlider.get());
			qmListener->addSeparator();
		}

		if (cContextChannel) {
			qmListener->addAction(qaChannelListen);
			qaChannelListen->setChecked(
				Global::get().channelListenerManager->isListening(Global::get().uiSession, cContextChannel->iId));
		}
	} else {
		qmListener->addAction(qaEmpty);
	}
}

void MainWindow::startChannelScreenShare() {
	Channel *c = getContextMenuChannel();
	if (!c || !m_screenShareManager) {
		return;
	}

	m_screenShareManager->requestStartChannelShare(static_cast< unsigned int >(c->iId));
}

void MainWindow::stopChannelScreenShare() {
	Channel *c = getContextMenuChannel();
	if (!c || !m_screenShareManager) {
		return;
	}

	const QString streamID = screenShareStreamForChannel(c);
	if (streamID.isEmpty()) {
		return;
	}

	m_screenShareManager->requestStopShare(streamID);
}

void MainWindow::watchChannelScreenShare() {
	Channel *c = getContextMenuChannel();
	if (!c || !m_screenShareManager) {
		return;
	}

	const QString streamID = screenShareStreamForChannel(c);
	if (streamID.isEmpty()) {
		return;
	}

	m_screenShareManager->requestStartViewing(streamID);
}

void MainWindow::stopWatchingChannelScreenShare() {
	Channel *c = getContextMenuChannel();
	if (!c || !m_screenShareManager) {
		return;
	}

	const QString streamID = screenShareStreamForChannel(c);
	if (streamID.isEmpty()) {
		return;
	}

	m_screenShareManager->requestStopViewing(streamID);
}

void MainWindow::on_qaUserMute_triggered() {
	ClientUser *p = getContextMenuUser();
	if (!p)
		return;

	MumbleProto::UserState mpus;
	mpus.set_session(p->uiSession);
	if (p->bMute || p->bSuppress) {
		if (p->bMute)
			mpus.set_mute(false);
		if (p->bSuppress)
			mpus.set_suppress(false);
	} else {
		mpus.set_mute(true);
	}
	Global::get().sh->sendMessage(mpus);
}

void MainWindow::on_qaUserLocalMute_triggered() {
	ClientUser *p = getContextMenuUser();
	if (!p) {
		return;
	}

	bool muted = qaUserLocalMute->isChecked();

	p->setLocalMute(muted);
	if (!p->qsHash.isEmpty()) {
		Global::get().db->setLocalMuted(p->qsHash, muted);
	} else {
		logChangeNotPermanent(QObject::tr("Local Mute"), p);
	}
}

void MainWindow::triggerUserRemoteSpeechCleanup() {
	ClientUser *p = getContextMenuUser();
	if (!p) {
		return;
	}

	const bool enabled = qaUserRemoteSpeechCleanup->isChecked();
	const std::optional< bool > override =
		enabled == Global::get().s.remoteSpeechCleanupEnabled ? std::nullopt : std::make_optional(enabled);

	p->setRemoteSpeechCleanupOverride(override);
	if (!p->qsHash.isEmpty()) {
		Global::get().db->setUserRemoteSpeechCleanup(p->qsHash, override);
	} else if (override.has_value()) {
		logChangeNotPermanent(QObject::tr("Remote Speech Cleanup"), p);
	}
}

void MainWindow::on_qaUserLocalIgnore_triggered() {
	ClientUser *p = getContextMenuUser();
	if (!p) {
		return;
	}

	bool ignored = qaUserLocalIgnore->isChecked();

	p->setLocalIgnore(ignored);
	if (!p->qsHash.isEmpty()) {
		Global::get().db->setLocalIgnored(p->qsHash, ignored);
	} else {
		logChangeNotPermanent(QObject::tr("Ignore Messages"), p);
	}
}

void MainWindow::on_qaUserLocalIgnoreTTS_triggered() {
	ClientUser *p = getContextMenuUser();
	if (!p) {
		return;
	}

	bool ignoredTTS = qaUserLocalIgnoreTTS->isChecked();

	p->setLocalIgnoreTTS(ignoredTTS);
	if (!p->qsHash.isEmpty()) {
		Global::get().db->setLocalIgnoredTTS(p->qsHash, ignoredTTS);
	} else {
		logChangeNotPermanent(QObject::tr("Disable Text-To-Speech"), p);
	}
}

void MainWindow::on_qaUserDeaf_triggered() {
	ClientUser *p = getContextMenuUser();
	if (!p)
		return;

	MumbleProto::UserState mpus;
	mpus.set_session(p->uiSession);
	mpus.set_deaf(!p->bDeaf);
	Global::get().sh->sendMessage(mpus);
}

void MainWindow::on_qaSelfPrioritySpeaker_triggered() {
	toggleSelfPrioritySpeaker();
}

void MainWindow::on_qaUserPrioritySpeaker_triggered() {
	ClientUser *p = getContextMenuUser();
	if (!p)
		return;

	MumbleProto::UserState mpus;
	mpus.set_session(p->uiSession);
	mpus.set_priority_speaker(!p->bPrioritySpeaker);
	Global::get().sh->sendMessage(mpus);
}

void MainWindow::on_qaUserRegister_triggered() {
	ClientUser *p = getContextMenuUser();
	if (!p)
		return;

	unsigned int session = p->uiSession;

	QMessageBox::StandardButton result;

	if (session == Global::get().uiSession)
		result = QMessageBox::question(
			this, tr("Register yourself as %1").arg(p->qsName),
			tr("<p>You are about to register yourself on this server. This action cannot be undone, and your username "
			   "cannot be changed once this is done. You will forever be known as '%1' on this server.</p><p>Are you "
			   "sure you want to register yourself?</p>")
				.arg(p->qsName.toHtmlEscaped()),
			QMessageBox::Yes | QMessageBox::No);
	else
		result = QMessageBox::question(
			this, tr("Register user %1").arg(p->qsName),
			tr("<p>You are about to register %1 on the server. This action cannot be undone, the username cannot be "
			   "changed, and as a registered user, %1 will have access to the server even if you change the server "
			   "password.</p><p>From this point on, %1 will be authenticated with the certificate currently in "
			   "use.</p><p>Are you sure you want to register %1?</p>")
				.arg(p->qsName.toHtmlEscaped()),
			QMessageBox::Yes | QMessageBox::No);

	if (result == QMessageBox::Yes) {
		p = ClientUser::get(session);
		if (!p)
			return;
		Global::get().sh->registerUser(p->uiSession);
	}
}

void MainWindow::on_qaUserFriendAdd_triggered() {
	ClientUser *p = getContextMenuUser();
	if (!p)
		return;

	Global::get().db->addFriend(p->qsName, p->qsHash);
	pmModel->setFriendName(p, p->qsName);
}

void MainWindow::on_qaUserFriendUpdate_triggered() {
	on_qaUserFriendAdd_triggered();
}

void MainWindow::on_qaUserFriendRemove_triggered() {
	ClientUser *p = getContextMenuUser();
	if (!p)
		return;

	Global::get().db->removeFriend(p->qsHash);
	pmModel->setFriendName(p, QString());
}

void MainWindow::on_qaUserKick_triggered() {
	ClientUser *p = getContextMenuUser();
	if (!p) {
		return;
	}

	unsigned int session = p->uiSession;

	bool ok;
	QString reason = QInputDialog::getText(this, tr("Kicking user %1").arg(p->qsName), tr("Enter reason"),
										   QLineEdit::Normal, QString(), &ok);

	p = ClientUser::get(session);
	if (!p) {
		return;
	}

	if (ok) {
		Global::get().sh->kickUser(p->uiSession, reason);
	}
}

void MainWindow::on_qaUserBan_triggered() {
	ClientUser *p = getContextMenuUser();
	if (!p) {
		return;
	}

	BanDialog *banDialog = new BanDialog(*p, this);
	banDialog->show();
}

void MainWindow::on_qaUserTextMessage_triggered() {
	ClientUser *p = getContextMenuUser();

	if (!p)
		return;

	openTextMessageDialog(p);
}

void MainWindow::openTextMessageDialog(ClientUser *p) {
	unsigned int session = p->uiSession;

	::TextMessage *texm = new ::TextMessage(this, tr("Sending message to %1").arg(p->qsName));
	int res             = texm->exec();

	// Try to get find the user using the session id.
	// This will return nullptr if the user disconnected while typing the message.
	p = ClientUser::get(session);

	if (p && (res == QDialog::Accepted)) {
		QString msg = texm->message();

		if (!msg.isEmpty()) {
			Global::get().sh->sendUserTextMessage(p->uiSession, msg);
			Global::get().l->log(Log::TextMessage,
								 tr("To %1: %2").arg(Log::formatClientUser(p, Log::Target), texm->message()),
								 tr("Message to %1").arg(p->qsName), true);
		}
	}
	delete texm;
}

void MainWindow::on_qaUserLocalNickname_triggered() {
	ClientUser *p = getContextMenuUser();

	if (!p)
		return;

	openUserLocalNicknameDialog(*p);
}

void MainWindow::openUserLocalNicknameDialog(const ClientUser &p) {
	unsigned int session = p.uiSession;
	UserLocalNicknameDialog::present(session, qmUserNicknameTracker, this);
}

void MainWindow::on_qaUserCommentView_triggered() {
	ClientUser *p = getContextMenuUser();
	// This has to be done here because UserModel could've set it.
	cuContextUser.clear();

	if (!p)
		return;

	if (!p->qbaCommentHash.isEmpty() && p->qsComment.isEmpty()) {
		p->qsComment = QString::fromUtf8(Global::get().db->blob(p->qbaCommentHash));
		if (p->qsComment.isEmpty()) {
			pmModel->uiSessionComment = ~(p->uiSession);
			MumbleProto::RequestBlob mprb;
			mprb.add_session_comment(p->uiSession);
			Global::get().sh->sendMessage(mprb);
			return;
		}
	}

	pmModel->seenComment(pmModel->index(p));

	::TextMessage *texm = new ::TextMessage(this, tr("View comment on user %1").arg(p->qsName));

	texm->rteMessage->setText(p->qsComment, true);
	texm->setAttribute(Qt::WA_DeleteOnClose, true);
	texm->show();
}

void MainWindow::on_qaUserCommentReset_triggered() {
	ClientUser *p = getContextMenuUser();

	if (!p)
		return;

	unsigned int session = p->uiSession;

	int ret = QMessageBox::question(
		this, QLatin1String("Mumble"),
		tr("Are you sure you want to reset the comment of user %1?").arg(p->qsName.toHtmlEscaped()), QMessageBox::Yes,
		QMessageBox::No);
	if (ret == QMessageBox::Yes) {
		Global::get().sh->setUserComment(session, QString());
	}
}

void MainWindow::on_qaUserTextureReset_triggered() {
	ClientUser *p = getContextMenuUser();

	if (!p)
		return;

	unsigned int session = p->uiSession;

	int ret = QMessageBox::question(
		this, QLatin1String("Mumble"),
		tr("Are you sure you want to reset the avatar of user %1?").arg(p->qsName.toHtmlEscaped()), QMessageBox::Yes,
		QMessageBox::No);
	if (ret == QMessageBox::Yes) {
		Global::get().sh->setUserTexture(session, QByteArray());
	}
}

void MainWindow::on_qaUserInformation_triggered() {
	ClientUser *p = getContextMenuUser();

	if (!p)
		return;

	Global::get().sh->requestUserStats(p->uiSession, false);
}

void MainWindow::on_qaQuit_triggered() {
	forceQuit = true;
	this->close();
}

void MainWindow::sendChatbarText(QString qsText, bool plainText) {
	const PersistentChatTarget target = currentPersistentChatTarget();
	if (target.valid && !target.readOnly && !target.directMessage && !target.legacyTextPath && Global::get().sh) {
		const QString normalizedText = qsText.replace(QLatin1String("\r\n"), QLatin1String("\n"))
										 .replace(QLatin1Char('\r'), QLatin1Char('\n'));
		Global::get().sh->sendChatMessage(
			target.scope, target.scopeID, normalizedText,
			plainText ? MumbleProto::ChatBodyFormatPlainText : MumbleProto::ChatBodyFormatMarkdownLite,
			m_pendingPersistentChatReply
				? std::optional< unsigned int >(m_pendingPersistentChatReply->message_id())
				: std::nullopt);
		setPersistentChatReplyTarget(std::nullopt);
		qteChat->clear();
		return;
	}

	if (plainText) {
		// Escape HTML, unify line endings, then convert spaces to non-breaking ones to prevent multiple
		// simultaneous ones from being collapsed into one (as a normal HTML renderer does).
		qsText = qsText.toHtmlEscaped()
					 .replace("\r\n", "\n")
					 .replace("\r", "\n")
					 .replace("\n", "<br>")
					 .replace(" ", "&nbsp;");
	} else {
		// Markdown::markdownToHTML also takes care of replacing line breaks (\n) with the respective
		// HTML code <br/>. Therefore if Markdown support is ever going to be removed from this
		// function, this job has to be done explicitly as otherwise line breaks won't be shown on
		// the receiving end of this text message.
		qsText = Markdown::markdownToHTML(qsText);
	}

	sendChatbarMessage(qsText);

	qteChat->clear();
}

void MainWindow::sendChatbarMessage(QString qsMessage) {
	if (Global::get().uiSession == 0)
		return; // Check if text & connection is available

	const PersistentChatTarget target = currentPersistentChatTarget();
	if (!target.valid || target.readOnly) {
		return;
	}

	if (m_pendingPersistentChatReply) {
		if (target.directMessage || target.legacyTextPath) {
			qsMessage = buildPersistentChatReplyHtml(*m_pendingPersistentChatReply, qsMessage);
		}
	}

	if (target.directMessage && target.user) {
		Global::get().sh->sendUserTextMessage(target.user->uiSession, qsMessage);
		Global::get().l->log(Log::TextMessage,
							 tr("To %1: %2").arg(Log::formatClientUser(target.user, Log::Target), qsMessage),
							 tr("Message to %1").arg(target.user->qsName), true);
		setPersistentChatReplyTarget(std::nullopt);
		return;
	}

	if (target.legacyTextPath && target.channel) {
		Global::get().sh->sendChannelTextMessage(target.channel->iId, qsMessage, false);
		Global::get().l->log(Log::TextMessage, tr("To %1: %2").arg(Log::formatChannel(target.channel), qsMessage),
							 tr("Message to channel %1").arg(target.channel->qsName), true);
		setPersistentChatReplyTarget(std::nullopt);
		return;
	}

	Global::get().sh->sendChatMessage(
		target.scope, target.scopeID, qsMessage, MumbleProto::ChatBodyFormatPlainText,
		m_pendingPersistentChatReply ? std::optional< unsigned int >(m_pendingPersistentChatReply->message_id()) : std::nullopt);
	setPersistentChatReplyTarget(std::nullopt);
}

/// Handles Backtab/Shift-Tab for qteChat, which allows
/// users to move focus to the previous widget in
/// MainWindow.
void MainWindow::on_qteChat_backtabPressed() {
	focusPreviousChild();
}

void MainWindow::on_qteChat_ctrlSpacePressed() {
	autocompleteUsername();
}

void MainWindow::on_qteChat_tabPressed() {
	// Only autocomplete the username, if the user entered text starts with a "@".
	// Otherwise TAB should be reserved for accessible keyboard navigation.
	QString currentText = qteChat->toPlainText();
	if (currentText.startsWith("@")) {
		currentText.remove(0, 1);

		qteChat->clear();
		QTextCursor tc = qteChat->textCursor();
		tc.insertText(currentText);
		qteChat->setTextCursor(tc);

		autocompleteUsername();
		return;
	}

	focusNextMainWidget();
}

void MainWindow::autocompleteUsername() {
	unsigned int res = qteChat->completeAtCursor();
	if (res == 0) {
		return;
	}
	qtvUsers->setCurrentIndex(pmModel->index(ClientUser::get(res)));
}

void MainWindow::on_qmConfig_aboutToShow() {
	// Don't remove the config, as that messes up OSX.
	for (QAction *a : qmConfig->actions()) {
		if (a != qaConfigDialog) {
			qmConfig->removeAction(a);
		}
	}
	qmConfig->addAction(qaAudioWizard);
	qmConfig->addAction(qaConfigCert);
	qmConfig->addSeparator();
	qaAudioTTS->setChecked(Global::get().s.bTTS);
	qmConfig->addAction(qaAudioTTS);
	qmConfig->addSeparator();
	qmConfig->addAction(qaConfigMinimal);
	qmConfig->addAction(qaFilterToggle);

	qaTalkingUIToggle->setChecked(Global::get().talkingUI && Global::get().talkingUI->isVisible());

	qmConfig->addAction(qaTalkingUIToggle);
	if (Global::get().s.bMinimalView)
		qmConfig->addAction(qaConfigHideFrame);
}

void MainWindow::qmChannel_aboutToShow() {
	Channel *c = getContextMenuTargets().channel;
	const ClientUser *self = ClientUser::get(Global::get().uiSession);
	const QString startScreenShareText = tr("Start Screen Share");

	qmChannel->clear();
	qaChannelScreenShareStart->setText(startScreenShareText);
	qaChannelScreenShareStart->setToolTip(QString());
	qaChannelScreenShareStart->setStatusTip(QString());

	if (c && self && self->cChannel && c->iId != self->cChannel->iId) {
		qmChannel->addAction(qaChannelJoin);
	}

	if (c && Global::get().sh && Global::get().sh->m_version >= Version::fromComponents(1, 4, 0)) {
		// If the server's version is less than 1.4, the listening feature is not supported yet
		// and thus it doesn't make sense to show the action for it
		qmChannel->addAction(qaChannelListen);
		qaChannelListen->setChecked(Global::get().channelListenerManager->isListening(Global::get().uiSession, c->iId));
	}

	if (c && self && self->cChannel && m_screenShareManager) {
		const QString streamID      = screenShareStreamForChannel(c);
		const bool hasScreenShare   = !streamID.isEmpty();
		const bool isCurrentChannel = c->iId == self->cChannel->iId;
		bool addedScreenShareAction = false;

		if (isCurrentChannel && !hasScreenShare) {
			const QString unavailableReason = m_screenShareManager->localShareUnavailableReason();
			qmChannel->addAction(qaChannelScreenShareStart);
			qaChannelScreenShareStart->setEnabled(unavailableReason.isEmpty());
			if (unavailableReason.isEmpty()) {
				qaChannelScreenShareStart->setText(startScreenShareText);
			} else {
				qaChannelScreenShareStart->setText(tr("Start Screen Share (Unavailable)"));
			}
			qaChannelScreenShareStart->setToolTip(unavailableReason);
			qaChannelScreenShareStart->setStatusTip(unavailableReason);
			addedScreenShareAction = true;
		} else if (hasScreenShare) {
			const ScreenShareSession session = m_screenShareManager->sessions().value(streamID);
			if (session.ownerSession == self->uiSession) {
				qmChannel->addAction(qaChannelScreenShareStop);
				qaChannelScreenShareStop->setEnabled(true);
			} else if (m_screenShareManager->isViewingSession(streamID)) {
				qmChannel->addAction(qaChannelScreenShareStopWatching);
				qaChannelScreenShareStopWatching->setEnabled(true);
			} else {
				qmChannel->addAction(qaChannelScreenShareWatch);
				qaChannelScreenShareWatch->setEnabled(m_screenShareManager->canViewSession(streamID));
			}
			addedScreenShareAction = true;
		}

		if (addedScreenShareAction) {
			qmChannel->addSeparator();
		}
	}

	qmChannel->addSeparator();

	qmChannel->addAction(qaChannelAdd);
	qmChannel->addAction(qaChannelACL);
	qmChannel->addAction(qaChannelRemove);
	qmChannel->addSeparator();
	qmChannel->addAction(qaChannelLink);
	qmChannel->addAction(qaChannelUnlink);
	qmChannel->addAction(qaChannelUnlinkAll);
	qmChannel->addSeparator();
	qmChannel->addAction(qaChannelCopyURL);
	qmChannel->addAction(qaChannelSendMessage);

	// hiding the root is nonsense
	if (c && c->cParent) {
		qmChannel->addSeparator();
		qmChannel->addAction(qaChannelHide);
		qmChannel->addAction(qaChannelPin);
	}

#ifndef Q_OS_MAC
	if (Global::get().s.bMinimalView) {
		qmChannel->addSeparator();
		qmChannel->addMenu(qmServer);
		qmChannel->addMenu(qmSelf);
		qmChannel->addMenu(qmConfig);
		qmChannel->addMenu(qmHelp);
	}
#endif

	if (!qlChannelActions.isEmpty()) {
		qmChannel->addSeparator();
		for (QAction *a : qlChannelActions) {
			qmChannel->addAction(a);
		}
	}

	bool add, remove, acl, link, unlink, unlinkall, msg;
	add = remove = acl = link = unlink = unlinkall = msg = false;

	if (Global::get().uiSession != 0) {
		add = true;
		acl = true;
		msg = true;

		Channel *home = ClientUser::get(Global::get().uiSession)->cChannel;

		if (c && c->iId != 0) {
			remove = true;
		}
		if (!c) {
			c = Channel::get(Mumble::ROOT_CHANNEL_ID);
		}
		unlinkall = (home->qhLinks.count() > 0);
		if (home != c) {
			if (c->allLinks().contains(home)) {
				unlink = true;
			} else {
				link = true;
			}
		}
	}

	if (c) {
		qaChannelHide->setChecked(c->m_filterMode == ChannelFilterMode::HIDE);
		qaChannelPin->setChecked(c->m_filterMode == ChannelFilterMode::PIN);
	}

	qaChannelAdd->setEnabled(add);
	qaChannelRemove->setEnabled(remove);
	qaChannelACL->setEnabled(acl);
	qaChannelLink->setEnabled(link);
	qaChannelUnlink->setEnabled(unlink);
	qaChannelUnlinkAll->setEnabled(unlinkall);
	qaChannelSendMessage->setEnabled(msg);
	updateMenuPermissions();
}

void MainWindow::qmPersistentTextChannel_aboutToShow() {
	updatePersistentTextChannelControls();
}

void MainWindow::on_qaChannelJoin_triggered() {
	Channel *c = getContextMenuChannel();

	if (c) {
		Global::get().sh->joinChannel(Global::get().uiSession, c->iId);
	}
}

void MainWindow::on_qaUserJoin_triggered() {
	const ClientUser *user = getContextMenuUser();

	if (user) {
		const Channel *channel = user->cChannel;

		if (channel) {
			Global::get().sh->joinChannel(Global::get().uiSession, channel->iId);
		}
	}
}

void MainWindow::on_qaUserMove_triggered() {
	const ClientUser *user = getContextMenuUser();

	if (user) {
		const Channel *channel = ClientUser::get(Global::get().uiSession)->cChannel;

		if (channel) {
			Global::get().sh->joinChannel(user->uiSession, channel->iId);
		}
	}
}

void MainWindow::on_qaChannelListen_triggered() {
	Channel *c = getContextMenuChannel();

	if (c) {
		if (qaChannelListen->isChecked()) {
			Global::get().sh->startListeningToChannel(c->iId);
		} else {
			Global::get().sh->stopListeningToChannel(c->iId);
		}
	}
}

void MainWindow::on_qaChannelHide_triggered() {
	Channel *c = getContextMenuChannel();

	if (c) {
		UserModel *um = static_cast< UserModel * >(qtvUsers->model());
		if (qaChannelHide->isChecked()) {
			c->setFilterMode(ChannelFilterMode::HIDE);
		} else {
			c->setFilterMode(ChannelFilterMode::NORMAL);
		}
		um->forceVisualUpdate(c);
	}
}

void MainWindow::on_qaChannelPin_triggered() {
	Channel *c = getContextMenuChannel();

	if (c) {
		UserModel *um = static_cast< UserModel * >(qtvUsers->model());
		if (qaChannelPin->isChecked()) {
			c->setFilterMode(ChannelFilterMode::PIN);
		} else {
			c->setFilterMode(ChannelFilterMode::NORMAL);
		}
		um->forceVisualUpdate(c);
	}
}

void MainWindow::on_qaChannelAdd_triggered() {
	Channel *c = getContextMenuChannel();
	if (aclEdit) {
		aclEdit->reject();
		delete aclEdit;
		aclEdit = nullptr;
	}

	aclEdit = new ACLEditor(c ? c->iId : 0, this);
	if (c && (c->uiPermissions & ChanACL::Cached) && !(c->uiPermissions & (ChanACL::Write | ChanACL::MakeChannel))) {
		aclEdit->qcbChannelTemporary->setEnabled(false);
		aclEdit->qcbChannelTemporary->setChecked(true);
	}

	aclEdit->show();
}

void MainWindow::on_qaChannelRemove_triggered() {
	int ret;
	Channel *c = getContextMenuChannel();
	if (!c)
		return;

	unsigned int id = c->iId;

	ret = QMessageBox::question(
		this, QLatin1String("Mumble"),
		tr("Are you sure you want to delete %1 and all its sub-channels?").arg(c->qsName.toHtmlEscaped()),
		QMessageBox::Yes, QMessageBox::No);

	c = Channel::get(id);
	if (!c)
		return;

	if (ret == QMessageBox::Yes) {
		Global::get().sh->removeChannel(c->iId);
	}
}

void MainWindow::on_qaChannelACL_triggered() {
	Channel *c = getContextMenuChannel();
	if (!c)
		c = Channel::get(Mumble::ROOT_CHANNEL_ID);
	unsigned int id = c->iId;

	if (!c->qbaDescHash.isEmpty() && c->qsDesc.isEmpty()) {
		c->qsDesc = QString::fromUtf8(Global::get().db->blob(c->qbaDescHash));
		if (c->qsDesc.isEmpty()) {
			MumbleProto::RequestBlob mprb;
			mprb.add_channel_description(id);
			Global::get().sh->sendMessage(mprb);
		}
	}

	Global::get().sh->requestACL(id);

	if (aclEdit) {
		aclEdit->reject();
		delete aclEdit;
		aclEdit = nullptr;
	}
}

void MainWindow::on_qaChannelLink_triggered() {
	Channel *c = ClientUser::get(Global::get().uiSession)->cChannel;
	Channel *l = getContextMenuChannel();
	if (!l)
		l = Channel::get(Mumble::ROOT_CHANNEL_ID);

	Global::get().sh->addChannelLink(c->iId, l->iId);
}

void MainWindow::on_qaChannelUnlink_triggered() {
	Channel *c = ClientUser::get(Global::get().uiSession)->cChannel;
	Channel *l = getContextMenuChannel();
	if (!l)
		l = Channel::get(Mumble::ROOT_CHANNEL_ID);

	Global::get().sh->removeChannelLink(c->iId, l->iId);
}

void MainWindow::on_qaChannelUnlinkAll_triggered() {
	Channel *c = ClientUser::get(Global::get().uiSession)->cChannel;

	MumbleProto::ChannelState mpcs;
	mpcs.set_channel_id(c->iId);
	for (Channel *l : c->qsPermLinks) {
		mpcs.add_links_remove(l->iId);
	}
	Global::get().sh->sendMessage(mpcs);
}

void MainWindow::on_qaChannelSendMessage_triggered() {
	Channel *c = getContextMenuChannel();

	if (!c)
		return;

	unsigned int id = c->iId;

	::TextMessage *texm = new ::TextMessage(this, tr("Sending message to channel %1").arg(c->qsName), true);
	int res             = texm->exec();

	c = Channel::get(id);

	if (c && (res == QDialog::Accepted)) {
		Global::get().sh->sendChannelTextMessage(id, texm->message(), texm->bTreeMessage);

		if (texm->bTreeMessage)
			Global::get().l->log(Log::TextMessage, tr("To %1 (Tree): %2").arg(Log::formatChannel(c), texm->message()),
								 tr("Message to tree %1").arg(c->qsName), true);
		else
			Global::get().l->log(Log::TextMessage, tr("To %1: %2").arg(Log::formatChannel(c), texm->message()),
								 tr("Message to channel %1").arg(c->qsName), true);
	}
	delete texm;
}

void MainWindow::on_qaChannelCopyURL_triggered() {
	Channel *c = getContextMenuChannel();
	QString host, uname, pw, channel;
	unsigned short port;

	if (!c)
		return;

	Global::get().sh->getConnectionInfo(host, port, uname, pw);
	// walk back up the channel list to build the URL.
	while (c->cParent) {
		channel.prepend(c->qsName);
		channel.prepend(QLatin1String("/"));
		c = c->cParent;
	}

	QApplication::clipboard()->setMimeData(ServerItem::toMimeData(c->qsName, host, port, channel),
										   QClipboard::Clipboard);
}

/**
 * This function updates the UI according to the permission of the user in the current channel.
 * If possible the permissions are fetched from a cache. Otherwise they are requested by the server
 * via a PermissionQuery call (whose reply updates the cache and calls this function again).
 * @see MainWindow::msgPermissionQuery(const MumbleProto::PermissionQuery &msg)
 */
void MainWindow::updateMenuPermissions() {
	ContextMenuTarget target = getContextMenuTargets();

	ChanACL::Permissions p =
		target.channel ? static_cast< ChanACL::Permissions >(target.channel->uiPermissions) : ChanACL::None;

	if (target.channel && !p) {
		Global::get().sh->requestChannelPermissions(target.channel->iId);
		if (target.channel->iId == 0)
			p = Global::get().pPermissions;
		else
			p = ChanACL::All;

		target.channel->uiPermissions = p;
	}

	ClientUser *user           = Global::get().uiSession ? ClientUser::get(Global::get().uiSession) : nullptr;
	Channel *homec             = user ? user->cChannel : nullptr;
	ChanACL::Permissions homep = homec ? static_cast< ChanACL::Permissions >(homec->uiPermissions) : ChanACL::None;
	bool isCurrentChannel      = target.channel && homec == target.channel;

	if (homec && !homep) {
		Global::get().sh->requestChannelPermissions(homec->iId);
		if (homec->iId == 0)
			homep = Global::get().pPermissions;
		else
			homep = ChanACL::All;

		homec->uiPermissions = homep;
	}

	if (target.user) {
		qaUserMute->setEnabled(p & (ChanACL::Write | ChanACL::MuteDeafen)
							   && ((target.user != user) || target.user->bMute || target.user->bSuppress));
		qaUserDeaf->setEnabled(p & (ChanACL::Write | ChanACL::MuteDeafen)
							   && ((target.user != user) || target.user->bDeaf));
		qaUserPrioritySpeaker->setEnabled(p & (ChanACL::Write | ChanACL::MuteDeafen));
		qaUserTextMessage->setEnabled(p & (ChanACL::Write | ChanACL::TextMessage));
		qaUserInformation->setEnabled((Global::get().pPermissions & (ChanACL::Write | ChanACL::Register))
									  || (p & (ChanACL::Write | ChanACL::Enter)) || (target.user == user));
	} else {
		qaUserMute->setEnabled(false);
		qaUserDeaf->setEnabled(false);
		qaUserPrioritySpeaker->setEnabled(false);
		qaUserTextMessage->setEnabled(false);
		qaUserInformation->setEnabled(false);
	}

	qaChannelJoin->setEnabled(p & (ChanACL::Write | ChanACL::Enter));

	qaChannelAdd->setEnabled(p & (ChanACL::Write | ChanACL::MakeChannel | ChanACL::MakeTempChannel));
	qaChannelRemove->setEnabled(p & ChanACL::Write);
	qaChannelACL->setEnabled((p & ChanACL::Write) || (Global::get().pPermissions & ChanACL::Write));

	qaChannelLink->setEnabled(!isCurrentChannel && (p & (ChanACL::Write | ChanACL::LinkChannel))
							  && (homep & (ChanACL::Write | ChanACL::LinkChannel)));
	qaChannelUnlink->setEnabled(
		!isCurrentChannel
		&& ((p & (ChanACL::Write | ChanACL::LinkChannel)) || (homep & (ChanACL::Write | ChanACL::LinkChannel))));
	qaChannelUnlinkAll->setEnabled(p & (ChanACL::Write | ChanACL::LinkChannel));
	qaChannelCopyURL->setEnabled(target.channel);
	qaChannelSendMessage->setEnabled(p & (ChanACL::Write | ChanACL::TextMessage));
	qaChannelHide->setEnabled(target.channel);
	qaChannelPin->setEnabled(target.channel);

	bool chatBarEnabled = false;
	if (Global::get().uiSession) {
		if (Global::get().s.bChatBarUseSelection && (target.channel || target.user)) {
			chatBarEnabled = p & (ChanACL::Write | ChanACL::TextMessage);
		} else if (homec) {
			chatBarEnabled = homep & (ChanACL::Write | ChanACL::TextMessage);
		}
	}
	syncPersistentChatInputState(chatBarEnabled);
}

void MainWindow::userStateChanged() {
	emit talkingStatusChanged();

	ClientUser *user = ClientUser::get(Global::get().uiSession);
	if (!user) {
		Global::get().bAttenuateOthers              = false;
		Global::get().prioritySpeakerActiveOverride = false;

		return;
	}

	switch (user->tsState) {
		case Settings::Talking:
		case Settings::Whispering:
		case Settings::Shouting:
			Global::get().bAttenuateOthers = Global::get().s.bAttenuateOthersOnTalk;

			Global::get().prioritySpeakerActiveOverride =
				Global::get().s.bAttenuateUsersOnPrioritySpeak && user->bPrioritySpeaker;

			break;
		case Settings::Passive:
		case Settings::MutedTalking:
		default:
			Global::get().bAttenuateOthers              = false;
			Global::get().prioritySpeakerActiveOverride = false;
			break;
	}
}

void MainWindow::on_channelStateChanged(Channel *channel, bool forceUpdateTree) {
	if (channel == pmModel->getChannel(qtvUsers->currentIndex())) {
		updateChatBar();
	}

	if (forceUpdateTree) {
		pmModel->forceVisualUpdate();
	}

	updateServerNavigatorChrome();
}

void MainWindow::on_qaAudioReset_triggered() {
	AudioInputPtr ai = Global::get().ai;
	if (ai)
		ai->bResetProcessor = true;
}

void MainWindow::on_qaFilterToggle_triggered() {
	Global::get().s.bFilterActive = qaFilterToggle->isChecked();
	if (!Global::get().s.bFilterActive) {
		qtvUsers->setAccessibleName(tr("Channels and users"));
	} else {
		qtvUsers->setAccessibleName(tr("Filtered channels and users"));
	}
	updateUserModel();
}

void MainWindow::on_qaAudioMute_triggered() {
	if (Global::get().bInAudioWizard) {
		qaAudioMute->setChecked(!qaAudioMute->isChecked());
		return;
	}

	AudioInputPtr ai = Global::get().ai;
	if (ai)
		ai->tIdle.restart();

	Global::get().s.bMute = qaAudioMute->isChecked();

	if (!Global::get().s.bMute && Global::get().s.bDeaf) {
		Global::get().s.bDeaf = false;
		qaAudioDeaf->setChecked(false);
		Global::get().l->log(Log::SelfUndeaf, tr("Unmuted and undeafened."));
	} else if (!Global::get().s.bMute) {
		Global::get().l->log(Log::SelfUnmute, tr("Unmuted."));
	} else {
		Global::get().l->log(Log::SelfMute, tr("Muted."));
	}

	if (Global::get().sh) {
		Global::get().sh->setSelfMuteDeafState(Global::get().s.bMute, Global::get().s.bDeaf);
	}

	updateAudioToolTips();
	emit talkingStatusChanged();
}

void MainWindow::setAudioMute(bool mute) {
	// Pretend the user pushed the button manually
	qaAudioMute->setChecked(mute);
	qaAudioMute->triggered(mute);
}

void MainWindow::on_qaAudioDeaf_triggered() {
	if (Global::get().bInAudioWizard) {
		qaAudioDeaf->setChecked(!qaAudioDeaf->isChecked());
		return;
	}

	if (!qaAudioDeaf->isChecked() && Global::get().s.unmuteOnUndeaf) {
		qaAudioDeaf->setChecked(true);
		qaAudioMute->setChecked(false);
		on_qaAudioMute_triggered();
		return;
	}

	AudioInputPtr ai = Global::get().ai;
	if (ai)
		ai->tIdle.restart();

	Global::get().s.bDeaf = qaAudioDeaf->isChecked();

	if (Global::get().s.bDeaf && !Global::get().s.bMute) {
		Global::get().s.unmuteOnUndeaf = true;
		Global::get().s.bMute          = true;
		qaAudioMute->setChecked(true);
		Global::get().l->log(Log::SelfDeaf, tr("Muted and deafened."));
	} else if (Global::get().s.bDeaf) {
		Global::get().l->log(Log::SelfDeaf, tr("Deafened."));
		Global::get().s.unmuteOnUndeaf = false;
	} else {
		Global::get().l->log(Log::SelfUndeaf, tr("Undeafened."));
	}

	if (Global::get().sh) {
		Global::get().sh->setSelfMuteDeafState(Global::get().s.bMute, Global::get().s.bDeaf);
	}

	updateAudioToolTips();
	emit talkingStatusChanged();
}

void MainWindow::setAudioDeaf(bool deaf) {
	// Pretend the user pushed the button manually
	qaAudioDeaf->setChecked(deaf);
	qaAudioDeaf->triggered(deaf);
}

void MainWindow::on_qaRecording_triggered() {
	recording();
}

void MainWindow::on_qaAudioTTS_triggered() {
	enableAudioTTS(qaAudioTTS->isChecked());
}

void MainWindow::on_qaAudioStats_triggered() {
	openAudioStatsDialog();
}

void MainWindow::on_qaAudioUnlink_triggered() {
	Global::get().pluginManager->unlinkPositionalData();
}

void MainWindow::on_qaConfigDialog_triggered() {
	openConfigDialog();
}

void MainWindow::on_qaConfigMinimal_triggered() {
	Global::get().s.bMinimalView = qaConfigMinimal->isChecked();
	updateWindowTitle();
	setupView();
}

void MainWindow::on_qaConfigHideFrame_triggered() {
	Global::get().s.bHideFrame = qaConfigHideFrame->isChecked();
	setupView(false);
}

void MainWindow::on_qaConfigCert_triggered() {
	openCertWizardDialog();
}

void MainWindow::on_qaAudioWizard_triggered() {
	openAudioWizardDialog();
}

void MainWindow::on_qaDeveloperConsole_triggered() {
	Global::get().c->show();
}

void MainWindow::on_qaPositionalAudioViewer_triggered() {
	if (m_paViewer) {
		m_paViewer->raise();
	} else {
		m_paViewer = std::make_unique< PositionalAudioViewer >(this);
		connect(m_paViewer.get(), &PositionalAudioViewer::finished, this, [this]() { m_paViewer.reset(); });
		m_paViewer->show();
	}
}

void MainWindow::on_qaHelpWhatsThis_triggered() {
	QWhatsThis::enterWhatsThisMode();
}

void MainWindow::on_qaHelpAbout_triggered() {
	openAboutDialog();
}

void MainWindow::on_qaHelpAboutQt_triggered() {
	openAboutQtDialog();
}

void MainWindow::on_qaHelpVersionCheck_triggered() {
	versionCheck();
}

void MainWindow::on_gsMuteSelf_down(QVariant v) {
	int val = v.toInt();
	if (((val > 0) && !Global::get().s.bMute) || ((val < 0) && Global::get().s.bMute) || (val == 0)) {
		qaAudioMute->setChecked(!qaAudioMute->isChecked());
		on_qaAudioMute_triggered();
	}
}

void MainWindow::on_gsDeafSelf_down(QVariant v) {
	int val = v.toInt();
	if (((val > 0) && !Global::get().s.bDeaf) || ((val < 0) && Global::get().s.bDeaf) || (val == 0)) {
		qaAudioDeaf->setChecked(!qaAudioDeaf->isChecked());
		on_qaAudioDeaf_triggered();
	}
}

void MainWindow::on_PushToTalk_triggered(bool down, QVariant) {
	Global::get().iPrevTarget = 0;
	if (down) {
		Global::get().uiDoublePush = static_cast< quint64 >(Global::get().tDoublePush.restart().count());
		Global::get().iPushToTalk++;
	} else if (Global::get().iPushToTalk > 0) {
		QTimer::singleShot(static_cast< int >(Global::get().s.pttHold), this, SLOT(pttReleased()));
	}
}

void MainWindow::pttReleased() {
	if (Global::get().iPushToTalk > 0) {
		Global::get().iPushToTalk--;
	}
}

void MainWindow::on_PushToMute_triggered(bool down, QVariant) {
	Global::get().bPushToMute = down;
	updateUserModel();
	emit talkingStatusChanged();
}

void MainWindow::on_VolumeUp_triggered(bool down, QVariant) {
	if (down) {
		float vol = Global::get().s.fVolume + 0.1f;
		if (vol > 2.0f) {
			vol = 2.0f;
		}
		Global::get().s.fVolume = vol;
	}
}

void MainWindow::on_VolumeDown_triggered(bool down, QVariant) {
	if (down) {
		float vol = Global::get().s.fVolume - 0.1f;
		if (vol < 0.0f) {
			vol = 0.0f;
		}
		Global::get().s.fVolume = vol;
	}
}

Channel *MainWindow::mapChannel(int idx) const {
	if (!Global::get().uiSession)
		return nullptr;

	Channel *c = nullptr;

	if (idx < 0) {
		switch (idx) {
			case SHORTCUT_TARGET_ROOT:
				c = Channel::get(Mumble::ROOT_CHANNEL_ID);
				break;
			case SHORTCUT_TARGET_PARENT:
			case SHORTCUT_TARGET_CURRENT:
				c = ClientUser::get(Global::get().uiSession)->cChannel;
				if (idx == SHORTCUT_TARGET_PARENT)
					c = c->cParent;
				break;
			default:
				if (idx <= SHORTCUT_TARGET_PARENT_SUBCHANNEL)
					c = pmModel->getSubChannel(ClientUser::get(Global::get().uiSession)->cChannel->cParent,
											   SHORTCUT_TARGET_PARENT_SUBCHANNEL - idx);
				else
					c = pmModel->getSubChannel(ClientUser::get(Global::get().uiSession)->cChannel,
											   SHORTCUT_TARGET_SUBCHANNEL - idx);
				break;
		}
	} else {
		c = Channel::get(static_cast< unsigned int >(idx));
	}
	return c;
}

void MainWindow::updateTarget() {
	Global::get().iPrevTarget = Global::get().iTarget;

	if (qmCurrentTargets.isEmpty()) {
		Global::get().bCenterPosition = false;
		Global::get().iTarget         = 0;
	} else {
		bool center = false;
		QList< ShortcutTarget > ql;
		for (const ShortcutTarget &st : qmCurrentTargets.keys()) {
			ShortcutTarget nt;
			center               = center || st.bForceCenter;
			nt.bUsers            = st.bUsers;
			nt.bCurrentSelection = st.bCurrentSelection;

			if (nt.bCurrentSelection) {
				Channel *c = pmModel->getSelectedChannel();
				if (c) {
					nt.bUsers    = false;
					nt.iChannel  = static_cast< int >(c->iId);
					nt.bLinks    = st.bLinks;
					nt.bChildren = st.bChildren;

					ql << nt;
				} else {
					ClientUser *user = pmModel->getSelectedUser();

					if (user) {
						nt.bUsers = true;
						nt.qlSessions << user->uiSession;

						ql << nt;
					}
				}
			} else if (st.bUsers) {
				for (const QString &hash : st.qlUsers) {
					ClientUser *p = pmModel->getUser(hash);
					if (p)
						nt.qlSessions.append(p->uiSession);
				}
				if (!nt.qlSessions.isEmpty())
					ql << nt;
			} else {
				Channel *c = mapChannel(st.iChannel);
				if (c) {
					nt.bLinks    = st.bLinks;
					nt.bChildren = st.bChildren;
					nt.iChannel  = static_cast< int >(c->iId);
					nt.qsGroup   = st.qsGroup;
					ql << nt;
				}
			}
		}
		if (ql.isEmpty()) {
			Global::get().iTarget = -1;
		} else {
			++iTargetCounter;

			int idx = qmTargets.value(ql);
			if (idx == 0) {
				// An idx of 0 means that we don't have a mapping for this shortcut yet
				// Thus we'll register it here
				QMap< int, int > qm;
				QMap< int, int >::const_iterator i;
				// We reverse the qmTargetsUse map into qm so that each key becomes a value and vice versa
				for (i = qmTargetUse.constBegin(); i != qmTargetUse.constEnd(); ++i) {
					qm.insert(i.value(), i.key());
				}

				// The reversal and the promise that when iterating over a QMap, the keys will appear sorted
				// leads to us now being able to get the next target ID as the value of the first entry in
				// the map.
				i   = qm.constBegin();
				idx = i.value();



				// Sets up a VoiceTarget (which is identified by the targetID idx) on the server for the given set
				// of ShortcutTargets
				MumbleProto::VoiceTarget mpvt;
				mpvt.set_id(static_cast< unsigned int >(idx));

				for (const ShortcutTarget &st : ql) {
					MumbleProto::VoiceTarget_Target *t = mpvt.add_targets();
					// st.bCurrentSelection has been taken care of at this point already (if it was set) so
					// we don't have to check for that here.
					if (st.bUsers) {
						for (unsigned int uisession : st.qlSessions) {
							t->add_session(uisession);
						}
					} else {
						t->set_channel_id(static_cast< unsigned int >(st.iChannel));
						if (st.bChildren)
							t->set_children(true);
						if (st.bLinks)
							t->set_links(true);
						if (!st.qsGroup.isEmpty())
							t->set_group(u8(st.qsGroup));
					}
				}
				Global::get().sh->sendMessage(mpvt);

				// Store a mapping of the list of ShortcutTargets and the used targetID
				qmTargets.insert(ql, idx);

				// Advance the iteration of qm (which contains the reverse mapping of qmTargetUse) by two.
				// Note that qmTargetUse is first populated in Messages.cpp so we will not overflow the map
				// by this.
				++i;
				++i;

				// Get the target ID for the targetID after next
				int oldidx = i.value();
				if (oldidx) {
					QHash< QList< ShortcutTarget >, int >::iterator mi;
					for (mi = qmTargets.begin(); mi != qmTargets.end(); ++mi) {
						if (mi.value() == oldidx) {
							// If we have used the targetID after next before, we clear the VoiceTarget for that
							// targetID on the server in order to be able to reuse that ID once we need it. We do
							// it 2 steps in advance as to not run into timing problems where the server might
							// receive this clearing message too late for us to recycle the ID.
							qmTargets.erase(mi);

							mpvt.Clear();
							mpvt.set_id(static_cast< unsigned int >(oldidx));
							Global::get().sh->sendMessage(mpvt);

							break;
						}
					}
				}
			}

			// This is where the magic happens. We replace the old value the used targetID was mapped to with
			// iTargetCounter. iTargetCounter is guaranteed to be bigger than any number a targetID is currently
			// mapped to in this map. This causes the mapping for the most recently used targetID to appear last
			// in the qm map the next time this function gets called. This causes targetIDs to be sorted according
			// to the time they have been assigned for the last time so that the targetID that comes last in qm will
			// be the one that has been assigned most recently. This trick turns qmTargetUse (or rather qm) into
			// something similar to a RingBuffer inside this method.
			qmTargetUse.insert(idx, iTargetCounter);
			Global::get().bCenterPosition = center;
			Global::get().iTarget         = idx;
		}
	}
}

void MainWindow::on_gsWhisper_triggered(bool down, QVariant scdata) {
	ShortcutTarget st = scdata.value< ShortcutTarget >();

	if (down) {
		if (gsJoinChannel->active()) {
			if (!st.bUsers) {
				Channel *c = mapChannel(st.iChannel);
				if (c) {
					Global::get().sh->joinChannel(Global::get().uiSession, c->iId);
				}
				return;
			}
		}

		if (gsLinkChannel->active()) {
			if (!st.bUsers) {
				Channel *c = ClientUser::get(Global::get().uiSession)->cChannel;
				Channel *l = mapChannel(st.iChannel);
				if (l) {
					if (c->qsPermLinks.contains(l)) {
						Global::get().sh->removeChannelLink(c->iId, l->iId);
					} else {
						Global::get().sh->addChannelLink(c->iId, l->iId);
					}
				}
				return;
			}
		}

		addTarget(&st);
		updateTarget();

		Global::get().iPushToTalk++;
	} else if (Global::get().iPushToTalk > 0) {
		SignalCurry *fwd = new SignalCurry(scdata, true, this);
		connect(fwd, SIGNAL(called(QVariant)), SLOT(whisperReleased(QVariant)));
		QTimer::singleShot(static_cast< int >(Global::get().s.pttHold), fwd, SLOT(call()));
	}
}

/* Add and remove ShortcutTargets from the qmCurrentTargets Map, which counts
 * the number of push-to-talk events for a given ShortcutTarget.  If this number
 * reaches 0, the ShortcutTarget is removed from qmCurrentTargets.
 */
void MainWindow::addTarget(ShortcutTarget *st) {
	if (qmCurrentTargets.contains(*st))
		qmCurrentTargets[*st] += 1;
	else
		qmCurrentTargets[*st] = 1;
}

void MainWindow::removeTarget(ShortcutTarget *st) {
	if (!qmCurrentTargets.contains(*st))
		return;

	if (qmCurrentTargets[*st] == 1)
		qmCurrentTargets.remove(*st);
	else
		qmCurrentTargets[*st] -= 1;
}

void MainWindow::on_gsCycleTransmitMode_triggered(bool down, QVariant) {
	if (down) {
		QString qsNewMode;

		switch (Global::get().s.atTransmit) {
			case Settings::Continuous:
				setTransmissionMode(Settings::VAD);
				break;
			case Settings::VAD:
				setTransmissionMode(Settings::PushToTalk);
				break;
			case Settings::PushToTalk:
				setTransmissionMode(Settings::Continuous);
				break;
		}
	}
}

void MainWindow::on_gsToggleMainWindowVisibility_triggered(bool down, QVariant) {
	if (down) {
		emit windowVisibilityToggled();
	}
}

void MainWindow::on_gsListenChannel_triggered(bool down, QVariant scdata) {
	ChannelTarget target = scdata.value< ChannelTarget >();
	const Channel *c     = Channel::get(target.channelID);

	if (down && c) {
		if (!Global::get().channelListenerManager->isListening(Global::get().uiSession, c->iId)) {
			Global::get().sh->startListeningToChannel(c->iId);
		} else {
			Global::get().sh->stopListeningToChannel(c->iId);
		}
	}
}

void MainWindow::on_gsTransmitModePushToTalk_triggered(bool down, QVariant) {
	if (down) {
		setTransmissionMode(Settings::PushToTalk);
	}
}

void MainWindow::on_gsTransmitModeContinuous_triggered(bool down, QVariant) {
	if (down) {
		setTransmissionMode(Settings::Continuous);
	}
}

void MainWindow::on_gsTransmitModeVAD_triggered(bool down, QVariant) {
	if (down) {
		setTransmissionMode(Settings::VAD);
	}
}

void MainWindow::on_gsSendTextMessage_triggered(bool down, QVariant scdata) {
	if (!down || !Global::get().sh || !Global::get().sh->isRunning() || Global::get().uiSession == 0) {
		return;
	}

	QString qsText = scdata.toString();
	if (qsText.isEmpty()) {
		return;
	}

	Channel *c = ClientUser::get(Global::get().uiSession)->cChannel;
	Global::get().sh->sendChannelTextMessage(c->iId, qsText, false);
	Global::get().l->log(Log::TextMessage, tr("To %1: %2").arg(Log::formatChannel(c), qsText),
						 tr("Message to channel %1").arg(c->qsName), true);
}

void MainWindow::on_gsSendClipboardTextMessage_triggered(bool down, QVariant) {
	if (!down || (QApplication::clipboard()->text().isEmpty())) {
		return;
	}

	// call sendChatbarMessage() instead of on_gsSendTextMessage_triggered() to handle
	// formatting of the content in the clipboard, i.e., href.
	sendChatbarMessage(QApplication::clipboard()->text());
}

void MainWindow::on_gsToggleTalkingUI_triggered(bool down, QVariant) {
	if (down) {
		qaTalkingUIToggle->trigger();
	}
}

void MainWindow::on_gsToggleSearch_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	toggleSearchDialogVisibility();
}

void MainWindow::on_gsServerConnect_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	openServerConnectDialog();
}

void MainWindow::on_gsServerDisconnect_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	disconnectFromServer();
}

void MainWindow::on_gsServerInformation_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	openServerInformationDialog();
}

void MainWindow::on_gsServerTokens_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	openServerTokensDialog();
}

void MainWindow::on_gsServerUserList_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	openServerUserListDialog();
}

void MainWindow::on_gsServerBanList_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	openServerBanListDialog();
}

void MainWindow::on_gsSelfPrioritySpeaker_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	toggleSelfPrioritySpeaker();
}

void MainWindow::on_gsRecording_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	recording();
}

void MainWindow::on_gsSelfComment_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	openSelfCommentDialog();
}

void MainWindow::on_gsServerTexture_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	changeServerTexture();
}

void MainWindow::on_gsServerTextureRemove_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	removeServerTexture();
}

void MainWindow::on_gsSelfRegister_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	selfRegister();
}

void MainWindow::on_gsAudioStats_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	openAudioStatsDialog();
}

void MainWindow::on_gsConfigDialog_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	openConfigDialog();
}

void MainWindow::on_gsAudioWizard_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	openAudioWizardDialog();
}

void MainWindow::on_gsConfigCert_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	openCertWizardDialog();
}

void MainWindow::on_gsAudioTTS_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	enableAudioTTS(!Global::get().s.bTTS);
}

void MainWindow::on_gsHelpAbout_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	openAboutDialog();
}

void MainWindow::on_gsHelpAboutQt_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	openAboutQtDialog();
}

void MainWindow::on_gsHelpVersionCheck_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	versionCheck();
}

void MainWindow::on_gsTogglePositionalAudio_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	enablePositionalAudio(!Global::get().s.bPositionalAudio);
}

void MainWindow::on_gsMoveBack_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	on_qaMoveBack_triggered();
}

void MainWindow::on_gsCycleListenerAttenuationMode_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	Global::get().s.alwaysAttenuateListeners = !Global::get().s.alwaysAttenuateListeners;
}

void MainWindow::on_gsListenerAttenuationUp_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	Global::get().s.listenerAttenuationFactor = std::min(Global::get().s.listenerAttenuationFactor + 0.1f, 1.0f);
}

void MainWindow::on_gsListenerAttenuationDown_triggered(bool down, QVariant) {
	if (!down) {
		return;
	}

	Global::get().s.listenerAttenuationFactor = std::max(Global::get().s.listenerAttenuationFactor - 0.1f, 0.0f);
}

void MainWindow::on_gsAdaptivePush_triggered(bool down, QVariant variant) {
	if (Global::get().s.atTransmit == Settings::PushToTalk) {
		on_PushToTalk_triggered(down, std::move(variant));
	} else {
		on_PushToMute_triggered(down, std::move(variant));
	}
}

void MainWindow::whisperReleased(QVariant scdata) {
	if (Global::get().iPushToTalk <= 0)
		return;

	ShortcutTarget st = scdata.value< ShortcutTarget >();

	Global::get().iPushToTalk--;

	removeTarget(&st);
	updateTarget();
}

void MainWindow::onResetAudio() {
	qWarning("MainWindow: Start audio reset");
	Audio::stop();
	Audio::start();
	qWarning("MainWindow: Audio reset complete");
}

void MainWindow::viewCertificate(bool) {
	ViewCert vc(Global::get().sh->qscCert, this);
	vc.exec();
}

/**
 * This function prepares the UI for receiving server data. It gets called once the
 * connection to the server is established but before the server Sync is complete.
 */
void MainWindow::serverConnected() {
	m_reconnectSoundBlocker.reset();

	Global::get().uiSession    = 0;
	Global::get().pPermissions = ChanACL::None;

#ifdef Q_OS_MAC
	// Suppress AppNap while we're connected to a server.
	MUSuppressAppNap(true);
#endif

	Global::get().l->clearIgnore();
	Global::get().l->setIgnore(Log::UserJoin);
	Global::get().l->setIgnore(Log::OtherSelfMute);
	QString host, uname, pw;
	unsigned short port;
	Global::get().sh->getConnectionInfo(host, port, uname, pw);
	Global::get().l->log(Log::ServerConnected, tr("Connected."));
	qaServerDisconnect->setEnabled(true);
	qaServerInformation->setEnabled(true);
	updateFavoriteButton();
	qaServerBanList->setEnabled(true);

	Channel *root = Channel::get(Mumble::ROOT_CHANNEL_ID);
	pmModel->renameChannel(root, tr("Root"));
	pmModel->setCommentHash(root, QByteArray());
	root->uiPermissions = 0;

	qtvUsers->setRowHidden(0, QModelIndex(), false);

	Global::get().bAllowHTML      = true;
	Global::get().bPersistentGlobalChatEnabled = false;
	Global::get().uiMessageLength = 5000;
	Global::get().uiImageLength   = 131072;
	Global::get().uiMaxUsers      = 0;
	m_hasPersistentChatSupport    = false;
	m_defaultPersistentTextChannelID = 0;
	m_persistentTextChannels.clear();
	m_persistentChatPreviews.clear();
	m_persistentChatAssetDownloads.clear();
	setPersistentChatWelcomeText(QString());
	rebuildPersistentChatChannelList();
	updateServerNavigatorChrome();

	enableRecording(true);

	if (Global::get().s.bMute || Global::get().s.bDeaf) {
		Global::get().sh->setSelfMuteDeafState(Global::get().s.bMute, Global::get().s.bDeaf);
	}

	// Update QActions and menus
	on_qmServer_aboutToShow();
	on_qmSelf_aboutToShow();
	qmChannel_aboutToShow();
	qmUser_aboutToShow();
	on_qmConfig_aboutToShow();

#ifdef Q_OS_WIN
	TaskList::addToRecentList(Global::get().s.qsLastServer, uname, host, port);
#endif

	qdwMinimalViewNote->hide();
}

void MainWindow::serverDisconnected(QAbstractSocket::SocketError err, QString reason) {
	// clear ChannelListener
	Global::get().channelListenerManager->clear();

	// Reset move-back history
	qaMoveBack->setEnabled(false);
	m_previousChannels = {};
	m_movedBackFromChannel.reset();

	Global::get().uiSession        = 0;
	Global::get().pPermissions     = ChanACL::None;
	Global::get().bAttenuateOthers = false;
	Global::get().bPersistentGlobalChatEnabled = false;
	m_hasPersistentChatSupport    = false;
	qaServerDisconnect->setEnabled(false);
	qaServerAddToFavorites->setEnabled(false);
	qaServerInformation->setEnabled(false);
	qaServerBanList->setEnabled(false);
	qtvUsers->setCurrentIndex(QModelIndex());
	qteChat->setEnabled(false);
	m_defaultPersistentTextChannelID = 0;
	m_persistentTextChannels.clear();
	m_persistentChatPreviews.clear();
	m_persistentChatAssetDownloads.clear();
	m_persistentChatLastReadByScope.clear();
	m_persistentChatUnreadByScope.clear();
	setPersistentChatWelcomeText(QString());
	rebuildPersistentChatChannelList();
	clearPersistentChatView(tr("Disconnected from server."), tr("Connection ended"),
							{ tr("Open Server to reconnect"), tr("Room history stays with each channel") });
	updateServerNavigatorChrome();

#ifdef Q_OS_MAC
	// Remove App Nap suppression now that we're disconnected.
	MUSuppressAppNap(false);
#endif

	QString uname, pw, host;
	unsigned short port;
	Global::get().sh->getConnectionInfo(host, port, uname, pw);

	if (Global::get().sh->hasSynchronized()) {
		QList< Shortcut > &shortcuts = Global::get().s.qlShortcuts;
		// Only save server-specific shortcuts if the client and server have been synchronized before as only then
		// did the client actually load them from the DB. If we store them without having loaded them, we will
		// effectively clear the server-specific shortcuts for this server.
		Global::get().db->setShortcuts(Global::get().sh->qbaDigest, shortcuts);

		// Clear server-specific shortcuts from the list of known shortcuts
		auto it = std::remove_if(shortcuts.begin(), shortcuts.end(),
								 [](const Shortcut &shortcut) { return shortcut.isServerSpecific(); });
		if (it != shortcuts.end()) {
			// Some shortcuts have to be removed
			shortcuts.erase(it, shortcuts.end());

			GlobalShortcutEngine::engine->bNeedRemap = true;
		}
	}

	if (aclEdit) {
		aclEdit->reject();
		delete aclEdit;
		aclEdit = nullptr;
	}

	if (banEdit) {
		banEdit->reject();
		delete banEdit;
		banEdit = nullptr;
	}

	if (userEdit) {
		userEdit->reject();
		delete userEdit;
		userEdit = nullptr;
	}

	if (tokenEdit) {
		tokenEdit->reject();
		delete tokenEdit;
		tokenEdit = nullptr;
	}

	QSet< QAction * > qs;
	qs += QSet< QAction * >(qlServerActions.begin(), qlServerActions.end());
	qs += QSet< QAction * >(qlChannelActions.begin(), qlChannelActions.end());
	qs += QSet< QAction * >(qlUserActions.begin(), qlUserActions.end());

	for (QAction *a : qs) {
		delete a;
	}

	qlServerActions.clear();
	qlChannelActions.clear();
	qlUserActions.clear();

	pmModel->removeAll();
	qtvUsers->setRowHidden(0, QModelIndex(), true);

	// Update QActions and menus
	on_qmServer_aboutToShow();
	on_qmSelf_aboutToShow();
	qmChannel_aboutToShow();
	qmUser_aboutToShow();
	on_qmConfig_aboutToShow();

	// We can't record without a server anyway, so we disable the functionality here
	enableRecording(false);
	Global::get().bPersistentGlobalChatEnabled = false;

	if (!Global::get().sh->qlErrors.isEmpty()) {
		for (const QSslError &e : Global::get().sh->qlErrors) {
			Global::get().l->log(Log::Warning, tr("SSL Verification failed: %1").arg(e.errorString().toHtmlEscaped()));
		}
		if (!Global::get().sh->qscCert.isEmpty()) {
			QSslCertificate c = Global::get().sh->qscCert.at(0);
			QString basereason;
			QString actual_digest = QString::fromLatin1(c.digest(QCryptographicHash::Sha1).toHex());
			QString digests_section =
				tr("<li>Server certificate digest (SHA-1):\t%1</li>").arg(ViewCert::prettifyDigest(actual_digest));
			QString expected_digest = Global::get().db->getDigest(host, port);
			if (!expected_digest.isNull()) {
				basereason =
					tr("<b>WARNING:</b> The server presented a certificate that was different from the stored one.");
				digests_section.append(tr("<li>Expected certificate digest (SHA-1):\t%1</li>")
										   .arg(ViewCert::prettifyDigest(expected_digest)));
			} else {
				basereason = tr("Server presented a certificate which failed verification.");
			}
			QStringList qsl;
			for (const QSslError &e : Global::get().sh->qlErrors) {
				qsl << QString::fromLatin1("<li>%1</li>").arg(e.errorString().toHtmlEscaped());
			}

			QMessageBox qmb(QMessageBox::Warning, QLatin1String("Mumble"),
							tr("<p>%1</p><ul>%2</ul><p>The specific errors with this certificate are:</p><ol>%3</ol>"
							   "<p>Do you wish to accept this certificate anyway?<br />(It will also be stored so you "
							   "won't be asked this again.)</p>")
								.arg(basereason)
								.arg(digests_section)
								.arg(qsl.join(QString())),
							QMessageBox::Yes | QMessageBox::No, this);

			qmb.setDefaultButton(QMessageBox::No);
			qmb.setEscapeButton(QMessageBox::No);

			QPushButton *qp = qmb.addButton(tr("&View Certificate"), QMessageBox::ActionRole);
			forever {
				int res = qmb.exec();

				if ((res == 0) && (qmb.clickedButton() == qp)) {
					ViewCert vc(Global::get().sh->qscCert, this);
					vc.exec();
					continue;
				} else if (res == QMessageBox::Yes) {
					Global::get().db->setDigest(host, port,
												QString::fromLatin1(c.digest(QCryptographicHash::Sha1).toHex()));
					qaServerDisconnect->setEnabled(true);
					on_Reconnect_timeout();
				}
				break;
			}
		}
	} else if (err == QAbstractSocket::SslHandshakeFailedError) {
		QMessageBox msgBox;
		msgBox.addButton(QMessageBox::Ok);
		msgBox.setIcon(QMessageBox::Warning);
		msgBox.setTextFormat(Qt::RichText);
		msgBox.setWindowTitle(tr("SSL error"));
		msgBox.setText(tr("Mumble is unable to establish a secure connection to the server. (\"%1\")").arg(reason));
		// clang-format off
		msgBox.setInformativeText(
			tr("This could be caused by one of the following scenarios:"
			   "<ul>"
			       "<li>Your client and the server use different encryption standards. This could be because you are using "
			       "a very old client or the server you are connecting to is very old. In the first case, you should update "
			       "your client and in the second case you should contact the server administrator so that they can update "
				   "their server.</li>"
				   "<li>Either your client or the server is using an old operating system that doesn't provide up-to-date "
				   "encryption methods. In this case you should consider updating your OS or contacting the server admin "
				   "so that they can update theirs.</li>"
				   "<li>The server you are connecting to isn't actually a Mumble server. Please ensure that the used server "
				   "address really belongs to a Mumble server and not e.g. to a game server.</li>"
				   "<li>The port you are connecting to does not belong to a Mumble server but instead is bound to a "
				   "completely unrelated process on the server-side. Please double-check you have used the correct port.</li>"
				"</ul>"));
		// clang-format on

		msgBox.exec();
	} else {
		if (!reason.isEmpty()) {
			Global::get().l->log(Log::ServerDisconnected,
								 tr("Server connection failed: %1.").arg(reason.toHtmlEscaped()));
		} else {
			Global::get().l->log(Log::ServerDisconnected, tr("Disconnected from server."));
		}

		ConnectDetails details;
		Global::get().sh->getConnectionInfo(details.host, details.port, details.username, details.password);

		switch (rtLast) {
			case MumbleProto::Reject_RejectType_InvalidUsername:
				(new FailedConnectionDialog(std::move(details), ConnectionFailType::InvalidUsername, this))->show();
				break;
			case MumbleProto::Reject_RejectType_UsernameInUse:
				(new FailedConnectionDialog(std::move(details), ConnectionFailType::UsernameAlreadyInUse, this))
					->show();
				break;
			case MumbleProto::Reject_RejectType_WrongUserPW:
				(new FailedConnectionDialog(std::move(details), ConnectionFailType::AuthenticationFailure, this))
					->show();
				break;
			case MumbleProto::Reject_RejectType_WrongServerPW:
				(new FailedConnectionDialog(std::move(details), ConnectionFailType::InvalidServerPassword, this))
					->show();
				break;
			default:
				if (Global::get().s.bReconnect && !reason.isEmpty()) {
					qaServerDisconnect->setEnabled(true);
					if (bRetryServer) {
						qtReconnect->start();
					}
				}
				break;
		}
	}
	AudioInput::setMaxBandwidth(-1);

	if (Global::get().s.bMinimalView) {
		qdwMinimalViewNote->show();
	}

	emit disconnectedFromServer();
}

void MainWindow::resolverError(QAbstractSocket::SocketError, QString reason) {
	if (!reason.isEmpty()) {
		Global::get().l->log(Log::ServerDisconnected, tr("Server connection failed: %1.").arg(reason.toHtmlEscaped()));
	} else {
		Global::get().l->log(Log::ServerDisconnected, tr("Server connection failed."));
	}

	if (Global::get().s.bReconnect) {
		qaServerDisconnect->setEnabled(true);
		if (bRetryServer) {
			qtReconnect->start();
		}
	}
}

void MainWindow::showRaiseWindow() {
	setWindowState(windowState() & ~Qt::WindowMinimized);
	QTimer::singleShot(0, [this]() {
		show();
		raise();
		activateWindow();
		setWindowState(windowState() | Qt::WindowActive);
	});
}

void MainWindow::highlightWindow() {
	QApplication::alert(this);
}

void MainWindow::on_qaTalkingUIToggle_triggered() {
	if (!Global::get().talkingUI) {
		qCritical("MainWindow: Attempting to show Talking UI before it has been created!");
		return;
	}

	Global::get().talkingUI->setVisible(!Global::get().talkingUI->isVisible());

	Global::get().s.bShowTalkingUI = Global::get().talkingUI->isVisible();
}

/**
 * This function updates the qteChat bar default text according to
 * the selected user/channel in the users treeview.
 */
void MainWindow::qtvUserCurrentChanged(const QModelIndex &, const QModelIndex &) {
	const QWidget *focusWidget = QApplication::focusWidget();
	const bool treeIsFocused =
		qtvUsers && (focusWidget == qtvUsers || (qtvUsers->viewport() && focusWidget == qtvUsers->viewport()));
	if (treeIsFocused) {
		setPersistentChatTargetUsesVoiceTree(true);
	}

	updateChatBar();
	updateServerNavigatorChrome();
}

void MainWindow::on_persistentChatScopeChanged(int) {
	setPersistentChatTargetUsesVoiceTree(false);

	QPointer< MainWindow > guardedThis(this);
	QMetaObject::invokeMethod(
		this,
		[guardedThis]() {
			if (!guardedThis) {
				return;
			}

			guardedThis->updatePersistentTextChannelControls();
			guardedThis->updateChatBar(false);
		},
		Qt::QueuedConnection);
}

void MainWindow::updateChatBar(bool forcePersistentChatReload) {
	const PersistentChatTarget target = currentPersistentChatTarget();
	updatePersistentChatChrome(target);
	updatePersistentChatScopeSelectorLabels();
	updatePersistentTextChannelControls();
	if (Global::get().uiSession == 0 || !target.valid) {
		qteChat->setDefaultText(tr("<div>Connect to chat</div>"), true);
		clearPersistentChatView(tr("Connect to a server to load conversations and history."),
							 tr("Start a conversation"),
							 { tr("Open Server to connect"), tr("Room chat and history appear here") });
	} else if (target.serverLog) {
		qteChat->setDefaultText(tr("<div>Read-only log</div>"), true);
		renderServerLogView(true);
	} else if (target.legacyTextPath && target.directMessage && target.user) {
		qteChat->setDefaultText(tr("<div>Reply to %1</div>").arg(target.user->qsName.toHtmlEscaped()));
		refreshPersistentChatView(forcePersistentChatReload);
	} else if (target.legacyTextPath && target.channel) {
		qteChat->setDefaultText(tr("<div>Reply in %1</div>").arg(target.channel->qsName.toHtmlEscaped()));
		refreshPersistentChatView(forcePersistentChatReload);
	} else if (target.directMessage && target.user) {
		qteChat->setDefaultText(tr("<div>Reply to %1</div>").arg(target.user->qsName.toHtmlEscaped()));
		clearPersistentChatView(tr("Direct messages still use the classic text message path and do not have persistent "
								   "history yet."),
							 tr("Direct messages"), { tr("Classic chat still handles direct messages") });
	} else if (target.scope == MumbleProto::ServerGlobal && !Global::get().bPersistentGlobalChatEnabled) {
		qteChat->setDefaultText(tr("<div>Global chat is disabled</div>"), true);
		clearPersistentChatView(target.statusMessage.isEmpty() ? tr("Global chat is disabled by this server.")
															  : target.statusMessage,
							 tr("Global chat is unavailable"), { tr("Choose a room conversation instead") });
	} else if (target.readOnly) {
		qteChat->setDefaultText(tr("<div>Choose a conversation to reply</div><div><small>All chats is read-only</small></div>"),
								true);
		refreshPersistentChatView(forcePersistentChatReload);
	} else if (target.scope == MumbleProto::ServerGlobal) {
		qteChat->setDefaultText(tr("<div>Reply in Lobby</div>"), true);
		refreshPersistentChatView(forcePersistentChatReload);
	} else if (target.scope == MumbleProto::TextChannel) {
		qteChat->setDefaultText(tr("<div>Reply in %1</div>").arg(target.label.toHtmlEscaped()), true);
		refreshPersistentChatView(forcePersistentChatReload);
	} else if (target.channel) {
		qteChat->setDefaultText(tr("<div>Reply in %1</div>").arg(target.channel->qsName.toHtmlEscaped()));
		refreshPersistentChatView(forcePersistentChatReload);
	} else {
		qteChat->setDefaultText(tr("<div>Reply</div>"), true);
		refreshPersistentChatView(forcePersistentChatReload);
	}

	updateMenuPermissions();
}

void MainWindow::customEvent(QEvent *evt) {
	if (evt->type() == MB_QEVENT) {
		MessageBoxEvent *mbe = static_cast< MessageBoxEvent * >(evt);
		Global::get().l->log(Log::Information, mbe->msg);
		return;
	} else if (evt->type() == OU_QEVENT) {
		OpenURLEvent *oue = static_cast< OpenURLEvent * >(evt);
		openUrl(oue->url);
		return;
	} else if (evt->type() != SERVERSEND_EVENT) {
		return;
	}

	ServerHandlerMessageEvent *shme = static_cast< ServerHandlerMessageEvent * >(evt);

#ifdef QT_NO_DEBUG
#	define PROCESS_MUMBLE_TCP_MESSAGE(name, value)                                                    \
		case Mumble::Protocol::TCPMessageType::name: {                                                 \
			MumbleProto::name msg;                                                                     \
			if (msg.ParseFromArray(shme->qbaMsg.constData(), static_cast< int >(shme->qbaMsg.size()))) \
				msg##name(msg);                                                                        \
			break;                                                                                     \
		}
#else
#	define PROCESS_MUMBLE_TCP_MESSAGE(name, value)                                                      \
		case Mumble::Protocol::TCPMessageType::name: {                                                   \
			MumbleProto::name msg;                                                                       \
			if (msg.ParseFromArray(shme->qbaMsg.constData(), static_cast< int >(shme->qbaMsg.size()))) { \
				printf("%s:\n", #name);                                                                  \
				msg.PrintDebugString();                                                                  \
				msg##name(msg);                                                                          \
			}                                                                                            \
			break;                                                                                       \
		}
#endif
	switch (shme->type) { MUMBLE_ALL_TCP_MESSAGES }


#undef PROCESS_MUMBLE_TCP_MESSAGE
}


void MainWindow::on_qteLog_anchorClicked(const QUrl &url) {
	if (url.scheme() == QLatin1String("mumble-chat") && url.host() == QLatin1String("load-older")) {
		requestOlderPersistentChatHistory();
		return;
	}

	if (url.scheme() == QLatin1String("mumble-chat") && url.host() == QLatin1String("scope")) {
		const QStringList pathParts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
		if (pathParts.size() == 2 && pathParts.front() == QLatin1String("text")) {
			bool ok = false;
			const unsigned int scopeID = pathParts.back().toUInt(&ok);
			if (ok && navigateToPersistentChatScope(MumbleProto::TextChannel, scopeID)) {
				return;
			}
		}
	}

	if (!handleSpecialContextMenu(url, QCursor::pos(), true)) {
#if defined(Q_OS_MAC) && defined(USE_OVERLAY)
		// Clicking a link can cause the user's default browser to pop up while
		// we're intercepting all events. This can be very confusing (because
		// the user can't click on anything before they dismiss the overlay
		// by hitting their toggle hotkey), so let's disallow clicking links
		// when embedded into the overlay for now.
		if (Global::get().ocIntercept)
			return;
#endif
		if (url.scheme() != QLatin1String("file") && url.scheme() != QLatin1String("qrc") && !url.isRelative())
			QDesktopServices::openUrl(url);
	}
}

void MainWindow::on_qteLog_highlighted(const QUrl &url) {
	if (url.scheme() == QString::fromLatin1("clientid") || url.scheme() == QString::fromLatin1("channelid")
		|| url.scheme() == QString::fromLatin1("mumble-chat"))
		return;

	if (!url.isValid())
		QToolTip::hideText();
	else {
		if (isActiveWindow()) {
			LogTextBrowser *browser = qobject_cast< LogTextBrowser * >(sender());
			QToolTip::showText(QCursor::pos(), url.toString(), browser ? browser : qteLog, QRect());
		}
	}
}

void MainWindow::context_triggered() {
	QAction *a = qobject_cast< QAction * >(sender());

	Channel *c    = pmModel->getChannel(qtvUsers->currentIndex());
	ClientUser *p = pmModel->getUser(qtvUsers->currentIndex());

	MumbleProto::ContextAction mpca;
	mpca.set_action(u8(a->data().toString()));
	if (p && p->uiSession)
		mpca.set_session(p->uiSession);
	if (c)
		mpca.set_channel_id(c->iId);
	Global::get().sh->sendMessage(mpca);
}

/**
 * Presents a file open dialog, opens the selected picture and returns it.
 * @return Pair consisting of the raw file contents and the image. Uninitialized on error or cancel.
 */
QPair< QByteArray, QImage > MainWindow::openImageFile() {
	QPair< QByteArray, QImage > retval;

	QString fname =
		QFileDialog::getOpenFileName(this, tr("Choose image file"), getImagePath(), tr("Images (*.png *.jpg *.jpeg)"));

	if (fname.isNull())
		return retval;

	QFile f(fname);
	if (!f.open(QIODevice::ReadOnly)) {
		QMessageBox::warning(this, tr("Failed to load image"), tr("Could not open file for reading."));
		return retval;
	}

	updateImagePath(fname);

	QByteArray qba = f.readAll();
	f.close();

	QBuffer qb(&qba);
	qb.open(QIODevice::ReadOnly);

	QImageReader qir;
	qir.setAutoDetectImageFormat(false);

	QByteArray fmt;
	if (!RichTextImage::isValidImage(qba, fmt)) {
		QMessageBox::warning(this, tr("Failed to load image"), tr("Image format not recognized."));
		return retval;
	}

	qir.setFormat(fmt);
	qir.setDevice(&qb);

	QImage img = qir.read();
	if (img.isNull()) {
		QMessageBox::warning(this, tr("Failed to load image"), tr("Image format not recognized."));
		return retval;
	}

	retval.first  = qba;
	retval.second = img;

	return retval;
}

void MainWindow::logChangeNotPermanent(const QString &actionName, ClientUser *const p) const {
	Global::get().l->log(
		Log::Warning,
		QObject::tr(
			"\"%1\" could not be saved permanently and is lost on restart because %2 does not have a certificate.")
			.arg(actionName)
			.arg(Log::formatClientUser(p, Log::Target)));
}

void MainWindow::destroyUserInformation() {
	UserInformation *ui = static_cast< UserInformation * >(sender());
	QMap< unsigned int, UserInformation * >::iterator i;
	for (i = qmUserInformations.begin(); i != qmUserInformations.end(); ++i) {
		if (i.value() == ui) {
			qmUserInformations.erase(i);
			return;
		}
	}
}

void MainWindow::openServerConnectDialog(bool autoconnect) {
	// Wait for this window to be mapped before opening the dialog, otherwise
	// Wayland compositors may not recognize the parent-child relationship.
	if (!windowHandle() || !windowHandle()->isExposed()) {
		// Ensure windowHandle() is non-null by forcing native window creation
		setAttribute(Qt::WA_NativeWindow);
		windowHandle()->installEventFilter(
			new ExposeEventFilter(this, [this, autoconnect]() { openServerConnectDialog(autoconnect); }));
		return;
	}

	ConnectDialog *cd = new ConnectDialog(this, autoconnect);
	int res           = cd->exec();

	if (cd->qsServer.isEmpty() || (cd->usPort == 0) || cd->qsUsername.isEmpty())
		res = QDialog::Rejected;

	if (res == QDialog::Accepted) {
		recreateServerHandler();
		qsDesiredChannel = QString();
		rtLast           = MumbleProto::Reject_RejectType_None;
		bRetryServer     = true;
		qaServerDisconnect->setEnabled(true);
		Global::get().l->log(
			Log::Information,
			tr("Connecting to server %1.").arg(Log::msgColor(cd->qsServer.toHtmlEscaped(), Log::Server)));
		Global::get().sh->setConnectionInfo(cd->qsServer, cd->usPort, cd->qsUsername, cd->qsPassword);
		Global::get().sh->start(QThread::TimeCriticalPriority);
	}
	delete cd;

	// update because the user might have changed his favorites
	updateFavoriteButton();
}

void MainWindow::disconnectFromServer() {
	if (qtReconnect->isActive()) {
		qtReconnect->stop();
		qaServerDisconnect->setEnabled(false);
	}

	m_reconnectSoundBlocker.reset();

	if (Global::get().sh && Global::get().sh->isRunning()) {
		Global::get().sh->disconnect();
	}
}

void MainWindow::addServerAsFavorite() {
	if (Global::get().uiSession == 0) {
		return;
	}
	QString host, username, password;
	unsigned short port;
	Global::get().sh->getConnectionInfo(host, port, username, password);
	ServerItem currentServer = ServerItem(host, host, port, username, password);
	Global::get().db->addFavorite(currentServer.toFavoriteServer());
	qaServerAddToFavorites->setEnabled(false);
	Global::get().l->log(Log::Information,
						 tr("Added %1 to favorites.").arg(Log::msgColor(host.toHtmlEscaped(), Log::Server)));
}

void MainWindow::openServerInformationDialog() {
	ServerInformation *infoDialog = new ServerInformation(this);
	infoDialog->show();
}

void MainWindow::openServerTokensDialog() {
	if (tokenEdit) {
		tokenEdit->reject();
		delete tokenEdit;
		tokenEdit = nullptr;
	}

	tokenEdit = new Tokens(this);
	tokenEdit->show();
}

void MainWindow::openServerUserListDialog() {
	Global::get().sh->requestUserList();

	if (userEdit) {
		userEdit->reject();
		delete userEdit;
		userEdit = nullptr;
	}
}

void MainWindow::openServerBanListDialog() {
	Global::get().sh->requestBanList();

	if (banEdit) {
		banEdit->reject();
		delete banEdit;
		banEdit = nullptr;
	}
}

void MainWindow::toggleSelfPrioritySpeaker() {
	ClientUser *p = ClientUser::get(Global::get().uiSession);
	if (!p)
		return;

	MumbleProto::UserState mpus;
	mpus.set_session(p->uiSession);
	mpus.set_priority_speaker(!p->bPrioritySpeaker);
	Global::get().sh->sendMessage(mpus);
}

void MainWindow::recording() {
	if (voiceRecorderDialog) {
		voiceRecorderDialog->show();
		voiceRecorderDialog->raise();
		voiceRecorderDialog->activateWindow();
	} else {
		voiceRecorderDialog = new VoiceRecorderDialog(this);
		connect(voiceRecorderDialog, SIGNAL(finished(int)), this, SLOT(voiceRecorderDialog_finished(int)));
		QObject::connect(Global::get().sh.get(), &ServerHandler::disconnected, voiceRecorderDialog, &QDialog::reject);
		voiceRecorderDialog->show();
	}
}

void MainWindow::openSelfCommentDialog() {
	ClientUser *p = ClientUser::get(Global::get().uiSession);
	if (!p)
		return;

	if (!p->qbaCommentHash.isEmpty() && p->qsComment.isEmpty()) {
		p->qsComment = QString::fromUtf8(Global::get().db->blob(p->qbaCommentHash));
		if (p->qsComment.isEmpty()) {
			pmModel->uiSessionComment = ~(p->uiSession);
			MumbleProto::RequestBlob mprb;
			mprb.add_session_comment(p->uiSession);
			Global::get().sh->sendMessage(mprb);
			return;
		}
	}

	unsigned int session = p->uiSession;

	::TextMessage *texm = new ::TextMessage(this, tr("Change your comment"));

	texm->rteMessage->setText(p->qsComment);
	int res = texm->exec();

	p = ClientUser::get(session);

	if (p && (res == QDialog::Accepted)) {
		const QString &msg = texm->message();
		MumbleProto::UserState mpus;
		mpus.set_session(session);
		mpus.set_comment(u8(msg));
		Global::get().sh->sendMessage(mpus);

		if (!msg.isEmpty())
			Global::get().db->setBlob(sha1(msg), msg.toUtf8());
	}
	delete texm;
}

void MainWindow::changeServerTexture() {
	QPair< QByteArray, QImage > choice = openImageFile();
	if (choice.first.isEmpty())
		return;

	const QImage &img = choice.second;

	if ((img.height() <= 1024) && (img.width() <= 1024))
		Global::get().sh->setUserTexture(Global::get().uiSession, choice.first);
}

void MainWindow::removeServerTexture() {
	Global::get().sh->setUserTexture(Global::get().uiSession, QByteArray());
}

void MainWindow::selfRegister() {
	ClientUser *p = ClientUser::get(Global::get().uiSession);
	if (!p)
		return;

	QMessageBox::StandardButton result;
	result =
		QMessageBox::question(this, tr("Register yourself as %1").arg(p->qsName),
							  tr("<p>You are about to register yourself on this server. This action cannot be undone, "
								 "and your username cannot be changed once this is done. You will forever be known as "
								 "'%1' on this server.</p><p>Are you sure you want to register yourself?</p>")
								  .arg(p->qsName.toHtmlEscaped()),
							  QMessageBox::Yes | QMessageBox::No);

	if (result == QMessageBox::Yes)
		Global::get().sh->registerUser(p->uiSession);
}

void MainWindow::openAudioStatsDialog() {
	AudioStats *as = new AudioStats(this);
	as->show();
}

void MainWindow::openConfigDialog() {
	ConfigDialog *dlg = new ConfigDialog(this);

	Global::get().inConfigUI = true;

	QObject::connect(dlg, &ConfigDialog::settingsAccepted, Global::get().talkingUI, &TalkingUI::on_settingsChanged);
	QObject::connect(dlg, &ConfigDialog::settingsAccepted, []() {
		if (Global::get().s.requireThemeApplication) {
			Themes::apply();
			if (Global::get().mw) {
				Global::get().mw->refreshCustomChromeStyles();
			}
		}
	});

	if (dlg->exec() == QDialog::Accepted) {
		setupView(false);
		showRaiseWindow();
		updateTransmitModeComboBox(Global::get().s.atTransmit);
		updateUserModel();
		emit talkingStatusChanged();

		if (Global::get().s.requireRestartToApply) {
			if (Global::get().s.requireRestartToApply
				&& QMessageBox::question(
					   this, tr("Restart Mumble?"),
					   tr("Some settings will only apply after a restart of Mumble. Restart Mumble now?"),
					   QMessageBox::Yes | QMessageBox::No)
					   == QMessageBox::Yes) {
				forceQuit     = true;
				restartOnQuit = true;

				close();
			}
		}
	}

	Global::get().inConfigUI = false;

	delete dlg;
}

void MainWindow::openAudioWizardDialog() {
	AudioWizard *aw = new AudioWizard(this);
	aw->exec();
	delete aw;
}

void MainWindow::openCertWizardDialog() {
	CertWizard *cw = new CertWizard(this);
	cw->exec();
	delete cw;
}

void MainWindow::enableAudioTTS(bool enable) {
	Global::get().s.bTTS = enable;
}

void MainWindow::openAboutDialog() {
	AboutDialog adAbout(this);
	adAbout.exec();
}

void MainWindow::openAboutQtDialog() {
	QMessageBox::aboutQt(this, tr("About Qt"));
}

void MainWindow::versionCheck() {
	new VersionCheck(false, this);
}

void MainWindow::enablePositionalAudio(bool enable) {
	Global::get().s.bPositionalAudio = enable;
}

void MainWindow::on_muteCuePopup_triggered() {
	showMuteCuePopup();
}

void MainWindow::showMuteCuePopup() {
	if (Global::get().s.muteCueShown || Global::get().inConfigUI) {
		return;
	}

	Global::get().s.muteCueShown = true;
	QMessageBox mb(
		QMessageBox::Warning, QLatin1String("Mumble"),
		tr("That sound was the mute cue. It activates when you speak while muted. Would you like to keep it enabled?"),
		QMessageBox::NoButton, this);
	QPushButton *accept = mb.addButton(tr("Yes"), QMessageBox::YesRole);
	QPushButton *reject = mb.addButton(tr("No"), QMessageBox::NoRole);
	mb.setDefaultButton(accept);
	mb.setEscapeButton(accept);
	mb.exec();

	if (mb.clickedButton() == reject) {
		Global::get().s.bTxMuteCue = false;
	}
}

void MainWindow::showImageDialog() {
	openImageDialog(m_selectedLogImage);
}
