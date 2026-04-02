// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Audio.h"
#include "SpeechCleanup.h"
#include "SpeechCleanupProcessor.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>

#include <sndfile.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
	constexpr unsigned int kFrameSize = SAMPLE_RATE / 100;
	constexpr float kEpsilon          = 1.0e-12f;

	enum class InputFormat {
		Auto,
		Wav,
		S16LE,
		F32LE
	};

	struct LoadedAudio {
		std::vector< float > samples;
		int sampleRate          = 0;
		int channels            = 0;
		QString resolvedFormat;
	};

	struct BenchmarkMetrics {
		double cpuMs            = 0.0;
		double audioMs          = 0.0;
		double realTimeFactor   = 0.0;
		std::size_t clippingCount = 0;
		float peak              = 0.0f;
		float rms               = 0.0f;
		QString activeModelId;
		QString activeModelPath;
		bool usedFallback       = false;
		std::optional< double > siSdr;
		std::optional< double > segmentalSnr;
	};

	InputFormat parseInputFormat(const QString &value) {
		const QString normalized = value.trimmed().toLower();
		if (normalized.isEmpty() || normalized == QLatin1String("auto")) {
			return InputFormat::Auto;
		}
		if (normalized == QLatin1String("wav")) {
			return InputFormat::Wav;
		}
		if (normalized == QLatin1String("s16le")) {
			return InputFormat::S16LE;
		}
		if (normalized == QLatin1String("f32le")) {
			return InputFormat::F32LE;
		}

		throw std::runtime_error(QStringLiteral("Unsupported input format: %1").arg(value).toStdString());
	}

	InputFormat resolveInputFormat(const QString &path, InputFormat requestedFormat) {
		if (requestedFormat != InputFormat::Auto) {
			return requestedFormat;
		}

		if (QFileInfo(path).suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0) {
			return InputFormat::Wav;
		}

		throw std::runtime_error(QStringLiteral("Unable to infer input format for %1").arg(path).toStdString());
	}

	std::vector< float > mixDownToMono(const std::vector< float > &interleavedSamples, int channels) {
		if (channels <= 0 || interleavedSamples.empty()) {
			return {};
		}
		if (channels == 1) {
			return interleavedSamples;
		}

		const std::size_t frameCount = interleavedSamples.size() / static_cast< std::size_t >(channels);
		std::vector< float > mono(frameCount, 0.0f);
		for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
			float mixed = 0.0f;
			for (int channelIndex = 0; channelIndex < channels; ++channelIndex) {
				mixed += interleavedSamples[frameIndex * static_cast< std::size_t >(channels) + channelIndex];
			}
			mono[frameIndex] = mixed / static_cast< float >(channels);
		}

		return mono;
	}

	LoadedAudio loadWav(const QString &path) {
		SF_INFO info = {};
		SNDFILE *file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
		if (!file) {
			throw std::runtime_error(QStringLiteral("Failed to open WAV file %1").arg(path).toStdString());
		}

		const std::size_t frameCount = static_cast< std::size_t >(info.frames);
		std::vector< float > interleavedSamples(frameCount * static_cast< std::size_t >(info.channels), 0.0f);
		const sf_count_t readFrames = sf_readf_float(file, interleavedSamples.data(), info.frames);
		sf_close(file);

		if (readFrames != info.frames) {
			throw std::runtime_error(QStringLiteral("Failed to read all samples from %1").arg(path).toStdString());
		}

		return { mixDownToMono(interleavedSamples, info.channels), info.samplerate, 1, QStringLiteral("wav") };
	}

	LoadedAudio loadRaw(const QString &path, InputFormat format, int sampleRate, int channels) {
		if (sampleRate <= 0) {
			throw std::runtime_error(QStringLiteral("A positive sample rate is required for raw input: %1")
										 .arg(path)
										 .toStdString());
		}
		if (channels <= 0) {
			throw std::runtime_error(
				QStringLiteral("A positive channel count is required for raw input: %1").arg(path).toStdString());
		}

		std::ifstream stream(path.toStdString(), std::ios::binary);
		if (!stream) {
			throw std::runtime_error(QStringLiteral("Failed to open raw input %1").arg(path).toStdString());
		}

		stream.seekg(0, std::ios::end);
		const std::streamoff fileSize = stream.tellg();
		stream.seekg(0, std::ios::beg);
		if (fileSize < 0) {
			throw std::runtime_error(QStringLiteral("Failed to determine raw input size for %1").arg(path).toStdString());
		}

		std::vector< float > interleavedSamples;
		if (format == InputFormat::S16LE) {
			if ((fileSize % static_cast< std::streamoff >(sizeof(std::int16_t))) != 0) {
				throw std::runtime_error(QStringLiteral("Raw s16le input has an invalid length: %1").arg(path).toStdString());
			}

			std::vector< std::int16_t > pcmSamples(static_cast< std::size_t >(fileSize / sizeof(std::int16_t)));
			stream.read(reinterpret_cast< char * >(pcmSamples.data()), fileSize);
			interleavedSamples.resize(pcmSamples.size());
			std::transform(pcmSamples.begin(), pcmSamples.end(), interleavedSamples.begin(),
						   [](std::int16_t sample) { return static_cast< float >(sample) / 32768.0f; });
		} else if (format == InputFormat::F32LE) {
			if ((fileSize % static_cast< std::streamoff >(sizeof(float))) != 0) {
				throw std::runtime_error(QStringLiteral("Raw f32le input has an invalid length: %1").arg(path).toStdString());
			}

			interleavedSamples.resize(static_cast< std::size_t >(fileSize / sizeof(float)));
			stream.read(reinterpret_cast< char * >(interleavedSamples.data()), fileSize);
		} else {
			throw std::runtime_error(QStringLiteral("Unsupported raw format for %1").arg(path).toStdString());
		}

		if (!stream) {
			throw std::runtime_error(QStringLiteral("Failed to read raw input %1").arg(path).toStdString());
		}
		if ((interleavedSamples.size() % static_cast< std::size_t >(channels)) != 0) {
			throw std::runtime_error(
				QStringLiteral("Raw input sample count is not divisible by channel count: %1").arg(path).toStdString());
		}

		QString resolvedFormat = (format == InputFormat::S16LE) ? QStringLiteral("s16le") : QStringLiteral("f32le");
		return { mixDownToMono(interleavedSamples, channels), sampleRate, 1, resolvedFormat };
	}

	LoadedAudio loadAudio(const QString &path, InputFormat requestedFormat, int sampleRate, int channels) {
		const InputFormat resolvedFormat = resolveInputFormat(path, requestedFormat);
		switch (resolvedFormat) {
			case InputFormat::Wav:
				return loadWav(path);
			case InputFormat::S16LE:
			case InputFormat::F32LE:
				return loadRaw(path, resolvedFormat, sampleRate, channels);
			case InputFormat::Auto:
				break;
		}

		throw std::runtime_error(QStringLiteral("Unsupported input format for %1").arg(path).toStdString());
	}

	void writeOutputWav(const QString &path, const std::vector< float > &samples, int sampleRate) {
		SF_INFO info = {};
		info.channels = 1;
		info.samplerate = sampleRate;
		info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

		SNDFILE *file = sf_open(path.toUtf8().constData(), SFM_WRITE, &info);
		if (!file) {
			throw std::runtime_error(QStringLiteral("Failed to open output WAV %1").arg(path).toStdString());
		}

		const sf_count_t writtenFrames = sf_writef_float(file, samples.data(), static_cast< sf_count_t >(samples.size()));
		sf_close(file);
		if (writtenFrames != static_cast< sf_count_t >(samples.size())) {
			throw std::runtime_error(QStringLiteral("Failed to write output WAV %1").arg(path).toStdString());
		}
	}

	Settings::SpeechCleanupBackend parseBackend(const QString &value) {
		for (Settings::SpeechCleanupBackend backend : Mumble::SpeechCleanup::supportedBackends) {
			if (QString::fromLatin1(Mumble::SpeechCleanup::backendDisplayName(backend))
					.compare(value.trimmed(), Qt::CaseInsensitive)
				== 0) {
				return backend;
			}
		}

		throw std::runtime_error(QStringLiteral("Unsupported backend: %1").arg(value).toStdString());
	}

	double computeSiSdr(const std::vector< float > &reference, const std::vector< float > &estimate) {
		const std::size_t sampleCount = std::min(reference.size(), estimate.size());
		if (sampleCount == 0) {
			return 0.0;
		}

		double referenceEnergy = 0.0;
		double dotProduct      = 0.0;
		for (std::size_t index = 0; index < sampleCount; ++index) {
			referenceEnergy += static_cast< double >(reference[index]) * static_cast< double >(reference[index]);
			dotProduct += static_cast< double >(reference[index]) * static_cast< double >(estimate[index]);
		}

		if (referenceEnergy <= kEpsilon) {
			return 0.0;
		}

		const double scale = dotProduct / referenceEnergy;
		double targetEnergy = 0.0;
		double noiseEnergy  = 0.0;
		for (std::size_t index = 0; index < sampleCount; ++index) {
			const double targetSample = scale * static_cast< double >(reference[index]);
			const double noiseSample  = static_cast< double >(estimate[index]) - targetSample;
			targetEnergy += targetSample * targetSample;
			noiseEnergy += noiseSample * noiseSample;
		}

		return 10.0 * std::log10(std::max(targetEnergy, static_cast< double >(kEpsilon))
								 / std::max(noiseEnergy, static_cast< double >(kEpsilon)));
	}

	double computeSegmentalSnr(const std::vector< float > &reference, const std::vector< float > &estimate) {
		const std::size_t sampleCount = std::min(reference.size(), estimate.size());
		if (sampleCount == 0) {
			return 0.0;
		}

		double accumulatedSnr = 0.0;
		std::size_t segmentCount = 0;
		for (std::size_t offset = 0; offset < sampleCount; offset += kFrameSize) {
			const std::size_t segmentLength = std::min<std::size_t>(kFrameSize, sampleCount - offset);
			double signalEnergy = 0.0;
			double noiseEnergy  = 0.0;
			for (std::size_t index = 0; index < segmentLength; ++index) {
				const double referenceSample = static_cast< double >(reference[offset + index]);
				const double errorSample = static_cast< double >(reference[offset + index] - estimate[offset + index]);
				signalEnergy += referenceSample * referenceSample;
				noiseEnergy += errorSample * errorSample;
			}

			if (signalEnergy <= kEpsilon) {
				continue;
			}

			double segmentSnr = 10.0 * std::log10(signalEnergy / std::max(noiseEnergy, static_cast< double >(kEpsilon)));
			segmentSnr        = std::clamp(segmentSnr, -10.0, 35.0);
			accumulatedSnr += segmentSnr;
			++segmentCount;
		}

		return segmentCount > 0 ? accumulatedSnr / static_cast< double >(segmentCount) : 0.0;
	}

	BenchmarkMetrics processSamples(const Mumble::SpeechCleanup::Selection &selection, std::vector< float > &samples) {
		if (!Mumble::SpeechCleanup::isBackendAvailable(selection.backend)) {
			throw std::runtime_error(
				QStringLiteral("Requested backend is unavailable: %1")
					.arg(Mumble::SpeechCleanup::backendDisplayName(selection.backend))
					.toStdString());
		}

		std::unique_ptr< SpeechCleanupProcessor > processor = createSpeechCleanupProcessor(selection);
		if (!processor || !processor->isReady()) {
			throw std::runtime_error(QStringLiteral("Failed to initialize speech cleanup backend for model %1")
										 .arg(selection.modelId)
										 .toStdString());
		}

		BenchmarkMetrics metrics;
		metrics.activeModelId   = processor->activeModelId();
		metrics.activeModelPath = processor->activeModelPath();
		metrics.usedFallback    = processor->usedFallback();
		const auto startTime = std::chrono::steady_clock::now();

		std::vector< float > frameBuffer(kFrameSize, 0.0f);
		for (std::size_t offset = 0; offset < samples.size(); offset += kFrameSize) {
			const std::size_t frameLength = std::min<std::size_t>(kFrameSize, samples.size() - offset);
			std::fill(frameBuffer.begin(), frameBuffer.end(), 0.0f);
			std::copy_n(samples.data() + offset, frameLength, frameBuffer.data());
			processor->processInPlace(frameBuffer.data(), static_cast< unsigned int >(frameBuffer.size()));
			std::copy_n(frameBuffer.data(), frameLength, samples.data() + offset);
		}

		const auto endTime = std::chrono::steady_clock::now();
		metrics.cpuMs = std::chrono::duration< double, std::milli >(endTime - startTime).count();

		double sumSquares = 0.0;
		for (float sample : samples) {
			const float absolute = std::fabs(sample);
			if (absolute > 1.0f) {
				++metrics.clippingCount;
			}
			metrics.peak = std::max(metrics.peak, absolute);
			sumSquares += static_cast< double >(sample) * static_cast< double >(sample);
		}

		metrics.audioMs = (samples.empty() ? 0.0 : (static_cast< double >(samples.size()) * 1000.0 / SAMPLE_RATE));
		metrics.realTimeFactor = (metrics.audioMs > 0.0) ? (metrics.cpuMs / metrics.audioMs) : 0.0;
		metrics.rms            = samples.empty() ? 0.0f : static_cast< float >(std::sqrt(sumSquares / samples.size()));
		return metrics;
	}
} // namespace

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("speech_cleanup_benchmark"));

	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Offline speech cleanup benchmark runner"));
	parser.addHelpOption();

	const QCommandLineOption backendOption(QStringList() << QStringLiteral("backend"),
										   QStringLiteral("Speech cleanup backend name"), QStringLiteral("backend"));
	const QCommandLineOption modelIdOption(QStringList() << QStringLiteral("model-id"),
										   QStringLiteral("Speech cleanup model identifier"), QStringLiteral("modelId"));
	const QCommandLineOption customModelPathOption(QStringList() << QStringLiteral("custom-model-path"),
												   QStringLiteral("Optional custom model path"),
												   QStringLiteral("path"));
	const QCommandLineOption inputOption(QStringList() << QStringLiteral("input"),
										 QStringLiteral("Input WAV or raw file"), QStringLiteral("path"));
	const QCommandLineOption inputFormatOption(QStringList() << QStringLiteral("input-format"),
											   QStringLiteral("Input format: auto, wav, s16le, f32le"),
											   QStringLiteral("format"), QStringLiteral("auto"));
	const QCommandLineOption inputSampleRateOption(QStringList() << QStringLiteral("sample-rate"),
												   QStringLiteral("Input sample rate for raw files"),
												   QStringLiteral("sampleRate"));
	const QCommandLineOption inputChannelsOption(QStringList() << QStringLiteral("channels"),
												 QStringLiteral("Input channel count for raw files"),
												 QStringLiteral("channels"), QStringLiteral("1"));
	const QCommandLineOption cleanReferenceOption(QStringList() << QStringLiteral("clean-reference"),
												  QStringLiteral("Optional clean-reference WAV or raw file"),
												  QStringLiteral("path"));
	const QCommandLineOption cleanFormatOption(QStringList() << QStringLiteral("clean-format"),
											   QStringLiteral("Clean-reference format: auto, wav, s16le, f32le"),
											   QStringLiteral("format"), QStringLiteral("auto"));
	const QCommandLineOption cleanSampleRateOption(QStringList() << QStringLiteral("clean-sample-rate"),
												   QStringLiteral("Clean-reference sample rate for raw files"),
												   QStringLiteral("sampleRate"));
	const QCommandLineOption cleanChannelsOption(QStringList() << QStringLiteral("clean-channels"),
												 QStringLiteral("Clean-reference channel count for raw files"),
												 QStringLiteral("channels"), QStringLiteral("1"));
	const QCommandLineOption outputOption(QStringList() << QStringLiteral("output"),
										  QStringLiteral("Output WAV path"), QStringLiteral("path"));
	const QCommandLineOption reportOption(QStringList() << QStringLiteral("report"),
										  QStringLiteral("Output JSON report path"), QStringLiteral("path"));

	parser.addOption(backendOption);
	parser.addOption(modelIdOption);
	parser.addOption(customModelPathOption);
	parser.addOption(inputOption);
	parser.addOption(inputFormatOption);
	parser.addOption(inputSampleRateOption);
	parser.addOption(inputChannelsOption);
	parser.addOption(cleanReferenceOption);
	parser.addOption(cleanFormatOption);
	parser.addOption(cleanSampleRateOption);
	parser.addOption(cleanChannelsOption);
	parser.addOption(outputOption);
	parser.addOption(reportOption);

	parser.process(app);

	try {
		if (!parser.isSet(backendOption) || !parser.isSet(modelIdOption) || !parser.isSet(inputOption)
			|| !parser.isSet(outputOption) || !parser.isSet(reportOption)) {
			throw std::runtime_error(
				"Required options: --backend, --model-id, --input, --output, --report");
		}

		const Settings::SpeechCleanupBackend backend = parseBackend(parser.value(backendOption));
		Mumble::SpeechCleanup::Selection selection = Mumble::SpeechCleanup::normalizeSelection({
			backend,
			parser.value(modelIdOption),
			parser.value(customModelPathOption),
		});

		const LoadedAudio input = loadAudio(parser.value(inputOption), parseInputFormat(parser.value(inputFormatOption)),
											parser.value(inputSampleRateOption).toInt(),
											parser.value(inputChannelsOption).toInt());
		if (input.sampleRate != SAMPLE_RATE) {
			throw std::runtime_error(QStringLiteral("Expected %1 Hz input, got %2 Hz")
										 .arg(SAMPLE_RATE)
										 .arg(input.sampleRate)
										 .toStdString());
		}

		std::vector< float > processed = input.samples;
		BenchmarkMetrics metrics       = processSamples(selection, processed);

		std::optional< LoadedAudio > cleanReference;
		if (parser.isSet(cleanReferenceOption)) {
			cleanReference = loadAudio(parser.value(cleanReferenceOption), parseInputFormat(parser.value(cleanFormatOption)),
									  parser.value(cleanSampleRateOption).toInt(),
									  parser.value(cleanChannelsOption).toInt());
			if (cleanReference->sampleRate != SAMPLE_RATE) {
				throw std::runtime_error(QStringLiteral("Expected %1 Hz clean reference, got %2 Hz")
											 .arg(SAMPLE_RATE)
											 .arg(cleanReference->sampleRate)
											 .toStdString());
			}

			metrics.siSdr = computeSiSdr(cleanReference->samples, processed);
			metrics.segmentalSnr = computeSegmentalSnr(cleanReference->samples, processed);
		}

		writeOutputWav(parser.value(outputOption), processed, input.sampleRate);

		nlohmann::json report = {
			{ "backend", Mumble::SpeechCleanup::backendDisplayName(selection.backend) },
			{ "model_id", selection.modelId.toStdString() },
			{ "custom_model_path", selection.customModelPath.toStdString() },
			{ "active_model_id", metrics.activeModelId.toStdString() },
			{ "active_model_path", metrics.activeModelPath.toStdString() },
			{ "used_fallback", metrics.usedFallback },
			{ "input_path", parser.value(inputOption).toStdString() },
			{ "input_format", input.resolvedFormat.toStdString() },
			{ "output_path", parser.value(outputOption).toStdString() },
			{ "report_path", parser.value(reportOption).toStdString() },
			{ "sample_rate", input.sampleRate },
			{ "sample_count", processed.size() },
			{ "cpu_ms", metrics.cpuMs },
			{ "audio_ms", metrics.audioMs },
			{ "rtf", metrics.realTimeFactor },
			{ "clipping_count", metrics.clippingCount },
			{ "peak", metrics.peak },
			{ "rms", metrics.rms },
		};

		if (cleanReference) {
			report["clean_reference_path"] = parser.value(cleanReferenceOption).toStdString();
			report["clean_reference_format"] = cleanReference->resolvedFormat.toStdString();
			report["clean_reference_sample_count"] = cleanReference->samples.size();
			report["si_sdr"] = *metrics.siSdr;
			report["segmental_snr"] = *metrics.segmentalSnr;
		}

		std::ofstream reportStream(parser.value(reportOption).toStdString(), std::ios::binary);
		if (!reportStream) {
			throw std::runtime_error(QStringLiteral("Failed to open report path %1")
										 .arg(parser.value(reportOption))
										 .toStdString());
		}
		reportStream << report.dump(2) << '\n';
		reportStream.close();
		return 0;
	} catch (const std::exception &e) {
		qCritical("speech_cleanup_benchmark: %s", e.what());
		return 1;
	}
}
