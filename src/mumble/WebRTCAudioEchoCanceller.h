// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WEBRTCAUDIOECHOCANCELLER_H_
#define MUMBLE_MUMBLE_WEBRTCAUDIOECHOCANCELLER_H_

#include <memory>

class WebRTCAudioEchoCanceller {
public:
	WebRTCAudioEchoCanceller(unsigned int sampleRate, unsigned int frameSize);
	~WebRTCAudioEchoCanceller();

	bool isReady() const;
	void reset();
	bool processCaptureFrame(const short *captureInput, short *captureOutput, const short *render, unsigned int frameSize,
							 unsigned int renderChannels);

private:
	class Implementation;

	std::unique_ptr< Implementation > m_impl;
};

#endif // MUMBLE_MUMBLE_WEBRTCAUDIOECHOCANCELLER_H_
