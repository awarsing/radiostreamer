/*
Radio Streamer
Copyright (C) 2026 Three Things Media

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "ffmpeg-pipe.hpp"

#include <QProcess>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace {

constexpr size_t MAX_QUEUE_SECONDS = 5;
constexpr size_t WORKER_READ_BYTES = 4096;
constexpr int START_TIMEOUT_MS = 5000;
constexpr int WRITE_TIMEOUT_MS = 5000;
constexpr int STOP_TIMEOUT_MS = 3000;
constexpr int RECENT_OUTPUT_LIMIT = 4096;

QString process_error(const QProcess &process)
{
	const QString errorText = process.errorString();
	return errorText.isEmpty() ? "Unknown FFmpeg process error." : errorText;
}

} // namespace

FfmpegPipe::FfmpegPipe(RadioSettings settings) : settings_(std::move(settings))
{
	maxQueuedBytes_ = static_cast<size_t>(settings_.sampleRate) * 2U * sizeof(int16_t) * MAX_QUEUE_SECONDS;
	audioBuffer_.resize(maxQueuedBytes_);
}

FfmpegPipe::~FfmpegPipe()
{
	stop();
}

bool FfmpegPipe::start(QString *error)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopRequested_ = false;
		stopRequestedForAudio_.store(false);
		startComplete_ = false;
		startOk_ = false;
		startError_.clear();
		lastError_.clear();
		recentOutput_.clear();
		audioRead_.store(0);
		audioWrite_.store(0);
	}

	worker_ = std::thread(&FfmpegPipe::run, this);

	std::unique_lock<std::mutex> lock(mutex_);
	cv_.wait(lock, [this] { return startComplete_; });

	if (!startOk_) {
		if (error)
			*error = startError_;
		lock.unlock();
		if (worker_.joinable())
			worker_.join();
		return false;
	}

	return true;
}

void FfmpegPipe::stop()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopRequested_ = true;
		stopRequestedForAudio_.store(true);
	}
	cv_.notify_all();

	if (worker_.joinable())
		worker_.join();
}

bool FfmpegPipe::enqueue(const uint8_t *data, size_t size, uint32_t frames)
{
	if (!data || size == 0 || failed_.load())
		return false;

	if (stopRequestedForAudio_.load() || !writeAudioBuffer(data, size)) {
		droppedFrames_.fetch_add(static_cast<int>(frames));
		return false;
	}

	totalBytes_.fetch_add(size);
	cv_.notify_one();
	return true;
}

bool FfmpegPipe::failed() const
{
	return failed_.load();
}

QString FfmpegPipe::lastError() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return lastError_;
}

uint64_t FfmpegPipe::totalBytes() const
{
	return totalBytes_.load();
}

int FfmpegPipe::droppedFrames() const
{
	return droppedFrames_.load();
}

void FfmpegPipe::run()
{
	QProcess process;
	process.setProgram(radio_resolved_ffmpeg_path());
	process.setArguments(radio_settings_ffmpeg_args(settings_));
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.start(QIODevice::ReadWrite);

	if (!process.waitForStarted(START_TIMEOUT_MS)) {
		setStartResult(false, QString("Failed to start FFmpeg: %1").arg(process_error(process)));
		return;
	}

	setStartResult(true, {});

	std::vector<char> chunk(std::min(WORKER_READ_BYTES, std::max<size_t>(maxQueuedBytes_, 1)));

	while (true) {
		size_t available = audioBytesAvailable();

		if (available == 0) {
			std::unique_lock<std::mutex> lock(mutex_);
			cv_.wait(lock, [this] { return stopRequested_ || audioBytesAvailable() > 0; });

			available = audioBytesAvailable();
			if (available == 0 && stopRequested_)
				break;

			if (available == 0)
				continue;
		}

		const size_t chunkSize = readAudioBuffer(chunk.data(), std::min(chunk.size(), available));
		if (chunkSize == 0)
			continue;

		if (process.state() != QProcess::Running) {
			setFailure("FFmpeg exited before the radio stream was stopped.");
			break;
		}

		writeToProcess(process, chunk.data(), chunkSize, "write audio to FFmpeg");

		appendProcessOutput(process.readAll());

		if (failed_.load())
			break;
	}

	if (process.state() == QProcess::Running) {
		process.closeWriteChannel();
		if (!process.waitForFinished(STOP_TIMEOUT_MS)) {
			process.terminate();
			if (!process.waitForFinished(STOP_TIMEOUT_MS))
				process.kill();
		}
	}

	appendProcessOutput(process.readAll());

	const bool stoppedByUser = [this] {
		std::lock_guard<std::mutex> lock(mutex_);
		return stopRequested_;
	}();

	if (!stoppedByUser && !failed_.load()) {
		setFailure(QString("FFmpeg exited unexpectedly with code %1.").arg(process.exitCode()));
	}
}

size_t FfmpegPipe::audioBytesAvailable() const
{
	const size_t read = audioRead_.load(std::memory_order_relaxed);
	const size_t write = audioWrite_.load(std::memory_order_acquire);
	return write - read;
}

// The OBS audio callback is the only producer and the FFmpeg worker is the only consumer.
bool FfmpegPipe::writeAudioBuffer(const uint8_t *data, size_t size)
{
	const size_t capacity = audioBuffer_.size();
	if (capacity == 0 || size > capacity)
		return false;

	const size_t read = audioRead_.load(std::memory_order_acquire);
	const size_t write = audioWrite_.load(std::memory_order_relaxed);
	if (size > capacity - (write - read))
		return false;

	const size_t index = write % capacity;
	const size_t first = std::min(size, capacity - index);
	std::memcpy(audioBuffer_.data() + index, data, first);
	if (first < size)
		std::memcpy(audioBuffer_.data(), data + first, size - first);

	audioWrite_.store(write + size, std::memory_order_release);
	return true;
}

size_t FfmpegPipe::readAudioBuffer(char *data, size_t maxSize)
{
	const size_t capacity = audioBuffer_.size();
	if (capacity == 0 || maxSize == 0)
		return 0;

	const size_t read = audioRead_.load(std::memory_order_relaxed);
	const size_t write = audioWrite_.load(std::memory_order_acquire);
	const size_t size = std::min(maxSize, write - read);
	if (size == 0)
		return 0;

	const size_t index = read % capacity;
	const size_t first = std::min(size, capacity - index);
	std::memcpy(data, audioBuffer_.data() + index, first);
	if (first < size)
		std::memcpy(data + first, audioBuffer_.data(), size - first);

	audioRead_.store(read + size, std::memory_order_release);
	return size;
}

bool FfmpegPipe::writeToProcess(QProcess &process, const char *data, size_t size, const QString &failurePrefix)
{
	const char *cursor = data;
	qsizetype remaining = static_cast<qsizetype>(size);

	while (remaining > 0) {
		const qint64 written = process.write(cursor, remaining);
		if (written <= 0) {
			setFailure(QString("Failed to %1: %2").arg(failurePrefix, process_error(process)));
			return false;
		}

		cursor += written;
		remaining -= written;

		if (!process.waitForBytesWritten(WRITE_TIMEOUT_MS)) {
			setFailure(QString("Timed out trying to %1: %2").arg(failurePrefix, process_error(process)));
			return false;
		}

		appendProcessOutput(process.readAll());
	}

	return true;
}

void FfmpegPipe::setStartResult(bool ok, const QString &error)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		startOk_ = ok;
		startError_ = error;
		startComplete_ = true;
		if (!ok) {
			failed_.store(true);
			lastError_ = error;
		}
	}
	cv_.notify_all();
}

void FfmpegPipe::setFailure(const QString &error)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!lastError_.isEmpty())
			return;

		QString message = radio_redact_credentials(error);
		if (!recentOutput_.isEmpty())
			message += QString(" FFmpeg output: %1")
					   .arg(radio_redact_credentials(QString::fromUtf8(recentOutput_)));
		lastError_ = message;
		failed_.store(true);
	}
	cv_.notify_all();
}

void FfmpegPipe::appendProcessOutput(const QByteArray &output)
{
	if (output.isEmpty())
		return;

	std::lock_guard<std::mutex> lock(mutex_);
	recentOutput_.append(output);
	if (recentOutput_.size() > RECENT_OUTPUT_LIMIT)
		recentOutput_ = recentOutput_.right(RECENT_OUTPUT_LIMIT);
	recentOutput_ = radio_redact_credentials(QString::fromUtf8(recentOutput_)).toUtf8();
}
