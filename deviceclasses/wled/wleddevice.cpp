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

  uint8_t brightness = 0;
  if (mColorLightBehaviour) {
    ChannelBehaviourPtr brCh = mColorLightBehaviour->getChannelByType(channeltype_brightness);
    if (brCh) {
      brightness = (uint8_t)(brCh->getChannelValue() / 100 * 255.0);
    }

    // Turn off
    if (brightness <= 0) {
      LOG(LOG_DEBUG, "WLED applyChannelValues: turning off");
      JsonObjectPtr onState = JsonObject::newObj();
      onState->add("on", JsonObject::newBool(false));
      mVdc.mWledComm.setState(onState, [this, aDoneCB](JsonObjectPtr aResponse, ErrorPtr aError) {
          if (Error::notOK(aError)) {
            LOG(LOG_ERR, "Failed to set WLED device state: %s", aError->text());
          } else {
            LOG(LOG_DEBUG, "WLED device state set successfully");
          }
          mSettingState = false;
          if (aDoneCB) {
            aDoneCB();
          }
          mColorLightBehaviour->appliedColorValues();
        });
      return;
    }

    ChannelBehaviourPtr hueCh = mColorLightBehaviour->getChannelByType(channeltype_hue);
    ChannelBehaviourPtr satCh = mColorLightBehaviour->getChannelByType(channeltype_saturation);
    uint8_t redVal{}, greenVal{}, blueVal{};

    hsvToRgb(hueCh->getChannelValue(), satCh->getChannelValue(), brCh->getChannelValue(),
              /*out*/redVal, /*out*/greenVal, /*out*/blueVal);

    LOG(LOG_DEBUG, "WLED applyChannelValues: hue=%.2f, sat=%.2f, val=%.2f => red=%d, green=%d, blue=%d",
      hueCh->getChannelValue(), satCh->getChannelValue(), brCh->getChannelValue(),
      redVal, greenVal, blueVal);

    JsonObjectPtr onState = JsonObject::newObj();
    onState->add("on", JsonObject::newBool(true));

    JsonObjectPtr color0Obj = JsonObject::newObj();  // primary color
    color0Obj->add("0", JsonObject::newInt32(redVal));
    color0Obj->add("1", JsonObject::newInt32(greenVal));
    color0Obj->add("2", JsonObject::newInt32(blueVal));
    JsonObjectPtr color1Obj = JsonObject::newObj();  // secondary color (black)
    color1Obj->add("0", JsonObject::newInt32(0));
    color1Obj->add("1", JsonObject::newInt32(0));
    color1Obj->add("2", JsonObject::newInt32(0));
    JsonObjectPtr color2Obj = JsonObject::newObj();  // tertiary color (gray)
    color2Obj->add("0", JsonObject::newInt32(32));
    color2Obj->add("1", JsonObject::newInt32(32));
    color2Obj->add("2", JsonObject::newInt32(32));

    // add color "0" object to seg[0].col[0] lists
    onState->add("seg", JsonObject::newArray());
    JsonObjectPtr seg0 = JsonObject::newObj();
    seg0->add("col", JsonObject::newArray());
    seg0->get("col")->arrayAppend(color0Obj);
    seg0->get("col")->arrayAppend(color1Obj);
    seg0->get("col")->arrayAppend(color2Obj);
    seg0->add("bri", JsonObject::newInt32((int)brightness));
    seg0->add("fx", JsonObject::newInt32(0)); // solid
    seg0->add("id", JsonObject::newInt32(0)); // first segment
    onState->get("seg")->arrayAppend(seg0);

    auto cctCh = mColorLightBehaviour->getChannelByType(channeltype_colortemp);
    if (cctCh) {
      // add CCT value if supported
      uint8_t cctVal = (uint8_t)cctCh->getChannelValue() / 100 * 255.0;
      seg0->add("cct", JsonObject::newInt32(cctVal));
    }

    mVdc.mWledComm.setState(onState, [this, aDoneCB](JsonObjectPtr aResponse, ErrorPtr aError) {
        if (Error::notOK(aError)) {
          LOG(LOG_ERR, "Failed to set WLED device state: %s", aError->text());
        } else {
          LOG(LOG_DEBUG, "WLED device state set successfully");
        }
        mSettingState = false;
        if (aDoneCB) {
          aDoneCB();
        }
        mColorLightBehaviour->appliedColorValues();
      });
    return;
  }

  if (mDimmerLightBehaviour) {
    double brightness = 0;
    ChannelBehaviourPtr brCh = mDimmerLightBehaviour->getChannelByType(channeltype_brightness);
    if (brCh) {
      brightness = brCh->getChannelValue() / 100 * 255.0;
    }
    LOG(LOG_DEBUG, "WLED applyChannelValues: brightness=%.2f", brightness);

    // Prepare state update JSON
    JsonObjectPtr onState = JsonObject::newObj();

    // Turn off
    if (brightness <= 0) {
      onState->add("on", JsonObject::newBool(false));
      mVdc.mWledComm.setState(onState, [this, aDoneCB](JsonObjectPtr aResponse, ErrorPtr aError) {
          if (Error::notOK(aError)) {
            LOG(LOG_ERR, "Failed to set WLED device state: %s", aError->text());
          } else {
            LOG(LOG_DEBUG, "WLED device state set successfully");
          }
          mSettingState = false;
          if (aDoneCB) {
            aDoneCB();
          }
          mDimmerLightBehaviour->brightnessApplied();
        });
      return;
    }
    onState->add("on", JsonObject::newBool(true));
    onState->add("bri", JsonObject::newInt32(brightness));
    mVdc.mWledComm.setState(onState, [this, aDoneCB](JsonObjectPtr aResponse, ErrorPtr aError) {
        if (Error::notOK(aError)) {
          LOG(LOG_ERR, "Failed to set WLED device state: %s", aError->text());
        } else {
          LOG(LOG_DEBUG, "WLED device state set successfully");
        }
        mSettingState = false;
        if (aDoneCB) {
          aDoneCB();
        }
        mDimmerLightBehaviour->brightnessApplied();
      });
    return;
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
  if (!aStateResponse || !mColorLightBehaviour || !mDimmerLightBehaviour) return;

  LOG(LOG_DEBUG, "WLED device updateState");

  JsonObjectPtr o;
  JsonObjectPtr state = aStateResponse->get("state");
  if (!state) {
    state = aStateResponse; // Sometimes state is at top level
  }

  updatePresenceState(true);
  LightBehaviourPtr l = getOutput<LightBehaviour>();
    if (l) {
      // on with brightness or off
      o = state->get("on");
      if (o && o->boolValue()) {
        // lamp is on, get brightness
        mCurrentlyOn = yes;
        o = state->get("bri");
        if (o) {
          double hb = o->int32Value(); // hue brightness from 0..255
          if (hb<1) hb=0;
          l->syncBrightnessFromHardware(hb, false); // only sync if no new value pending already
        }
      }
      else {
        mCurrentlyOn = o ? no : undefined; // if no "on" field was included, consider undefined
        l->syncBrightnessFromHardware(0); // off
      }
      ColorLightBehaviourPtr cl = boost::dynamic_pointer_cast<ColorLightBehaviour>(l);
      if (cl) {
        // color information
        auto segObj = state->get("seg");
        auto firstSeg = segObj ? segObj->arrayGet(0) : JsonObjectPtr();
        auto colArray = firstSeg ? firstSeg->get("col") : JsonObjectPtr();
        auto color0 = colArray ? colArray->arrayGet(0) : JsonObjectPtr();
        if (color0) {
          string mode = "hs"; // default mode
          if (mode == "hs") {
            cl->mColorMode = colorLightModeHueSaturation;
            auto redVal = color0->get("0")->int32Value();
            auto greenVal = color0->get("1")->int32Value();
            auto blueVal = color0->get("2")->int32Value();
            auto brightnessVal = firstSeg->get("bri")->int32Value();
            cl->mBrightness->syncChannelValue((brightnessVal)/255.0*100.0);
            // convert RGB to HSV
            double hueVal, satVal, valVal;
            rgbToHsv(redVal, greenVal, blueVal,
                     /*out*/hueVal, /*out*/satVal, /*out*/valVal);
            cl->mHue->syncChannelValue(hueVal);
            cl->mSaturation->syncChannelValue(satVal);
          }
          else if (mode=="ct") {
            cl->mColorMode = colorLightModeCt;
            o = firstSeg->get("cct");
            if (o) cl->mCt->syncChannelValue(o->int32Value());
          }
          else {
            cl->mColorMode = colorLightModeNone;
          }
        }
      } // color
    } // light
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

#endif // ENABLE_WLED
