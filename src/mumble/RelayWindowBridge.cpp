// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "RelayWindowBridge.h"

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

RelayWindowBridge::RelayWindowBridge(QObject *parent) : QObject(parent) {
}

void RelayWindowBridge::ready() {
	emit bootReady();
}

void RelayWindowBridge::requestFallback(const QString &reason) {
	emit fallbackRequested(reason.trimmed());
}

void RelayWindowBridge::requestClose(const QString &reason) {
	emit closeRequested(reason.trimmed());
}

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)
