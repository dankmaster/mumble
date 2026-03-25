// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Server.h"

#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QSslError>

void Server::initRegister() {
	log("Public registration disabled in this build");
}

void Server::update() {
	// Public registration is disabled in this build.
}

void Server::finished() {
	QNetworkReply *rep = qobject_cast< QNetworkReply * >(sender());

	if (rep->error() != QNetworkReply::NoError) {
		log(QString("Registration failed: %1").arg(rep->errorString()));
	} else {
		QByteArray qba = rep->readAll();
		log(QString("Registration: %1").arg(QLatin1String(qba)));
	}
	rep->deleteLater();
}

void Server::regSslError(const QList< QSslError > &errs) {
	for (const QSslError &e : errs) {
		log(QString("Registration: SSL Handshake error: %1").arg(e.errorString()));
	}
}
