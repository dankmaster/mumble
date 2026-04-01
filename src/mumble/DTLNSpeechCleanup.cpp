// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DTLNSpeechCleanup.h"

#ifdef USE_DTLN

#include "Audio.h"
#include "Global.h"
#include "smallft.h"

#include <speex/speex_resampler.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {
	constexpr unsigned int DTLN_RUNTIME_SAMPLE_RATE  = SAMPLE_RATE;
	constexpr unsigned int DTLN_MODEL_SAMPLE_RATE    = 16000;
	constexpr unsigned int DTLN_BLOCK_LENGTH         = 512;
	constexpr unsigned int DTLN_BLOCK_SHIFT          = 128;
	constexpr unsigned int DTLN_FFT_BIN_COUNT        = (DTLN_BLOCK_LENGTH / 2) + 1;
	constexpr unsigned int DTLN_STARTUP_LATENCY      = (DTLN_RUNTIME_SAMPLE_RATE / DTLN_MODEL_SAMPLE_RATE) * DTLN_BLOCK_SHIFT;
	constexpr unsigned int DTLN_MAX_MONO_SAMPLES     = (DTLN_RUNTIME_SAMPLE_RATE * 120) / 1000;
	constexpr unsigned int DTLN_MAX_DOWNSAMPLED      = 4096;
	constexpr unsigned int DTLN_MAX_UPSAMPLED_BLOCK  = 768;
	constexpr unsigned int DTLN_OUTPUT_QUEUE_CAP     = 8192;
	constexpr float DTLN_TIME_DOMAIN_CLAMP           = 1.0f;

	QString dtlnModelDirectory() {
		return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("dtln"));
	}

	std::size_t tensorElementCount(const std::vector< int64_t > &shape) {
		if (shape.empty()) {
			return 0;
		}

		std::size_t count = 1;
		for (const int64_t dim : shape) {
			const int64_t normalized = dim > 0 ? dim : 1;
			count *= static_cast< std::size_t >(normalized);
		}

		return count;
	}

	std::vector< int64_t > normalizeShape(std::vector< int64_t > shape) {
		for (int64_t &dim : shape) {
			if (dim <= 0) {
				dim = 1;
			}
		}

		return shape;
	}

	std::basic_string< ORTCHAR_T > toOrtPath(const QString &path) {
#ifdef _WIN32
		return QDir::toNativeSeparators(path).toStdWString();
#else
		return QDir::toNativeSeparators(path).toStdString();
#endif
	}

	class DTLNModelRuntime {
	public:
		static DTLNModelRuntime &instance() {
			static DTLNModelRuntime runtime;
			return runtime;
		}

		bool isReady() const {
			return m_ready;
		}

		bool runModel1(std::span< float, DTLN_FFT_BIN_COUNT > magnitude, std::span< float > state,
					   std::span< float, DTLN_FFT_BIN_COUNT > mask) const {
			if (!m_ready || state.size() != m_model1StateElementCount) {
				return false;
			}

			std::array< Ort::Value, 2 > inputs = {
				Ort::Value::CreateTensor< float >(m_memoryInfo, magnitude.data(), magnitude.size(),
												  m_model1MagnitudeShape.data(), m_model1MagnitudeShape.size()),
				Ort::Value::CreateTensor< float >(m_memoryInfo, state.data(), state.size(), m_model1StateShape.data(),
												  m_model1StateShape.size())
			};

			std::array< float, DTLN_FFT_BIN_COUNT > maskBuffer = {};
			std::vector< float > nextState(m_model1StateElementCount);
			std::array< Ort::Value, 2 > outputs = {
				Ort::Value::CreateTensor< float >(m_memoryInfo, maskBuffer.data(), maskBuffer.size(), m_model1MaskShape.data(),
												  m_model1MaskShape.size()),
				Ort::Value::CreateTensor< float >(m_memoryInfo, nextState.data(), nextState.size(), m_model1StateShape.data(),
												  m_model1StateShape.size())
			};

			try {
				m_model1Session->Run(Ort::RunOptions{ nullptr }, m_model1InputNamePtrs.data(), inputs.data(), inputs.size(),
									 m_model1OutputNamePtrs.data(), outputs.data(), outputs.size());
			} catch (const Ort::Exception &e) {
				qWarning("DTLN model 1 inference failed: %s", e.what());
				return false;
			}

			std::copy(maskBuffer.begin(), maskBuffer.end(), mask.begin());
			std::copy(nextState.begin(), nextState.end(), state.begin());
			return true;
		}

		bool runModel2(std::span< float, DTLN_BLOCK_LENGTH > block, std::span< float > state,
					   std::span< float, DTLN_BLOCK_LENGTH > outBlock) const {
			if (!m_ready || state.size() != m_model2StateElementCount) {
				return false;
			}

			std::array< Ort::Value, 2 > inputs = {
				Ort::Value::CreateTensor< float >(m_memoryInfo, block.data(), block.size(), m_model2BlockShape.data(),
												  m_model2BlockShape.size()),
				Ort::Value::CreateTensor< float >(m_memoryInfo, state.data(), state.size(), m_model2StateShape.data(),
												  m_model2StateShape.size())
			};

			std::array< float, DTLN_BLOCK_LENGTH > outputBuffer = {};
			std::vector< float > nextState(m_model2StateElementCount);
			std::array< Ort::Value, 2 > outputs = {
				Ort::Value::CreateTensor< float >(m_memoryInfo, outputBuffer.data(), outputBuffer.size(),
												  m_model2OutputShape.data(), m_model2OutputShape.size()),
				Ort::Value::CreateTensor< float >(m_memoryInfo, nextState.data(), nextState.size(), m_model2StateShape.data(),
												  m_model2StateShape.size())
			};

			try {
				m_model2Session->Run(Ort::RunOptions{ nullptr }, m_model2InputNamePtrs.data(), inputs.data(), inputs.size(),
									 m_model2OutputNamePtrs.data(), outputs.data(), outputs.size());
			} catch (const Ort::Exception &e) {
				qWarning("DTLN model 2 inference failed: %s", e.what());
				return false;
			}

			std::copy(outputBuffer.begin(), outputBuffer.end(), outBlock.begin());
			std::copy(nextState.begin(), nextState.end(), state.begin());
			return true;
		}

		std::size_t model1StateElementCount() const {
			return m_model1StateElementCount;
		}

		std::size_t model2StateElementCount() const {
			return m_model2StateElementCount;
		}

	private:
		DTLNModelRuntime()
			: m_env(ORT_LOGGING_LEVEL_WARNING, "mumble-dtln"),
			  m_memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
			initialize();
		}

		void initialize() {
			const QString modelDir    = dtlnModelDirectory();
			const QString model1Path  = QDir(modelDir).filePath(QStringLiteral("model_1.onnx"));
			const QString model2Path  = QDir(modelDir).filePath(QStringLiteral("model_2.onnx"));

			if (!QFileInfo::exists(model1Path) || !QFileInfo::exists(model2Path)) {
				qWarning("DTLN models are missing under %s", qUtf8Printable(modelDir));
				return;
			}

			try {
				Ort::SessionOptions sessionOptions;
				sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
				sessionOptions.SetIntraOpNumThreads(1);
				sessionOptions.SetInterOpNumThreads(1);

				m_model1Session = std::make_unique< Ort::Session >(m_env, toOrtPath(model1Path).c_str(), sessionOptions);
				m_model2Session = std::make_unique< Ort::Session >(m_env, toOrtPath(model2Path).c_str(), sessionOptions);

				initializeModelMetadata(*m_model1Session, DTLN_FFT_BIN_COUNT, DTLN_FFT_BIN_COUNT, m_model1InputNames,
										 m_model1OutputNames, m_model1InputNamePtrs, m_model1OutputNamePtrs,
										 m_model1MagnitudeShape, m_model1MaskShape, m_model1StateShape, m_model1StateElementCount);
				initializeModelMetadata(*m_model2Session, DTLN_BLOCK_LENGTH, DTLN_BLOCK_LENGTH, m_model2InputNames,
										 m_model2OutputNames, m_model2InputNamePtrs, m_model2OutputNamePtrs,
										 m_model2BlockShape, m_model2OutputShape, m_model2StateShape, m_model2StateElementCount);

				m_ready = true;
			} catch (const Ort::Exception &e) {
				qWarning("Failed to initialize DTLN runtime: %s", e.what());
			}
		}

		static void initializeModelMetadata(const Ort::Session &session, std::size_t expectedInputElements,
											std::size_t expectedOutputElements, std::array< std::string, 2 > &inputNames,
											std::array< std::string, 2 > &outputNames,
											std::array< const char *, 2 > &inputNamePtrs,
											std::array< const char *, 2 > &outputNamePtrs,
											std::vector< int64_t > &signalInputShape, std::vector< int64_t > &signalOutputShape,
											std::vector< int64_t > &stateShape, std::size_t &stateElementCount) {
			Ort::AllocatorWithDefaultOptions allocator;

			for (std::size_t index = 0; index < 2; ++index) {
				auto inputName       = session.GetInputNameAllocated(index, allocator);
				auto outputName      = session.GetOutputNameAllocated(index, allocator);
				inputNames[index]    = inputName.get();
				outputNames[index]   = outputName.get();
				inputNamePtrs[index] = inputNames[index].c_str();
				outputNamePtrs[index] = outputNames[index].c_str();
			}

			signalInputShape = normalizeShape(
				session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape());
			signalOutputShape = normalizeShape(
				session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape());
			stateShape = normalizeShape(
				session.GetInputTypeInfo(1).GetTensorTypeAndShapeInfo().GetShape());

			const std::size_t signalInputCount  = tensorElementCount(signalInputShape);
			const std::size_t signalOutputCount = tensorElementCount(signalOutputShape);
			stateElementCount                   = tensorElementCount(stateShape);

			if (signalInputCount != expectedInputElements || signalOutputCount != expectedOutputElements || stateElementCount == 0) {
				throw Ort::Exception("Unexpected DTLN model tensor shapes", ORT_FAIL);
			}
		}

		Ort::Env m_env;
		Ort::MemoryInfo m_memoryInfo;
		std::unique_ptr< Ort::Session > m_model1Session;
		std::unique_ptr< Ort::Session > m_model2Session;
		bool m_ready = false;

		std::array< std::string, 2 > m_model1InputNames  = {};
		std::array< std::string, 2 > m_model1OutputNames = {};
		std::array< const char *, 2 > m_model1InputNamePtrs  = {};
		std::array< const char *, 2 > m_model1OutputNamePtrs = {};
		std::vector< int64_t > m_model1MagnitudeShape;
		std::vector< int64_t > m_model1MaskShape;
		std::vector< int64_t > m_model1StateShape;
		std::size_t m_model1StateElementCount = 0;

		std::array< std::string, 2 > m_model2InputNames  = {};
		std::array< std::string, 2 > m_model2OutputNames = {};
		std::array< const char *, 2 > m_model2InputNamePtrs  = {};
		std::array< const char *, 2 > m_model2OutputNamePtrs = {};
		std::vector< int64_t > m_model2BlockShape;
		std::vector< int64_t > m_model2OutputShape;
		std::vector< int64_t > m_model2StateShape;
		std::size_t m_model2StateElementCount = 0;
	};
} // namespace

