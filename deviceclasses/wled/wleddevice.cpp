//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Michael Troß <digitalstrom@tross.org>
//
//  This file is part of p44vdc.
//
//  p44vdc is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44vdc is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44vdc. If not, see <http://www.gnu.org/licenses/>.
//

#include "wleddevice.hpp"
#include "wledvdc.hpp"

#if ENABLE_WLED

#include "math.h"
#include "mainloop.hpp"
#include "outputbehaviour.hpp"

using namespace p44;


// MARK: - Color conversion utilities

void WledDevice::rgbToHsv(uint8_t aRed, uint8_t aGreen, uint8_t aBlue,
                          double &aHue, double &aSaturation, double &aValue)
{
  double r = aRed / 255.0;
  double g = aGreen / 255.0;
  double b = aBlue / 255.0;

  double maxC = max({r, g, b});
  double minC = min({r, g, b});
  double delta = maxC - minC;

  // Value (brightness)
  aValue = maxC;

  // Saturation
  if (maxC != 0.0) {
    aSaturation = (delta / maxC) * 100.0;
  } else {
    aSaturation = 0.0;
  }

  // Hue
  if (delta == 0.0) {
    aHue = 0.0;
  } else if (maxC == r) {
    aHue = fmod((g - b) / delta, 6.0) * 60.0;
    if (aHue < 0.0) aHue += 360.0;
  } else if (maxC == g) {
    aHue = ((b - r) / delta + 2.0) * 60.0;
  } else {
    aHue = ((r - g) / delta + 4.0) * 60.0;
  }
}


void WledDevice::hsvToRgb(double aHue, double aSaturation, double aValue,
                          uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue)
{
  double s = aSaturation / 100.0;
  double v = aValue;
  double c = v * s;
  double h = aHue / 60.0;
  double x = c * (1.0 - fabs(fmod(h, 2.0) - 1.0));
  double m = v - c;

  double r, g, b;

  if (h < 1.0) {
    r = c; g = x; b = 0;
  } else if (h < 2.0) {
    r = x; g = c; b = 0;
  } else if (h < 3.0) {
    r = 0; g = c; b = x;
  } else if (h < 4.0) {
    r = 0; g = x; b = c;
  } else if (h < 5.0) {
    r = x; g = 0; b = c;
  } else {
    r = c; g = 0; b = x;
  }

  aRed = (uint8_t)((r + m) * 255.0);
  aGreen = (uint8_t)((g + m) * 255.0);
  aBlue = (uint8_t)((b + m) * 255.0);
}


// MARK: - WledDevice

WledDevice::WledDevice(WledVdc *aVdcP, JsonObjectPtr aDeviceInfo) :
  inherited(aVdcP),
  mVdc(*aVdcP),
  mHasRgb(false),
  mHasRgbw(false),
  mHasCct(false),
  mLedCount(0),
  mSettingState(false)
{
  LOG(LOG_DEBUG, "Creating WLED device: %s", aDeviceInfo->json_str().c_str());

  // Get device info from JSON
  if (aDeviceInfo) {
    JsonObjectPtr nameObj = aDeviceInfo->get("name");
    if (nameObj) {
      mDeviceName = nameObj->stringValue();
    }
    
    JsonObjectPtr macObj = aDeviceInfo->get("deviceId");
    if (macObj) {
      mUniqueId = macObj->stringValue();
    }
    
    JsonObjectPtr verObj = aDeviceInfo->get("ver");
    if (verObj) {
      mSwVersion = verObj->stringValue();
    }
    
    JsonObjectPtr archObj = aDeviceInfo->get("arch");
    if (archObj) {
      mHwVersion = archObj->stringValue();
    }

    JsonObjectPtr ledInfo = aDeviceInfo->get("leds");
    if (ledInfo) {
      JsonObjectPtr countObj = ledInfo->get("count");
      if (countObj) {
        mLedCount = countObj->int32Value();
      }

      // Determine capabilities from light capability info
      // leds.lc contains bitwise AND of segment capabilities
      // Bit 0: RGB, Bit 1: White channel, Bit 2: CCT
      JsonObjectPtr lcObj = ledInfo->get("lc");
      int lc = lcObj ? lcObj->int32Value() : 7; // Default to RGB | W | CCT
      mHasRgb = (lc & 1) != 0;
      mHasRgbw = (lc & 2) != 0;
      mHasCct = (lc & 4) != 0;
    }
  }

  // Generate unique device identification
  string uniqueIdForDsuid = mUniqueId;
  if (uniqueIdForDsuid.empty()) {
    uniqueIdForDsuid = string_format("wled_%d", aVdcP->getInstanceNumber());
  }

  // Derive dSUID using vdcNamespace UUID
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  mDSUID.setNameInSpace(uniqueIdForDsuid, vdcNamespace);

  setColorClass(class_yellow_light);

  // - use color light settings, which include a color scene table
  installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
  
  // Initialize behaviors based on capabilities
  initializeBehaviors(aDeviceInfo);

  LOG(LOG_INFO, "WLED device created: %s (RGB:%d, RGBW:%d, CCT:%d, LEDs:%d)",
    mDeviceName.c_str(), mHasRgb, mHasRgbw, mHasCct, mLedCount);
}


WledDevice::~WledDevice()
{
}


void WledDevice::setLogLevelOffset(int aLogLevelOffset)
{
  inherited::setLogLevelOffset(aLogLevelOffset);
}


string WledDevice::description()
{
  return string_format("WLED light: %s", mDeviceName.c_str());
}


