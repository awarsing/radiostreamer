/*
Radio Streamer
Copyright (C) 2026 Three Things Media

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "radio-settings.hpp"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

QString radio_codec_id(RadioCodec codec)
{
	switch (codec) {
	case RadioCodec::OPUS:
		return "opus";
	case RadioCodec::AAC:
		return "aac";
	case RadioCodec::MP3:
	default:
		return "mp3";
	}
}

QString radio_codec_label(RadioCodec codec)
{
	switch (codec) {
	case RadioCodec::OPUS:
		return "Opus";
	case RadioCodec::AAC:
		return "AAC";
	case RadioCodec::MP3:
	default:
		return "MP3";
	}
}

RadioCodec radio_codec_from_id(const QString &id)
{
	const QString normalized = id.trimmed().toLower();
	if (normalized == "opus" || normalized == "ogg_opus")
		return RadioCodec::OPUS;
	if (normalized == "aac" || normalized == "acc")
		return RadioCodec::AAC;
	return RadioCodec::MP3;
}

QString radio_settings_validate(const RadioSettings &settings)
{
	const QUrl url(settings.icecastUrl.trimmed());
	if (!url.isValid() || url.scheme() != "icecast")
		return "URL must start with icecast://.";
	if (url.userName().isEmpty() || url.password().isEmpty())
		return "URL must include username and password.";
	if (url.host().isEmpty())
		return "URL must include an Icecast host.";
	if (url.port() < 1 || url.port() > 65535)
		return "URL must include an Icecast port.";
	if (url.path().isEmpty() || url.path() == "/")
		return "URL must include an Icecast mount.";
	if (settings.bitrateKbps < 32 || settings.bitrateKbps > 320)
		return "Bitrate must be between 32 and 320 kbps.";
	if (settings.audioTrack < 1 || settings.audioTrack > 6)
		return "Audio track must be between 1 and 6.";
	if (settings.sampleRate < 0)
		return "OBS sample rate is invalid.";

	return {};
}

QString radio_settings_icecast_url(const RadioSettings &settings, bool includePassword)
{
	QUrl url(settings.icecastUrl.trimmed());
	if (!includePassword)
		url.setPassword({});
	return url.toString(QUrl::FullyEncoded);
}

QString radio_redact_credentials(const QString &text)
{
	static const QRegularExpression credentialUrl(R"(([A-Za-z][A-Za-z0-9+.-]*://)([^/?#\s:@]+):([^/?#\s@]*)@)");
	return QString(text).replace(credentialUrl, R"(\1\2:***@)");
}

QStringList radio_settings_ffmpeg_args(const RadioSettings &settings)
{
	QStringList args;
	args << "-hide_banner";
	args << "-loglevel" << "warning";
	args << "-fflags" << "nobuffer";
	args << "-probesize" << "32";
	args << "-analyzeduration" << "0";
	args << "-f" << "s16le";
	args << "-ar" << QString::number(settings.sampleRate);
	args << "-ac" << "2";
	args << "-i" << "pipe:0";
	args << "-vn";
	args << "-flush_packets" << "1";
	args << "-max_delay" << "0";
	args << "-muxdelay" << "0";
	args << "-muxpreload" << "0";

	if (settings.codec == RadioCodec::MP3) {
		args << "-c:a" << "libmp3lame";
		args << "-b:a" << QString("%1k").arg(settings.bitrateKbps);
		args << "-content_type" << "audio/mpeg";
		args << "-f" << "mp3";
	} else if (settings.codec == RadioCodec::AAC) {
		args << "-c:a" << "aac";
		args << "-b:a" << QString("%1k").arg(settings.bitrateKbps);
		args << "-content_type" << "audio/aac";
		args << "-f" << "adts";
	} else {
		args << "-c:a" << "libopus";
		args << "-ar" << "48000";
		args << "-application" << "audio";
		args << "-b:a" << QString("%1k").arg(settings.bitrateKbps);
		args << "-content_type" << "audio/ogg";
		args << "-page_duration" << "200000";
		args << "-f" << "ogg";
	}

	args << radio_settings_icecast_url(settings, true);
	return args;
}

QString radio_resolved_ffmpeg_path()
{
	const QString pathExecutable = QStandardPaths::findExecutable("ffmpeg");
	if (!pathExecutable.isEmpty())
		return pathExecutable;

	const QStringList candidates = {
		"/opt/homebrew/bin/ffmpeg",
		"/usr/local/bin/ffmpeg",
		"/usr/bin/ffmpeg",
	};

	for (const QString &candidate : candidates) {
		const QFileInfo info(candidate);
		if (info.exists() && info.isExecutable())
			return candidate;
	}

	return "ffmpeg";
}