class DTLNSpeechCleanup::Implementation {
public:
	Implementation() {
		const DTLNModelRuntime &runtime = DTLNModelRuntime::instance();
		if (!runtime.isReady()) {
			return;
		}

		int error = RESAMPLER_ERR_SUCCESS;
		m_downsampler = speex_resampler_init(1, DTLN_RUNTIME_SAMPLE_RATE, DTLN_MODEL_SAMPLE_RATE, 5, &error);
		if (!m_downsampler || error != RESAMPLER_ERR_SUCCESS) {
			qWarning("Failed to initialize DTLN downsampler: %s", speex_resampler_strerror(error));
			return;
		}

		m_upsampler = speex_resampler_init(1, DTLN_MODEL_SAMPLE_RATE, DTLN_RUNTIME_SAMPLE_RATE, 5, &error);
		if (!m_upsampler || error != RESAMPLER_ERR_SUCCESS) {
			qWarning("Failed to initialize DTLN upsampler: %s", speex_resampler_strerror(error));
			return;
		}

		m_model1State.resize(runtime.model1StateElementCount(), 0.0f);
		m_model2State.resize(runtime.model2StateElementCount(), 0.0f);

		mumble_drft_init(&m_fft, DTLN_BLOCK_LENGTH);
		reset();
		m_ready = true;
	}

