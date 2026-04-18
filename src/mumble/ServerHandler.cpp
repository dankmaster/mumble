// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QSslConfiguration>
#include <QtCore/QtGlobal>

#ifdef Q_OS_WIN
#	include "win.h"
#endif

#include "ServerHandler.h"

#include "AudioInput.h"
#include "AudioOutput.h"
#include "Cert.h"
#include "Connection.h"
#include "Database.h"
#include "HostAddress.h"
#include "MainWindow.h"
#include "Net.h"
#include "NetworkConfig.h"
#include "OSInfo.h"
#include "PacketDataStream.h"
#include "ProtoUtils.h"
#include "RichTextEditor.h"
#include "ScreenShareHelperClient.h"
#include "SSL.h"
#include "ServerResolver.h"
#include "ServerResolverRecord.h"
#include "User.h"
#include "Utils.h"
#include "Global.h"

#include <QDateTime>
#include <QFile>
#include <QPainter>
#include <QtCore/QtEndian>
#include <QtGui/QImageReader>
#include <QtNetwork/QSslConfiguration>
#include <QtNetwork/QUdpSocket>

#include <openssl/crypto.h>

#include <cassert>
#include <chrono>
#include <span>

#ifdef Q_OS_WIN
// <delayimp.h> is not protected with an include guard on MinGW, resulting in
// redefinitions if the PCH header is used.
// The workaround consists in including the header only if _DELAY_IMP_VER
// (defined in the header) is not defined.
#	ifndef _DELAY_IMP_VER
#		include <delayimp.h>
#	endif
#	include <qos2.h>
#	include <wincrypt.h>
#	include <winsock2.h>
#else
#	if defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD)
#		include <netinet/in.h>
#	endif
#	include <netinet/ip.h>
#	include <sys/socket.h>
#endif

// Init ServerHandler::nextConnectionID
int ServerHandler::nextConnectionID = -1;
QMutex ServerHandler::nextConnectionIDMutex;

namespace {
	bool connectTraceEnabled() {
		static const bool enabled = qEnvironmentVariableIntValue("MUMBLE_CONNECT_TRACE") != 0;
		return enabled;
	}

	void appendServerHandlerTrace(const QString &message) {
		if (!connectTraceEnabled()) {
			return;
		}

		QFile traceFile(Global::get().qdBasePath.filePath(QLatin1String("shared-modern-connect-trace.log")));
		if (!traceFile.open(QIODevice::Append | QIODevice::Text)) {
			return;
		}

		const QByteArray line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8() + " SH "
								+ message.toUtf8() + '\n';
		traceFile.write(line);
		traceFile.flush();
	}

	QString utf8Hex(const QString &value) {
		return QString::fromLatin1(value.toUtf8().toHex());
	}

	QString codePointList(const QString &value) {
		QStringList codePoints;
		const QList< uint > unicode = value.toUcs4();
		codePoints.reserve(unicode.size());
		for (uint codePoint : unicode) {
			codePoints << QString::fromLatin1("U+%1").arg(codePoint, 0, 16);
		}

		return codePoints.join(QLatin1Char(','));
	}

	QString sslErrorsSummary(const QList< QSslError > &errors) {
		QStringList summaries;
		summaries.reserve(errors.size());
		for (const QSslError &error : errors) {
			summaries << QString::fromLatin1("%1:%2")
							 .arg(QString::number(static_cast< int >(error.error())), error.errorString());
		}

		return summaries.join(QLatin1String(" | "));
	}
} // namespace

ServerHandlerMessageEvent::ServerHandlerMessageEvent(const QByteArray &msg, Mumble::Protocol::TCPMessageType type,
													 bool flush)
	: QEvent(static_cast< QEvent::Type >(SERVERSEND_EVENT)) {
	qbaMsg     = msg;
	this->type = type;
	bFlush     = flush;
}

#ifdef Q_OS_WIN
static HANDLE loadQoS() {
	HANDLE hQoS = nullptr;

	HRESULT hr = E_FAIL;

// We don't support delay-loading QoS on MinGW. Only enable it for MSVC.
#	ifdef _MSC_VER
	__try {
		hr = __HrLoadAllImportsForDll("qwave.dll");
	}

	__except (EXCEPTION_EXECUTE_HANDLER) {
		hr = E_FAIL;
	}
#	endif

	if (!SUCCEEDED(hr)) {
		qWarning("ServerHandler: Failed to load qWave.dll, no QoS available");
	} else {
		QOS_VERSION qvVer;
		qvVer.MajorVersion = 1;
		qvVer.MinorVersion = 0;

		if (!QOSCreateHandle(&qvVer, &hQoS)) {
			qWarning("ServerHandler: Failed to create QOS2 handle");
			hQoS = nullptr;
		} else {
			qWarning("ServerHandler: QOS2 loaded");
		}
	}
	return hQoS;
}
#endif

ServerHandler::ServerHandler() : database(new Database(QLatin1String("ServerHandler"))) {
	cConnection.reset();
	qusUdp                  = nullptr;
	bStrong                 = false;
	usPort                  = 0;
	bUdp                    = true;
	tConnectionTimeoutTimer = nullptr;
	m_version               = Version::UNKNOWN;
	iInFlightTCPPings       = 0;

	// assign connection ID
	{
		QMutexLocker lock(&nextConnectionIDMutex);
		nextConnectionID++;
		connectionID = nextConnectionID;
	}

	// Historically, the qWarning line below initialized OpenSSL for us.
	// It used to have this comment:
	//
	//     "For some strange reason, on Win32, we have to call
	//      supportsSsl before the cipher list is ready."
	//
	// Now, OpenSSL is initialized in main() via MumbleSSL::initialize(),
	// but since it's handy to have the OpenSSL version available, we
	// keep this one around as well.
	qWarning("OpenSSL Support: %d (%s)", QSslSocket::supportsSsl(), SSLeay_version(SSLEAY_VERSION));

	MumbleSSL::addSystemCA();

	{
		QList< QSslCipher > ciphers = MumbleSSL::ciphersFromOpenSSLCipherString(Global::get().s.qsSslCiphers);
		if (ciphers.isEmpty()) {
			qFatal("Invalid 'net/sslciphers' config option. Either the cipher string is invalid or none of the ciphers "
				   "are available:: \"%s\"",
				   qPrintable(Global::get().s.qsSslCiphers));
		}

		QSslConfiguration config = QSslConfiguration::defaultConfiguration();
		config.setCiphers(ciphers);
		QSslConfiguration::setDefaultConfiguration(config);

		QStringList pref;
		for (const QSslCipher &c : ciphers) {
			pref << c.name();
		}
		qWarning("ServerHandler: TLS cipher preference is \"%s\"", qPrintable(pref.join(QLatin1String(":"))));
	}

#ifdef Q_OS_WIN
	hQoS = loadQoS();
	if (hQoS)
		Connection::setQoS(hQoS);
#endif

	QObject::connect(this, &ServerHandler::pingRequested, this, &ServerHandler::sendPingInternal, Qt::QueuedConnection);
	QObject::connect(this, &ServerHandler::abortRequested, this, &ServerHandler::abortConnection);
}

