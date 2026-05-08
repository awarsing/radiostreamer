/*
Radio Streamer
Copyright (C) 2026 Three Things Media

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "radio-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/config-file.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPalette>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr const char *CONFIG_SECTION = "OBSRadio";
constexpr const char *DOCK_OUTPUT_NAME = "Radio Streamer Icecast";
constexpr int RECONNECT_DELAY_SECONDS[] = {2, 5, 10, 20, 30};
constexpr int RECONNECT_STABLE_MS = 60000;
constexpr int ICECAST_HEALTH_INTERVAL_MS = 5000;
constexpr int ICECAST_HEALTH_TIMEOUT_SECONDS = 3;

QString config_string(config_t *config, const char *key, const QString &fallback)
{
	const char *value = config_get_string(config, CONFIG_SECTION, key);
	if (!value || !*value)
		return fallback;
	return QString::fromUtf8(value);
}

void config_set_qstring(config_t *config, const char *key, const QString &value)
{
	const QByteArray bytes = value.toUtf8();
	config_set_string(config, CONFIG_SECTION, key, bytes.constData());
}

int combo_index_for_data(QComboBox *combo, const QVariant &data, int fallback = 0)
{
	const int index = combo->findData(data);
	return index >= 0 ? index : fallback;
}

QLabel *control_label(const QString &text, QWidget *parent, QWidget *buddy = nullptr)
{
	auto *label = new QLabel(text, parent);
	label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	if (buddy)
		label->setBuddy(buddy);
	return label;
}

QLabel *muted_label(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	QFont labelFont = label->font();
	const qreal pointSize = labelFont.pointSizeF();
	if (pointSize > 10.0)
		labelFont.setPointSizeF(pointSize - 1.0);
	label->setFont(labelFont);

	QPalette palette = label->palette();
	palette.setColor(QPalette::WindowText, palette.color(QPalette::Disabled, QPalette::WindowText));
	label->setPalette(palette);
	return label;
}

QString legacy_url_from_config(config_t *config)
{
	const QString password = config_string(config, "Password", {});
	if (password.isEmpty())
		return {};

	QString mount = config_string(config, "Mount", "radio").trimmed();
	if (!mount.startsWith('/'))
		mount.prepend('/');

	QUrl url;
	url.setScheme("icecast");
	url.setHost(config_string(config, "Host", "localhost").trimmed());
	url.setPort(static_cast<int>(config_get_uint(config, CONFIG_SECTION, "Port")));
	url.setUserName(config_string(config, "Username", "source").trimmed());
	url.setPassword(password);
	url.setPath(mount);
	return url.toString(QUrl::FullyEncoded);
}

QString output_stop_message(int code)
{
	switch (code) {
	case OBS_OUTPUT_SUCCESS:
		return "Stopped";
	case OBS_OUTPUT_CONNECT_FAILED:
		return "Connection failed";
	case OBS_OUTPUT_INVALID_STREAM:
		return "Invalid Icecast mount or credentials";
	case OBS_OUTPUT_DISCONNECTED:
		return "Disconnected";
	case OBS_OUTPUT_UNSUPPORTED:
		return "Unsupported settings";
	default:
		return QString("Stopped with OBS output code %1").arg(code);
	}
}

int reconnect_delay_seconds(int attempt)
{
	constexpr int delayCount = sizeof(RECONNECT_DELAY_SECONDS) / sizeof(RECONNECT_DELAY_SECONDS[0]);
	if (attempt < 1)
		return RECONNECT_DELAY_SECONDS[0];

	const int index = attempt - 1 < delayCount ? attempt - 1 : delayCount - 1;
	return RECONNECT_DELAY_SECONDS[index];
}

int json_int_value(const QJsonValue &value, int fallback = -1)
{
	if (value.isDouble())
		return value.toInt(fallback);
	if (value.isString()) {
		bool ok = false;
		const int parsed = value.toString().toInt(&ok);
		return ok ? parsed : fallback;
	}
	return fallback;
}

bool source_matches_mount(const QJsonObject &source, const QString &mountPath)
{
	if (source.value("mount").toString() == mountPath)
		return true;

	const QUrl listenUrl(source.value("listenurl").toString());
	return listenUrl.isValid() && listenUrl.path() == mountPath;
}

QJsonObject source_for_mount(const QJsonValue &sourceValue, const QString &mountPath)
{
	if (sourceValue.isObject()) {
		const QJsonObject source = sourceValue.toObject();
		if (source_matches_mount(source, mountPath) || !source.contains("listenurl"))
			return source;
	}

	if (!sourceValue.isArray())
		return {};

	const QJsonArray sources = sourceValue.toArray();
	for (const QJsonValue value : sources) {
		if (!value.isObject())
			continue;

		const QJsonObject source = value.toObject();
		if (source_matches_mount(source, mountPath))
			return source;
	}

	return {};
}

} // namespace

RadioDock::RadioDock(QWidget *parent) : QWidget(parent)
{
	buildUi();
	loadSettings();
	setRunningUi(false);

	statsTimer_ = new QTimer(this);
	connect(statsTimer_, &QTimer::timeout, this, [this] { updateStats(); });
	statsTimer_->start(1000);

	icecastHealthTimer_ = new QTimer(this);
	icecastHealthTimer_->setInterval(ICECAST_HEALTH_INTERVAL_MS);
	connect(icecastHealthTimer_, &QTimer::timeout, this, [this] { pollIcecastHealth(); });

	reconnectTimer_ = new QTimer(this);
	reconnectTimer_->setSingleShot(true);
	connect(reconnectTimer_, &QTimer::timeout, this, [this] { startRadio(true); });

	reconnectStableTimer_ = new QTimer(this);
	reconnectStableTimer_->setSingleShot(true);
	connect(reconnectStableTimer_, &QTimer::timeout, this, [this] {
		if (output_ && obs_output_active(output_))
			reconnectAttempt_ = 0;
	});
}

RadioDock::~RadioDock()
{
	shuttingDown_ = true;
	if (reconnectTimer_)
		reconnectTimer_->stop();
	if (reconnectStableTimer_)
		reconnectStableTimer_->stop();
	if (icecastHealthTimer_)
		icecastHealthTimer_->stop();
	cancelIcecastHealthPoll();

	if (output_) {
		signal_handler_t *handler = obs_output_get_signal_handler(output_);
		signal_handler_disconnect(handler, "stop", outputStopped, this);
		obs_output_force_stop(output_);
		obs_output_release(output_);
		output_ = nullptr;
	}
}

void RadioDock::buildUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);
	root->setSpacing(4);

	auto *urlRow = new QHBoxLayout();
	urlRow->setContentsMargins(0, 0, 0, 0);
	urlRow->setSpacing(6);

	url_ = new QLineEdit(this);
	url_->setPlaceholderText("icecast://source:password@server:8000/mountpoint");
	url_->setClearButtonEnabled(true);
	urlRow->addWidget(control_label("URL", this, url_));
	urlRow->addWidget(url_, 1);

	startButton_ = new QPushButton("Start", this);
	stopButton_ = new QPushButton("Stop", this);
	startButton_->setMinimumWidth(86);
	stopButton_->setMinimumWidth(74);
	startButton_->setToolTip("Start radio output.");
	stopButton_->setToolTip("Stop radio output.");
	urlRow->addWidget(startButton_);
	urlRow->addWidget(stopButton_);
	root->addLayout(urlRow);

	auto *controls = new QHBoxLayout();
	controls->setContentsMargins(0, 0, 0, 0);
	controls->setSpacing(6);

	auto *urlHint = muted_label("format: icecast://source:password@server:port/mountpoint | encoder: FFmpeg", this);
	urlHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
	urlHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

	controls->addWidget(urlHint);
	controls->addStretch(1);

	codec_ = new QComboBox(this);
	codec_->addItem("MP3", "mp3");
	codec_->addItem("AAC", "aac");
	codec_->addItem("Opus", "opus");
	codec_->setMinimumWidth(86);
	codec_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	controls->addWidget(control_label("Codec", this, codec_));
	controls->addWidget(codec_);

	bitrate_ = new QSpinBox(this);
	bitrate_->setRange(32, 320);
	bitrate_->setSingleStep(8);
	bitrate_->setSuffix(" kbps");
	bitrate_->setMinimumWidth(108);
	bitrate_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	controls->addWidget(control_label("Bitrate", this, bitrate_));
	controls->addWidget(bitrate_);

	audioTrack_ = new QComboBox(this);
	for (int i = 1; i <= 6; ++i)
		audioTrack_->addItem(QString("Track %1").arg(i), i);
	audioTrack_->setMinimumWidth(100);
	audioTrack_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	controls->addWidget(control_label("Track", this, audioTrack_));
	controls->addWidget(audioTrack_);

	reconnect_ = new QCheckBox("Reconnect", this);
	reconnect_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	reconnect_->setToolTip("Retry automatically if the Icecast connection drops.");
	controls->addWidget(reconnect_);

	root->addLayout(controls);

	status_ = new QLabel(this);
	status_->setText("Stopped | - listeners | 0.0 MiB sent | server idle");
	status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	status_->setWordWrap(true);
	status_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	root->addWidget(status_);
	root->addStretch();

	connect(startButton_, &QPushButton::clicked, this, [this] { startRadio(); });
	connect(stopButton_, &QPushButton::clicked, this, [this] { stopRadio(); });
	connect(reconnect_, &QCheckBox::toggled, this, [this](bool enabled) {
		if (enabled || !reconnectTimer_ || !reconnectTimer_->isActive())
			return;

		reconnectTimer_->stop();
		reconnectAttempt_ = 0;
		setStatus("Reconnect cancelled.");
	});
}

void RadioDock::loadSettings()
{
	config_t *config = obs_frontend_get_profile_config();
	config_set_default_string(config, CONFIG_SECTION, "Url", "");
	config_set_default_string(config, CONFIG_SECTION, "Codec", "mp3");
	config_set_default_uint(config, CONFIG_SECTION, "BitrateKbps", 128);
	config_set_default_uint(config, CONFIG_SECTION, "AudioTrack", 1);
	config_set_default_bool(config, CONFIG_SECTION, "Reconnect", true);

	RadioSettings settings;
	settings.icecastUrl = config_string(config, "Url", {});
	if (settings.icecastUrl.isEmpty())
		settings.icecastUrl = legacy_url_from_config(config);
	settings.codec = radio_codec_from_id(config_string(config, "Codec", "mp3"));
	settings.bitrateKbps = static_cast<int>(config_get_uint(config, CONFIG_SECTION, "BitrateKbps"));
	settings.audioTrack = static_cast<int>(config_get_uint(config, CONFIG_SECTION, "AudioTrack"));
	settings.reconnectEnabled = config_get_bool(config, CONFIG_SECTION, "Reconnect");

	applySettingsToUi(settings);
}

void RadioDock::saveSettings() const
{
	config_t *config = obs_frontend_get_profile_config();
	const RadioSettings settings = settingsFromUi();

	config_set_qstring(config, "Url", settings.icecastUrl);
	config_set_qstring(config, "Codec", radio_codec_id(settings.codec));
	config_set_uint(config, CONFIG_SECTION, "BitrateKbps", static_cast<uint64_t>(settings.bitrateKbps));
	config_set_uint(config, CONFIG_SECTION, "AudioTrack", static_cast<uint64_t>(settings.audioTrack));
	config_set_bool(config, CONFIG_SECTION, "Reconnect", settings.reconnectEnabled);

	config_save_safe(config, "tmp", nullptr);
}

RadioSettings RadioDock::settingsFromUi() const
{
	RadioSettings settings;
	settings.icecastUrl = url_->text().trimmed();
	settings.codec = radio_codec_from_id(codec_->currentData().toString());
	settings.bitrateKbps = bitrate_->value();
	settings.audioTrack = audioTrack_->currentData().toInt();
	settings.reconnectEnabled = reconnect_->isChecked();
	return settings;
}

void RadioDock::applySettingsToUi(const RadioSettings &settings)
{
	url_->setText(settings.icecastUrl);
	codec_->setCurrentIndex(combo_index_for_data(codec_, radio_codec_id(settings.codec)));
	bitrate_->setValue(settings.bitrateKbps);
	audioTrack_->setCurrentIndex(combo_index_for_data(audioTrack_, settings.audioTrack));
	reconnect_->setChecked(settings.reconnectEnabled);
}

void RadioDock::startRadio(bool fromReconnect)
{
	if (output_)
		return;

	if (!fromReconnect) {
		if (reconnectTimer_)
			reconnectTimer_->stop();
		reconnectAttempt_ = 0;
	}
	userStopRequested_ = false;

	const RadioSettings settings = settingsFromUi();
	const QString validationError = radio_settings_validate(settings);
	if (!validationError.isEmpty()) {
		setStatus("Error");
		status_->setToolTip(validationError);
		return;
	}

	saveSettings();
	sentMiB_ = 0.0;
	listenerCount_ = -1;
	serverState_ = "server checking";
	setStatus("Starting");

	obs_data_t *data = radio_settings_to_data(settings);
	output_ = obs_output_create(RADIO_OUTPUT_ID, DOCK_OUTPUT_NAME, data, nullptr);
	obs_data_release(data);

	if (!output_) {
		setStatus("Error");
		status_->setToolTip("Failed to create OBS radio output.");
		return;
	}

	signal_handler_t *handler = obs_output_get_signal_handler(output_);
	signal_handler_connect(handler, "stop", outputStopped, this);

	if (!obs_output_start(output_)) {
		const char *error = obs_output_get_last_error(output_);
		setStatus("Error");
		status_->setToolTip(error && *error ? QString::fromUtf8(error)
						    : QString("Failed to start radio output."));
		serverState_ = "server idle";
		updateHealthLine();
		releaseOutput();
		return;
	}

	setRunningUi(true);
	if (reconnectStableTimer_)
		reconnectStableTimer_->start(RECONNECT_STABLE_MS);
	setStatus("Live");
	status_->setToolTip(
		QString("Streaming %1 to %2")
			.arg(radio_codec_label(settings.codec), radio_settings_icecast_url(settings, false)));
	if (icecastHealthTimer_)
		icecastHealthTimer_->start();
	pollIcecastHealth();
}

void RadioDock::stopRadio()
{
	if (!output_)
		return;

	userStopRequested_ = true;
	if (reconnectTimer_)
		reconnectTimer_->stop();
	if (reconnectStableTimer_)
		reconnectStableTimer_->stop();
	if (icecastHealthTimer_)
		icecastHealthTimer_->stop();
	cancelIcecastHealthPoll();

	serverState_ = "server idle";
	listenerCount_ = -1;
	setStatus("Stopping");
	stopButton_->setEnabled(false);
	obs_output_stop(output_);
}

void RadioDock::scheduleReconnect(const QString &reason)
{
	if (shuttingDown_ || !reconnect_ || !reconnect_->isChecked() || !reconnectTimer_)
		return;

	if (reconnectStableTimer_)
		reconnectStableTimer_->stop();

	const int delaySeconds = reconnect_delay_seconds(++reconnectAttempt_);
	serverState_ = "server retrying";
	listenerCount_ = -1;
	setStatus(QString("Retrying in %1s").arg(delaySeconds));
	status_->setToolTip(reason);
	reconnectTimer_->start(delaySeconds * 1000);
}

void RadioDock::setRunningUi(bool running)
{
	startButton_->setEnabled(!running);
	stopButton_->setEnabled(running);

	const QList<QWidget *> fields = {
		url_, codec_, bitrate_, audioTrack_, reconnect_,
	};

	for (QWidget *field : fields)
		field->setEnabled(!running);

	if (!running)
		updateHealthLine();
}

void RadioDock::setStatus(const QString &status)
{
	statusState_ = status;
	if (status_)
		status_->setToolTip(status);
	updateHealthLine();
}

void RadioDock::updateHealthLine()
{
	if (!status_)
		return;

	const QString listenerText =
		listenerCount_ >= 0 ? QString("%1 listener%2").arg(listenerCount_).arg(listenerCount_ == 1 ? "" : "s")
				    : QString("- listeners");
	status_->setText(QString("%1 | %2 | %3 MiB sent | %4")
				 .arg(statusState_, listenerText, QString::number(sentMiB_, 'f', 1), serverState_));
}

void RadioDock::updateStats()
{
	if (output_ && obs_output_active(output_)) {
		const uint64_t bytes = obs_output_get_total_bytes(output_);
		sentMiB_ = static_cast<double>(bytes) / 1024.0 / 1024.0;
	}

	updateHealthLine();
}

void RadioDock::pollIcecastHealth()
{
	if (!output_ || !obs_output_active(output_) || icecastHealthReply_)
		return;

	const QUrl statusUrl = icecastStatusUrl();
	if (!statusUrl.isValid()) {
		serverState_ = "server unknown";
		updateHealthLine();
		return;
	}

	if (!icecastHealthManager_)
		icecastHealthManager_ = new QNetworkAccessManager(this);

	QNetworkRequest request(statusUrl);
	request.setTransferTimeout(ICECAST_HEALTH_TIMEOUT_SECONDS * 1000);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

	icecastHealthReply_ = icecastHealthManager_->get(request);
	connect(icecastHealthReply_, &QNetworkReply::finished, this, [this] { handleIcecastHealthFinished(); });
}

void RadioDock::handleIcecastHealthFinished()
{
	QNetworkReply *reply = icecastHealthReply_;
	if (!reply)
		return;

	icecastHealthReply_ = nullptr;

	const QByteArray output = reply->readAll();
	const QString errorText = reply->errorString().trimmed();
	const bool ok = reply->error() == QNetworkReply::NoError;
	reply->deleteLater();

	if (!ok) {
		serverState_ = "server unavailable";
		listenerCount_ = -1;
		if (!errorText.isEmpty())
			status_->setToolTip(errorText);
		updateHealthLine();
		return;
	}

	QJsonParseError parseError = {};
	const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);

	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		serverState_ = "server unreadable";
		listenerCount_ = -1;
		updateHealthLine();
		return;
	}

	const QUrl sourceUrl(url_->text().trimmed());
	const QString mountPath = sourceUrl.path();
	const QJsonObject icestats = document.object().value("icestats").toObject();
	const QJsonObject source = source_for_mount(icestats.value("source"), mountPath);
	if (source.isEmpty()) {
		serverState_ = "server no mount";
		listenerCount_ = -1;
		updateHealthLine();
		return;
	}

	listenerCount_ = json_int_value(source.value("listeners"));
	serverState_ = "server OK";
	updateHealthLine();
}

void RadioDock::cancelIcecastHealthPoll()
{
	if (!icecastHealthReply_)
		return;

	QNetworkReply *reply = icecastHealthReply_;
	icecastHealthReply_ = nullptr;
	reply->disconnect(this);
	reply->abort();
	reply->deleteLater();
}

QUrl RadioDock::icecastStatusUrl() const
{
	const QUrl sourceUrl(url_->text().trimmed());
	if (!sourceUrl.isValid() || sourceUrl.host().isEmpty())
		return {};

	QUrl statusUrl;
	const QString sourceScheme = sourceUrl.scheme().toLower();
	const bool useTlsStatus = sourceScheme == "https" || sourceScheme == "icecasts" || sourceUrl.port() == 443;
	statusUrl.setScheme(useTlsStatus ? "https" : "http");
	statusUrl.setHost(sourceUrl.host());
	if (sourceUrl.port() > 0)
		statusUrl.setPort(sourceUrl.port());
	statusUrl.setPath("/status-json.xsl");
	return statusUrl;
}

void RadioDock::releaseOutput()
{
	if (!output_)
		return;

	signal_handler_t *handler = obs_output_get_signal_handler(output_);
	signal_handler_disconnect(handler, "stop", outputStopped, this);
	obs_output_release(output_);
	output_ = nullptr;
}

void RadioDock::handleOutputStopped(int code)
{
	if (reconnectStableTimer_)
		reconnectStableTimer_->stop();
	if (icecastHealthTimer_)
		icecastHealthTimer_->stop();
	cancelIcecastHealthPoll();

	setRunningUi(false);

	QString message = output_stop_message(code);
	if (output_) {
		const uint64_t bytes = obs_output_get_total_bytes(output_);
		sentMiB_ = static_cast<double>(bytes) / 1024.0 / 1024.0;
		const char *error = obs_output_get_last_error(output_);
		if (error && *error)
			message = QString::fromUtf8(error);
	}

	releaseOutput();

	const bool shouldReconnect = !userStopRequested_ && code != OBS_OUTPUT_SUCCESS && reconnect_ &&
				     reconnect_->isChecked();
	if (shouldReconnect) {
		scheduleReconnect(message);
	} else {
		serverState_ = "server idle";
		listenerCount_ = -1;
		setStatus(message);
		reconnectAttempt_ = 0;
	}
}

void RadioDock::outputStopped(void *data, calldata_t *cd)
{
	auto *dock = static_cast<RadioDock *>(data);
	const int code = static_cast<int>(calldata_int(cd, "code"));
	QMetaObject::invokeMethod(dock, "handleOutputStopped", Qt::QueuedConnection, Q_ARG(int, code));
}
