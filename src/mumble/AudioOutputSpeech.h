// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_AUDIOOUTPUTSPEECH_H_
#define MUMBLE_MUMBLE_AUDIOOUTPUTSPEECH_H_

#include <speex/speex_jitter.h>
#include <speex/speex_resampler.h>

#include <QtCore/QMutex>

#include "AudioOutputBuffer.h"
#include "AudioOutputCache.h"
#include "MumbleProtocol.h"

#include <array>
#include <memory>
#include <mutex>
#include <vector>

class ClientUser;
class DTLNSpeechCleanup;
struct DenoiseState;
struct OpusDecoder;

class AudioOutputSpeech : public AudioOutputBuffer {
private:
	Q_OBJECT
	Q_DISABLE_COPY(AudioOutputSpeech)
protected:
	static std::mutex s_audioCachesMutex;
	static std::vector< AudioOutputCache > s_audioCaches;

	static void invalidateAudioOutputCache(void *maskedIndex);
	static std::size_t storeAudioOutputCache(const Mumble::Protocol::AudioData &audioData);

	unsigned int iAudioBufferSize;
	unsigned int iBufferOffset;
	unsigned int iBufferFilled;
	unsigned int iOutputSize;
	unsigned int iLastConsume;
	unsigned int iFrameSize;
	unsigned int iFrameSizePerChannel;
	unsigned int iSampleRate;
	unsigned int iMixerFreq;
	bool bLastAlive;
	bool bHasTerminator;

	float *fFadeIn;
	float *fFadeOut;
	float *fResamplerBuffer;

	SpeexResamplerState *srs;

	QMutex qmJitter;
	JitterBuffer *jbJitter;
	int iMissCount;

	OpusDecoder *opusState;

	QList< QByteArray > qlFrames;

#ifdef USE_RNNOISE
	DenoiseState *m_remoteSpeechCleanupState = nullptr;
	unsigned int m_remoteSpeechCleanupFrameSize = 0;
	std::array< float, 480 > m_remoteSpeechCleanupInputBuffer = {};
	std::array< float, 480 > m_remoteSpeechCleanupOutputBuffer = {};
#endif
#ifdef USE_DTLN
	std::unique_ptr< DTLNSpeechCleanup > m_dtlnSpeechCleanup;
	std::array< float, 5760 > m_remoteSpeechCleanupMonoBuffer = {};
#endif

	bool isEffectivelyDualMono(const float *samples, unsigned int sampleCount) const;
	void applyRemoteSpeechCleanup(float *samples, unsigned int sampleCount);

public:
	Mumble::Protocol::audio_context_t m_audioContext;
	Mumble::Protocol::AudioCodec m_codec;
	int iMissedFrames;
	ClientUser *p;

	/// Fetch and decode frames from the jitter buffer. Called in mix().
	///
	/// @param frameCount Number of frames to decode. frame means a bundle of one sample from each channel.
	virtual bool prepareSampleBuffer(unsigned int frameCount) Q_DECL_OVERRIDE;

	void addFrameToBuffer(const Mumble::Protocol::AudioData &audioData);

	/// @param systemMaxBufferSize maximum number of samples the system audio play back may request each time
	AudioOutputSpeech(ClientUser *, unsigned int freq, Mumble::Protocol::AudioCodec codec,
					  unsigned int systemMaxBufferSize);
	~AudioOutputSpeech() Q_DECL_OVERRIDE;
};

#endif // AUDIOOUTPUTSPEECH_H_