ServerHandler::~ServerHandler() {
	wait();
	cConnection.reset();
#ifdef Q_OS_WIN
	if (hQoS) {
		QOSCloseHandle(hQoS);
		Connection::setQoS(nullptr);
	}
#endif
}

void ServerHandler::customEvent(QEvent *evt) {
	if (evt->type() != SERVERSEND_EVENT) {
		return;
	}

	ServerHandlerMessageEvent *shme = static_cast< ServerHandlerMessageEvent * >(evt);

	ConnectionPtr connection(cConnection);
	if (connection) {
		if (shme->qbaMsg.size() > 0) {
			connection->sendMessage(shme->qbaMsg);
			if (shme->bFlush) {
				connection->forceFlush();
			}
		}
	}
}

void ServerHandler::changeState(ServerHandlerState state) {
	if (isAborted()) {
		return;
	}

	m_state = state;

	if (isAborted()) {
		exit(0);
	}
}

void ServerHandler::abortConnection() {
	changeState(ServerHandlerState::Aborted);
}

bool ServerHandler::isAborted() {
	return m_state == ServerHandlerState::Aborted;
}

int ServerHandler::getConnectionID() const {
	return connectionID;
}

void ServerHandler::setProtocolVersion(Version::full_t version) {
	m_version = version;

	m_udpPingEncoder.setProtocolVersion(version);
	m_udpDecoder.setProtocolVersion(version);
	m_tcpTunnelDecoder.setProtocolVersion(version);
}

void ServerHandler::udpReady() {
	while (qusUdp->hasPendingDatagrams()) {
		char encrypted[Mumble::Protocol::MAX_UDP_PACKET_SIZE];
		unsigned int buflen = static_cast< unsigned int >(qusUdp->pendingDatagramSize());

		if (buflen > Mumble::Protocol::MAX_UDP_PACKET_SIZE) {
			// Discard datagrams that exceed our buffer's size as we'd have to trim them down anyways and it is not very
			// likely that the data is valid in the trimmed down form.
			// As we're using a maxSize of 0 it is okay to pass nullptr as the data buffer. Qt's docs (5.15) ensures
			// that a maxSize of 0 means discarding the datagram.
			qusUdp->readDatagram(nullptr, 0);
			continue;
		}

		QHostAddress senderAddr;
		quint16 senderPort;
		qusUdp->readDatagram(encrypted, buflen, &senderAddr, &senderPort);

		if (!(HostAddress(senderAddr) == HostAddress(qhaRemote)) || (senderPort != usResolvedPort))
			continue;

		ConnectionPtr connection(cConnection);
		if (!connection)
			continue;

		if (!connection->csCrypt->isValid())
			continue;

		if (buflen < 5)
			continue;

		std::span< Mumble::Protocol::byte > buffer = m_udpDecoder.getBuffer();

		// 4 bytes is the overhead of the encryption
		assert(buffer.size() >= buflen - 4);

		if (!connection->csCrypt->decrypt(reinterpret_cast< const unsigned char * >(encrypted), buffer.data(),
										  buflen)) {
			if (connection->csCrypt->tLastGood.elapsed() > std::chrono::seconds(5)) {
				if (connection->csCrypt->tLastRequest.elapsed() > std::chrono::seconds(5)) {
					connection->csCrypt->tLastRequest.restart();
					MumbleProto::CryptSetup mpcs;
					sendMessage(mpcs);
				}
			}
			continue;
		}

		if (m_udpDecoder.decode(buffer.subspan(0, buflen - 4))) {
			switch (m_udpDecoder.getMessageType()) {
				case Mumble::Protocol::UDPMessageType::Ping: {
					const Mumble::Protocol::PingData pingData = m_udpDecoder.getPingData();

					accUDP(static_cast< double >(static_cast< std::uint64_t >(tTimestamp.elapsed().count())
												 - pingData.timestamp)
						   / 1000.0);

					break;
				}
				case Mumble::Protocol::UDPMessageType::Audio: {
					const Mumble::Protocol::AudioData audioData = m_udpDecoder.getAudioData();

					handleVoicePacket(audioData);
					break;
				};
			}
		}
	}
}

void ServerHandler::handleVoicePacket(const Mumble::Protocol::AudioData &audioData) {
	if (audioData.usedCodec != Mumble::Protocol::AudioCodec::Opus) {
		qWarning("Dropping audio packet using invalid codec (not Opus): %d", static_cast< int >(audioData.usedCodec));
		return;
	}

	ClientUser *sender = ClientUser::get(audioData.senderSession);

	AudioOutputPtr ao = Global::get().ao;
	if (ao && sender
		&& !((audioData.targetOrContext == Mumble::Protocol::AudioContext::WHISPER) && Global::get().s.bWhisperFriends
			 && sender->qsFriendName.isEmpty())) {
		ao->addFrameToBuffer(sender, audioData);
	}
}

