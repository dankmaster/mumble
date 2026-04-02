// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_SPEECHCLEANUPPROCESSOR_H_
#define MUMBLE_MUMBLE_SPEECHCLEANUPPROCESSOR_H_

#include "SpeechCleanup.h"

#include <QString>

#include <memory>

class SpeechCleanupProcessor {
public:
	virtual ~SpeechCleanupProcessor() = default;

	virtual bool isReady() const = 0;
	virtual void reset()         = 0;
	virtual void processInPlace(float *samples, unsigned int sampleCount, float mixFactor = 1.0f) = 0;
	virtual QString activeModelId() const {
		return {};
	}
	virtual QString activeModelPath() const {
		return {};
	}
	virtual bool usedFallback() const {
		return false;
	}
};

std::unique_ptr< SpeechCleanupProcessor > createSpeechCleanupProcessor(
	const Mumble::SpeechCleanup::Selection &selection);

#endif // MUMBLE_MUMBLE_SPEECHCLEANUPPROCESSOR_H_
