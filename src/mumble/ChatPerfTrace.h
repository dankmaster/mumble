// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CHATPERFTRACE_H_
#define MUMBLE_MUMBLE_CHATPERFTRACE_H_

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QtGlobal>

#include <algorithm>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace mumble {
namespace chatperf {
	namespace detail {
		struct TimingStats {
			quint64 count = 0;
			qint64 totalNs = 0;
			qint64 maxNs = 0;
		};

		struct ValueStats {
			quint64 count = 0;
			qint64 total = 0;
			qint64 max = std::numeric_limits< qint64 >::lowest();
		};

		inline bool enabled() {
			static const bool kEnabled = qEnvironmentVariableIntValue("MUMBLE_CHAT_PERF_TRACE") > 0;
			return kEnabled;
		}

		inline QElapsedTimer &sessionTimer() {
			static QElapsedTimer timer;
			static const bool started = []() {
				timer.start();
				return true;
			}();
			Q_UNUSED(started);
			return timer;
		}

		inline std::mutex &traceMutex() {
			static std::mutex mutex;
			return mutex;
		}

		inline std::unordered_map< std::string, TimingStats > &timingStats() {
			static std::unordered_map< std::string, TimingStats > stats;
			return stats;
		}

		inline std::unordered_map< std::string, ValueStats > &valueStats() {
			static std::unordered_map< std::string, ValueStats > stats;
			return stats;
		}

		inline qint64 &lastFlushMs() {
			static qint64 value = 0;
			return value;
		}

		inline quint64 &pendingRecordCount() {
			static quint64 value = 0;
			return value;
		}

		inline QString logPath() {
			static const QString path = []() {
				const QString configuredPath = qEnvironmentVariable("MUMBLE_CHAT_PERF_TRACE_PATH");
				return configuredPath.isEmpty() ? QDir(QDir::tempPath()).filePath(QLatin1String("mumble-chat-perf.log"))
											   : configuredPath;
			}();
			return path;
		}

		inline void appendLineLocked(const QString &line) {
			QFile file(logPath());
			if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
				return;
			}

			file.write(line.toUtf8());
			file.write("\n");
			file.close();
		}