void ServerHandler::sendMessage(const unsigned char *data, int len, bool force) {
	static std::vector< unsigned char > crypto;
	crypto.resize(static_cast< std::size_t >(len + 4));

	QMutexLocker qml(&qmUdp);

	if (!qusUdp)
		return;

	ConnectionPtr connection(cConnection);
	if (!connection || !connection->csCrypt->isValid())
		return;

	if (!force && (NetworkConfig::TcpModeEnabled() || !bUdp)) {
		QByteArray qba;

		qba.resize(len + 6);
		unsigned char *uc = reinterpret_cast< unsigned char * >(qba.data());
		*reinterpret_cast< quint16 * >(&uc[0]) =
			qToBigEndian(static_cast< quint16 >(Mumble::Protocol::TCPMessageType::UDPTunnel));
		*reinterpret_cast< quint32 * >(&uc[2]) = qToBigEndian(static_cast< quint32 >(len));
		memcpy(uc + 6, data, static_cast< std::size_t >(len));

		QApplication::postEvent(this,
								new ServerHandlerMessageEvent(qba, Mumble::Protocol::TCPMessageType::UDPTunnel, true));
	} else {
		if (!connection->csCrypt->encrypt(reinterpret_cast< const unsigned char * >(data), crypto.data(),
										  static_cast< unsigned int >(len))) {
			return;
		}
		qusUdp->writeDatagram(reinterpret_cast< const char * >(crypto.data()), len + 4, qhaRemote, usResolvedPort);
	}
}

void ServerHandler::sendProtoMessage(const ::google::protobuf::Message &msg, Mumble::Protocol::TCPMessageType type) {
	QByteArray qba;

	if (QThread::currentThread() != thread()) {
		Connection::messageToNetwork(msg, type, qba);
		ServerHandlerMessageEvent *shme = new ServerHandlerMessageEvent(qba, type, false);
		QApplication::postEvent(this, shme);
	} else {
		ConnectionPtr connection(cConnection);
		if (!connection)
			return;

		connection->sendMessage(msg, type, qba);
	}
}

bool ServerHandler::isConnected() const {
	// If the digest isn't empty, then we are currently connected to a server (the digest being a hash
	// of the server's certificate)
	return !qbaDigest.isEmpty();
}

bool ServerHandler::hasSynchronized() const {
	return serverSynchronized;
}

void ServerHandler::setServerSynchronized(bool synchronized) {
	serverSynchronized = synchronized;
}

void ServerHandler::hostnameResolved() {
	ServerResolver *sr                    = qobject_cast< ServerResolver * >(QObject::sender());
	QList< ServerResolverRecord > records = sr->records();

	// Exit the ServerHandler thread's event loop with an
	// error code in case our hostname lookup failed.
	if (records.isEmpty()) {
		exit(-1);
		return;
	}

	// Create the list of target host:port pairs
	// that the ServerHandler should try to connect to.
	QList< ServerAddress > ql;
	QHash< ServerAddress, QString > qh;
	for (ServerResolverRecord &record : records) {
		for (const HostAddress &addr : record.addresses()) {
			auto sa = ServerAddress(addr, record.port());
			ql.append(sa);
			qh[sa] = record.hostname();
		}
	}
	qlAddresses = ql;
	qhHostnames = qh;

	// Exit the event loop with 'success' status code,
	// to continue connecting to the server.
	exit(0);
}

void ServerHandler::run() {
	// Resolve the hostname...

	changeState(ServerHandlerState::DNSQuery);

	{
		ServerResolver sr;
		QObject::connect(&sr, &ServerResolver::resolved, this, &ServerHandler::hostnameResolved);
		sr.resolve(qsHostName, usPort);
		int ret = exec();
		if (ret < 0) {
			qWarning("ServerHandler: failed to resolve hostname");
			changeState(ServerHandlerState::DNSFailed);
			emit error(QAbstractSocket::HostNotFoundError, tr("Unable to resolve hostname"));
			return;
		}
		changeState(ServerHandlerState::DNSResolved);
	}

	if (isAborted()) {
		// Connection aborted during DNS resolve...
		return;
	}

	QList< ServerAddress > targetAddresses(qlAddresses);
	bool shouldTryNextTargetServer = true;
	do {
		saTargetServer = qlAddresses.takeFirst();

		tConnectionTimeoutTimer = nullptr;
		qbaDigest               = QByteArray();
		bStrong                 = true;
		qtsSock                 = new QSslSocket(this);
		qtsSock->setPeerVerifyName(qhHostnames[saTargetServer]);

		if (!Global::get().s.bSuppressIdentity && CertWizard::validateCert(Global::get().s.kpCertificate)) {
			qtsSock->setPrivateKey(Global::get().s.kpCertificate.second);
			qtsSock->setLocalCertificate(Global::get().s.kpCertificate.first.at(0));
			QSslConfiguration config       = qtsSock->sslConfiguration();
			QList< QSslCertificate > certs = config.caCertificates();
			certs << Global::get().s.kpCertificate.first;
			config.setCaCertificates(certs);
			qtsSock->setSslConfiguration(config);
		}

		{
			ConnectionPtr connection(new Connection(this, qtsSock));
			cConnection = connection;

			// Technically it isn't necessary to reset this flag here since a ServerHandler will not be used
			// for multiple connections in a row but just in case that at some point it will, we'll reset the
			// flag here.
			serverSynchronized = false;

			qlErrors.clear();
			qscCert.clear();

			QObject::connect(qtsSock, &QSslSocket::encrypted, this, &ServerHandler::serverConnectionConnected);
			QObject::connect(qtsSock, &QSslSocket::stateChanged, this, &ServerHandler::serverConnectionStateChanged);
			QObject::connect(connection.get(), &Connection::connectionClosed, this,
							 &ServerHandler::serverConnectionClosed);
			QObject::connect(connection.get(), &Connection::message, this, &ServerHandler::message);
			QObject::connect(connection.get(), &Connection::handleSslErrors, this, &ServerHandler::setSslErrors);
		}
		bUdp = false;

#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
		qtsSock->setProtocol(QSsl::TlsV1_2OrLater);
#else
		qtsSock->setProtocol(QSsl::TlsV1_0OrLater);
#endif

		qtsSock->connectToHost(saTargetServer.host.toAddress(), saTargetServer.port);

		tTimestamp.restart();

		// Setup ping timer;
		QTimer *ticker = new QTimer(this);
		QObject::connect(ticker, &QTimer::timeout, this, &ServerHandler::sendPing);
		ticker->start(Global::get().s.iPingIntervalMsec);

		Global::get().mw->rtLast = MumbleProto::Reject_RejectType_None;

		accUDP = accTCP = accClean;

		m_version   = Version::UNKNOWN;
		qsRelease   = QString();
		qsOS        = QString();
		qsOSVersion = QString();

		changeState(ServerHandlerState::AwaitingConnection);
		int ret = exec();
		if (ret == -2) {
			shouldTryNextTargetServer = true;
		} else {
			shouldTryNextTargetServer = false;
		}
		changeState(ServerHandlerState::Disconnecting);

		if (qusUdp) {
			QMutexLocker qml(&qmUdp);

#ifdef Q_OS_WIN
			if (hQoS) {
				if (!QOSRemoveSocketFromFlow(hQoS, 0, dwFlowUDP, 0)) {
					qWarning("ServerHandler: Failed to remove UDP from QoS. QOSRemoveSocketFromFlow() failed with "
							 "error %lu!",
							 GetLastError());
				}

				dwFlowUDP = 0;
			}
#endif
			delete qusUdp;
			qusUdp = nullptr;
		}

		ticker->stop();

		ConnectionPtr cptr(cConnection);
		if (cptr) {
			cptr->disconnectSocket(true);
		}

		cConnection.reset();
		while (cptr.use_count() > 1) {
			msleep(100);
		}
		delete qtsSock;
		delete tConnectionTimeoutTimer;
	} while (shouldTryNextTargetServer && !qlAddresses.isEmpty());
}

