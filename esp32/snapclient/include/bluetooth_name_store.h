#pragma once

#include <Arduino.h>

String loadBluetoothNamePreference();
void saveBluetoothNamePreference(const String &name);
bool isValidBluetoothName(const String &name);
