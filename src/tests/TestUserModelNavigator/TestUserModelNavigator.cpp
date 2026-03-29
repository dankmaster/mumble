// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore/QDir>
#include <QtTest>

#include "Channel.h"
#include "ClientUser.h"
#include "Global.h"
#include "MumbleConstants.h"
#include "UserModel.h"

class TestableUserModel : public UserModel {
public:
	using UserModel::UserModel;

	void markLinkedChannel(Channel *channel) { qsLinked.insert(channel); }
};

namespace {
	void ensureGlobalState() {
		if (!Global::g_global_struct) {
			const QString configPath =
				QDir::tempPath() + QLatin1String("/mumble-user-model-navigator-test/settings.json");
			Global::g_global_struct = new Global(configPath);
		}

		Global::get().s.bShowUserCount = true;
		Global::get().s.bUserTop       = false;
		Global::get().s.ceExpand       = Settings::NoChannels;

		if (!Channel::get(Mumble::ROOT_CHANNEL_ID)) {
			Channel::add(Mumble::ROOT_CHANNEL_ID, QStringLiteral("Root"));
		}
	}

	Channel *attachChannel(unsigned int id, const QString &name, Channel *parentChannel) {
		Channel *channel = new Channel(id, name, parentChannel);
		Channel::c_qhChannels.insert(id, channel);

		ModelItem *item = new ModelItem(channel);
		ModelItem *parentItem = ModelItem::c_qhChannels.value(parentChannel);
		parentItem->qlChildren << item;
		return channel;
	}

	ClientUser *attachUser(UserModel &model, unsigned int session, const QString &name, Channel *channel) {
		ClientUser *user = ClientUser::add(session, &model);
		user->qsName     = name;
		channel->addUser(user);

		ModelItem *item = new ModelItem(user);
		ModelItem *parentItem = ModelItem::c_qhChannels.value(channel);
		item->parent = parentItem;
		parentItem->qlChildren << item;
		for (ModelItem *current = parentItem; current; current = current->parent) {
			current->iUsers++;
		}

		return user;
	}

	QModelIndex attachListener(UserModel &model, ClientUser *user, Channel *channel) {
		ModelItem *item = new ModelItem(user, true);
		ModelItem *parentItem = ModelItem::c_qhChannels.value(channel);
		item->parent = parentItem;
		parentItem->qlChildren << item;
		for (ModelItem *current = parentItem; current; current = current->parent) {
			current->iUsers++;
		}

		return model.index(item);
	}
} // namespace

class TestUserModelNavigator : public QObject {
	Q_OBJECT

private slots:
	void exposesChannelKindOccupancyAndCurrentLocation();
	void exposesUserFallbackAndTalkState();
	void exposesLinkedChannelsAndListenerKind();
};

void TestUserModelNavigator::exposesChannelKindOccupancyAndCurrentLocation() {
	ensureGlobalState();

	auto *model = new TestableUserModel();
	Channel *root = Channel::get(Mumble::ROOT_CHANNEL_ID);
	Channel *landing = attachChannel(1001, QStringLiteral("Landing"), root);
	ClientUser *self = attachUser(*model, 2001, QStringLiteral("Self User"), landing);
	attachUser(*model, 2002, QStringLiteral("Friend Person"), landing);
	Global::get().uiSession = self->uiSession;

	const QModelIndex channelIndex = model->index(landing);

	QCOMPARE(model->data(channelIndex, UserModel::NavigatorItemKindRole).toInt(),
			 static_cast< int >(UserModel::NavigatorChannelItem));
	QCOMPARE(model->data(channelIndex, UserModel::NavigatorTitleRole).toString(), QStringLiteral("Landing"));
	QCOMPARE(model->data(channelIndex, UserModel::NavigatorOccupancyRole).toInt(), 2);
	QVERIFY(model->data(channelIndex, UserModel::NavigatorCurrentLocationRole).toBool());
	QVERIFY(!model->data(channelIndex, UserModel::NavigatorLinkedLocationRole).toBool());
}

void TestUserModelNavigator::exposesUserFallbackAndTalkState() {
	ensureGlobalState();

	auto *model = new TestableUserModel();
	Channel *root = Channel::get(Mumble::ROOT_CHANNEL_ID);
	Channel *cs = attachChannel(1003, QStringLiteral("CS"), root);
	ClientUser *user = attachUser(*model, 2003, QStringLiteral("Alice Example"), cs);
	user->tsState = Settings::Talking;
	Global::get().uiSession = user->uiSession;

	const QModelIndex userIndex = model->index(user);

	QCOMPARE(model->data(userIndex, UserModel::NavigatorItemKindRole).toInt(),
			 static_cast< int >(UserModel::NavigatorUserItem));
	QCOMPARE(model->data(userIndex, UserModel::NavigatorAvatarFallbackRole).toString(), QStringLiteral("AE"));
	QCOMPARE(model->data(userIndex, UserModel::NavigatorTalkStateRole).toInt(),
			 static_cast< int >(Settings::Talking));
	QVERIFY(model->data(userIndex, UserModel::NavigatorCurrentLocationRole).toBool());
}

void TestUserModelNavigator::exposesLinkedChannelsAndListenerKind() {
	ensureGlobalState();

	auto *model = new TestableUserModel();
	Channel *root = Channel::get(Mumble::ROOT_CHANNEL_ID);
	Channel *landing = attachChannel(1004, QStringLiteral("Landing"), root);
	Channel *afk = attachChannel(1005, QStringLiteral("AFK"), root);
	ClientUser *self = attachUser(*model, 2004, QStringLiteral("Self User"), landing);
	Global::get().uiSession = self->uiSession;
	model->markLinkedChannel(afk);

	const QModelIndex linkedChannelIndex = model->index(afk);
	const QModelIndex listenerIndex = attachListener(*model, self, afk);

	QVERIFY(model->data(linkedChannelIndex, UserModel::NavigatorLinkedLocationRole).toBool());
	QCOMPARE(model->data(listenerIndex, UserModel::NavigatorItemKindRole).toInt(),
			 static_cast< int >(UserModel::NavigatorListenerItem));
}

QTEST_MAIN(TestUserModelNavigator)
#include "TestUserModelNavigator.moc"