#ifdef Q_OS_WIN
extern DWORD WinVerifySslCert(const QByteArray &cert);
#endif

void ServerHandler::setSslErrors(const QList< QSslError > &errors) {
	ConnectionPtr connection(cConnection);
	if (!connection)
		return;

	qscCert                      = connection->peerCertificateChain();
	QList< QSslError > newErrors = errors;
	const QString actualDigest =
		qscCert.isEmpty() ? QString()
						  : QString::fromLatin1(qscCert.at(0).digest(QCryptographicHash::Sha1).toHex());
	const QString storedDigest = database->getDigest(qsHostName, usPort);
	appendServerHandlerTrace(QStringLiteral("setSslErrors host=%1 port=%2 certs=%3 stored_digest=%4 actual_digest=%5 "
											"errors=%6")
								 .arg(qsHostName, QString::number(usPort), QString::number(qscCert.size()), storedDigest,
									  actualDigest, sslErrorsSummary(errors)));

#ifdef Q_OS_WIN
	bool bRevalidate = false;
	QList< QSslError > errorsToRemove;
	for (const QSslError &e : errors) {
		switch (e.error()) {
			case QSslError::UnableToGetLocalIssuerCertificate:
			case QSslError::SelfSignedCertificateInChain:
				bRevalidate = true;
				errorsToRemove << e;
				break;
			default:
				break;
		}
	}

	if (bRevalidate) {
		QByteArray der    = qscCert.first().toDer();
		DWORD errorStatus = WinVerifySslCert(der);
		if (errorStatus == CERT_TRUST_NO_ERROR) {
			for (const QSslError &e : errorsToRemove) {
				newErrors.removeOne(e);
			}
		}
		if (newErrors.isEmpty()) {
			appendServerHandlerTrace(QStringLiteral("setSslErrors proceed-anyway reason=win-revalidate-cleared"));
			connection->proceedAnyway();
			return;
		}
	}
#endif

	bStrong = false;
	if ((qscCert.size() > 0) && (actualDigest == storedDigest)) {
		appendServerHandlerTrace(QStringLiteral("setSslErrors proceed-anyway reason=stored-digest-match"));
		connection->proceedAnyway();
	} else {
		appendServerHandlerTrace(QStringLiteral("setSslErrors store-errors remaining=%1")
									 .arg(sslErrorsSummary(newErrors)));
		qlErrors = newErrors;
	}
}

void ServerHandler::sendPing() {
	emit pingRequested();
}

void ServerHandler::sendPingInternal() {
	ConnectionPtr connection(cConnection);
	if (!connection)
		return;

	if (qtsSock->state() != QAbstractSocket::ConnectedState) {
		return;
	}

	// Ensure the TLS handshake has completed before sending pings.
	if (!qtsSock->isEncrypted()) {
		return;
	}

	if (Global::get().s.iMaxInFlightTCPPings > 0 && iInFlightTCPPings >= Global::get().s.iMaxInFlightTCPPings) {
		serverConnectionClosed(QAbstractSocket::UnknownSocketError, tr("Server is not responding to TCP pings"));
		return;
	}

	quint64 t = static_cast< quint64 >(tTimestamp.elapsed().count());

	if (qusUdp) {
		Mumble::Protocol::PingData pingData;
		pingData.timestamp                    = t;
		pingData.requestAdditionalInformation = false;

		m_udpPingEncoder.setProtocolVersion(m_version);
		std::span< const Mumble::Protocol::byte > encodedPacket = m_udpPingEncoder.encodePingPacket(pingData);

		sendMessage(encodedPacket.data(), static_cast< int >(encodedPacket.size()), true);
	}

	MumbleProto::Ping mpp;

	mpp.set_timestamp(t);
	mpp.set_good(connection->csCrypt->m_statsLocal.good);
	mpp.set_late(connection->csCrypt->m_statsLocal.late);
	mpp.set_lost(connection->csCrypt->m_statsLocal.lost);
	mpp.set_resync(connection->csCrypt->m_statsLocal.resync);


	if (boost::accumulators::count(accUDP)) {
		mpp.set_udp_ping_avg(static_cast< float >(boost::accumulators::mean(accUDP)));
		mpp.set_udp_ping_var(static_cast< float >(boost::accumulators::variance(accUDP)));
	}
	mpp.set_udp_packets(static_cast< unsigned int >(boost::accumulators::count(accUDP)));

	if (boost::accumulators::count(accTCP)) {
		mpp.set_tcp_ping_avg(static_cast< float >(boost::accumulators::mean(accTCP)));
		mpp.set_tcp_ping_var(static_cast< float >(boost::accumulators::variance(accTCP)));
	}
	mpp.set_tcp_packets(static_cast< unsigned int >(boost::accumulators::count(accTCP)));

	sendMessage(mpp);

	iInFlightTCPPings += 1;
}

