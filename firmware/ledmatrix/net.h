// WiFi (STA with captive-portal AP fallback), HTTP API, embedded web, mDNS and NTP.
#pragma once
#include "config.h"

void netBegin();
void netLoop();
bool netConnected();
bool netApMode();
String netIp();
String netApSsid();
bool netRestartRequested();
void netSaveWifi(const String &ssid, const String &pass);
void netForgetWifi();
String netStatusLine();
