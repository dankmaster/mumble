// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PersistentChatHistoryModel.h"

namespace {
	QString messageGroupSignature(const PersistentChatHistoryRow &row) {
		if (!row.messageGroup) {
			return row.signature;
		}

		const PersistentChatMessageGroupRowSpec &group = *row.messageGroup;
		QStringList groupSignatureParts { row.rowId, group.header.actorLabel, group.header.timeLabel,
										 group.header.scopeLabel,
										 QString::number(static_cast< int >(group.header.displayMode)) };
		for (const PersistentChatBubbleSpec &bubble : group.bubbles) {
			groupSignatureParts << QString::number(bubble.messageID)
								<< QString::number(static_cast< int >(bubble.displayMode)) << bubble.bodyHtml
								<< bubble.transcriptActorLabel << bubble.transcriptTimeLabel << bubble.replyActor
								<< bubble.replySnippet << bubble.actionText
								<< QString::number(static_cast< int >(bubble.previewSpec.kind));
		}

		return QString::number(qHash(groupSignatureParts.join(QLatin1Char('|'))));
	}

	void emitChangedSignatureRanges(PersistentChatHistoryModel *model, const QVector< PersistentChatHistoryRow > &oldRows,
									const QVector< PersistentChatHistoryRow > &newRows, int oldOffset, int newOffset,
									int length) {
		if (!model || length <= 0) {
			return;
		}

		int changedRangeStart = -1;
		for (int i = 0; i < length; ++i) {
			const bool changed = oldRows.at(oldOffset + i).signature != newRows.at(newOffset + i).signature;
			if (changed) {
				if (changedRangeStart < 0) {
					changedRangeStart = i;
				}
				continue;
			}

			if (changedRangeStart >= 0) {
				emit model->dataChanged(model->index(newOffset + changedRangeStart, 0),
										model->index(newOffset + i - 1, 0));
				changedRangeStart = -1;
			}
		}

		if (changedRangeStart >= 0) {
			emit model->dataChanged(model->index(newOffset + changedRangeStart, 0),
									model->index(newOffset + length - 1, 0));
		}
	}
}

PersistentChatHistoryModel::PersistentChatHistoryModel(QObject *parent) : QAbstractListModel(parent) {
}

int PersistentChatHistoryModel::rowCount(const QModelIndex &parent) const {
	return parent.isValid() ? 0 : m_rows.size();
}

QVariant PersistentChatHistoryModel::data(const QModelIndex &index, int role) const {
	const PersistentChatHistoryRow *row = rowAt(index.row());
	if (!row) {
		return QVariant();
	}

	switch (role) {
		case RowIdRole:
			return row->rowId;
		case RowKindRole:
			return static_cast< int >(row->kind);
		default:
			break;
	}

	return QVariant();
}

const PersistentChatHistoryRow *PersistentChatHistoryModel::rowAt(int row) const {
	return row >= 0 && row < m_rows.size() ? &m_rows.at(row) : nullptr;
}

QString PersistentChatHistoryModel::rowIdAt(int row) const {
	const PersistentChatHistoryRow *rowData = rowAt(row);
	return rowData ? rowData->rowId : QString();
}

int PersistentChatHistoryModel::rowForId(const QString &rowId) const {
	for (int i = 0; i < m_rows.size(); ++i) {
		if (m_rows.at(i).rowId == rowId) {
			return i;
		}
	}

	return -1;
}

QString PersistentChatHistoryModel::lastMessageGroupRowId() const {
	for (int i = m_rows.size() - 1; i >= 0; --i) {
		if (m_rows.at(i).kind == PersistentChatHistoryRowKind::MessageGroup) {
			return m_rows.at(i).rowId;
		}
	}

	return QString();
}

