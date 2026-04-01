// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include "PersistentChatHistoryModel.h"

namespace {
	PersistentChatHistoryRow makeStateRow(const QString &rowId, const QString &signature) {
		PersistentChatHistoryRow row;
		row.kind      = PersistentChatHistoryRowKind::State;
		row.rowId     = rowId;
		row.signature = signature;
		row.state     = PersistentChatStateRowSpec { QStringLiteral("Text"), rowId, signature, {}, 220 };
		return row;
	}
}

class TestPersistentChatHistoryModel : public QObject {
	Q_OBJECT

private slots:
	void updatesChangedRowsWithoutReset();
	void prependsRowsWithoutReset();
	void removesLeadingRowsWithoutReset();
};

void TestPersistentChatHistoryModel::updatesChangedRowsWithoutReset() {
	PersistentChatHistoryModel model;
	model.setRows({ makeStateRow(QStringLiteral("a"), QStringLiteral("sig-a")),
					makeStateRow(QStringLiteral("b"), QStringLiteral("sig-b")),
					makeStateRow(QStringLiteral("c"), QStringLiteral("sig-c")) });

	QSignalSpy dataChangedSpy(&model, &QAbstractItemModel::dataChanged);
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

	model.setRows({ makeStateRow(QStringLiteral("a"), QStringLiteral("sig-a")),
					makeStateRow(QStringLiteral("b"), QStringLiteral("sig-b-2")),
					makeStateRow(QStringLiteral("c"), QStringLiteral("sig-c")) });

	QCOMPARE(resetSpy.count(), 0);
	QCOMPARE(dataChangedSpy.count(), 1);

	const QList< QVariant > args = dataChangedSpy.takeFirst();
	QCOMPARE(args.at(0).value< QModelIndex >().row(), 1);
	QCOMPARE(args.at(1).value< QModelIndex >().row(), 1);
}

void TestPersistentChatHistoryModel::prependsRowsWithoutReset() {
	PersistentChatHistoryModel model;
	model.setRows({ makeStateRow(QStringLiteral("a"), QStringLiteral("sig-a")),
					makeStateRow(QStringLiteral("b"), QStringLiteral("sig-b")) });

	QSignalSpy rowsInsertedSpy(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

	model.setRows({ makeStateRow(QStringLiteral("load"), QStringLiteral("sig-load")),
					makeStateRow(QStringLiteral("a"), QStringLiteral("sig-a")),
					makeStateRow(QStringLiteral("b"), QStringLiteral("sig-b")) });

	QCOMPARE(resetSpy.count(), 0);
	QCOMPARE(rowsInsertedSpy.count(), 1);

	const QList< QVariant > args = rowsInsertedSpy.takeFirst();
	QCOMPARE(args.at(1).toInt(), 0);
	QCOMPARE(args.at(2).toInt(), 0);
}

void TestPersistentChatHistoryModel::removesLeadingRowsWithoutReset() {
	PersistentChatHistoryModel model;
	model.setRows({ makeStateRow(QStringLiteral("load"), QStringLiteral("sig-load")),
					makeStateRow(QStringLiteral("a"), QStringLiteral("sig-a")),
					makeStateRow(QStringLiteral("b"), QStringLiteral("sig-b")) });

	QSignalSpy rowsRemovedSpy(&model, &QAbstractItemModel::rowsRemoved);
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

	model.setRows({ makeStateRow(QStringLiteral("a"), QStringLiteral("sig-a")),
					makeStateRow(QStringLiteral("b"), QStringLiteral("sig-b")) });

	QCOMPARE(resetSpy.count(), 0);
	QCOMPARE(rowsRemovedSpy.count(), 1);

	const QList< QVariant > args = rowsRemovedSpy.takeFirst();
	QCOMPARE(args.at(1).toInt(), 0);
	QCOMPARE(args.at(2).toInt(), 0);
}

QTEST_MAIN(TestPersistentChatHistoryModel)
#include "TestPersistentChatHistoryModel.moc"
