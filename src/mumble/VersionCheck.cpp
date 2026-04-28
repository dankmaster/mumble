// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "VersionCheck.h"

#include "Global.h"
#include "MainWindow.h"
#include "NetworkConfig.h"
#include "Version.h"

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>

namespace {

constexpr int MaxRedirects = 5;

QUrl defaultReleaseApiUrl() {
	return QUrl(QStringLiteral("https://api.github.com/repos/dankmaster/mumble/releases/tags/mumble-forked"));
}

QUrl configuredReleaseApiUrl() {
	const QString override = qEnvironmentVariable("MUMBLE_FORK_UPDATE_URL").trimmed();
	if (!override.isEmpty()) {
		const QUrl url(override);
		if (url.isValid()) {
			return url;
		}
	}

	return defaultReleaseApiUrl();
}

bool forceUpdateNotification() {
	const QString value = qEnvironmentVariable("MUMBLE_FORK_FORCE_UPDATE_NOTIFICATION").trimmed().toLower();
	return value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
		   || value == QLatin1String("on");
}

int jsonInt(const QJsonObject &object, const QString &key, int fallback = -1) {
	const QJsonValue value = object.value(key);
	if (value.isDouble()) {
		return value.toInt(fallback);
	}
	if (value.isString()) {
		bool ok = false;
		const int parsed = value.toString().trimmed().toInt(&ok);
		if (ok) {
			return parsed;
		}
	}
	return fallback;
}

QUrl jsonUrl(const QJsonObject &object, const QString &key) {
	const QUrl url(object.value(key).toString().trimmed());
	return url.isValid() ? url : QUrl();
}

QJsonObject parseJsonObject(const QByteArray &data, QString *errorMessage) {
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(data, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		if (errorMessage) {
			*errorMessage = VersionCheck::tr("The update response was not valid JSON.");
		}
		return {};
	}

	return document.object();
}

QString releaseBodyValue(const QString &body, const QString &key) {
	const QRegularExpression regex(
		QRegularExpression::anchoredPattern(QStringLiteral(".*(?:^|\\n)\\s*-\\s*%1:\\s*([^\\n\\r]+).*").arg(key)),
		QRegularExpression::DotMatchesEverythingOption);
	const QRegularExpressionMatch match = regex.match(body);
	return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QJsonObject updateInfoFromRelease(const QJsonObject &release, QUrl *manifestUrl) {
	QJsonObject info;
	info.insert(QStringLiteral("releaseUrl"), release.value(QStringLiteral("html_url")).toString());
	info.insert(QStringLiteral("publishedAt"), release.value(QStringLiteral("published_at")).toString());

	const QString body = release.value(QStringLiteral("body")).toString();
	const QString build = releaseBodyValue(body, QStringLiteral("Build"));
	const QString version = releaseBodyValue(body, QStringLiteral("Version"));
	const QString commit = releaseBodyValue(body, QStringLiteral("Commit"));
	if (!build.isEmpty()) {
		info.insert(QStringLiteral("build"), build);
	}
	if (!version.isEmpty()) {
		info.insert(QStringLiteral("version"), version);
	}
	if (!commit.isEmpty()) {
		info.insert(QStringLiteral("commit"), commit);
	} else {
		info.insert(QStringLiteral("commit"), release.value(QStringLiteral("target_commitish")).toString());
	}

	const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
	for (const QJsonValue &assetValue : assets) {
		const QJsonObject asset = assetValue.toObject();
		const QString name      = asset.value(QStringLiteral("name")).toString();
		const QString url       = asset.value(QStringLiteral("browser_download_url")).toString();
		if (name == QLatin1String("mumble-forked-update.json") && manifestUrl) {
			*manifestUrl = QUrl(url);
		} else if (name == QLatin1String("mumble-forked.msi")) {
			info.insert(QStringLiteral("installerUrl"), url);
		}
	}

	return info;
}

QJsonObject normalizeManifestInfo(const QJsonObject &manifest) {
	QJsonObject info = manifest;
	if (!info.contains(QStringLiteral("releaseUrl"))) {
		info.insert(QStringLiteral("releaseUrl"),
					QStringLiteral("https://github.com/dankmaster/mumble/releases/tag/mumble-forked"));
	}
	if (!info.contains(QStringLiteral("installerUrl"))) {
		info.insert(QStringLiteral("installerUrl"),
					QStringLiteral("https://github.com/dankmaster/mumble/releases/download/mumble-forked/mumble-forked.msi"));
	}
	return info;
}

QString latestLabel(const QJsonObject &info) {
	const QString version = info.value(QStringLiteral("version")).toString().trimmed();
	const int build       = jsonInt(info, QStringLiteral("build"));

	if (!version.isEmpty() && build >= 0) {
		return VersionCheck::tr("%1, build %2").arg(version).arg(build);
	}
	if (!version.isEmpty()) {
		return version;
	}
	if (build >= 0) {
		return VersionCheck::tr("build %1").arg(build);
	}
	return VersionCheck::tr("the latest build");
}

bool isUpdateAvailable(const QJsonObject &info) {
	if (forceUpdateNotification()) {
		return true;
	}

	const int latestBuild  = jsonInt(info, QStringLiteral("build"));
	const int currentBuild = Version::getPatch(Version::get());
	if (latestBuild >= 0) {
		return latestBuild > currentBuild;
	}

	const QString latestVersion = info.value(QStringLiteral("version")).toString().trimmed();
	if (!latestVersion.isEmpty()) {
		const Version::full_t parsedVersion = Version::fromString(latestVersion);
		return parsedVersion != Version::UNKNOWN && parsedVersion > Version::get();
	}

	return false;
}

} // namespace

VersionCheck::VersionCheck(bool autocheck, QObject *parent, bool) : QObject(parent), m_autocheck(autocheck) {
	QTimer::singleShot(0, this, &VersionCheck::performRequest);
}

void VersionCheck::performRequest() {
	request(configuredReleaseApiUrl(), RequestKind::Release);
}

void VersionCheck::request(const QUrl &url, RequestKind kind) {
	if (!url.isValid() || url.scheme() != QLatin1String("https")) {
		finishWithFailure(tr("The forked update URL is invalid."));
		return;
	}

	m_requestKind = kind;
	m_requestURL  = url;

	QNetworkRequest request(url);
	Network::prepareRequest(request);
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

	m_reply = Global::get().nam->get(request);
	connect(m_reply, &QNetworkReply::finished, this, &VersionCheck::replyFinished);
}

void VersionCheck::replyFinished() {
	QNetworkReply *reply = qobject_cast< QNetworkReply * >(sender());
	if (!reply) {
		finishWithFailure(tr("The forked update request failed."));
		return;
	}

	const QUrl replyUrl = reply->url();
	const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
	if (redirectTarget.isValid()) {
		const QUrl nextUrl = replyUrl.resolved(redirectTarget.toUrl());
		reply->deleteLater();
		m_reply = nullptr;

		if (m_redirectCount >= MaxRedirects) {
			finishWithFailure(tr("The forked update request redirected too many times."));
			return;
		}
		++m_redirectCount;
		request(nextUrl, m_requestKind);
		return;
	}

	const QByteArray data = reply->readAll();
	const QNetworkReply::NetworkError error = reply->error();
	const QString errorString = reply->errorString();
	reply->deleteLater();
	m_reply = nullptr;

	if (error != QNetworkReply::NoError) {
		finishWithFailure(tr("Mumble failed to retrieve forked update information from GitHub: %1").arg(errorString));
		return;
	}

	QString parseError;
	const QJsonObject object = parseJsonObject(data, &parseError);
	if (object.isEmpty()) {
		finishWithFailure(parseError);
		return;
	}

	if (m_requestKind == RequestKind::Release) {
		QUrl manifestUrl;
		const QJsonObject releaseInfo = updateInfoFromRelease(object, &manifestUrl);
		if (manifestUrl.isValid()) {
			m_redirectCount = 0;
			request(manifestUrl, RequestKind::Manifest);
			return;
		}
		finishWithInfo(releaseInfo);
	} else {
		finishWithInfo(normalizeManifestInfo(object));
	}
}

void VersionCheck::finishWithInfo(const QJsonObject &info) {
	if (isUpdateAvailable(info)) {
		const QUrl installerUrl = jsonUrl(info, QStringLiteral("installerUrl"));
		const QUrl releaseUrl   = jsonUrl(info, QStringLiteral("releaseUrl"));
		const QUrl openUrl      = installerUrl.isValid() ? installerUrl : releaseUrl;

		QMessageBox messageBox(QMessageBox::Information, tr("Mumble update available"),
							   tr("A new mumble-forked build is available."), QMessageBox::NoButton,
							   Global::get().mw);
		messageBox.setInformativeText(
			tr("Current: %1, build %2\nLatest: %3")
				.arg(Version::getRelease())
				.arg(Version::getPatch(Version::get()))
				.arg(latestLabel(info)));

		QString details;
		const QString commit = info.value(QStringLiteral("commit")).toString().trimmed();
		if (!commit.isEmpty()) {
			details += tr("Commit: %1\n").arg(commit);
		}
		const QString sha256 = info.value(QStringLiteral("sha256")).toString().trimmed();
		if (!sha256.isEmpty()) {
			details += tr("SHA256: %1\n").arg(sha256);
		}
		if (releaseUrl.isValid()) {
			details += tr("Release: %1\n").arg(releaseUrl.toString());
		}
		if (!details.isEmpty()) {
			messageBox.setDetailedText(details.trimmed());
		}

		QPushButton *openButton = nullptr;
		if (openUrl.isValid()) {
			openButton = messageBox.addButton(tr("Open download"), QMessageBox::AcceptRole);
		}
		messageBox.addButton(tr("Not now"), QMessageBox::RejectRole);
		messageBox.exec();

		if (openButton && messageBox.clickedButton() == openButton) {
			QDesktopServices::openUrl(openUrl);
		}
	} else if (!m_autocheck && Global::get().mw) {
		Global::get().mw->msgBox(
			tr("You're on the latest mumble-forked build (%1, build %2).")
				.arg(Version::getRelease())
				.arg(Version::getPatch(Version::get())));
	}

	deleteLater();
}

void VersionCheck::finishWithFailure(const QString &message) {
	if (!m_autocheck && Global::get().mw) {
		Global::get().mw->msgBox(message);
	}
	deleteLater();
}

void VersionCheck::fetched(QByteArray data, QUrl url) {
	Q_UNUSED(data);
	Q_UNUSED(url);
}