void WledDevice::initializeBehaviors(JsonObjectPtr aDeviceInfo)
{
  // Create color light behavior if device supports color
  if (!(mHasRgb || mHasRgbw || mHasCct)) {
    LightBehaviourPtr light = new LightBehaviour(*this);
    light->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, true, 10.0);
    light->setHardwareName("wled light");
    light->initMinBrightness(DS_BRIGHTNESS_STEP);
    addBehaviour(light);
    mColorLightBehaviour = light;
  } else {
    ColorLightBehaviourPtr cl = ColorLightBehaviourPtr(new ColorLightBehaviour(*this, mHasCct));
    cl->setHardwareOutputConfig(outputFunction_colordimmer, outputmode_gradual, usage_undefined, true, 8.5);
    cl->setHardwareName(string_format("wled %s light #%s", "color", mDeviceName.c_str()));
    cl->initMinBrightness(DS_BRIGHTNESS_STEP); // min brightness
    addBehaviour(cl);
    mColorLightBehaviour = cl;
  }
}


void WledDevice::updateInfo(JsonObjectPtr aDeviceInfo)
{
  if (!aDeviceInfo) return;

  JsonObjectPtr nameObj = aDeviceInfo->get("name");
  if (nameObj) {
    mDeviceName = nameObj->stringValue();
  }
  
  JsonObjectPtr verObj = aDeviceInfo->get("ver");
  if (verObj) {
    mSwVersion = verObj->stringValue();
  }

  JsonObjectPtr ledInfo = aDeviceInfo->get("leds");
  if (ledInfo) {
    JsonObjectPtr countObj = ledInfo->get("count");
    if (countObj) {
      mLedCount = countObj->int32Value();
    }
  }

  LOG(LOG_DEBUG, "WLED device info updated");
}


bool WledDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  LOG(LOG_DEBUG, "WLED device identify");

  // Call callback with error NULL and this device
  if (aIdentifyCB) {
    aIdentifyCB(ErrorPtr(), this);
  }
  return true;
}


void WledDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  LOG(LOG_DEBUG, "WLED device applyChannelValues");

  if (mSettingState) {
    if (aDoneCB) aDoneCB();
    return;
  }

  mSettingState = true;

  // Get brightness from light behavior
  if (mColorLightBehaviour) {
    double brightness = 0;
    ChannelBehaviourPtr brCh = mColorLightBehaviour->getChannelByType(channeltype_brightness);
    if (brCh) {
      brightness = brCh->getChannelValue();
    }
    
    // For now, just query state
    mVdc.mWledComm.getState(boost::bind(&WledDevice::handleStateResponse, this, _1, _2));
  }

  if (aDoneCB) {
    aDoneCB();
  }
}


void WledDevice::queryState()
{
  LOG(LOG_DEBUG, "WLED device queryState");

  mVdc.mWledComm.getState(boost::bind(&WledDevice::handleStateResponse, this, _1, _2));
}


void WledDevice::handleStateResponse(JsonObjectPtr aState, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    LOG(LOG_ERR, "Failed to get WLED device state: %s", aError->text());
    mSettingState = false;
    return;
  }

  if (aState) {
    LOG(LOG_DEBUG, "WLED device state received: %s", aState->json_str().c_str());
    updateState(aState);
    return;
  }

  mSettingState = false;
}


void WledDevice::updateState(JsonObjectPtr aStateResponse)
{
  if (!aStateResponse || !mColorLightBehaviour) return;

  LOG(LOG_DEBUG, "WLED device updateState");

  // For now, just log state received
  JsonObjectPtr stateObj = aStateResponse->get("state");
  if (!stateObj) {
    stateObj = aStateResponse; // Sometimes state is at top level
  }

  if (!stateObj) return;

  // Get brightness
  JsonObjectPtr briObj = stateObj->get("bri");
  int bri = briObj ? briObj->int32Value() : 0;
  double brightness = bri / 255.0;

  LOG(LOG_DEBUG, "WLED state updated: brightness=%.2f", brightness);
}


void WledDevice::extractRgbFromState(JsonObjectPtr aState, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue)
{
  // Fallback to white
  aRed = 255;
  aGreen = 255;
  aBlue = 255;
}


ErrorPtr WledDevice::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  LOG(LOG_INFO, "WLED device handleMethod: %s", aMethod.c_str());

  ErrorPtr err;

  if (uequals(aMethod, "identify")) {
    identifyDevice(NULL);
    aRequest->sendResult(aRequest->connection()->newApiValue());
    return ErrorPtr();
  }
  else if (uequals(aMethod, "getState")) {
    queryState();
    aRequest->sendResult(aRequest->connection()->newApiValue());
    return ErrorPtr();
  }

  return inherited::handleMethod(aRequest, aMethod, aParams);
}


void WledDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  LOG(LOG_DEBUG, "WLED device checkPresence");

  // query the device
  //mVdc.mWledComm.getState(boost::bind(&WledDevice::presenceStateReceived, this, _1, _2));
}


void WledDevice::presenceStateReceived(PresenceCB aPresenceResultHandler, JsonObjectPtr aDeviceInfo, ErrorPtr aError)
{
  bool reachable = false;
  if (Error::isOK(aError) && aDeviceInfo) {
    JsonObjectPtr state = aDeviceInfo->get("state");
    if (state) {
      JsonObjectPtr o = state->get("reachable");
      reachable = o && o->boolValue();
    }
  }
  aPresenceResultHandler(reachable);
}

#endif // ENABLE_WLED
