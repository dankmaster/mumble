// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_DEEPFILTERNETSPEECHCLEANUP_H_
#define MUMBLE_MUMBLE_DEEPFILTERNETSPEECHCLEANUP_H_

#include "SpeechCleanupProcessor.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <vector>

class DeepFilterNetSpeechCleanup final : public SpeechCleanupProcessor {
public:
	explicit DeepFilterNetSpeechCleanup(const Mumble::SpeechCleanup::Selection &selection);
	~DeepFilterNetSpeechCleanup() override;

	bool isReady() const override;
	void reset() override;
	void processInPlace(float *samples, unsigned int sampleCount, float mixFactor = 1.0f) override;

private:
	class Implementation;

	std::unique_ptr< Implementation > m_impl;
};

#endif // MUMBLE_MUMBLE_DEEPFILTERNETSPEECHCLEANUP_H_
