// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include "MenuLabel.h"
#include "ModernShellMenuSerializer.h"
#include "VolumeSliderWidgetAction.h"

#include <QtWidgets/QMenu>

class TestModernShellMenuSerializer : public QObject {
	Q_OBJECT

private slots:
	void serializesActionSeparatorLabelSliderAndDynamicContextAction();
};

void TestModernShellMenuSerializer::serializesActionSeparatorLabelSliderAndDynamicContextAction() {
	QMenu menu;

	QAction regularAction(QObject::tr("Plain Action"), &menu);
	QAction dynamicAction(QObject::tr("Dynamic Action"), &menu);
	dynamicAction.setData(QStringLiteral("dynamic-token"));
	MenuLabel label(QObject::tr("Local Volume Adjustment:"), &menu);
	VolumeSliderWidgetAction slider(&menu);
	slider.setSliderAccessibleName(QObject::tr("Listener volume adjustment"));

	menu.addAction(&regularAction);
	menu.addSeparator();
	menu.addAction(&label);
	menu.addAction(&slider);
	menu.addAction(&dynamicAction);

	ModernShellMenuSerializer::ActionRegistry registry;
	const QVariantList items = ModernShellMenuSerializer::serializeMenu(
		&menu,
		[&regularAction, &dynamicAction, &slider](const QAction *action) {
			ModernShellMenuSerializer::ActionDefinition definition;
			if (action == &regularAction) {
				definition.id = QStringLiteral("plainAction");
			} else if (action == &dynamicAction) {
				definition.id = ModernShellMenuSerializer::contextActionId(QStringLiteral("channel"), action->data());
				definition.contextActionData = action->data().toString();
			} else if (action == &slider) {
				definition.id = QStringLiteral("listenerVolume");
			}
			return definition;
		},
		&registry);

	QCOMPARE(items.size(), 5);
	QCOMPARE(items.at(0).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("action"));
	QCOMPARE(items.at(1).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("separator"));
	QCOMPARE(items.at(2).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("label"));
	QCOMPARE(items.at(2).toMap().value(QStringLiteral("label")).toString(),
			 QStringLiteral("Local Volume Adjustment:"));
	QCOMPARE(items.at(3).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("slider"));
	QCOMPARE(items.at(3).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("listenerVolume"));
	QCOMPARE(items.at(3).toMap().value(QStringLiteral("min")).toInt(), -30);
	QCOMPARE(items.at(3).toMap().value(QStringLiteral("max")).toInt(), 30);
	QCOMPARE(items.at(4).toMap().value(QStringLiteral("id")).toString(),
			 QStringLiteral("context:channel:dynamic-token"));

	const auto dynamicRegistryEntry = registry.value(QStringLiteral("context:channel:dynamic-token"));
	QVERIFY(dynamicRegistryEntry.action == &dynamicAction);
	QCOMPARE(dynamicRegistryEntry.contextActionData, QStringLiteral("dynamic-token"));
}

QTEST_MAIN(TestModernShellMenuSerializer)
#include "TestModernShellMenuSerializer.moc"
