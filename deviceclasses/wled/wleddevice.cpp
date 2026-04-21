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
  double v = aValue / 100.0;
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
  #if ENABLE_JSON_WEBSOCKET
  , mWebsocketEnabled(false),
  mNormalPollInterval(10*Second),
  mReducedPollInterval(60*Second),
  mWebsocketUpdatePending(false)
  #endif
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

  #if ENABLE_JSON_WEBSOCKET
  // Enable WebSocket connection for state updates
  mVdc.mWledComm.enableWebsocket(true);
  enableWebsocket(true);
  #endif
}


WledDevice::~WledDevice()
{
  #if ENABLE_JSON_WEBSOCKET
  // Cleanup WebSocket connection if active
  if (mWebsocketEnabled) {
    websocketDisconnect();
  }
  #endif
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
  if (!(mHasRgb || mHasRgbw || mHasCct)) {
    LightBehaviourPtr light = new LightBehaviour(*this);
    light->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, true, 10.0);
    light->setHardwareName("wled light");
    light->initMinBrightness(DS_BRIGHTNESS_STEP);
    addBehaviour(light);
    mDimmerLightBehaviour = light;
  }
  if (mHasRgb || mHasRgbw) {
    ColorLightBehaviourPtr cl = ColorLightBehaviourPtr(new ColorLightBehaviour(*this, mHasCct));
    cl->setHardwareOutputConfig(outputFunction_colordimmer, outputmode_gradual, usage_undefined, true, 8.5);
    cl->setHardwareName(string_format("wled %s light #%s", "color", mDeviceName.c_str()));
    cl->initMinBrightness(DS_BRIGHTNESS_STEP); // min brightness
    addBehaviour(cl);
    mColorLightBehaviour = cl;
  }
  if (mHasCct && !(mHasRgb || mHasRgbw)) {
    // CCT only device
    ColorLightBehaviourPtr cl = ColorLightBehaviourPtr(new ColorLightBehaviour(*this, true));
    cl->setHardwareOutputConfig(outputFunction_ctdimmer, outputmode_gradual, usage_undefined, true, 8.5);
    cl->setHardwareName(string_format("wled %s light #%s", "tunable white", mDeviceName.c_str()));
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

  if (aIdentifyCB) {
    aIdentifyCB(ErrorPtr(), this);
  }
  return true;
}


void WledDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  #if ENABLE_JSON_WEBSOCKET
  if (mWebsocketEnabled && mVdc.mWledComm.isWebsocketConnected()) {
    // WebSocket is already connected and has already pushed current state — a separate
    // HTTP query would compete for the device's limited TCP connections and is redundant.
    inherited::initializeDevice(aCompletedCB, aFactoryReset);
    return;
  }
  #endif
  // WebSocket not available: fall back to HTTP query for initial state sync
  mVdc.mWledComm.getState(boost::bind(&WledDevice::deviceStateReceived, this, aCompletedCB, aFactoryReset, _1, _2));
}