	~Implementation() {
		if (m_downsampler) {
			speex_resampler_destroy(m_downsampler);
		}
		if (m_upsampler) {
			speex_resampler_destroy(m_upsampler);
		}
		mumble_drft_clear(&m_fft);
	}

	bool isReady() const {
		return m_ready;
	}

	void reset() {
		if (m_downsampler) {
			speex_resampler_reset_mem(m_downsampler);
		}
		if (m_upsampler) {
			speex_resampler_reset_mem(m_upsampler);
		}

		std::fill(m_model1State.begin(), m_model1State.end(), 0.0f);
		std::fill(m_model2State.begin(), m_model2State.end(), 0.0f);
		m_analysisBuffer.fill(0.0f);
		m_synthesisBuffer.fill(0.0f);
		m_pendingInputCount = 0;
		m_outputQueueHead   = 0;
		m_outputQueueSize   = 0;

		for (unsigned int i = 0; i < DTLN_STARTUP_LATENCY; ++i) {
			pushOutputSample(0.0f);
		}
	}

	void processNormalizedMonoInPlace(float *samples, unsigned int sampleCount, float mixFactor) {
		if (!m_ready || !samples || sampleCount == 0 || sampleCount > DTLN_MAX_MONO_SAMPLES) {
			return;
		}

		mixFactor = std::clamp(mixFactor, 0.0f, 1.0f);
		if (mixFactor <= 0.0f) {
			return;
		}

		std::copy(samples, samples + sampleCount, m_originalBuffer.begin());

		spx_uint32_t inputCount       = sampleCount;
		spx_uint32_t downsampledCount = DTLN_MAX_DOWNSAMPLED;
		speex_resampler_process_float(m_downsampler, 0, m_originalBuffer.data(), &inputCount, m_downsampleBuffer.data(),
									  &downsampledCount);

		if (inputCount != sampleCount) {
			qWarning("DTLN downsampler did not consume the entire input block");
			reset();
			return;
		}

		for (spx_uint32_t i = 0; i < downsampledCount; ++i) {
			m_pendingInput[m_pendingInputCount++] = m_downsampleBuffer[i];
			if (m_pendingInputCount == DTLN_BLOCK_SHIFT) {
				if (!processShiftBlock()) {
					reset();
					return;
				}
				m_pendingInputCount = 0;
			}
		}

		for (unsigned int i = 0; i < sampleCount; ++i) {
			const float cleaned = popOutputSample();
			const float blended = std::clamp(cleaned * mixFactor + m_originalBuffer[i] * (1.0f - mixFactor),
											 -DTLN_TIME_DOMAIN_CLAMP, DTLN_TIME_DOMAIN_CLAMP);
			samples[i] = blended;
		}
	}

private:
	bool processShiftBlock() {
		std::move(m_analysisBuffer.begin() + DTLN_BLOCK_SHIFT, m_analysisBuffer.end(), m_analysisBuffer.begin());
		std::copy(m_pendingInput.begin(), m_pendingInput.end(), m_analysisBuffer.end() - DTLN_BLOCK_SHIFT);

		m_fftTimeBuffer = m_analysisBuffer;
		mumble_drft_forward(&m_fft, m_fftTimeBuffer.data());

		m_magnitude.fill(0.0f);
		m_phase.fill(0.0f);
		m_mask.fill(0.0f);

		m_magnitude[0] = std::fabs(m_fftTimeBuffer[0]);
		m_phase[0]     = m_fftTimeBuffer[0] < 0.0f ? static_cast< float >(M_PI) : 0.0f;

		for (unsigned int bin = 1; bin < (DTLN_FFT_BIN_COUNT - 1); ++bin) {
			const float real = m_fftTimeBuffer[(2 * bin) - 1];
			const float imag = m_fftTimeBuffer[2 * bin];
			m_magnitude[bin] = std::sqrt(real * real + imag * imag);
			m_phase[bin]     = std::atan2(imag, real);
		}

		const float nyquist = m_fftTimeBuffer[DTLN_BLOCK_LENGTH - 1];
		m_magnitude[DTLN_FFT_BIN_COUNT - 1] = std::fabs(nyquist);
		m_phase[DTLN_FFT_BIN_COUNT - 1]     = nyquist < 0.0f ? static_cast< float >(M_PI) : 0.0f;

		DTLNModelRuntime &runtime = DTLNModelRuntime::instance();
		if (!runtime.runModel1(m_magnitude, m_model1State, m_mask)) {
			return false;
		}

		m_fftTimeBuffer.fill(0.0f);
		m_fftTimeBuffer[0] = m_magnitude[0] * m_mask[0] * std::cos(m_phase[0]);
		for (unsigned int bin = 1; bin < (DTLN_FFT_BIN_COUNT - 1); ++bin) {
			const float maskedMagnitude    = m_magnitude[bin] * m_mask[bin];
			m_fftTimeBuffer[(2 * bin) - 1] = maskedMagnitude * std::cos(m_phase[bin]);
			m_fftTimeBuffer[2 * bin]       = maskedMagnitude * std::sin(m_phase[bin]);
		}
		m_fftTimeBuffer[DTLN_BLOCK_LENGTH - 1] =
			m_magnitude[DTLN_FFT_BIN_COUNT - 1] * m_mask[DTLN_FFT_BIN_COUNT - 1] * std::cos(m_phase[DTLN_FFT_BIN_COUNT - 1]);

		mumble_drft_backward(&m_fft, m_fftTimeBuffer.data());
		for (float &sample : m_fftTimeBuffer) {
			sample /= static_cast< float >(DTLN_BLOCK_LENGTH);
		}

		if (!runtime.runModel2(m_fftTimeBuffer, m_model2State, m_model2Output)) {
			return false;
		}

		std::move(m_synthesisBuffer.begin() + DTLN_BLOCK_SHIFT, m_synthesisBuffer.end(), m_synthesisBuffer.begin());
		std::fill(m_synthesisBuffer.end() - DTLN_BLOCK_SHIFT, m_synthesisBuffer.end(), 0.0f);
		for (unsigned int i = 0; i < DTLN_BLOCK_LENGTH; ++i) {
			m_synthesisBuffer[i] += m_model2Output[i];
		}

		spx_uint32_t inLength  = DTLN_BLOCK_SHIFT;
		spx_uint32_t outLength = DTLN_MAX_UPSAMPLED_BLOCK;
		speex_resampler_process_float(m_upsampler, 0, m_synthesisBuffer.data(), &inLength, m_upsampleBuffer.data(), &outLength);

		if (inLength != DTLN_BLOCK_SHIFT) {
			qWarning("DTLN upsampler did not consume the entire synthesis block");
			return false;
		}

		if ((m_outputQueueSize + outLength) > DTLN_OUTPUT_QUEUE_CAP) {
			qWarning("DTLN output queue overflow");
			return false;
		}

		for (spx_uint32_t i = 0; i < outLength; ++i) {
			pushOutputSample(m_upsampleBuffer[i]);
		}

		return true;
	}