void ServerHandler::message(Mumble::Protocol::TCPMessageType type, const QByteArray &qbaMsg) {
	const char *ptr = qbaMsg.constData();
	if (type == Mumble::Protocol::TCPMessageType::UDPTunnel) {
		// audio tunneled through tcp.
		// since it could happen that we are receiving udp and tcp messages at the same time (e.g. the server used to
		// send us packages via TCP but has now switched to UDP again and the first UDP packages arrive at the same time
		// as the last TCP ones), we want to use a dedicated decoder for this (to make sure there is no concurrent
		// access to the decoder's internal buffer).
		if (m_tcpTunnelDecoder.decode(
				{ reinterpret_cast< const Mumble::Protocol::byte * >(ptr), static_cast< std::size_t >(qbaMsg.size()) })
			&& m_tcpTunnelDecoder.getMessageType() == Mumble::Protocol::UDPMessageType::Audio) {
			handleVoicePacket(m_tcpTunnelDecoder.getAudioData());
		}
	} else if (type == Mumble::Protocol::TCPMessageType::Ping) {
		MumbleProto::Ping msg;
		if (msg.ParseFromArray(qbaMsg.constData(), static_cast< int >(qbaMsg.size()))) {
			ConnectionPtr connection(cConnection);
			if (!connection)
				return;

			// Reset in-flight TCP ping counter to 0.
			// We've received a ping. That means the
			// connection is still OK.
			iInFlightTCPPings = 0;

			connection->csCrypt->m_statsRemote.good   = msg.good();
			connection->csCrypt->m_statsRemote.late   = msg.late();
			connection->csCrypt->m_statsRemote.lost   = msg.lost();
			connection->csCrypt->m_statsRemote.resync = msg.resync();
			accTCP(static_cast< double >(static_cast< std::uint64_t >(tTimestamp.elapsed().count()) - msg.timestamp())
				   / 1000.0);

			if (((connection->csCrypt->m_statsRemote.good == 0) || (connection->csCrypt->m_statsLocal.good == 0))
				&& bUdp && (tTimestamp.elapsed() > std::chrono::seconds(20))) {
				bUdp = false;
				if (!NetworkConfig::TcpModeEnabled()) {
					if ((connection->csCrypt->m_statsRemote.good == 0) && (connection->csCrypt->m_statsLocal.good == 0))
						Global::get().mw->msgBox(
							tr("UDP packets cannot be sent to or received from the server. Switching to TCP mode."));
					else if (connection->csCrypt->m_statsRemote.good == 0)
						Global::get().mw->msgBox(
							tr("UDP packets cannot be sent to the server. Switching to TCP mode."));
					else
						Global::get().mw->msgBox(
							tr("UDP packets cannot be received from the server. Switching to TCP mode."));

					database->setUdp(qbaDigest, false);
				}
			} else if (!bUdp && (connection->csCrypt->m_statsRemote.good > 3)
					   && (connection->csCrypt->m_statsLocal.good > 3)) {
				bUdp = true;
				if (!NetworkConfig::TcpModeEnabled()) {
					Global::get().mw->msgBox(
						tr("UDP packets can be sent to and received from the server. Switching back to UDP mode."));

					database->setUdp(qbaDigest, true);
				}
			}
		}
	} else {
		ServerHandlerMessageEvent *shme = new ServerHandlerMessageEvent(qbaMsg, type, false);
		QApplication::postEvent(Global::get().mw, shme);
	}
}

void ServerHandler::disconnect() {
	// Change the state of this connection to "aborted", but use the thread of
	// the event loop.
	emit abortRequested();
}

void ServerHandler::serverConnectionClosed(QAbstractSocket::SocketError err, const QString &reason) {
	changeState(ServerHandlerState::ConnectionOver);

	Connection *c = cConnection.get();
	if (!c) {
		return;
	}

	if (c->bDisconnectedEmitted) {
		return;
	}

	c->bDisconnectedEmitted = true;

	AudioOutputPtr ao = Global::get().ao;
	if (ao)
		ao->wipe();

	// Try next server in the list if possible.
	// Otherwise, emit disconnect and exit with
	// a normal status code.
	if (!qlAddresses.isEmpty()) {
		if (err == QAbstractSocket::ConnectionRefusedError || err == QAbstractSocket::SocketTimeoutError) {
			qWarning("ServerHandler: connection attempt to %s:%i failed: %s (%li); trying next server....",
					 qPrintable(saTargetServer.host.toString()), static_cast< int >(saTargetServer.port),
					 qPrintable(reason), static_cast< long >(err));
			exit(-2);
			return;
		}
	}

	// Having 2 signals here that basically fire at the same time is wanted behavior!
	// See the documentation of "aboutToDisconnect" for an explanation.
	emit aboutToDisconnect(err, reason);
	emit disconnected(err, reason);

	exit(0);
}

void ServerHandler::serverConnectionTimeoutOnConnect() {
	ConnectionPtr connection(cConnection);
	if (connection) {
		connection->disconnectSocket(true);
	}

	serverConnectionClosed(QAbstractSocket::SocketTimeoutError, tr("Connection timed out"));
}

void ServerHandler::serverConnectionStateChanged(QAbstractSocket::SocketState state) {
	if (state == QAbstractSocket::ConnectingState) {
		// Start timer for connection timeout during connect after resolving is completed
		tConnectionTimeoutTimer = new QTimer();
		QObject::connect(tConnectionTimeoutTimer, &QTimer::timeout, this,
						 &ServerHandler::serverConnectionTimeoutOnConnect);
		tConnectionTimeoutTimer->setSingleShot(true);
		tConnectionTimeoutTimer->start(Global::get().s.iConnectionTimeoutDurationMsec);
	} else if (state == QAbstractSocket::ConnectedState) {
		// Start TLS handshake
		changeState(ServerHandlerState::TLSHandshake);
		qtsSock->startClientEncryption();
	}
}

