// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_VERSIONCHECK_H_
#define MUMBLE_MUMBLE_VERSIONCHECK_H_

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QUrl>

class QNetworkReply;

class VersionCheck : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(VersionCheck)

	enum class RequestKind { Release, Manifest };

	QUrl m_requestURL;
	QNetworkReply *m_reply = nullptr;
	RequestKind m_requestKind = RequestKind::Release;
	bool m_autocheck         = false;
	int m_redirectCount      = 0;

	void request(const QUrl &url, RequestKind kind);
	void finishWithInfo(const QJsonObject &info);
	void finishWithFailure(const QString &message);
protected slots:
	void performRequest();
	void replyFinished();
public slots:
	void fetched(QByteArray data, QUrl url);

public:
	VersionCheck(bool autocheck, QObject *parent = nullptr, bool focus = false);
};

#endif
