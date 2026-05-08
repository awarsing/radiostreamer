/*
Radio Streamer
Copyright (C) 2026 Three Things Media

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <QString>
#include <QStringList>

struct obs_data;
typedef struct obs_data obs_data_t;

constexpr const char *RADIO_OUTPUT_ID = "obs_radio_icecast_output";

enum class RadioCodec {
	MP3,
	AAC,
	OPUS,
};

struct RadioSettings {
	QString icecastUrl;
	RadioCodec codec = RadioCodec::MP3;
	int bitrateKbps = 128;
	int sampleRate = 0;
	int audioTrack = 1;
	bool reconnectEnabled = true;
};

void radio_settings_set_defaults(obs_data_t *data);
RadioSettings radio_settings_from_data(obs_data_t *data);
obs_data_t *radio_settings_to_data(const RadioSettings &settings);

QString radio_codec_id(RadioCodec codec);
QString radio_codec_label(RadioCodec codec);
RadioCodec radio_codec_from_id(const QString &id);

QString radio_settings_validate(const RadioSettings &settings);
QString radio_settings_icecast_url(const RadioSettings &settings, bool includePassword);
QString radio_redact_credentials(const QString &text);
QStringList radio_settings_ffmpeg_args(const RadioSettings &settings);
QString radio_resolved_ffmpeg_path();