void ServerHandler::serverConnectionConnected() {
	ConnectionPtr connection(cConnection);
	if (!connection) {
		return;
	}

	// The ephemeralServerKey property is only a non-null key, if forward secrecy is used.
	// See also https://doc.qt.io/qt-5/qsslconfiguration.html#ephemeralServerKey
	connectionUsesPerfectForwardSecrecy = !qtsSock->sslConfiguration().ephemeralServerKey().isNull();

	iInFlightTCPPings = 0;

	tConnectionTimeoutTimer->stop();

	if (Global::get().s.bQoS) {
		connection->setToS();
	}

	qscCert   = connection->peerCertificateChain();
	qscCipher = connection->sessionCipher();

	if (!qscCert.isEmpty()) {
		// Get the server's immediate SSL certificate
		const QSslCertificate &qsc = qscCert.first();
		qbaDigest                  = sha1(qsc.publicKey().toDer());
		bUdp                       = database->getUdp(qbaDigest);
		appendServerHandlerTrace(QStringLiteral("serverConnectionConnected host=%1 port=%2 cert_digest=%3 pubkey_digest=%4")
									 .arg(qsHostName, QString::number(usPort),
										  QString::fromLatin1(qsc.digest(QCryptographicHash::Sha1).toHex()),
										  QString::fromLatin1(qbaDigest.toHex())));
	} else {
		// Shouldn't reach this
		qCritical("Server must have a certificate. Dropping connection");
		disconnect();
		return;
	}

	changeState(ServerHandlerState::ConnectionEstablished);

	MumbleProto::Version mpv;
	const QString advertisedRelease = Global::get().s.qsAdvertisedReleaseOverride.trimmed().isEmpty()
										 ? Version::getRelease()
										 : Global::get().s.qsAdvertisedReleaseOverride.trimmed();
	mpv.set_release(u8(advertisedRelease));
	MumbleProto::setVersion(mpv, Version::get());
	mpv.set_supports_persistent_chat(true);
	ScreenShareHelperClient::applyAdvertisedCapabilities(mpv);

	const QString advertisedOS        = Global::get().s.qsAdvertisedOSOverride.trimmed();
	const QString advertisedOSVersion = Global::get().s.qsAdvertisedOSVersionOverride.trimmed();
	const bool overrideOSIdentity     = !advertisedOS.isEmpty() || !advertisedOSVersion.isEmpty();
	if (overrideOSIdentity || !Global::get().s.bHideOS) {
		mpv.set_os(u8(advertisedOS.isEmpty() ? OSInfo::getOS() : advertisedOS));
		mpv.set_os_version(u8(advertisedOSVersion.isEmpty() ? OSInfo::getOSDisplayableVersion() : advertisedOSVersion));
	}

	sendMessage(mpv);

	MumbleProto::Authenticate mpa;
	mpa.set_username(u8(qsUserName));
	mpa.set_password(u8(qsPassword));
	appendServerHandlerTrace(QStringLiteral("authenticate host=%1 port=%2 username=%3 username_utf8=%4 "
											"username_codepoints=%5 password_len=%6")
								 .arg(qsHostName, QString::number(usPort), qsUserName, utf8Hex(qsUserName),
									  codePointList(qsUserName), QString::number(qsPassword.size())));

	QStringList tokens = database->getTokens(qbaDigest);
	for (const QString &qs : tokens) {
		mpa.add_tokens(u8(qs));
	}

	mpa.set_opus(true);
	sendMessage(mpa);

	{
		QMutexLocker qml(&qmUdp);

		qhaRemote      = connection->peerAddress();
		qhaLocal       = connection->localAddress();
		usResolvedPort = connection->peerPort();
		if (qhaLocal.isNull()) {
			qFatal("ServerHandler: qhaLocal is unexpectedly a null addr");
		}

		qusUdp = new QUdpSocket(this);
		if (!qusUdp) {
			qFatal("ServerHandler: qusUdp is unexpectedly a null addr");
		}
		if (Global::get().s.bUdpForceTcpAddr) {
			qusUdp->bind(qhaLocal, 0);
		} else {
			if (qhaRemote.protocol() == QAbstractSocket::IPv6Protocol) {
				qusUdp->bind(QHostAddress(QHostAddress::AnyIPv6), 0);
			} else {
				qusUdp->bind(QHostAddress(QHostAddress::Any), 0);
			}
		}

		QObject::connect(qusUdp, &QUdpSocket::readyRead, this, &ServerHandler::udpReady);

		if (Global::get().s.bQoS) {
#if defined(Q_OS_UNIX)
			int val = 0xe0;
			if (setsockopt(static_cast< int >(qusUdp->socketDescriptor()), IPPROTO_IP, IP_TOS, &val, sizeof(val))) {
				val = 0x80;
				if (setsockopt(static_cast< int >(qusUdp->socketDescriptor()), IPPROTO_IP, IP_TOS, &val, sizeof(val)))
					qWarning("ServerHandler: Failed to set TOS for UDP Socket");
			}
#	if defined(SO_PRIORITY)
			socklen_t optlen = sizeof(val);
			if (getsockopt(static_cast< int >(qusUdp->socketDescriptor()), SOL_SOCKET, SO_PRIORITY, &val, &optlen)
				== 0) {
				if (val == 0) {
					val = 6;
					setsockopt(static_cast< int >(qusUdp->socketDescriptor()), SOL_SOCKET, SO_PRIORITY, &val,
							   sizeof(val));
				}
			}
#	endif
#elif defined(Q_OS_WIN)
			if (hQoS) {
				struct sockaddr_in addr;
				memset(&addr, 0, sizeof(addr));
				addr.sin_family      = AF_INET;
				addr.sin_port        = htons(usPort);
				addr.sin_addr.s_addr = htonl(qhaRemote.toIPv4Address());

				dwFlowUDP = 0;
				if (!QOSAddSocketToFlow(hQoS, qusUdp->socketDescriptor(), reinterpret_cast< sockaddr * >(&addr),
										QOSTrafficTypeVoice, QOS_NON_ADAPTIVE_FLOW,
										reinterpret_cast< PQOS_FLOWID >(&dwFlowUDP)))
					qWarning("ServerHandler: Failed to add UDP to QOS");
			}
#endif
		}
	}

	emit connected();
}

void ServerHandler::setConnectionInfo(const QString &host, unsigned short port, const QString &username,
									  const QString &pw) {
	qsHostName = host;
	usPort     = port;
	qsUserName = username;
	qsPassword = pw;
	appendServerHandlerTrace(QStringLiteral("setConnectionInfo host=%1 port=%2 username=%3 username_utf8=%4 "
											"username_codepoints=%5 password_len=%6")
								 .arg(qsHostName, QString::number(usPort), qsUserName, utf8Hex(qsUserName),
									  codePointList(qsUserName), QString::number(qsPassword.size())));
}