void WledDevice::deviceStateReceived(StatusCB aCompletedCB, bool aFactoryReset, JsonObjectPtr aState, ErrorPtr aError)
{
  if (Error::isOK(aError) && aState) {
    updateState(aState);
  }
  // let the base class finish initialization (stores settings, fires callback)
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


void WledDevice::syncChannelValues(SimpleCB aDoneCB)
{
  #if ENABLE_JSON_WEBSOCKET
  if (mWebsocketEnabled && mVdc.mWledComm.isWebsocketConnected()) {
    // WebSocket delivers state in real time — channel values are already current.
    // Skip HTTP query to avoid overloading the device's TCP connection limit.
    inherited::syncChannelValues(aDoneCB);
    return;
  }
  #endif
  // WebSocket not available: query current hardware state via HTTP before scene save
  mVdc.mWledComm.getState(boost::bind(&WledDevice::channelValuesReceived, this, aDoneCB, _1, _2));
}


void WledDevice::channelValuesReceived(SimpleCB aDoneCB, JsonObjectPtr aState, ErrorPtr aError)
{
  if (Error::isOK(aError) && aState) {
    updateState(aState);
  }
  inherited::syncChannelValues(aDoneCB);
}


void WledDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  LOG(LOG_DEBUG, "WLED device applyChannelValues");

  if (mSettingState) {
    if (aDoneCB) aDoneCB();
    return;
  }
  mSettingState = true;

  // Get the transition time from the framework (max across all pending channels)
  MLMicroSeconds tt = 0;
  needsToApplyChannels(&tt);
  int64_t wledTt = tt / (100 * MilliSecond); // WLED "tt" field: units of 100 ms

  JsonObjectPtr onState = JsonObject::newObj();
  // Always include transition time so we override WLED's internal default
  onState->add("tt", JsonObject::newInt64(wledTt));

  if (mColorLightBehaviour) {
    ChannelBehaviourPtr brCh = mColorLightBehaviour->getChannelByType(channeltype_brightness);
    double bri = brCh ? brCh->getChannelValue() : 0;
    uint8_t wledBri = (uint8_t)(bri / 100.0 * 255.0);

    // Turn off
    if (wledBri == 0) {
      LOG(LOG_DEBUG, "WLED applyChannelValues: turning off (tt=%lld00ms)", (long long)wledTt);
      onState->add("on", JsonObject::newBool(false));
      mVdc.mWledComm.setState(onState, [this, aDoneCB](JsonObjectPtr aResponse, ErrorPtr aError) {
        if (Error::notOK(aError)) LOG(LOG_ERR, "Failed to set WLED state: %s", aError->text());
        mSettingState = false;
        mColorLightBehaviour->appliedColorValues();
        if (aDoneCB) aDoneCB();
      });
      return;
    }

    onState->add("on", JsonObject::newBool(true));
    onState->add("bri", JsonObject::newInt32(wledBri));

    // Build segment 0 object
    JsonObjectPtr seg0 = JsonObject::newObj();
    seg0->add("id", JsonObject::newInt32(0));
    seg0->add("fx", JsonObject::newInt32(0)); // solid effect

    // Derive color mode from which channels have pending values
    mColorLightBehaviour->deriveColorMode();

    if (mColorLightBehaviour->mColorMode == colorLightModeCt) {
      // CCT mode: send color temperature to WLED cct field (0=warm/2000K, 255=cold/6500K)
      ChannelBehaviourPtr cctCh = mColorLightBehaviour->getChannelByType(channeltype_colortemp);
      if (cctCh) {
        double mired = cctCh->getChannelValue();
        // Map mired 500(warm/2000K)->0, 153(cold/6500K)->255
        int wledCct = (int)((500.0 - mired) / (500.0 - 153.0) * 255.0 + 0.5);
        if (wledCct < 0) wledCct = 0;
        if (wledCct > 255) wledCct = 255;
        LOG(LOG_DEBUG, "WLED applyChannelValues: CT mode mired=%.0f => cct=%d", mired, wledCct);
        seg0->add("cct", JsonObject::newInt32(wledCct));
      }
    }
    else {
      // HS mode (default): convert HSV to RGB and send as JSON arrays
      ChannelBehaviourPtr hueCh = mColorLightBehaviour->getChannelByType(channeltype_hue);
      ChannelBehaviourPtr satCh = mColorLightBehaviour->getChannelByType(channeltype_saturation);
      double hue = hueCh ? hueCh->getChannelValue() : 0;
      double sat = satCh ? satCh->getChannelValue() : 0;
      uint8_t r{}, g{}, b{};
      hsvToRgb(hue, sat, bri, r, g, b);
      LOG(LOG_DEBUG, "WLED applyChannelValues: HS mode hue=%.2f sat=%.2f bri=%.2f => r=%d g=%d b=%d",
        hue, sat, bri, r, g, b);

      // WLED JSON API: col is array of [R,G,B] or [R,G,B,W] arrays
      // For RGBW devices append W=0 (hardware white; let WLED's auto-white handle mixing)
      JsonObjectPtr col = JsonObject::newArray();
      JsonObjectPtr c0 = JsonObject::newArray(); // primary color
      c0->arrayAppend(JsonObject::newInt32(r));
      c0->arrayAppend(JsonObject::newInt32(g));
      c0->arrayAppend(JsonObject::newInt32(b));
      if (mHasRgbw) c0->arrayAppend(JsonObject::newInt32(0)); // W channel
      JsonObjectPtr c1 = JsonObject::newArray(); // secondary (off)
      c1->arrayAppend(JsonObject::newInt32(0));
      c1->arrayAppend(JsonObject::newInt32(0));
      c1->arrayAppend(JsonObject::newInt32(0));
      if (mHasRgbw) c1->arrayAppend(JsonObject::newInt32(0));
      JsonObjectPtr c2 = JsonObject::newArray(); // tertiary (off)
      c2->arrayAppend(JsonObject::newInt32(0));
      c2->arrayAppend(JsonObject::newInt32(0));
      c2->arrayAppend(JsonObject::newInt32(0));
      if (mHasRgbw) c2->arrayAppend(JsonObject::newInt32(0));
      col->arrayAppend(c0);
      col->arrayAppend(c1);
      col->arrayAppend(c2);
      seg0->add("col", col);

      // Also set CCT if device supports it
      if (mHasCct) {
        ChannelBehaviourPtr cctCh = mColorLightBehaviour->getChannelByType(channeltype_colortemp);
        if (cctCh) {
          double mired = cctCh->getChannelValue();
          int wledCct = (int)((500.0 - mired) / (500.0 - 153.0) * 255.0 + 0.5);
          if (wledCct < 0) wledCct = 0;
          if (wledCct > 255) wledCct = 255;
          seg0->add("cct", JsonObject::newInt32(wledCct));
        }
      }
    }

    JsonObjectPtr segArray = JsonObject::newArray();
    segArray->arrayAppend(seg0);
    onState->add("seg", segArray);

    mVdc.mWledComm.setState(onState, [this, aDoneCB](JsonObjectPtr aResponse, ErrorPtr aError) {
      if (Error::notOK(aError)) LOG(LOG_ERR, "Failed to set WLED state: %s", aError->text());
      mSettingState = false;
      mColorLightBehaviour->appliedColorValues();
      if (aDoneCB) aDoneCB();
    });
    return;
  }

  if (mDimmerLightBehaviour) {
    ChannelBehaviourPtr brCh = mDimmerLightBehaviour->getChannelByType(channeltype_brightness);
    double bri = brCh ? brCh->getChannelValue() : 0;
    uint8_t wledBri = (uint8_t)(bri / 100.0 * 255.0);
    LOG(LOG_DEBUG, "WLED applyChannelValues: dimmer bri=%.2f => wledBri=%d (tt=%lld00ms)", bri, wledBri, (long long)wledTt);

    if (wledBri == 0) {
      onState->add("on", JsonObject::newBool(false));
    } else {
      onState->add("on", JsonObject::newBool(true));
      onState->add("bri", JsonObject::newInt32(wledBri));
    }
    mVdc.mWledComm.setState(onState, [this, aDoneCB](JsonObjectPtr aResponse, ErrorPtr aError) {
      if (Error::notOK(aError)) LOG(LOG_ERR, "Failed to set WLED state: %s", aError->text());
      mSettingState = false;
      mDimmerLightBehaviour->brightnessApplied();
      if (aDoneCB) aDoneCB();
    });
    return;
  }

  mSettingState = false;
  if (aDoneCB) aDoneCB();
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
  if (!aStateResponse) return;

  LOG(LOG_DEBUG, "WLED device updateState");

  // state can be wrapped in a "state" key or be at top level
  JsonObjectPtr state = aStateResponse->get("state");
  if (!state) state = aStateResponse;

  updatePresenceState(true);

  LightBehaviourPtr l = getOutput<LightBehaviour>();
  if (!l) return;

  // on/off and global brightness (0-255 → 0-100)
  JsonObjectPtr o = state->get("on");
  if (o && o->boolValue()) {
    mCurrentlyOn = yes;
    o = state->get("bri");
    if (o) {
      double hb = o->int32Value() / 255.0 * 100.0;
      l->syncBrightnessFromHardware(hb, false);
    }
  }
  else {
    mCurrentlyOn = o ? no : undefined;
    l->syncBrightnessFromHardware(0, false);
  }

  ColorLightBehaviourPtr cl = boost::dynamic_pointer_cast<ColorLightBehaviour>(l);
  if (cl) {
    auto segObj = state->get("seg");
    auto firstSeg = segObj ? segObj->arrayGet(0) : JsonObjectPtr();
    if (firstSeg) {
      auto colArray = firstSeg->get("col");
      auto color0 = colArray ? colArray->arrayGet(0) : JsonObjectPtr();
      auto cctObj = firstSeg->get("cct");

      if (!mHasRgb && !mHasRgbw && mHasCct) {
        // CCT-only device: read cct field (0=warm/500mired, 255=cold/153mired)
        cl->mColorMode = colorLightModeCt;
        if (cctObj) {
          double wledCct = cctObj->int32Value();
          double mired = 500.0 - (wledCct / 255.0) * (500.0 - 153.0);
          cl->mCt->syncChannelValue(mired);
        }
      }
      else if (color0) {
        // RGB device: col[0] is [R,G,B] array
        JsonObjectPtr r0 = color0->arrayGet(0);
        JsonObjectPtr g0 = color0->arrayGet(1);
        JsonObjectPtr b0 = color0->arrayGet(2);
        uint8_t r = r0 ? (uint8_t)r0->int32Value() : 0;
        uint8_t g = g0 ? (uint8_t)g0->int32Value() : 0;
        uint8_t b = b0 ? (uint8_t)b0->int32Value() : 0;
        cl->mColorMode = colorLightModeHueSaturation;
        double hueVal, satVal, valVal;
        rgbToHsv(r, g, b, hueVal, satVal, valVal);
        cl->mHue->syncChannelValue(hueVal);
        cl->mSaturation->syncChannelValue(satVal);
        // Also sync CCT if device supports it
        if (mHasCct && cctObj) {
          double wledCct = cctObj->int32Value();
          double mired = 500.0 - (wledCct / 255.0) * (500.0 - 153.0);
          cl->mCt->syncChannelValue(mired);
        }
      }
    }
  }
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
  mVdc.mWledComm.getNetwork(boost::bind(&WledDevice::presenceStateReceived, this, aPresenceResultHandler, _1, _2));
}


