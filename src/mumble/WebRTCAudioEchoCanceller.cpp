// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "WebRTCAudioEchoCanceller.h"

#include <QtCore/QDebug>

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

#ifdef USE_WEBRTC_AEC
#	ifdef _MSC_VER
#		pragma warning(push)
#		pragma warning(disable : 4005)
#	endif
#	include "webrtc/modules/audio_processing/include/audio_processing.h"
#	ifdef _MSC_VER
#		pragma warning(pop)
#	endif
#endif

namespace {
	short clampFloatToShort(float sample) {
		const float scaled = sample * 32768.0f;
		return static_cast< short >(std::clamp(scaled, static_cast< float >(std::numeric_limits< short >::min()),
											   static_cast< float >(std::numeric_limits< short >::max())));
	}
}

class WebRTCAudioEchoCanceller::Implementation {
public:
	Implementation(unsigned int sampleRate, unsigned int frameSize)
		: m_sampleRate(sampleRate), m_frameSize(frameSize), m_renderFrame(frameSize, 0.0f),
		  m_renderScratch(frameSize, 0.0f), m_captureFrame(frameSize, 0.0f), m_captureScratch(frameSize, 0.0f) {
#ifdef USE_WEBRTC_AEC
		if (sampleRate == 0 || frameSize == 0) {
			qWarning("WebRTCAudioEchoCanceller: Invalid frame configuration (%u Hz, %u samples)", sampleRate,
					 frameSize);
			return;
		}

		webrtc::Config config;
		config.Set< webrtc::ExtendedFilter >(new webrtc::ExtendedFilter(true));
		config.Set< webrtc::DelayAgnostic >(new webrtc::DelayAgnostic(true));
		config.Set< webrtc::ExperimentalAgc >(new webrtc::ExperimentalAgc(false));
		config.Set< webrtc::ExperimentalNs >(new webrtc::ExperimentalNs(false));

		m_apm.reset(webrtc::AudioProcessing::Create(config));
		if (!m_apm) {
			qWarning("WebRTCAudioEchoCanceller: Failed to create WebRTC audio processor");
			return;
		}

		m_apm->high_pass_filter()->Enable(true);
		m_apm->echo_cancellation()->enable_drift_compensation(false);
		m_apm->echo_cancellation()->set_suppression_level(webrtc::EchoCancellation::kHighSuppression);
		m_apm->echo_cancellation()->Enable(true);

		m_ready = true;
		reset();
#else
		(void) m_sampleRate;
		(void) m_frameSize;
#endif
	}

	bool isReady() const {
		return m_ready;
	}

	void reset() {
#ifdef USE_WEBRTC_AEC
		if (!m_apm) {
			return;
		}

		const webrtc::StreamConfig monoStream(static_cast< int >(m_sampleRate), 1);
		webrtc::ProcessingConfig processingConfig;
		processingConfig.input_stream()         = monoStream;
		processingConfig.output_stream()        = monoStream;
		processingConfig.reverse_input_stream() = monoStream;
		processingConfig.reverse_output_stream() = monoStream;

		if (m_apm->Initialize(processingConfig) != 0) {
			qWarning("WebRTCAudioEchoCanceller: Failed to initialize WebRTC audio processor");
			m_ready = false;
			return;
		}

		m_ready = true;
		m_apm->set_stream_delay_ms(0);
		m_apm->set_stream_key_pressed(false);
		std::fill(m_renderFrame.begin(), m_renderFrame.end(), 0.0f);
		std::fill(m_renderScratch.begin(), m_renderScratch.end(), 0.0f);
		std::fill(m_captureFrame.begin(), m_captureFrame.end(), 0.0f);
		std::fill(m_captureScratch.begin(), m_captureScratch.end(), 0.0f);
#endif
	}

	bool processCaptureFrame(const short *captureInput, short *captureOutput, const short *render, unsigned int frameSize,
							 unsigned int renderChannels) {
#ifdef USE_WEBRTC_AEC
		if (!m_ready || !m_apm || !captureInput || !captureOutput || !render || frameSize != m_frameSize
			|| renderChannels == 0) {
			return false;
		}

		for (unsigned int i = 0; i < frameSize; ++i) {
			float renderSample = 0.0f;
			const unsigned int renderOffset = i * renderChannels;
			for (unsigned int channel = 0; channel < renderChannels; ++channel) {
				renderSample += static_cast< float >(render[renderOffset + channel]);
			}
			m_renderFrame[i] = (renderSample / static_cast< float >(renderChannels)) / 32768.0f;
			m_captureFrame[i] = static_cast< float >(captureInput[i]) / 32768.0f;
		}

		const float *renderInputChannels[1] = { m_renderFrame.data() };
		float *renderOutputChannels[1]      = { m_renderScratch.data() };
		if (m_apm->ProcessReverseStream(renderInputChannels, webrtc::StreamConfig(static_cast< int >(m_sampleRate), 1),
										webrtc::StreamConfig(static_cast< int >(m_sampleRate), 1),
										renderOutputChannels)
			!= 0) {
			return false;
		}

		m_apm->set_stream_delay_ms(0);

		const float *captureInputChannels[1] = { m_captureFrame.data() };
		float *captureOutputChannels[1]      = { m_captureScratch.data() };
		if (m_apm->ProcessStream(captureInputChannels, webrtc::StreamConfig(static_cast< int >(m_sampleRate), 1),
								 webrtc::StreamConfig(static_cast< int >(m_sampleRate), 1), captureOutputChannels)
			!= 0) {
			return false;
		}

		for (unsigned int i = 0; i < frameSize; ++i) {
			captureOutput[i] = clampFloatToShort(m_captureScratch[i]);
		}

		return true;
#else
		(void) captureInput;
		(void) captureOutput;
		(void) render;
		(void) frameSize;
		(void) renderChannels;
		return false;
#endif
	}

private:
	unsigned int m_sampleRate = 0;
	unsigned int m_frameSize  = 0;
	bool m_ready              = false;
	std::vector< float > m_renderFrame;
	std::vector< float > m_renderScratch;
	std::vector< float > m_captureFrame;
	std::vector< float > m_captureScratch;

#ifdef USE_WEBRTC_AEC
	std::unique_ptr< webrtc::AudioProcessing > m_apm;
#endif
};

WebRTCAudioEchoCanceller::WebRTCAudioEchoCanceller(unsigned int sampleRate, unsigned int frameSize)
	: m_impl(std::make_unique< Implementation >(sampleRate, frameSize)) {
}

WebRTCAudioEchoCanceller::~WebRTCAudioEchoCanceller() = default;

bool WebRTCAudioEchoCanceller::isReady() const {
	return m_impl && m_impl->isReady();
}

void WebRTCAudioEchoCanceller::reset() {
	if (m_impl) {
		m_impl->reset();
	}
}

bool WebRTCAudioEchoCanceller::processCaptureFrame(const short *captureInput, short *captureOutput,
												   const short *render, unsigned int frameSize,
												   unsigned int renderChannels) {
	return m_impl && m_impl->processCaptureFrame(captureInput, captureOutput, render, frameSize, renderChannels);
}
