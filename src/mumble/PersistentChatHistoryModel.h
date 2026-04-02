// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATHISTORYMODEL_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATHISTORYMODEL_H_

#include "widgets/PersistentChatMessageGroupWidget.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QVector>

#include <optional>

enum class PersistentChatHistoryRowKind {
	State,
	LoadOlder,
	DateDivider,
	UnreadDivider,
	MessageGroup
};

struct PersistentChatStateRowSpec {
	QString eyebrow;
	QString title;
	QString body;
	QStringList hints;
	int minimumHeight = 220;
};

struct PersistentChatLoadOlderRowSpec {
	QString text;
	bool loading = false;
	bool enabled = true;
};

struct PersistentChatTextRowSpec {
	QString text;
};

struct PersistentChatMessageGroupRowSpec {
	PersistentChatGroupHeaderSpec header;
	QString avatarFallbackText;
	QVector< PersistentChatBubbleSpec > bubbles;
	unsigned int firstMessageID = 0;
	unsigned int firstThreadID  = 0;
};

struct PersistentChatHistoryRow {
	PersistentChatHistoryRowKind kind = PersistentChatHistoryRowKind::State;
	QString rowId;
	QString signature;
	std::optional< PersistentChatStateRowSpec > state;
	std::optional< PersistentChatLoadOlderRowSpec > loadOlder;
	std::optional< PersistentChatTextRowSpec > text;
	std::optional< PersistentChatMessageGroupRowSpec > messageGroup;
};

class PersistentChatHistoryModel : public QAbstractListModel {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PersistentChatHistoryModel)

public:
	enum Roles {
		RowIdRole = Qt::UserRole + 1,
		RowKindRole
	};

	explicit PersistentChatHistoryModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

	const PersistentChatHistoryRow *rowAt(int row) const;
	QString rowIdAt(int row) const;
	int rowForId(const QString &rowId) const;
	QString lastMessageGroupRowId() const;
	void setRows(const QVector< PersistentChatHistoryRow > &rows);
	bool removeUnreadDivider();
	bool updateBubblePreview(unsigned int messageID, unsigned int threadID, const PersistentChatPreviewSpec &previewSpec);

private:
	static bool sameRowIds(const QVector< PersistentChatHistoryRow > &lhs,
						   const QVector< PersistentChatHistoryRow > &rhs,
						   int lhsOffset = 0, int rhsOffset = 0, int length = -1);

	QVector< PersistentChatHistoryRow > m_rows;
};

#endif // MUMBLE_MUMBLE_PERSISTENTCHATHISTORYMODEL_H_