void WledDevice::presenceStateReceived(PresenceCB aPresenceResultHandler, JsonObjectPtr aDeviceInfo, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    LOG(LOG_ERR, "Failed to get WLED device presence: %s", aError->text());
    aPresenceResultHandler(false);
    return;
  }

  if (aDeviceInfo) {
    LOG(LOG_DEBUG, "WLED device info received: %s", aDeviceInfo->json_str().c_str());
    aPresenceResultHandler(true);
    return;
  }
}


#if ENABLE_JSON_WEBSOCKET

void WledDevice::enableWebsocket(bool aEnable)
{
  LOG(LOG_DEBUG, "WledDevice enableWebsocket: %d", aEnable);
  
  if (aEnable == mWebsocketEnabled) {
    return; // No change needed
  }
  
  mWebsocketEnabled = aEnable;
  
  if (aEnable) {
    // Enable WebSocket for this device
    LOG(LOG_INFO, "Enabling WebSocket for device: %s", mDeviceName.c_str());
    websocketConnect();
  } else {
    // Disable WebSocket
    LOG(LOG_INFO, "Disabling WebSocket for device: %s", mDeviceName.c_str());
    websocketDisconnect();
  }
  
  updatePollingFrequency();
}


bool WledDevice::isWebsocketConnected() const
{
  // Query actual WebSocket connection status from WledComm
  if (!mWebsocketEnabled) {
    return false; // Not enabled = not connected
  }
  
  // Return actual connection status from WledComm's WebSocket client
  return mVdc.mWledComm.isWebsocketConnected();
}