void ServerHandler::getConnectionInfo(QString &host, unsigned short &port, QString &username, QString &pw) const {
	host     = qsHostName;
	port     = usPort;
	username = qsUserName;
	pw       = qsPassword;
}

bool ServerHandler::isStrong() const {
	return bStrong;
}

void ServerHandler::requestUserStats(unsigned int uiSession, bool statsOnly) {
	MumbleProto::UserStats mpus;
	mpus.set_session(uiSession);
	mpus.set_stats_only(statsOnly);
	sendMessage(mpus);
}

void ServerHandler::joinChannel(unsigned int uiSession, unsigned int channel) {
	static const QStringList EMPTY;

	joinChannel(uiSession, channel, EMPTY);
}

void ServerHandler::joinChannel(unsigned int uiSession, unsigned int channel,
								const QStringList &temporaryAccessTokens) {
	MumbleProto::UserState mpus;
	mpus.set_session(uiSession);
	mpus.set_channel_id(channel);

	for (const QString &tmpToken : temporaryAccessTokens) {
		mpus.add_temporary_access_tokens(tmpToken.toUtf8().constData());
	}

	sendMessage(mpus);
}

void ServerHandler::startListeningToChannel(unsigned int channel) {
	startListeningToChannels({ channel });
}

void ServerHandler::startListeningToChannels(const QList< unsigned int > &channelIDs) {
	if (channelIDs.isEmpty()) {
		return;
	}

	MumbleProto::UserState mpus;
	mpus.set_session(Global::get().uiSession);

	for (unsigned int currentChannel : channelIDs) {
		// The naming of the function is a bit unfortunate but what this does is to add
		// the channel ID to the message field listening_channel_add
		mpus.add_listening_channel_add(currentChannel);
	}

	sendMessage(mpus);
}

void ServerHandler::stopListeningToChannel(unsigned int channel) {
	stopListeningToChannels({ channel });
}

void ServerHandler::stopListeningToChannels(const QList< unsigned int > &channelIDs) {
	if (channelIDs.isEmpty()) {
		return;
	}

	MumbleProto::UserState mpus;
	mpus.set_session(Global::get().uiSession);

	for (unsigned int currentChannel : channelIDs) {
		// The naming of the function is a bit unfortunate but what this does is to add
		// the channel ID to the message field listening_channel_remove
		mpus.add_listening_channel_remove(currentChannel);
	}

	sendMessage(mpus);
}

void ServerHandler::createChannel(unsigned int parent_id, const QString &name, const QString &description,
								  unsigned int position, bool temporary, unsigned int maxUsers) {
	MumbleProto::ChannelState mpcs;
	mpcs.set_parent(parent_id);
	mpcs.set_name(u8(name));
	mpcs.set_description(u8(description));
	mpcs.set_position(static_cast< int >(position));
	mpcs.set_temporary(temporary);
	mpcs.set_max_users(maxUsers);
	sendMessage(mpcs);
}

void ServerHandler::requestBanList() {
	MumbleProto::BanList mpbl;
	mpbl.set_query(true);
	sendMessage(mpbl);
}

void ServerHandler::requestUserList() {
	MumbleProto::UserList mpul;
	sendMessage(mpul);
}

void ServerHandler::requestACL(unsigned int channel) {
	MumbleProto::ACL mpacl;
	mpacl.set_channel_id(channel);
	mpacl.set_query(true);
	sendMessage(mpacl);
}

void ServerHandler::registerUser(unsigned int uiSession) {
	MumbleProto::UserState mpus;
	mpus.set_session(uiSession);
	mpus.set_user_id(0);
	sendMessage(mpus);
}

void ServerHandler::kickUser(unsigned int uiSession, const QString &reason) {
	MumbleProto::UserRemove mpur;
	mpur.set_session(uiSession);
	mpur.set_reason(u8(reason));
	mpur.set_ban(false);
	sendMessage(mpur);
}

void ServerHandler::banUser(unsigned int uiSession, const QString &reason, bool banCertificate, bool banIP) {
	MumbleProto::UserRemove mpur;
	mpur.set_session(uiSession);
	mpur.set_reason(u8(reason));
	mpur.set_ban(true);
	mpur.set_ban_certificate(banCertificate);
	mpur.set_ban_ip(banIP);
	sendMessage(mpur);
}

void ServerHandler::sendUserTextMessage(unsigned int uiSession, const QString &message_) {
	MumbleProto::TextMessage mptm;
	mptm.add_session(uiSession);
	mptm.set_message(u8(message_));
	sendMessage(mptm);
}

void ServerHandler::sendChannelTextMessage(unsigned int channel, const QString &message_, bool tree) {
	MumbleProto::TextMessage mptm;
	if (tree) {
		mptm.add_tree_id(channel);
	} else {
		mptm.add_channel_id(channel);

		if (message_ == QString::fromUtf8(Global::get().ccHappyEaster + 10))
			Global::get().bHappyEaster = true;
	}
	mptm.set_message(u8(message_));
	sendMessage(mptm);
}

void ServerHandler::sendChatMessage(MumbleProto::ChatScope scope, unsigned int scopeID, const QString &message_,
									MumbleProto::ChatBodyFormat bodyFormat,
									std::optional< unsigned int > replyToMessageID) {
	MumbleProto::ChatSend message;
	message.set_scope(scope);
	message.set_scope_id(scopeID);
	message.set_message(u8(message_));
	message.set_body_text(u8(message_));
	message.set_body_format(bodyFormat);
	if (replyToMessageID) {
		message.set_reply_to_message_id(replyToMessageID.value());
	}
	sendMessage(message);
}

void ServerHandler::sendChatReactionToggle(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int threadID,
										   unsigned int messageID, const QString &emoji, bool active) {
	if (messageID == 0 || emoji.trimmed().isEmpty()) {
		return;
	}

	MumbleProto::ChatReactionToggle toggle;
	toggle.set_scope(scope);
	toggle.set_scope_id(scopeID);
	if (threadID > 0) {
		toggle.set_thread_id(threadID);
	}
	toggle.set_message_id(messageID);
	toggle.set_emoji(u8(emoji.trimmed()));
	toggle.set_active(active);
	sendMessage(toggle);
}