	void pushOutputSample(float sample) {
		if (m_outputQueueSize >= DTLN_OUTPUT_QUEUE_CAP) {
			return;
		}

		const unsigned int tail = (m_outputQueueHead + m_outputQueueSize) % DTLN_OUTPUT_QUEUE_CAP;
		m_outputQueue[tail]     = sample;
		++m_outputQueueSize;
	}

	float popOutputSample() {
		if (m_outputQueueSize == 0) {
			return 0.0f;
		}

		const float sample = m_outputQueue[m_outputQueueHead];
		m_outputQueueHead  = (m_outputQueueHead + 1) % DTLN_OUTPUT_QUEUE_CAP;
		--m_outputQueueSize;
		return sample;
	}

	bool m_ready = false;
	SpeexResamplerState *m_downsampler = nullptr;
	SpeexResamplerState *m_upsampler   = nullptr;
	drft_lookup m_fft                  = {};

	std::vector< float > m_model1State;
	std::vector< float > m_model2State;

	std::array< float, DTLN_MAX_MONO_SAMPLES > m_originalBuffer = {};
	std::array< float, DTLN_MAX_DOWNSAMPLED > m_downsampleBuffer = {};
	std::array< float, DTLN_BLOCK_SHIFT > m_pendingInput = {};
	unsigned int m_pendingInputCount = 0;

