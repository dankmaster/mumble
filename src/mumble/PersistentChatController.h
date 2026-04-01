// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PERSISTENTCHATCONTROLLER_H_
#define MUMBLE_MUMBLE_PERSISTENTCHATCONTROLLER_H_

#include "PersistentChatGateway.h"
#include "PersistentChatStore.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>

class PersistentChatController : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PersistentChatController)

public:
	explicit PersistentChatController(QObject *parent = nullptr);

	void setGateway(PersistentChatGateway *gateway);
	void reset();

	void setActiveScope(const PersistentChatScopeKey &key, bool forceReload);
	void clearActiveScope();

	PersistentChatScopeKey activeScope() const;
	bool hasActiveScope() const;
	bool activeScopeMatches(MumbleProto::ChatScope scope, unsigned int scopeID) const;
	PersistentChatScopeStateSnapshot activeSnapshot() const;
	int unreadCount(MumbleProto::ChatScope scope, unsigned int scopeID) const;
	int totalUnreadCount() const;

	bool requestOlderForActiveScope();
	bool markActiveScopeRead();
	bool applyEmbedState(const MumbleProto::ChatEmbedState &state);

signals:
	void activeSnapshotChanged();
	void unreadStateChanged();

private slots:
	void handleHistoryResponse(const MumbleProto::ChatHistoryResponse &response);
	void handleMessage(const MumbleProto::ChatMessage &message);
	void handleReadState(const MumbleProto::ChatReadStateUpdate &update);

private:
	void setUnreadFromMessages(PersistentChatStore::ScopeState &state);
	void startInitialLoad(PersistentChatStore::ScopeState &state, bool forceReload);

	QPointer< PersistentChatGateway > m_gateway;
	PersistentChatStore m_store;
	PersistentChatScopeKey m_activeScope;
	quint64 m_connectionGeneration = 0;
};

#endif // MUMBLE_MUMBLE_PERSISTENTCHATCONTROLLER_H_
