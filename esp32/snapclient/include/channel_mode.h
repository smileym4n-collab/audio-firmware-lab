#pragma once

#include <Arduino.h>

namespace app_config {

enum class ChannelMode : uint8_t { Stereo, Left, Right };

inline const char *channelModeName(ChannelMode mode) {
  switch (mode) {
    case ChannelMode::Stereo:
      return "stereo";
    case ChannelMode::Left:
      return "left";
    case ChannelMode::Right:
      return "right";
    default:
      return "stereo";
  }
}

inline bool parseChannelMode(const String &value, ChannelMode &mode) {
  if (value == "stereo") {
    mode = ChannelMode::Stereo;
    return true;
  }
  if (value == "left") {
    mode = ChannelMode::Left;
    return true;
  }
  if (value == "right") {
    mode = ChannelMode::Right;
    return true;
  }
  return false;
}

}  // namespace app_config
