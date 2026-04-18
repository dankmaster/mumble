// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "widgets/FailedConnectionDialog.h"
#include "Database.h"
#include "MainWindow.h"
#include "ServerHandler.h"
#include "Global.h"

#include <QAction>
#include <QChar>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>

namespace {
	bool isStockServerSafeAsciiUserChar(const QChar ch) {
		const ushort value = ch.unicode();
		if (value > 0x7F) {
			return false;
		}

		return (value >= 0x20 && value <= 0x3D) || (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z')
			   || value == '_' || value == '@' || value == '[' || value == ']' || value == '{' || value == '}'
			   || value == '|';
	}

	void appendCompatibilityReplacement(QString &result, const QChar ch) {
		switch (ch.unicode()) {
			case 0x00C6:
				result += QStringLiteral("AE");
				break;
			case 0x00E6:
				result += QStringLiteral("ae");
				break;
			case 0x00D8:
				result += QLatin1Char('O');
				break;
			case 0x00F8:
				result += QLatin1Char('o');
				break;
			case 0x00DE:
				result += QStringLiteral("TH");
				break;
			case 0x00FE:
				result += QStringLiteral("th");
				break;
			case 0x00DF:
				result += QStringLiteral("ss");
				break;
			case 0x0110:
				result += QLatin1Char('D');
				break;
			case 0x0111:
				result += QLatin1Char('d');
				break;
			case 0x0141:
				result += QLatin1Char('L');
				break;
			case 0x0142:
				result += QLatin1Char('l');
				break;
			case 0x0152:
				result += QStringLiteral("OE");
				break;
			case 0x0153:
				result += QStringLiteral("oe");
				break;
			default:
				break;
		}
	}

	QString buildServerSafeUsernameSuggestion(const QString &userName) {
		const QString normalized = userName.normalized(QString::NormalizationForm_KD).trimmed();
		QString suggestion;
		suggestion.reserve(normalized.size());

		for (const QChar ch : normalized) {
			const QChar::Category category = ch.category();
			if (category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining
				|| category == QChar::Mark_Enclosing) {
				continue;
			}

			if (isStockServerSafeAsciiUserChar(ch)) {
				suggestion += ch;
				continue;
			}

			appendCompatibilityReplacement(suggestion, ch);
		}

		return suggestion.trimmed();
	}
} // namespace

FailedConnectionDialog::FailedConnectionDialog(ConnectDetails details, ConnectionFailType type, QWidget *parent)
	: QDialog(parent), m_details(std::move(details)) {
	setupUi(this);

	invalidUserNameInput->setText(m_details.username);
	usedUsernameInput->setText(m_details.username);

	userPasswordInput->setText(m_details.password);
	serverPasswordInput->setText(m_details.password);

	connectSignals();

	switch (type) {
		case ConnectionFailType::AuthenticationFailure:
			pageStack->setCurrentWidget(authenticationFailed);
			wrongPasswordMsg->setVisible(!m_details.password.isEmpty());
			wrongCertificateMsg->setVisible(m_details.password.isEmpty());
			break;
		case ConnectionFailType::InvalidServerPassword:
			pageStack->setCurrentWidget(wrongServerPassword);
			break;
		case ConnectionFailType::InvalidUsername:
			pageStack->setCurrentWidget(invalidUsername);
			usernameAlreadyUsedMsg->setVisible(false);
			refreshInvalidUsernameSuggestion();
			break;
		case ConnectionFailType::UsernameAlreadyInUse:
			pageStack->setCurrentWidget(invalidUsername);
			invalidUsernameMsg->setVisible(false);
			serverSafeUsernameHint->setVisible(false);
			useSuggestedUsernameButton->setVisible(false);
			break;
	}
}

void FailedConnectionDialog::connectSignals() {
	QObject::connect(reconnectButton, &QPushButton::clicked, this, &FailedConnectionDialog::initiateReconnect);

	QObject::connect(reconnectButton, &QPushButton::clicked, this, &QDialog::close);
	QObject::connect(cancelButton, &QPushButton::clicked, this, &QDialog::close);

	// auth failure page
	QObject::connect(usedUsernameInput, &QLineEdit::textChanged,
					 [this](QString name) { m_details.username = std::move(name); });
	QObject::connect(userPasswordInput, &QLineEdit::textChanged,
					 [this](QString pw) { m_details.password = std::move(pw); });
	QObject::connect(changeCertButton, &QPushButton::clicked, Global::get().mw->qaConfigCert, &QAction::triggered);

	// invalid server password page
	QObject::connect(serverPasswordInput, &QLineEdit::textChanged,
					 [this](QString pw) { m_details.password = std::move(pw); });

	// invalid username page
	QObject::connect(invalidUserNameInput, &QLineEdit::textChanged, [this](QString name) {
		m_details.username = std::move(name);
		refreshInvalidUsernameSuggestion();
	});
	QObject::connect(useSuggestedUsernameButton, &QPushButton::clicked, [this]() {
		if (m_serverSafeUsernameSuggestion.isEmpty()) {
			return;
		}

		invalidUserNameInput->setText(m_serverSafeUsernameSuggestion);
		invalidUserNameInput->setFocus();
		invalidUserNameInput->selectAll();
	});
}

void FailedConnectionDialog::refreshInvalidUsernameSuggestion() {
	m_serverSafeUsernameSuggestion = buildServerSafeUsernameSuggestion(invalidUserNameInput->text());
	const bool hasSuggestion = !m_serverSafeUsernameSuggestion.isEmpty()
							   && m_serverSafeUsernameSuggestion.compare(invalidUserNameInput->text().trimmed()) != 0;

	serverSafeUsernameHint->setVisible(hasSuggestion);
	useSuggestedUsernameButton->setVisible(hasSuggestion);

	if (!hasSuggestion) {
		serverSafeUsernameHint->clear();
		useSuggestedUsernameButton->setText(tr("Use suggested username"));
		return;
	}

	serverSafeUsernameHint->setText(
		tr("Some stock Mumble servers only accept ASCII-style usernames. Suggested compatible name: %1")
			.arg(m_serverSafeUsernameSuggestion.toHtmlEscaped()));
	useSuggestedUsernameButton->setText(tr("Use %1").arg(m_serverSafeUsernameSuggestion));
}

void FailedConnectionDialog::initiateReconnect() {
	if (!Global::get().s.bSuppressIdentity) {
		Global::get().db->setPassword(m_details.host, m_details.port, m_details.username, m_details.password);
	}

	Global::get().sh->setConnectionInfo(m_details.host, m_details.port, m_details.username, m_details.password);

	// Reuse logic implemented for automatic reconnect attempts to actually perform the connection
	Global::get().mw->on_Reconnect_timeout();
}