	std::array< float, DTLN_BLOCK_LENGTH > m_analysisBuffer = {};
	std::array< float, DTLN_BLOCK_LENGTH > m_synthesisBuffer = {};
	std::array< float, DTLN_BLOCK_LENGTH > m_fftTimeBuffer = {};
	std::array< float, DTLN_BLOCK_LENGTH > m_model2Output = {};
	std::array< float, DTLN_FFT_BIN_COUNT > m_magnitude = {};
	std::array< float, DTLN_FFT_BIN_COUNT > m_phase = {};
	std::array< float, DTLN_FFT_BIN_COUNT > m_mask = {};

	std::array< float, DTLN_MAX_UPSAMPLED_BLOCK > m_upsampleBuffer = {};
	std::array< float, DTLN_OUTPUT_QUEUE_CAP > m_outputQueue = {};
	unsigned int m_outputQueueHead = 0;
	unsigned int m_outputQueueSize = 0;
};

DTLNSpeechCleanup::DTLNSpeechCleanup() : m_impl(std::make_unique< Implementation >()) {
}

DTLNSpeechCleanup::~DTLNSpeechCleanup() = default;

bool DTLNSpeechCleanup::isReady() const {
	return m_impl && m_impl->isReady();
}

void DTLNSpeechCleanup::reset() {
	if (m_impl) {
		m_impl->reset();
	}
}

void DTLNSpeechCleanup::processNormalizedMonoInPlace(float *samples, unsigned int sampleCount, float mixFactor) {
	if (m_impl) {
		m_impl->processNormalizedMonoInPlace(samples, sampleCount, mixFactor);
	}
}

#else

class DTLNSpeechCleanup::Implementation {
};

DTLNSpeechCleanup::DTLNSpeechCleanup() = default;

DTLNSpeechCleanup::~DTLNSpeechCleanup() = default;

bool DTLNSpeechCleanup::isReady() const {
	return false;
}

void DTLNSpeechCleanup::reset() {
}

void DTLNSpeechCleanup::processNormalizedMonoInPlace(float *samples, unsigned int sampleCount, float mixFactor) {
	static_cast< void >(samples);
	static_cast< void >(sampleCount);
	static_cast< void >(mixFactor);
}

#endif