void WledDevice::websocketConnect()
{
  if (!mWebsocketEnabled) {
    LOG(LOG_WARNING, "WebSocket not enabled for device: %s", mDeviceName.c_str());
    return;
  }
  
  LOG(LOG_DEBUG, "Requesting WebSocket connection for device: %s", mDeviceName.c_str());
  
  // Register update callback and request connection from WledComm
  #if ENABLE_JSON_WEBSOCKET
  mVdc.mWledComm.setWebsocketUpdateCallback(
    boost::bind(&WledDevice::onWebsocketUpdate, this, _1, _2)
  );
  
  mVdc.mWledComm.websocketConnect(
    boost::bind(&WledDevice::onWebsocketStatus, this, _1, _2)
  );
  #endif
  
  updatePollingFrequency();
}


void WledDevice::websocketDisconnect()
{
  LOG(LOG_DEBUG, "Requesting WebSocket disconnection for device: %s", mDeviceName.c_str());
  
  #if ENABLE_JSON_WEBSOCKET
  mVdc.mWledComm.websocketDisconnect();
  #endif
  
  updatePollingFrequency();
}


void WledDevice::updatePollingFrequency()
{
  if (!mWebsocketEnabled) {
    LOG(LOG_DEBUG, "Polling frequency for device: %s (WebSocket disabled, using normal polling)", 
      mDeviceName.c_str());
    return;
  }
  
  bool connected = isWebsocketConnected();
  
  LOG(LOG_DEBUG, "Polling frequency for device: %s (WebSocket: %s)", 
    mDeviceName.c_str(), connected ? "connected, using reduced polling" : "disconnected, using normal polling");
  
  // Current implementation logs polling status and stores desired interval in member variables
  // These intervals can be used by future implementations to:
  // 1. Dynamically adjust VDC-level polling frequency based on device status
  // 2. Implement device-specific polling strategies
  // 3. Track polling statistics per device
  
  // When WebSocket connected: mReducedPollInterval (60 seconds)
  // When WebSocket disconnected: mNormalPollInterval (10 seconds)
  // Future Phase 4+ enhancements:
  // - Pass polling preference to VDC container
  // - Implement adaptive polling based on aggregated device status
  // - Add per-device polling adjustments to VDC periodic collection
}