		inline void flushLocked(qint64 elapsedMs) {
			const qint64 windowMs = elapsedMs - lastFlushMs();
			if (windowMs < 1000 && pendingRecordCount() < 200) {
				return;
			}

			auto &timings = timingStats();
			auto &values  = valueStats();
			if (timings.empty() && values.empty()) {
				lastFlushMs() = elapsedMs;
				pendingRecordCount() = 0;
				return;
			}

			struct SortedTimingEntry {
				std::string name;
				TimingStats stats;
			};
			struct SortedValueEntry {
				std::string name;
				ValueStats stats;
			};

			std::vector< SortedTimingEntry > sortedTimings;
			sortedTimings.reserve(timings.size());
			for (const auto &entry : timings) {
				sortedTimings.push_back(SortedTimingEntry { entry.first, entry.second });
			}
			std::sort(sortedTimings.begin(), sortedTimings.end(),
					  [](const SortedTimingEntry &lhs, const SortedTimingEntry &rhs) {
						  return lhs.stats.totalNs > rhs.stats.totalNs;
					  });

			std::vector< SortedValueEntry > sortedValues;
			sortedValues.reserve(values.size());
			for (const auto &entry : values) {
				sortedValues.push_back(SortedValueEntry { entry.first, entry.second });
			}
			std::sort(sortedValues.begin(), sortedValues.end(),
					  [](const SortedValueEntry &lhs, const SortedValueEntry &rhs) {
						  return lhs.stats.total > rhs.stats.total;
					  });

			appendLineLocked(QString::fromLatin1("[chat-perf] window_ms=%1 timing_entries=%2 value_entries=%3")
								 .arg(windowMs)
								 .arg(sortedTimings.size())
								 .arg(sortedValues.size()));

			const std::size_t maxTimingEntries = std::min< std::size_t >(sortedTimings.size(), 12);
			for (std::size_t i = 0; i < maxTimingEntries; ++i) {
				const SortedTimingEntry &entry = sortedTimings[i];
				const double totalMs = static_cast< double >(entry.stats.totalNs) / 1000000.0;
				const double avgMs =
					entry.stats.count > 0 ? totalMs / static_cast< double >(entry.stats.count ) : 0.0;
				const double maxMs = static_cast< double >(entry.stats.maxNs) / 1000000.0;
				appendLineLocked(QString::fromLatin1("[chat-perf][timing] %1 count=%2 total_ms=%3 avg_ms=%4 max_ms=%5")
									 .arg(QString::fromStdString(entry.name))
									 .arg(entry.stats.count)
									 .arg(totalMs, 0, 'f', 3)
									 .arg(avgMs, 0, 'f', 3)
									 .arg(maxMs, 0, 'f', 3));
			}

			const std::size_t maxValueEntries = std::min< std::size_t >(sortedValues.size(), 12);
			for (std::size_t i = 0; i < maxValueEntries; ++i) {
				const SortedValueEntry &entry = sortedValues[i];
				const double avgValue =
					entry.stats.count > 0
						? static_cast< double >(entry.stats.total) / static_cast< double >(entry.stats.count)
						: 0.0;
				appendLineLocked(QString::fromLatin1("[chat-perf][value] %1 count=%2 total=%3 avg=%4 max=%5")
									 .arg(QString::fromStdString(entry.name))
									 .arg(entry.stats.count)
									 .arg(entry.stats.total)
									 .arg(avgValue, 0, 'f', 2)
									 .arg(entry.stats.max));
			}

			timings.clear();
			values.clear();
			lastFlushMs() = elapsedMs;
			pendingRecordCount() = 0;
		}
	} // namespace detail

	inline bool enabled() {
		return detail::enabled();
	}

	inline void recordDuration(const char *name, qint64 elapsedNs) {
		if (!detail::enabled()) {
			return;
		}

		std::lock_guard< std::mutex > lock(detail::traceMutex());
		detail::TimingStats &stats = detail::timingStats()[name];
		++stats.count;
		stats.totalNs += elapsedNs;
		stats.maxNs = std::max(stats.maxNs, elapsedNs);
		++detail::pendingRecordCount();
		detail::flushLocked(detail::sessionTimer().elapsed());
	}

	inline void recordValue(const char *name, qint64 value) {
		if (!detail::enabled()) {
			return;
		}

		std::lock_guard< std::mutex > lock(detail::traceMutex());
		detail::ValueStats &stats = detail::valueStats()[name];
		++stats.count;
		stats.total += value;
		stats.max = std::max(stats.max, value);
		++detail::pendingRecordCount();
		detail::flushLocked(detail::sessionTimer().elapsed());
	}

	inline void recordNote(const char *name, const QString &value) {
		if (!detail::enabled()) {
			return;
		}

		QString sanitized = value;
		sanitized.replace(QLatin1Char('\r'), QLatin1Char(' '));
		sanitized.replace(QLatin1Char('\n'), QLatin1Char(' '));
		if (sanitized.size() > 512) {
			sanitized = sanitized.left(512);
		}

		std::lock_guard< std::mutex > lock(detail::traceMutex());
		detail::appendLineLocked(QString::fromLatin1("[chat-perf][note] t_ms=%1 %2 %3")
									 .arg(detail::sessionTimer().elapsed())
									 .arg(QString::fromLatin1(name))
									 .arg(sanitized));
	}

	class ScopedDuration {
	public:
		explicit ScopedDuration(const char *name) : m_name(name), m_enabled(detail::enabled()) {
			if (m_enabled) {
				m_timer.start();
			}
		}

		~ScopedDuration() {
			if (m_enabled) {
				recordDuration(m_name, m_timer.nsecsElapsed());
			}
		}

	private:
		const char *m_name;
		bool m_enabled = false;
		QElapsedTimer m_timer;
	};
} // namespace chatperf
} // namespace mumble

#endif // MUMBLE_MUMBLE_CHATPERFTRACE_H_
