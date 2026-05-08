/*
Radio Streamer
Copyright (C) 2026 Three Things Media

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "radio-settings.hpp"

#include <QByteArray>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

class QProcess;

class FfmpegPipe {
public:
	explicit FfmpegPipe(RadioSettings settings);
	~FfmpegPipe();

	FfmpegPipe(const FfmpegPipe &) = delete;
	FfmpegPipe &operator=(const FfmpegPipe &) = delete;

	bool start(QString *error);
	void stop();
	bool enqueue(const uint8_t *data, size_t size, uint32_t frames);

	bool failed() const;
	QString lastError() const;
	uint64_t totalBytes() const;
	int droppedFrames() const;

private:
	void run();
	size_t audioBytesAvailable() const;
	bool writeAudioBuffer(const uint8_t *data, size_t size);
	size_t readAudioBuffer(char *data, size_t maxSize);
	bool writeToProcess(QProcess &process, const char *data, size_t size, const QString &failurePrefix);
	void setStartResult(bool ok, const QString &error);
	void setFailure(const QString &error);
	void appendProcessOutput(const QByteArray &output);

	RadioSettings settings_;
	std::thread worker_;

	std::vector<uint8_t> audioBuffer_;
	std::atomic<size_t> audioRead_{0};
	std::atomic<size_t> audioWrite_{0};
	std::atomic<bool> stopRequestedForAudio_{false};

	mutable std::mutex mutex_;
	std::condition_variable cv_;
	size_t maxQueuedBytes_ = 0;
	bool stopRequested_ = false;
	bool startComplete_ = false;
	bool startOk_ = false;
	QString startError_;
	QString lastError_;
	QByteArray recentOutput_;

	std::atomic<bool> failed_{false};
	std::atomic<uint64_t> totalBytes_{0};
	std::atomic<int> droppedFrames_{0};
};
