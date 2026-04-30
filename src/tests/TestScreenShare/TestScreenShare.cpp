// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include "ScreenShare.h"

namespace {
int codecValue(const MumbleProto::ScreenShareCodec codec) {
	return static_cast< int >(codec);
}
} // namespace

class TestScreenShare : public QObject {
	Q_OBJECT

private slots:
	void parsesAndFormatsVp8CodecPreferences();
	void keepsDirectDefaultH264First();
	void usesVp8ForBrowserWebRtcRuntime();
	void negotiatesWebRtcRelayWithLegacyFallback();
	void recommendsVp8Bitrate();
};

void TestScreenShare::parsesAndFormatsVp8CodecPreferences() {
	const QList< int > codecs =
		Mumble::ScreenShare::parseCodecPreferenceString(QStringLiteral("vp8 h264,av1 vp9 vp8 nope"));

	QCOMPARE(codecs, (QList< int >{
						 codecValue(MumbleProto::ScreenShareCodecVP8), codecValue(MumbleProto::ScreenShareCodecH264),
						 codecValue(MumbleProto::ScreenShareCodecAV1), codecValue(MumbleProto::ScreenShareCodecVP9) }));
	QCOMPARE(Mumble::ScreenShare::codecPreferenceString(codecs), QStringLiteral("vp8 h264 av1 vp9"));
	QCOMPARE(Mumble::ScreenShare::codecToConfigToken(MumbleProto::ScreenShareCodecVP8), QStringLiteral("vp8"));
	QVERIFY(Mumble::ScreenShare::isValidCodec(MumbleProto::ScreenShareCodecVP8));
}

void TestScreenShare::keepsDirectDefaultH264First() {
	const QList< int > codecs = Mumble::ScreenShare::defaultCodecPreferenceList();

	QVERIFY(!codecs.isEmpty());
	QCOMPARE(codecs.first(), codecValue(MumbleProto::ScreenShareCodecH264));
	QVERIFY(codecs.contains(codecValue(MumbleProto::ScreenShareCodecVP8)));
}

void TestScreenShare::usesVp8ForBrowserWebRtcRuntime() {
	QCOMPARE(Mumble::ScreenShare::browserWebRtcCodecPreferenceList(),
			 (QList< int >{ codecValue(MumbleProto::ScreenShareCodecVP8) }));
	QCOMPARE(
		Mumble::ScreenShare::webRtcRelayCodecPreferenceList(),
		(QList< int >{ codecValue(MumbleProto::ScreenShareCodecVP8), codecValue(MumbleProto::ScreenShareCodecH264),
					   codecValue(MumbleProto::ScreenShareCodecAV1), codecValue(MumbleProto::ScreenShareCodecVP9) }));
}

void TestScreenShare::negotiatesWebRtcRelayWithLegacyFallback() {
	const QList< int > webRtcPreferences = Mumble::ScreenShare::webRtcRelayCodecPreferenceList();

	QCOMPARE(
		Mumble::ScreenShare::selectPreferredCodec(webRtcPreferences, { codecValue(MumbleProto::ScreenShareCodecVP8),
																	   codecValue(MumbleProto::ScreenShareCodecH264) }),
		MumbleProto::ScreenShareCodecVP8);
	QCOMPARE(
		Mumble::ScreenShare::selectPreferredCodec(webRtcPreferences, { codecValue(MumbleProto::ScreenShareCodecH264),
																	   codecValue(MumbleProto::ScreenShareCodecAV1) }),
		MumbleProto::ScreenShareCodecH264);
}

void TestScreenShare::recommendsVp8Bitrate() {
	const unsigned int h264Bitrate = Mumble::ScreenShare::defaultBitrateKbps(
		MumbleProto::ScreenShareCodecH264, Mumble::ScreenShare::DEFAULT_MAX_WIDTH,
		Mumble::ScreenShare::DEFAULT_MAX_HEIGHT, Mumble::ScreenShare::DEFAULT_MAX_FPS);
	const unsigned int vp8Bitrate = Mumble::ScreenShare::defaultBitrateKbps(
		MumbleProto::ScreenShareCodecVP8, Mumble::ScreenShare::DEFAULT_MAX_WIDTH,
		Mumble::ScreenShare::DEFAULT_MAX_HEIGHT, Mumble::ScreenShare::DEFAULT_MAX_FPS);

	QCOMPARE(h264Bitrate, 3000U);
	QCOMPARE(vp8Bitrate, 3150U);
}

QTEST_MAIN(TestScreenShare)
#include "TestScreenShare.moc"
