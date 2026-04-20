#pragma once

#include "snapclient_config.h"

app_config::OperatingMode detectOperatingMode();
void requestOperatingModeOnNextRestart(app_config::OperatingMode mode);
