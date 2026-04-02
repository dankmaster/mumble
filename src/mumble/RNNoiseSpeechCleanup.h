// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_RNNOISESPEECHCLEANUP_H_
#define MUMBLE_MUMBLE_RNNOISESPEECHCLEANUP_H_

#include "SpeechCleanupProcessor.h"

#include <array>

struct DenoiseState;

class RNNoiseSpeechCleanup final : public SpeechCleanupProcessor {
public:
	explicit RNNoiseSpeechCleanup(const Mumble::SpeechCleanup::Selection &selection);
	~RNNoiseSpeechCleanup() override;

	bool isReady() const override;
	void reset() override;
	void processInPlace(float *samples, unsigned int sampleCount, float mixFactor = 1.0f) override;
	QString activeModelId() const override;
	QString activeModelPath() const override;
	bool usedFallback() const override;

private:
	static constexpr unsigned int FRAME_SIZE = 480;

	Mumble::SpeechCleanup::Selection m_selection;
	QString m_activeModelId;
	QString m_activeModelPath;
	bool m_usedFallback = false;
	struct RNNModel *m_model = nullptr;
	DenoiseState *m_state = nullptr;
	std::array< float, FRAME_SIZE > m_inputBuffer  = {};
	std::array< float, FRAME_SIZE > m_outputBuffer = {};
};

#endif // MUMBLE_MUMBLE_RNNOISESPEECHCLEANUP_H_