void PersistentChatHistoryModel::setRows(const QVector< PersistentChatHistoryRow > &rows) {
	if (m_rows.isEmpty()) {
		beginResetModel();
		m_rows = rows;
		endResetModel();
		return;
	}

	if (rows.isEmpty()) {
		beginResetModel();
		m_rows.clear();
		endResetModel();
		return;
	}

	if (sameRowIds(m_rows, rows)) {
		const QVector< PersistentChatHistoryRow > oldRows = m_rows;
		m_rows = rows;
		emitChangedSignatureRanges(this, oldRows, m_rows, 0, 0, m_rows.size());
		return;
	}

	if (rows.size() > m_rows.size()
		&& sameRowIds(m_rows, rows, 0, 0, m_rows.size())) {
		const QVector< PersistentChatHistoryRow > oldRows = m_rows;
		const int firstInsertedRow = oldRows.size();
		const int lastInsertedRow  = rows.size() - 1;
		beginInsertRows(QModelIndex(), firstInsertedRow, lastInsertedRow);
		m_rows = rows;
		endInsertRows();
		emitChangedSignatureRanges(this, oldRows, m_rows, 0, 0, oldRows.size());
		return;
	}

	if (rows.size() > m_rows.size()
		&& sameRowIds(m_rows, rows, 0, rows.size() - m_rows.size(), m_rows.size())) {
		const QVector< PersistentChatHistoryRow > oldRows = m_rows;
		const int insertedCount = rows.size() - oldRows.size();
		beginInsertRows(QModelIndex(), 0, insertedCount - 1);
		m_rows = rows;
		endInsertRows();
		emitChangedSignatureRanges(this, oldRows, m_rows, 0, insertedCount, oldRows.size());
		return;
	}

	if (rows.size() < m_rows.size()
		&& sameRowIds(rows, m_rows, 0, 0, rows.size())) {
		const QVector< PersistentChatHistoryRow > oldRows = m_rows;
		const int firstRemovedRow = rows.size();
		const int lastRemovedRow  = oldRows.size() - 1;
		beginRemoveRows(QModelIndex(), firstRemovedRow, lastRemovedRow);
		m_rows = rows;
		endRemoveRows();
		emitChangedSignatureRanges(this, oldRows, m_rows, 0, 0, m_rows.size());
		return;
	}

	if (rows.size() < m_rows.size()
		&& sameRowIds(rows, m_rows, 0, m_rows.size() - rows.size(), rows.size())) {
		const QVector< PersistentChatHistoryRow > oldRows = m_rows;
		const int removedCount = oldRows.size() - rows.size();
		beginRemoveRows(QModelIndex(), 0, removedCount - 1);
		m_rows = rows;
		endRemoveRows();
		emitChangedSignatureRanges(this, oldRows, m_rows, removedCount, 0, m_rows.size());
		return;
	}

	beginResetModel();
	m_rows = rows;
	endResetModel();
}

bool PersistentChatHistoryModel::removeUnreadDivider() {
	for (int rowIndex = 0; rowIndex < m_rows.size(); ++rowIndex) {
		if (m_rows.at(rowIndex).kind != PersistentChatHistoryRowKind::UnreadDivider) {
			continue;
		}

		beginRemoveRows(QModelIndex(), rowIndex, rowIndex);
		m_rows.removeAt(rowIndex);
		endRemoveRows();
		return true;
	}

	return false;
}

bool PersistentChatHistoryModel::updateBubblePreview(unsigned int messageID, unsigned int threadID,
													 const PersistentChatPreviewSpec &previewSpec) {
	for (int rowIndex = 0; rowIndex < m_rows.size(); ++rowIndex) {
		PersistentChatHistoryRow &row = m_rows[rowIndex];
		if (row.kind != PersistentChatHistoryRowKind::MessageGroup || !row.messageGroup) {
			continue;
		}

		bool updated = false;
		for (PersistentChatBubbleSpec &bubble : row.messageGroup->bubbles) {
			if (bubble.messageID != messageID || bubble.threadID != threadID) {
				continue;
			}

			bubble.previewSpec = previewSpec;
			updated = true;
			break;
		}

		if (!updated) {
			continue;
		}

		row.signature = messageGroupSignature(row);
		emit dataChanged(index(rowIndex, 0), index(rowIndex, 0));
		return true;
	}

	return false;
}

bool PersistentChatHistoryModel::sameRowIds(const QVector< PersistentChatHistoryRow > &lhs,
											const QVector< PersistentChatHistoryRow > &rhs,
											int lhsOffset, int rhsOffset, int length) {
	if (lhsOffset < 0 || rhsOffset < 0) {
		return false;
	}

	const int resolvedLength = length >= 0 ? length : std::min(lhs.size() - lhsOffset, rhs.size() - rhsOffset);
	if ((lhsOffset + resolvedLength) > lhs.size() || (rhsOffset + resolvedLength) > rhs.size()) {
		return false;
	}

	for (int i = 0; i < resolvedLength; ++i) {
		if (lhs.at(lhsOffset + i).rowId != rhs.at(rhsOffset + i).rowId) {
			return false;
		}
	}

	return true;
}
