#pragma once

#include "channel_mode.h"

app_config::ChannelMode loadChannelModePreference();
void saveChannelModePreference(app_config::ChannelMode mode);
