/*
Radio Streamer
Copyright (C) 2026 Three Things Media

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "radio-output.hpp"

#include "ffmpeg-pipe.hpp"
#include "radio-settings.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace {

class CallbackGuard {
public:
	explicit CallbackGuard(std::atomic<int> &count) : count_(count) { count_.fetch_add(1); }
	~CallbackGuard() { count_.fetch_sub(1); }

	CallbackGuard(const CallbackGuard &) = delete;
	CallbackGuard &operator=(const CallbackGuard &) = delete;

private:
	std::atomic<int> &count_;
};

class RadioOutput {
public:
	RadioOutput(obs_data_t *settings, obs_output_t *output) : output_(output) { update(settings); }

	~RadioOutput()
	{
		active_.store(false, std::memory_order_release);
		stopPipe();
	}

	void update(obs_data_t *settings) { settings_ = radio_settings_from_data(settings); }

	bool start()
	{
		const QString validationError = radio_settings_validate(settings_);
		if (!validationError.isEmpty()) {
			setLastError(validationError);
			return false;
		}

		audio_t *audio = obs_get_audio();
		if (!audio) {
			setLastError("OBS audio is not available.");
			return false;
		}

		settings_.sampleRate = static_cast<int>(audio_output_get_sample_rate(audio));
		if (settings_.sampleRate <= 0) {
			setLastError("OBS audio sample rate is not available.");
			return false;
		}

		struct audio_convert_info conversion = {};
		conversion.samples_per_sec = static_cast<uint32_t>(settings_.sampleRate);
		conversion.format = AUDIO_FORMAT_16BIT;
		conversion.speakers = SPEAKERS_STEREO;
		obs_output_set_audio_conversion(output_, &conversion);
		obs_output_set_mixer(output_, static_cast<size_t>(settings_.audioTrack - 1));
		obs_output_set_media(output_, nullptr, audio);

		if (!obs_output_can_begin_data_capture(output_, 0)) {
			setLastError("OBS cannot begin radio audio capture right now.");
			return false;
		}

		auto pipe = std::make_unique<FfmpegPipe>(settings_);

		QString ffmpegError;
		if (!pipe->start(&ffmpegError)) {
			setLastError(ffmpegError);
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(pipeMutex_);
			pipe_ = std::move(pipe);
			activePipe_.store(pipe_.get(), std::memory_order_release);
		}

		active_.store(true, std::memory_order_release);
		if (!obs_output_begin_data_capture(output_, 0)) {
			active_.store(false, std::memory_order_release);
			stopPipe();
			setLastError("OBS failed to begin radio audio capture.");
			return false;
		}

		obs_log(LOG_INFO, "radio output started (%s, %d kbps, track %d)",
			radio_codec_id(settings_.codec).toUtf8().constData(), settings_.bitrateKbps,
			settings_.audioTrack);
		return true;
	}

	void stop(uint64_t ts)
	{
		UNUSED_PARAMETER(ts);
		active_.store(false, std::memory_order_release);
		obs_output_end_data_capture(output_);
		stopPipe();
		obs_log(LOG_INFO, "radio output stopped");
	}

	void rawAudio(struct audio_data *frames)
	{
		if (!frames || !frames->data[0])
			return;

		if (!active_.load(std::memory_order_acquire))
			return;

		CallbackGuard guard(callbacksInFlight_);

		if (!active_.load(std::memory_order_acquire))
			return;

		FfmpegPipe *pipe = activePipe_.load(std::memory_order_acquire);
		if (!pipe)
			return;

		bool failed = false;
		if (pipe->failed()) {
			failed = true;
		} else {
			const size_t bytes = static_cast<size_t>(frames->frames) * 2U * sizeof(int16_t);
			pipe->enqueue(frames->data[0], bytes, frames->frames);
			failed = pipe->failed();
		}

		if (failed)
			failFromPipe();
	}

	uint64_t totalBytes() const
	{
		std::lock_guard<std::mutex> lock(pipeMutex_);
		return pipe_ ? pipe_->totalBytes() : finalBytes_.load();
	}

	int droppedFrames() const
	{
		std::lock_guard<std::mutex> lock(pipeMutex_);
		return pipe_ ? pipe_->droppedFrames() : finalDroppedFrames_.load();
	}

private:
	void stopPipe()
	{
		activePipe_.store(nullptr, std::memory_order_release);
		waitForAudioCallbacks();

		std::unique_ptr<FfmpegPipe> pipe;
		{
			std::lock_guard<std::mutex> lock(pipeMutex_);
			if (!pipe_)
				return;

			finalBytes_.store(pipe_->totalBytes());
			finalDroppedFrames_.store(pipe_->droppedFrames());
			pipe = std::move(pipe_);
		}

		pipe->stop();
	}

	void waitForAudioCallbacks() const
	{
		while (callbacksInFlight_.load(std::memory_order_acquire) > 0)
			std::this_thread::yield();
	}

	void setLastError(const QString &error)
	{
		const QByteArray bytes = radio_redact_credentials(error).toUtf8();
		obs_output_set_last_error(output_, bytes.constData());
	}

	void failFromPipe()
	{
		if (!active_.exchange(false))
			return;

		QString error;
		{
			std::lock_guard<std::mutex> lock(pipeMutex_);
			if (pipe_) {
				error = pipe_->lastError();
				finalBytes_.store(pipe_->totalBytes());
				finalDroppedFrames_.store(pipe_->droppedFrames());
			}
		}

		if (error.isEmpty())
			error = "FFmpeg radio output failed.";

		setLastError(error);
		obs_log(LOG_ERROR, "%s", error.toUtf8().constData());

		obs_output_signal_stop(output_, OBS_OUTPUT_DISCONNECTED);
	}

	obs_output_t *output_ = nullptr;
	RadioSettings settings_;
	std::unique_ptr<FfmpegPipe> pipe_;
	std::atomic<FfmpegPipe *> activePipe_{nullptr};
	mutable std::mutex pipeMutex_;
	std::atomic<bool> active_{false};
	mutable std::atomic<int> callbacksInFlight_{0};
	std::atomic<uint64_t> finalBytes_{0};
	std::atomic<int> finalDroppedFrames_{0};
};

const char *radio_output_name(void *typeData)
{
	UNUSED_PARAMETER(typeData);
	return "Radio Streamer Icecast";
}

void *radio_output_create(obs_data_t *settings, obs_output_t *output)
{
	return new RadioOutput(settings, output);
}

void radio_output_destroy(void *data)
{
	delete static_cast<RadioOutput *>(data);
}

bool radio_output_start(void *data)
{
	return static_cast<RadioOutput *>(data)->start();
}

void radio_output_stop(void *data, uint64_t ts)
{
	static_cast<RadioOutput *>(data)->stop(ts);
}

void radio_output_raw_audio(void *data, struct audio_data *frames)
{
	static_cast<RadioOutput *>(data)->rawAudio(frames);
}

void radio_output_update(void *data, obs_data_t *settings)
{
	static_cast<RadioOutput *>(data)->update(settings);
}

void radio_output_defaults(obs_data_t *settings)
{
	radio_settings_set_defaults(settings);
}

uint64_t radio_output_total_bytes(void *data)
{
	return static_cast<RadioOutput *>(data)->totalBytes();
}

int radio_output_dropped_frames(void *data)
{
	return static_cast<RadioOutput *>(data)->droppedFrames();
}

} // namespace

void register_radio_output()
{
	struct obs_output_info info = {};
	info.id = RADIO_OUTPUT_ID;
	info.flags = OBS_OUTPUT_AUDIO;
	info.get_name = radio_output_name;
	info.create = radio_output_create;
	info.destroy = radio_output_destroy;
	info.start = radio_output_start;
	info.stop = radio_output_stop;
	info.raw_audio = radio_output_raw_audio;
	info.update = radio_output_update;
	info.get_defaults = radio_output_defaults;
	info.get_total_bytes = radio_output_total_bytes;
	info.get_dropped_frames = radio_output_dropped_frames;

	obs_register_output(&info);
}