void WledDevice::onWebsocketUpdate(JsonObjectPtr aState, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    LOG(LOG_ERR, "WebSocket update error for device %s: %s", 
      mDeviceName.c_str(), aError->text());
    mWebsocketUpdatePending = false;
    return;
  }
  
  if (!aState) {
    LOG(LOG_WARNING, "Empty state received via WebSocket for device: %s", mDeviceName.c_str());
    mWebsocketUpdatePending = false;
    return;
  }
  
  // Prevent feedback loops while we're setting state
  if (mSettingState) {
    LOG(LOG_DEBUG, "Ignoring WebSocket update while setting state on device: %s", mDeviceName.c_str());
    mWebsocketUpdatePending = false;
    return;
  }
  
  // Check for duplicate updates
  if (mWebsocketUpdatePending) {
    LOG(LOG_DEBUG, "WebSocket update already pending for device: %s", mDeviceName.c_str());
    return;
  }
  
  mWebsocketUpdatePending = true;
  
  LOG(LOG_DEBUG, "Processing WebSocket state update for device: %s - %s", 
    mDeviceName.c_str(), aState->json_str().c_str());
  
  // Update device state from WebSocket message
  updateState(aState);
  
  mWebsocketUpdatePending = false;
}


void WledDevice::onWebsocketStatus(bool aConnected, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    LOG(LOG_ERR, "WebSocket status error for device %s: %s", 
      mDeviceName.c_str(), aError->text());
    updatePollingFrequency();
    return;
  }
  
  LOG(LOG_INFO, "WebSocket status changed for device %s: %s", 
    mDeviceName.c_str(), aConnected ? "connected" : "disconnected");
  
  // Optimize polling based on WebSocket connection status
  updatePollingFrequency();
}

#endif // ENABLE_JSON_WEBSOCKET

#endif // ENABLE_WLED
