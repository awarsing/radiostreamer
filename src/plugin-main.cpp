/*
Radio Streamer
Copyright (C) 2026 Three Things Media

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "radio-dock.hpp"
#include "radio-output.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QDockWidget>
#include <QPointer>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *RADIO_DOCK_ID = "radiostreamer-dock";
QPointer<RadioDock> radioDock;
bool radioDockRegistered = false;

void destroyRadioDock()
{
	if (!radioDock)
		return;

	RadioDock *dockWidget = radioDock.data();
	if (auto *dock = qobject_cast<QDockWidget *>(dockWidget->parentWidget()))
		dock->setWidget(nullptr);

	delete dockWidget;
	radioDock = nullptr;
}

} // namespace

bool obs_module_load(void)
{
	register_radio_output();

	radioDock = new RadioDock();
	radioDockRegistered = obs_frontend_add_dock_by_id(RADIO_DOCK_ID, obs_module_text("Dock.Title"), radioDock);
	if (!radioDockRegistered) {
		obs_log(LOG_WARNING, "failed to add radio dock");
		destroyRadioDock();
	}

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	if (radioDockRegistered)
		obs_frontend_remove_dock(RADIO_DOCK_ID);

	destroyRadioDock();
	radioDockRegistered = false;
	obs_log(LOG_INFO, "plugin unloaded");
}
