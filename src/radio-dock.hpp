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

#include <QWidget>

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QUrl;
class QPushButton;
class QSpinBox;
class QTimer;

struct calldata;
typedef struct calldata calldata_t;
struct obs_output;
typedef struct obs_output obs_output_t;

class RadioDock : public QWidget {
	Q_OBJECT

public:
	explicit RadioDock(QWidget *parent = nullptr);
	~RadioDock() override;

private slots:
	void handleOutputStopped(int code);

private:
	void buildUi();
	void loadSettings();
	void saveSettings() const;
	RadioSettings settingsFromUi() const;
	void applySettingsToUi(const RadioSettings &settings);
	void startRadio(bool fromReconnect = false);
	void stopRadio();
	void scheduleReconnect(const QString &reason);
	void setRunningUi(bool running);
	void setStatus(const QString &status);
	void updateHealthLine();
	void updateStats();
	void pollIcecastHealth();
	void handleIcecastHealthFinished();
	void cancelIcecastHealthPoll();
	QUrl icecastStatusUrl() const;
	void releaseOutput();

	static void outputStopped(void *data, calldata_t *cd);

	QLineEdit *url_ = nullptr;
	QComboBox *codec_ = nullptr;
	QSpinBox *bitrate_ = nullptr;
	QComboBox *audioTrack_ = nullptr;
	QCheckBox *reconnect_ = nullptr;
	QPushButton *startButton_ = nullptr;
	QPushButton *stopButton_ = nullptr;
	QLabel *status_ = nullptr;
	QTimer *statsTimer_ = nullptr;
	QTimer *icecastHealthTimer_ = nullptr;
	QTimer *reconnectTimer_ = nullptr;
	QTimer *reconnectStableTimer_ = nullptr;
	QNetworkAccessManager *icecastHealthManager_ = nullptr;
	QNetworkReply *icecastHealthReply_ = nullptr;
	obs_output_t *output_ = nullptr;
	QString statusState_ = "Stopped";
	QString serverState_ = "server idle";
	int listenerCount_ = -1;
	int reconnectAttempt_ = 0;
	double sentMiB_ = 0.0;
	bool userStopRequested_ = false;
	bool shuttingDown_ = false;
};
