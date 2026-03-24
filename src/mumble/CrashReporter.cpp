// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "CrashReporter.h"

#include "OSInfo.h"
#include "Global.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtNetwork/QHostAddress>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QApplication>

CrashReporter::CrashReporter(QWidget *p) : QDialog(p) {
}

CrashReporter::~CrashReporter() {
}

void CrashReporter::run() {
	QByteArray qbaDumpContents;
	QFile qfCrashDump(Global::get().qdBasePath.filePath(QLatin1String("mumble.dmp")));
	if (!qfCrashDump.exists())
		return;

	if (!qfCrashDump.open(QIODevice::ReadOnly)) {
		qWarning("CrashReporter: Failed to open crash dump file: %s", qUtf8Printable(qfCrashDump.errorString()));
		return;
	}

#if defined(Q_OS_WIN)
	/* On Windows, the .dmp file is a real minidump. */

	if (qfCrashDump.peek(4) != "MDMP")
		return;
	qbaDumpContents = qfCrashDump.readAll();

#elif defined(Q_OS_MAC)
	/*
	 * On OSX, the .dmp file is simply a dummy file that we
	 * use to find the *real* crash dump, made by the OSX
	 * built in crash reporter.
	 */
	QFileInfo qfiDump(qfCrashDump);
	QDateTime qdtModification = qfiDump.lastModified();

	/* Find the real crash report. */
	QDir qdCrashReports(QDir::home().absolutePath() + QLatin1String("/Library/Logs/DiagnosticReports/"));
	if (!qdCrashReports.exists()) {
		qdCrashReports.setPath(QDir::home().absolutePath() + QLatin1String("/Library/Logs/CrashReporter/"));
	}

	QStringList qslFilters;
	qslFilters << QString::fromLatin1("Mumble_*.crash");
	qdCrashReports.setNameFilters(qslFilters);
	qdCrashReports.setSorting(QDir::Time);
	QFileInfoList qfilEntries = qdCrashReports.entryInfoList();

	/*
	 * Figure out if our delta is sufficiently close to the Apple crash dump, or
	 * if something weird happened.
	 */
	for (const QFileInfo &fi : qfilEntries) {
		qint64 delta = qAbs< qint64 >(qdtModification.secsTo(fi.lastModified()));
		if (delta < 8) {
			QFile f(fi.absoluteFilePath());
			if (!f.open(QIODevice::ReadOnly)) {
				qWarning("CrashReporter: Failed to open crash report file: %s", qUtf8Printable(f.errorString()));
				continue;
			}
			qbaDumpContents = f.readAll();
			break;
		}
	}
#endif

	QString details;
#ifdef Q_OS_WIN
	details = QLatin1String("Windows minidump archived locally. No crash data was uploaded.");
#endif

	if (qbaDumpContents.isEmpty()) {
		qWarning("CrashReporter: Empty crash dump file, not reporting.");
		return;
	}

	const QString timestamp = QDateTime::currentDateTime().toString(QLatin1String("yyyyMMdd-HHmmss"));
	QDir crashRoot(Global::get().qdBasePath.filePath(QLatin1String("crash-reports")));
	if (!crashRoot.mkpath(QLatin1String("."))) {
		qWarning("CrashReporter: Unable to create crash report directory");
		return;
	}

	const QString archiveDirPath = crashRoot.filePath(timestamp);
	if (!crashRoot.mkpath(timestamp)) {
		qWarning("CrashReporter: Unable to create crash report archive");
		return;
	}

	QFile archivedDump(QDir(archiveDirPath).filePath(QLatin1String("mumble.dmp")));
	if (!archivedDump.open(QIODevice::WriteOnly)) {
		qWarning("CrashReporter: Unable to write archived dump");
		return;
	}
	archivedDump.write(qbaDumpContents);
	archivedDump.close();

	QFile metadata(QDir(archiveDirPath).filePath(QLatin1String("metadata.txt")));
	if (metadata.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QStringList lines;
		lines << QString::fromLatin1("timestamp=%1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
		lines << QString::fromLatin1("version=%1").arg(Version::getRelease());
		lines << QString::fromLatin1("os=%1 %2").arg(OSInfo::getOS(), OSInfo::getOSVersion());
		lines << QString::fromLatin1("app=%1").arg(QFileInfo(qApp->applicationFilePath()).fileName());
		if (!details.isEmpty()) {
			lines << QString();
			lines << details;
		}
		metadata.write(lines.join(QLatin1Char('\n')).toUtf8());
		metadata.close();
	}

	if (!qfCrashDump.remove())
		qWarning("CrashReporeter: Unable to remove crash file.");

	QMessageBox::information(nullptr, tr("Crash archived"),
							 tr("Crash data was written locally to:\n%1\n\nNo crash data was uploaded.")
								 .arg(QDir::toNativeSeparators(archiveDirPath)));
}
