/*
Radio Streamer
Copyright (C) 2026 Three Things Media

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "radio-settings.hpp"

#include <obs.h>

namespace {

constexpr const char *KEY_URL = "url";
constexpr const char *KEY_CODEC = "codec";
constexpr const char *KEY_BITRATE = "bitrate_kbps";
constexpr const char *KEY_SAMPLE_RATE = "sample_rate";
constexpr const char *KEY_AUDIO_TRACK = "audio_track";
constexpr const char *KEY_RECONNECT = "reconnect";

QString data_string(obs_data_t *data, const char *key)
{
	const char *value = obs_data_get_string(data, key);
	return QString::fromUtf8(value ? value : "");
}

void set_data_string(obs_data_t *data, const char *key, const QString &value)
{
	const QByteArray bytes = value.toUtf8();
	obs_data_set_string(data, key, bytes.constData());
}

int clamp_int(long long value, int minValue, int maxValue)
{
	if (value < minValue)
		return minValue;
	if (value > maxValue)
		return maxValue;
	return static_cast<int>(value);
}

} // namespace

void radio_settings_set_defaults(obs_data_t *data)
{
	obs_data_set_default_string(data, KEY_URL, "");
	obs_data_set_default_string(data, KEY_CODEC, "mp3");
	obs_data_set_default_int(data, KEY_BITRATE, 128);
	obs_data_set_default_int(data, KEY_SAMPLE_RATE, 0);
	obs_data_set_default_int(data, KEY_AUDIO_TRACK, 1);
	obs_data_set_default_bool(data, KEY_RECONNECT, true);
}

RadioSettings radio_settings_from_data(obs_data_t *data)
{
	if (!data) {
		obs_data_t *defaults = obs_data_create();
		radio_settings_set_defaults(defaults);
		RadioSettings settings = radio_settings_from_data(defaults);
		obs_data_release(defaults);
		return settings;
	}

	radio_settings_set_defaults(data);

	RadioSettings settings;
	settings.icecastUrl = data_string(data, KEY_URL).trimmed();
	settings.codec = radio_codec_from_id(data_string(data, KEY_CODEC));
	settings.bitrateKbps = clamp_int(obs_data_get_int(data, KEY_BITRATE), 32, 320);
	settings.sampleRate = clamp_int(obs_data_get_int(data, KEY_SAMPLE_RATE), 0, 192000);
	settings.audioTrack = clamp_int(obs_data_get_int(data, KEY_AUDIO_TRACK), 1, 6);
	settings.reconnectEnabled = obs_data_get_bool(data, KEY_RECONNECT);

	return settings;
}

obs_data_t *radio_settings_to_data(const RadioSettings &settings)
{
	obs_data_t *data = obs_data_create();
	radio_settings_set_defaults(data);

	set_data_string(data, KEY_URL, settings.icecastUrl);
	set_data_string(data, KEY_CODEC, radio_codec_id(settings.codec));
	obs_data_set_int(data, KEY_BITRATE, settings.bitrateKbps);
	obs_data_set_int(data, KEY_SAMPLE_RATE, settings.sampleRate);
	obs_data_set_int(data, KEY_AUDIO_TRACK, settings.audioTrack);
	obs_data_set_bool(data, KEY_RECONNECT, settings.reconnectEnabled);

	return data;
}
