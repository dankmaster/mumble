// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Logger.h"

#include <QString>
#include <QtGlobal>

#include <atomic>
#include <cstdlib>

#include <spdlog/sinks/dup_filter_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using MasterSink = spdlog::sinks::dup_filter_sink_st;
using StdOutSink = spdlog::sinks::stdout_color_sink_st;

#ifdef Q_OS_WIN
#	include <spdlog/sinks/msvc_sink.h>

using DebuggerSink = spdlog::sinks::msvc_sink_st;
#endif

using namespace mumble;

static std::shared_ptr< MasterSink > masterSink;
static QtMessageHandler previousQtMessageHandler = nullptr;
static std::atomic_bool qtMessageHandlerEnabled = false;

namespace {
	std::shared_ptr< spdlog::logger > &qtMessageLogger() {
		// Keep the logger alive for the lifetime of the process so late Qt messages
		// cannot call through spdlog's raw default-logger pointer after registry teardown.
		static auto *logger = new std::shared_ptr< spdlog::logger >();
		return *logger;
	}

	spdlog::level::level_enum toSpdlogLevel(const QtMsgType type) {
		switch (type) {
			default:
				return spdlog::level::trace;
			case QtDebugMsg:
				return spdlog::level::debug;
			case QtInfoMsg:
				return spdlog::level::info;
			case QtWarningMsg:
				return spdlog::level::warn;
			case QtCriticalMsg:
			case QtFatalMsg:
				return spdlog::level::err;
		}
	}
} // namespace

static void qtMessageHandler(const QtMsgType type, const QMessageLogContext &context, const QString &msg) {
	const QByteArray localMessage = msg.toLocal8Bit();
	if (!qtMessageHandlerEnabled.load(std::memory_order_acquire)) {
		if (previousQtMessageHandler) {
			previousQtMessageHandler(type, context, msg);
		}
		return;
	}

	const std::shared_ptr< spdlog::logger > logger = qtMessageLogger();
	if (!logger) {
		if (previousQtMessageHandler) {
			previousQtMessageHandler(type, context, msg);
		}
		return;
	}

	logger->log(spdlog::source_loc {}, toSpdlogLevel(type), "{}", localMessage.constData());
	if (type == QtFatalMsg) {
		logger->flush();
		std::abort();
	}
}

void log::addSink(std::shared_ptr< spdlog::sinks::sink > sink) {
	if (!masterSink) {
		log::fatal("Attempted to addSink before master sink has been initialized");
		return;
	}

	sink->set_pattern("%^<%L>%$%Y-%m-%d %H:%M:%S.%e %v");

	masterSink->add_sink(std::move(sink));
}

void log::init(spdlog::level::level_enum logLevel) {
	// Skips the message if the previous one is identical and less than 5 seconds have passed.
	masterSink = std::make_shared< MasterSink >(std::chrono::seconds(5));
#ifdef Q_OS_WIN
	addSink(std::make_shared< DebuggerSink >());
#endif
	addSink(std::make_shared< StdOutSink >());

	auto logger = std::make_shared< spdlog::logger >(MainLoggerName, masterSink);

	logger->set_level(logLevel);

	qtMessageLogger() = logger;
	set_default_logger(std::move(logger));

	previousQtMessageHandler = qInstallMessageHandler(qtMessageHandler);
	qtMessageHandlerEnabled.store(true, std::memory_order_release);
}

void log::restoreQtMessageHandler() {
	qtMessageHandlerEnabled.store(false, std::memory_order_release);
	qInstallMessageHandler(previousQtMessageHandler);
}
