// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_DTLNSPEECHCLEANUP_H_
#define MUMBLE_MUMBLE_DTLNSPEECHCLEANUP_H_

#include <memory>

class DTLNSpeechCleanup {
public:
	DTLNSpeechCleanup();
	~DTLNSpeechCleanup();

	bool isReady() const;
	void reset();

	/// Process normalized mono PCM samples in-place.
	///
	/// The expected input range is [-1.0, 1.0]. If mixFactor is below 1.0, the
	/// cleaned signal is blended with the original input.
	void processNormalizedMonoInPlace(float *samples, unsigned int sampleCount, float mixFactor = 1.0f);

private:
	class Implementation;

	std::unique_ptr< Implementation > m_impl;
};

#endif // MUMBLE_MUMBLE_DTLNSPEECHCLEANUP_H_