void ServerHandler::upsertTextChannel(unsigned int textChannelID, const QString &name, const QString &description,
									  unsigned int aclChannelID, unsigned int position, bool create) {
	MumbleProto::TextChannelSync sync;
	sync.set_action(create ? MumbleProto::TextChannelSync_Action_Create : MumbleProto::TextChannelSync_Action_Update);
	sync.set_target_text_channel_id(textChannelID);

	MumbleProto::TextChannelInfo *channel = sync.add_channels();
	channel->set_text_channel_id(textChannelID);
	channel->set_name(u8(name));
	channel->set_description(u8(description));
	channel->set_acl_channel_id(aclChannelID);
	channel->set_position(position);

	sendMessage(sync);
}

void ServerHandler::removeTextChannel(unsigned int textChannelID) {
	MumbleProto::TextChannelSync sync;
	sync.set_action(MumbleProto::TextChannelSync_Action_Delete);
	sync.set_target_text_channel_id(textChannelID);
	sendMessage(sync);
}

void ServerHandler::setDefaultTextChannel(unsigned int textChannelID) {
	MumbleProto::TextChannelSync sync;
	sync.set_action(MumbleProto::TextChannelSync_Action_SetDefault);
	sync.set_target_text_channel_id(textChannelID);
	sendMessage(sync);
}

void ServerHandler::requestChatHistory(MumbleProto::ChatScope scope, unsigned int scopeID, unsigned int startOffset,
									   unsigned int limit, std::optional< unsigned int > beforeMessageID) {
	MumbleProto::ChatHistoryRequest request;
	request.set_scope(scope);
	request.set_scope_id(scopeID);
	request.set_start_offset(startOffset);
	request.set_limit(limit);
	if (beforeMessageID) {
		request.set_before_message_id(beforeMessageID.value());
	}
	sendMessage(request);
}

void ServerHandler::updateChatReadState(MumbleProto::ChatScope scope, unsigned int scopeID,
										unsigned int lastReadMessageID) {
	MumbleProto::ChatReadStateUpdate update;
	update.set_scope(scope);
	update.set_scope_id(scopeID);
	update.set_last_read_message_id(lastReadMessageID);
	sendMessage(update);
}

void ServerHandler::setUserComment(unsigned int uiSession, const QString &comment) {
	MumbleProto::UserState mpus;
	mpus.set_session(uiSession);
	mpus.set_comment(u8(comment));
	sendMessage(mpus);
}

void ServerHandler::setUserTexture(unsigned int uiSession, const QByteArray &qba) {
	QByteArray texture;

	if ((m_version >= Version::fromComponents(1, 2, 2)) || qba.isEmpty()) {
		texture = qba;
	} else {
		QByteArray raw = qba;

		QBuffer qb(&raw);
		qb.open(QIODevice::ReadOnly);

		QImageReader qir;
		qir.setDecideFormatFromContent(false);

		QByteArray fmt;
		if (!RichTextImage::isValidImage(qba, fmt)) {
			return;
		}

		qir.setFormat(fmt);
		qir.setDevice(&qb);

		QSize sz                 = qir.size();
		const int TEX_MAX_WIDTH  = 600;
		const int TEX_MAX_HEIGHT = 60;
		const int TEX_RGBA_SIZE  = TEX_MAX_WIDTH * TEX_MAX_HEIGHT * 4;
		sz.scale(TEX_MAX_WIDTH, TEX_MAX_HEIGHT, Qt::KeepAspectRatio);
		qir.setScaledSize(sz);

		QImage tex = qir.read();
		if (tex.isNull()) {
			return;
		}

		raw = QByteArray(TEX_RGBA_SIZE, 0);
		QImage img(reinterpret_cast< unsigned char * >(raw.data()), TEX_MAX_WIDTH, TEX_MAX_HEIGHT,
				   QImage::Format_ARGB32);

		QPainter imgp(&img);
		imgp.setRenderHint(QPainter::Antialiasing);
		imgp.setRenderHint(QPainter::TextAntialiasing);
		imgp.setCompositionMode(QPainter::CompositionMode_SourceOver);
		imgp.drawImage(0, 0, tex);

		texture = qCompress(QByteArray(reinterpret_cast< const char * >(img.bits()), TEX_RGBA_SIZE));
	}

	MumbleProto::UserState mpus;
	mpus.set_session(uiSession);
	mpus.set_texture(blob(texture));
	sendMessage(mpus);

	if (!texture.isEmpty()) {
		database->setBlob(sha1(texture), texture);
	}
}

void ServerHandler::setTokens(const QStringList &tokens) {
	MumbleProto::Authenticate msg;
	for (const QString &qs : tokens) {
		msg.add_tokens(u8(qs));
	}
	sendMessage(msg);
}

void ServerHandler::removeChannel(unsigned int channel) {
	MumbleProto::ChannelRemove mpcr;
	mpcr.set_channel_id(channel);
	sendMessage(mpcr);
}

void ServerHandler::addChannelLink(unsigned int channel, unsigned int link) {
	MumbleProto::ChannelState mpcs;
	mpcs.set_channel_id(channel);
	mpcs.add_links_add(link);
	sendMessage(mpcs);
}

void ServerHandler::removeChannelLink(unsigned int channel, unsigned int link) {
	MumbleProto::ChannelState mpcs;
	mpcs.set_channel_id(channel);
	mpcs.add_links_remove(link);
	sendMessage(mpcs);
}

void ServerHandler::requestChannelPermissions(unsigned int channel) {
	MumbleProto::PermissionQuery mppq;
	mppq.set_channel_id(channel);
	sendMessage(mppq);
}

void ServerHandler::setSelfMuteDeafState(bool mute, bool deaf) {
	MumbleProto::UserState mpus;
	mpus.set_self_mute(mute);
	mpus.set_self_deaf(deaf);
	sendMessage(mpus);
}

void ServerHandler::announceRecordingState(bool recording) {
	MumbleProto::UserState mpus;
	mpus.set_recording(recording);
	sendMessage(mpus);
}

QUrl ServerHandler::getServerURL(bool withPassword) const {
	QUrl url;

	url.setScheme(QLatin1String("mumble"));
	url.setHost(qsHostName);
	if (usPort != DEFAULT_MUMBLE_PORT) {
		url.setPort(usPort);
	}

	url.setUserName(qsUserName);

	if (withPassword && !qsPassword.isEmpty()) {
		url.setPassword(qsPassword);
	}

	return url;
}
