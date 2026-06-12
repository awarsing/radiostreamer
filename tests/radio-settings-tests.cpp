/*
Radio Streamer
Copyright (C) 2026 Three Things Media

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "radio-settings.hpp"

#include <QStringList>

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

std::string printable(const QString &value)
{
	return value.toStdString();
}

void expect(bool condition, const char *message)
{
	if (condition)
		return;

	std::cerr << "FAIL: " << message << '\n';
	++failures;
}

void expect_eq(const QString &actual, const QString &expected, const char *message)
{
	if (actual == expected)
		return;

	std::cerr << "FAIL: " << message << "\n  expected: " << printable(expected)
		  << "\n  actual:   " << printable(actual) << '\n';
	++failures;
}

bool contains_pair(const QStringList &values, const QString &option, const QString &value)
{
	for (int i = 0; i + 1 < values.size(); ++i) {
		if (values[i] == option && values[i + 1] == value)
			return true;
	}

	return false;
}

RadioSettings valid_settings()
{
	RadioSettings settings;
	settings.icecastUrl = "icecast://source:hunter2@example.com:8000/live";
	settings.codec = RadioCodec::MP3;
	settings.bitrateKbps = 160;
	settings.sampleRate = 44100;
	settings.audioTrack = 2;
	return settings;
}

void test_codec_mapping()
{
	expect_eq(radio_codec_id(RadioCodec::MP3), "mp3", "MP3 codec id");
	expect_eq(radio_codec_id(RadioCodec::AAC), "aac", "AAC codec id");
	expect_eq(radio_codec_id(RadioCodec::OPUS), "opus", "Opus codec id");

	expect_eq(radio_codec_label(RadioCodec::MP3), "MP3", "MP3 codec label");
	expect_eq(radio_codec_label(RadioCodec::AAC), "AAC", "AAC codec label");
	expect_eq(radio_codec_label(RadioCodec::OPUS), "Opus", "Opus codec label");

	expect(radio_codec_from_id(" mp3 ") == RadioCodec::MP3, "MP3 id parser trims input");
	expect(radio_codec_from_id("AAC") == RadioCodec::AAC, "AAC id parser is case-insensitive");
	expect(radio_codec_from_id("ogg_opus") == RadioCodec::OPUS, "legacy Opus id parser");
	expect(radio_codec_from_id("unexpected") == RadioCodec::MP3, "unknown codec falls back to MP3");
}

void test_validation()
{
	RadioSettings settings = valid_settings();
	expect(radio_settings_validate(settings).isEmpty(), "valid settings pass validation");

	settings.icecastUrl = "http://source:hunter2@example.com:8000/live";
	expect_eq(radio_settings_validate(settings), "URL must start with icecast://.", "reject non-Icecast scheme");

	settings = valid_settings();
	settings.icecastUrl = "icecast://source@example.com:8000/live";
	expect_eq(radio_settings_validate(settings), "URL must include username and password.",
		  "reject missing password");

	settings = valid_settings();
	settings.icecastUrl = "icecast://source:hunter2@:8000/live";
	expect_eq(radio_settings_validate(settings), "URL must include an Icecast host.", "reject missing host");

	settings = valid_settings();
	settings.icecastUrl = "icecast://source:hunter2@example.com/live";
	expect_eq(radio_settings_validate(settings), "URL must include an Icecast port.", "reject missing port");

	settings = valid_settings();
	settings.icecastUrl = "icecast://source:hunter2@example.com:8000/";
	expect_eq(radio_settings_validate(settings), "URL must include an Icecast mount.", "reject missing mount");

	settings = valid_settings();
	settings.bitrateKbps = 31;
	expect_eq(radio_settings_validate(settings), "Bitrate must be between 32 and 320 kbps.", "reject low bitrate");

	settings = valid_settings();
	settings.audioTrack = 7;
	expect_eq(radio_settings_validate(settings), "Audio track must be between 1 and 6.",
		  "reject invalid audio track");

	settings = valid_settings();
	settings.sampleRate = -1;
	expect_eq(radio_settings_validate(settings), "OBS sample rate is invalid.", "reject negative sample rate");
}

void test_url_password_handling()
{
	const RadioSettings settings = valid_settings();

	const QString redactedUrl = radio_settings_icecast_url(settings, false);
	expect(!redactedUrl.contains("hunter2"), "redacted Icecast URL does not expose password");
	expect(redactedUrl.contains("source@example.com:8000/live"), "redacted Icecast URL keeps endpoint");

	const QString fullUrl = radio_settings_icecast_url(settings, true);
	expect(fullUrl.contains("source:hunter2@example.com:8000/live"), "full Icecast URL includes password");
}

void test_credential_redaction()
{
	const QString error = "Error opening output icecast://source:hunter2@example.com:8000/live: Connection refused";
	const QString redacted = radio_redact_credentials(error);

	expect(!redacted.contains("hunter2"), "redacted FFmpeg output does not expose password");
	expect(redacted.contains("icecast://source:***@example.com:8000/live"), "redacted URL keeps endpoint");

	const QString tcpError = "Connection to tcp://127.0.0.1:8000 failed.";
	expect_eq(radio_redact_credentials(tcpError), tcpError, "non-credential URL is unchanged");
}

void test_ffmpeg_args()
{
	RadioSettings settings = valid_settings();

	QStringList args = radio_settings_ffmpeg_args(settings);
	expect(contains_pair(args, "-fflags", "nobuffer"), "FFmpeg input args reduce optional buffering");
	expect(contains_pair(args, "-probesize", "32"), "FFmpeg input args skip raw pipe probing delay");
	expect(contains_pair(args, "-analyzeduration", "0"), "FFmpeg input args skip raw pipe analysis delay");
	expect(contains_pair(args, "-flush_packets", "1"), "FFmpeg output args flush packets immediately");
	expect(contains_pair(args, "-max_delay", "0"), "FFmpeg output args disable mux delay");
	expect(contains_pair(args, "-muxdelay", "0"), "FFmpeg output args disable mux queueing");
	expect(contains_pair(args, "-muxpreload", "0"), "FFmpeg output args disable mux preload");
	expect(args.contains("libmp3lame"), "MP3 args use libmp3lame");
	expect(args.contains("audio/mpeg"), "MP3 args set content type");
	expect(args.contains("mp3"), "MP3 args set muxer");
	expect(args.last().contains("source:hunter2@example.com:8000/live"), "FFmpeg destination includes password");

	settings.codec = RadioCodec::AAC;
	args = radio_settings_ffmpeg_args(settings);
	expect(args.contains("aac"), "AAC args use aac encoder");
	expect(args.contains("audio/aac"), "AAC args set content type");
	expect(args.contains("adts"), "AAC args set muxer");

	settings.codec = RadioCodec::OPUS;
	args = radio_settings_ffmpeg_args(settings);
	expect(args.contains("libopus"), "Opus args use libopus encoder");
	expect(args.contains("48000"), "Opus args resample output to 48 kHz");
	expect(args.contains("audio/ogg"), "Opus args set content type");
	expect(contains_pair(args, "-page_duration", "200000"),
	       "Opus args shorten Ogg page duration for live startup");
	expect(args.contains("ogg"), "Opus args set muxer");
}

} // namespace

int main()
{
	test_codec_mapping();
	test_validation();
	test_url_password_handling();
	test_credential_redaction();
	test_ffmpeg_args();

	if (failures == 0)
		return EXIT_SUCCESS;

	std::cerr << failures << " test expectation(s) failed.\n";
	return EXIT_FAILURE;
}
