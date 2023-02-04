//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
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

#include "huedevice.hpp"

#if ENABLE_HUE

#include "huevdc.hpp"

using namespace p44;


// hue API conversion factors


// - hue: brightness: Brightness of the light. This is a scale from the minimum brightness the light is capable of, 1,
//   to the maximum capable brightness, 254. 0 does not turn off the light
// - dS: non-off brightness: 0.39..100
// Using equation hue = ds*HUEAPI_FACTOR_BRIGHTNESS + HUEAPI_OFFSET_BRIGHTNESS
// describing a straight line trough points (DS_BRIGHTNESS_STEP,1) and (100,254)
#define HUEAPI_FACTOR_BRIGHTNESS ((254.0-1)/(100-DS_BRIGHTNESS_STEP))
#define HUEAPI_OFFSET_BRIGHTNESS (1.0-DS_BRIGHTNESS_STEP*HUEAPI_FACTOR_BRIGHTNESS)

// - hue: hue: Wrapping value between 0 and 65535. Both 0 and 65535 are red, 25500 is green and 46920 is blue.
// - dS: hue: 0..360(exclusive) degrees. This means we will never see a channel value 360, because it is considered identical to 0
#define HUEAPI_FACTOR_HUE (65535.0/360)

// - hue: Saturation: 254 is the most saturated (colored) and 0 is the least saturated (white)
// - dS: 0..100%
#define HUEAPI_FACTOR_SATURATION (254.0/100)

// - hue: color temperature: 153..500 mired for 2012's hue bulbs
// - dS: color temperature: 100..10000 mired

// - CIE x,y: hue and dS use 0..1 for x and y

#define DEFAULT_REAPPLY_DELAY (1*Second)



// MARK: - HueDevice


HueDevice::HueDevice(HueVdc *aVdcP, const string &aLightID, HueType aHueType, const string &aUniqueID) :
  inherited(aVdcP),
  mLightID(aLightID),
  mUniqueID(aUniqueID),
  mHueCertified(undefined),
  mReapplyAfter(DEFAULT_REAPPLY_DELAY),
  mCurrentlyOn(undefined),
  mLastSentBri(0), // undefined (bri starts at 1)
  mSeparateOnAndChannels(false)
{
  // hue devices are lights
  setColorClass(class_yellow_light);
  if (aHueType==fullcolor || aHueType==colortemperature) {
    // color lamp
    // - use color light settings, which include a color scene table
    installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
    // - set the behaviour
    ColorLightBehaviourPtr cl = ColorLightBehaviourPtr(new ColorLightBehaviour(*this, aHueType==colortemperature));
    cl->setHardwareOutputConfig(aHueType==colortemperature ? outputFunction_ctdimmer : outputFunction_colordimmer, outputmode_gradual, usage_undefined, true, 8.5); // hue lights are always dimmable, one hue = 8.5W
    cl->setHardwareName(string_format("%s light #%s", aHueType==colortemperature ? "tunable white" : "color", mLightID.c_str()));
    cl->initMinBrightness(DS_BRIGHTNESS_STEP); // min brightness
    addBehaviour(cl);
  }
  else {
    // model as dimmable lamp (but onoff-only will use dim level threshold for switching on)
    // - use normal light settings
    installSettings(DeviceSettingsPtr(new LightDeviceSettings(*this)));
    // - set the behaviour
    LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*this));
    if (aHueType==onoff) {
      l->setHardwareOutputConfig(outputFunction_switch, outputmode_binary, usage_undefined, false, -1);
      l->setHardwareName(string_format("on/off switch #%s", mLightID.c_str()));
    }
    else {
      l->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, true, 8.5); // hue lights are always dimmable, one hue = 8.5W
      l->setHardwareName(string_format("monochrome light #%s", mLightID.c_str()));
    }
    l->initMinBrightness(DS_BRIGHTNESS_STEP); // min brightness
    addBehaviour(l);
  }
  // derive the dSUID
  deriveDsUid();
}


bool HueDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}


string HueDevice::getExtraInfo()
{
  return string_format("Light #%s", mLightID.c_str());
}



HueVdc &HueDevice::hueVdc()
{
  return *(static_cast<HueVdc *>(mVdcP));
}


HueComm &HueDevice::hueComm()
{
  return (static_cast<HueVdc *>(mVdcP))->mHueComm;
}



void HueDevice::setName(const string &aName)
{
  string oldname = getName();
  inherited::setName(aName);
  if (getName()!=oldname) {
    // really changed, propagate to hue
    JsonObjectPtr params = JsonObject::newObj();
    params->add("name", JsonObject::newString(getName()));
    string url = string_format("/lights/%s", mLightID.c_str());
    hueComm().apiAction(httpMethodPUT, url.c_str(), params, NoOP);
  }
}


void HueDevice::checkBrokenDevices(JsonObjectPtr aDeviceInfo)
{
  JsonObjectPtr o;
  if (
    // Molto Luce VOLARE ZB3 with TCI electronics v.1.2 is quite broken
    // (random brightness when "on" and "bri" are changed in same command)
    aDeviceInfo->get("modelid", o) && o->stringValue()=="VOLARE ZB3" &&
    aDeviceInfo->get("swversion", o) && o->stringValue()=="v.1.2"
  ) {
    OLOG(LOG_WARNING, "Model %s is known broken, enabling tweaks. device info:\n%s", mHueModel.c_str(), aDeviceInfo->c_strValue());
    mSeparateOnAndChannels = true;
  }
}



void HueDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // query light attributes and state
  string url = string_format("/lights/%s", mLightID.c_str());
  hueComm().apiQuery(url.c_str(), boost::bind(&HueDevice::deviceStateReceived, this, aCompletedCB, aFactoryReset, _1, _2));
}


// TODO: once hue bridge 1.3 is common, this information could be read from the collection result
void HueDevice::deviceStateReceived(StatusCB aCompletedCB, bool aFactoryReset, JsonObjectPtr aDeviceInfo, ErrorPtr aError)
{
  if (Error::isOK(aError) && aDeviceInfo) {
    JsonObjectPtr o;
    // get model name from device (note: with 1.3 bridge and later this could be read at collection, but pre-1.3 needs this separate call)
    mHueModel.clear();
    mHueVendor.clear();
    o = aDeviceInfo->get("type");
    if (o) {
      mHueModel = o->stringValue();
    }
    o = aDeviceInfo->get("modelid");
    if (o) {
      mHueModel += ": " + o->stringValue();
    }
    o = aDeviceInfo->get("swversion");
    if (o) {
      mSwVersion = o->stringValue();
    }
    o = aDeviceInfo->get("manufacturername");
    if (o) {
      mHueVendor = o->stringValue();
    }
    // check capabilities
    o = aDeviceInfo->get("capabilities");
    if (o) {
      // certified state
      JsonObjectPtr o2 = o->get("certified");
      if (o2) {
        mHueCertified = o2->boolValue() ? yes : no;
      }
    }
    // look for known bad devices and possibly enable tweaks
    checkBrokenDevices(aDeviceInfo);
    // now look at state
    parseLightState(aDeviceInfo);
  }
  // let superclass initialize as well
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


bool HueDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  const char *iconname = NULL;
  if (getOutput()) {
    switch (getOutput()->getOutputFunction()) {
      case outputFunction_colordimmer: iconname = "hue"; break;
      case outputFunction_ctdimmer: iconname = "hue_ct"; break;
      default: iconname = "hue_lux"; break;
    }
  }
  if (iconname && getIcon(iconname, aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}



bool HueDevice::prepareForOptimizedSet(NotificationDeliveryStatePtr aDeliveryState)
{
  // in general, we don't optimize for APIs before 1.11
  if (!hueVdc().mHas_1_11_api) return false;
  if (aDeliveryState->mOptimizedType==ntfy_callscene) {
    // scenes are generally optimizable, unless there is a transition time override
    // TODO: remove the condition once hue bridge allows overriding scene transition times
    return transitionTimeOverride()==Infinite;
  }
  else if (aDeliveryState->mOptimizedType==ntfy_dimchannel) {
    // only brightness, saturation and hue dimming is optimizable for now
    return
      mCurrentDimChannel && // actually prepared for dimming
      (mCurrentDimChannel->getChannelType()==channeltype_brightness ||
       mCurrentDimChannel->getChannelType()==channeltype_hue ||
       mCurrentDimChannel->getChannelType()==channeltype_saturation);
  }
  return false;
}


// optimized hue dimming implementation
void HueDevice::dimChannel(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode, bool aDoApply)
{
  if (aDoApply) {
    // not optimized: use generic dimming
    inherited::dimChannel(aChannel, aDimMode, aDoApply);
  }
  else {
    // part of optimized vdc level dimming: just retrieve dim end state
    if (aDimMode==dimmode_stop) {
      // retrieve status at end of dimming
      // Note: does not work when called immediately - so we delay that a bit
      mDimTicket.executeOnce(boost::bind(&HueDevice::syncChannelValues, this, SimpleCB()), 3*Second);
    }
  }
}


string HueDevice::modelName()
{
  return mHueModel;
}


string HueDevice::hardwareGUID()
{
  if (!mUniqueID.empty())
    return string_format("hueuid:%s", mUniqueID.c_str());
  else
    return inherited::hardwareGUID();
}

string HueDevice::modelVersion() const
{
  return mSwVersion;
}


string HueDevice::vendorName()
{
  return mHueVendor;
};


int HueDevice::opStateLevel()
{
  return mHueCertified==no ? 80 : 100; // explicitly non-certified lights are given some negative points
}


string HueDevice::getOpStateText()
{
  string t;
  if (mHueCertified==no) {
    t += "not certified";
  }
  return t;
}




void HueDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  // query the device
  string url = string_format("/lights/%s", mLightID.c_str());
  hueComm().apiQuery(url.c_str(), boost::bind(&HueDevice::presenceStateReceived, this, aPresenceResultHandler, _1, _2));
}



void HueDevice::presenceStateReceived(PresenceCB aPresenceResultHandler, JsonObjectPtr aDeviceInfo, ErrorPtr aError)
{
  bool reachable = false;
  if (Error::isOK(aError) && aDeviceInfo) {
    JsonObjectPtr state = aDeviceInfo->get("state");
    if (state) {
      // Note: 2012 hue bridge firmware always returns 1 for this.
      JsonObjectPtr o = state->get("reachable");
      reachable = o && o->boolValue();
    }
  }
  aPresenceResultHandler(reachable);
}



void HueDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  checkPresence(boost::bind(&HueDevice::disconnectableHandler, this, aForgetParams, aDisconnectResultHandler, _1));
}


void HueDevice::disconnectableHandler(bool aForgetParams, DisconnectCB aDisconnectResultHandler, bool aPresent)
{
  if (!aPresent) {
    // call inherited disconnect
    inherited::disconnect(aForgetParams, aDisconnectResultHandler);
  }
  else {
    // not disconnectable
    if (aDisconnectResultHandler) {
      aDisconnectResultHandler(false);
    }
  }
}




void HueDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  mReapplyTicket.cancel();
  MLMicroSeconds tt = 0; // none so far, applyLigtState will determine highest time
  if (applyLightState(aDoneCB, aForDimming, false, tt)) {
    // actually applied something, schedule reapply if enabled and not dimming
    if (!aForDimming && mReapplyAfter!=Never) {
      // initially re-apply shortly after, but not before transition time is over
      mReapplyTicket.executeOnce(boost::bind(&HueDevice::reapplyTimerHandler, this, tt), tt>mReapplyAfter ? tt : mReapplyAfter);
    }
  }
}



void HueDevice::reapplyTimerHandler(MLMicroSeconds aTransitionTime)
{
  mReapplyTicket.cancel();
  OLOG(LOG_INFO, "Re-applying values to hue to make sure light actually is udpated");
  applyLightState(NoOP, false, true, aTransitionTime);
}




bool HueDevice::applyLightState(SimpleCB aDoneCB, bool aForDimming, bool aReapply, MLMicroSeconds &aTransitionTime)
{
  // Update of light state needed
  LightBehaviourPtr l = getOutput<LightBehaviour>();
  if (l) {
    if (!aReapply && !needsToApplyChannels(&aTransitionTime)) {
      // NOP for this call
      channelValuesSent(l, aDoneCB, JsonObjectPtr(), ErrorPtr());
      return false; // no changes
    }
    ColorLightBehaviourPtr cl = getOutput<ColorLightBehaviour>();
    // build hue API light state
    string url = string_format("/lights/%s/state", mLightID.c_str());
    JsonObjectPtr newState = JsonObject::newObj();
    // brightness is always re-applied unless it's dimming
    bool lightIsOn = mCurrentlyOn!=no; // assume on even if unknown
    if (aReapply || !aForDimming || l->brightness->needsApplying()) {
      Brightness b = l->brightnessForHardware();
      lightIsOn = b>=DS_BRIGHTNESS_STEP;
      if (l->getOutputFunction()==outputFunction_switch) {
        // just on and off
        newState->add("on", JsonObject::newBool(lightIsOn));
      }
      else {
        if (!lightIsOn) {
          // light should be off, no other parameters
          if (mSeparateOnAndChannels) {
            newState->add("bri", JsonObject::newInt32(1));
            mLastSentBri = 1;
          }
          newState->add("on", JsonObject::newBool(false));
          mCurrentlyOn = no; // assume off from now on (actual response might change it)
        }
        else {
          // light on
          uint8_t newBri = b*HUEAPI_FACTOR_BRIGHTNESS+HUEAPI_OFFSET_BRIGHTNESS+0.5; // DS_BRIGHTNESS_STEP..100 -> 1..254
          if (mSeparateOnAndChannels) {
            // known broken light, make sure on is never sent together with brightness, but always separately before
            if (mCurrentlyOn!=yes || aReapply) {
              if (mLastSentBri!=newBri || aReapply) {
                // both on and bri changes -> need to send "on" ahead
                OLOG(LOG_INFO, "light with known broken API: send \"on\":true separately, transition %d mS", (int)(aTransitionTime/MilliSecond));
                JsonObjectPtr onState = JsonObject::newObj();
                onState->add("on", JsonObject::newBool(true));
                onState->add("bri", JsonObject::newInt32(newBri)); // send it here already a first time
                onState->add("transitiontime", JsonObject::newInt64(aTransitionTime/(100*MilliSecond)));
                // just send, don't care about the answer
                hueComm().apiAction(httpMethodPUT, url.c_str(), onState, NoOP);
                // Note: hueComm will make sure next API command is paced in >=100mS distance,
                // so we can go on creating the bri/color state change right now
                newState->add("bri", JsonObject::newInt32(newBri));
              }
              else {
                // no brightness change, safe to send on now (no matter if changed or not)
                newState->add("on", JsonObject::newBool(true));
              }
            }
            else {
              // no "on" change, just send brightness (no matter if changed or not)
              newState->add("bri", JsonObject::newInt32(newBri));
            }
          }
          else {
            // normal light, can send on and bri together
            newState->add("on", JsonObject::newBool(true));
            newState->add("bri", JsonObject::newInt32(newBri));
          }
          mCurrentlyOn = yes; // assume off from now on (actual response might change it)
          mLastSentBri = newBri;
        }
      }
    }
    // for color lights, also check color (but not if light is off)
    if (cl) {
      // Color light
      // - derive (possibly new) color mode from changed channels
      cl->deriveColorMode();
      if (lightIsOn) {
        // light is on - add color in case it was set (by scene call)
        switch (cl->colorMode) {
          case colorLightModeHueSaturation: {
            // for dimming, only actually changed component (hue or saturation)
            if (aReapply || !aForDimming || cl->hue->needsApplying()) {
              newState->add("hue", JsonObject::newInt32(cl->hue->getChannelValue()*HUEAPI_FACTOR_HUE+0.5));
            }
            if (aReapply || !aForDimming || cl->saturation->needsApplying()) {
              newState->add("sat", JsonObject::newInt32(cl->saturation->getChannelValue()*HUEAPI_FACTOR_SATURATION+0.5));
            }
            break;
          }
          case colorLightModeXY: {
            // x,y are always applied together
            if (aReapply || cl->cieX->needsApplying() || cl->cieY->needsApplying()) {
              JsonObjectPtr xyArr = JsonObject::newArray();
              xyArr->arrayAppend(JsonObject::newDouble(cl->cieX->getChannelValue()));
              xyArr->arrayAppend(JsonObject::newDouble(cl->cieY->getChannelValue()));
              newState->add("xy", xyArr);
            }
            break;
          }
          case colorLightModeCt: {
            if (aReapply || cl->ct->needsApplying()) {
              newState->add("ct", JsonObject::newInt32(cl->ct->getChannelValue()));
            }
            break;
          }
          default:
            break;
        }
      }
      // confirm early, as subsequent request might set new value again
      // Note: includes confirming brightness
      cl->appliedColorValues();
    }
    else {
      // non-color light
      // - confirm brightness applied
      l->brightness->channelValueApplied(true); // confirm early, as subsequent request might set new value again
    }
    // show what we are doing
    if (OLOGENABLED(LOG_INFO) && (!aForDimming || OLOGENABLED(LOG_DEBUG))) {
      OLOG(LOG_INFO, "sending new light state: light is %s, brightness=%0.0f, transition %d mS", lightIsOn ? "ON" : "OFF", l->brightness->getChannelValue(), (int)(aTransitionTime/MilliSecond));
      if (cl) {
        switch (cl->colorMode) {
          case colorLightModeHueSaturation:
            LOG(LOG_INFO, "- color mode HSV: hue=%0.0f, saturation=%0.0f", cl->hue->getChannelValue(), cl->saturation->getChannelValue());
            break;
          case colorLightModeXY:
            LOG(LOG_INFO, "- color mode xyV: x=%0.3f, y=%0.3f", cl->cieX->getChannelValue(), cl->cieY->getChannelValue());
            break;
          case colorLightModeCt:
            LOG(LOG_INFO, "- color mode color temperature: mired=%0.0f", cl->ct->getChannelValue());
            break;
          default:
            LOG(LOG_INFO, "- NO color");
            break;
        }
      }
    }
    // use transition time from (1/10 = 100mS resolution)
    if (l->getOutputFunction()!=outputFunction_switch) {
      newState->add("transitiontime", JsonObject::newInt64(aTransitionTime/(100*MilliSecond)));
    }
    // send the command
    hueComm().apiAction(httpMethodPUT, url.c_str(), newState, boost::bind(&HueDevice::channelValuesSent, this, l, aDoneCB, _1, _2));
  }
  return true;
}



void HueDevice::channelValuesSent(LightBehaviourPtr aLightBehaviour, SimpleCB aDoneCB, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (!mReapplyTicket) {
    // synchronize actual channel values as hue delivers them back, but only if not re-apply still pending
    if (aResult) {
      ColorLightBehaviourPtr cl = boost::dynamic_pointer_cast<ColorLightBehaviour>(aLightBehaviour);
      // [{"success":{"\/lights\/1\/state\/transitiontime":1}},{"success":{"\/lights\/1\/state\/on":true}},{"success":{"\/lights\/1\/state\/hue":0}},{"success":{"\/lights\/1\/state\/sat":255}},{"success":{"\/lights\/1\/state\/bri":255}}]
      bool blockBrightness = false;
      for (int i=0; i<aResult->arrayLength(); i++) {
        JsonObjectPtr staObj = HueComm::getSuccessItem(aResult, i);
        if (staObj) {
          // dispatch results
          staObj->resetKeyIteration();
          string key;
          JsonObjectPtr val;
          if (staObj->nextKeyValue(key, val)) {
            // match path
            string param = key.substr(key.find_last_of('/')+1);
            if (cl && param=="hue") {
              cl->hue->syncChannelValue(val->int32Value()/HUEAPI_FACTOR_HUE, false); // only sync if no new value pending already
            }
            else if (cl && param=="sat") {
              cl->saturation->syncChannelValue(val->int32Value()/HUEAPI_FACTOR_SATURATION, false); // only sync if no new value pending already
            }
            else if (cl && param=="xy") {
              JsonObjectPtr e = val->arrayGet(0);
              if (e) cl->cieX->syncChannelValue(e->doubleValue(), false); // only sync if no new value pending already, volatile
              e = val->arrayGet(1);
              if (e) cl->cieY->syncChannelValue(e->doubleValue(), false); // only sync if no new value pending already, volatile
            }
            else if (cl && param=="ct") {
              cl->ct->syncChannelValue(val->int32Value(), false); // only sync if no new value pending already, volatile
            }
            else if (param=="on") {
              mCurrentlyOn = val->boolValue() ? yes : no;
              if (mCurrentlyOn==no) {
                aLightBehaviour->syncBrightnessFromHardware(0, false); // only sync if no new value pending already, volatile
                blockBrightness = true; // prevent syncing brightness, lamp is off, logical brightness is 0
              }
            }
            else if (param=="bri" && !blockBrightness) {
              double hb = val->int32Value(); // hue brightness from 1..254
              if (hb<1) hb=1;
              aLightBehaviour->syncBrightnessFromHardware(hb/HUEAPI_FACTOR_BRIGHTNESS, false, true); // only sync if no new value pending already, volatile
            }
          } // status data key/val
        } // status item found
      } // all success items
    }
  }
  // confirm done
  if (aDoneCB) aDoneCB();
}



void HueDevice::parseLightState(JsonObjectPtr aDeviceInfo)
{
  JsonObjectPtr o;
  // get current color settings
  JsonObjectPtr state = aDeviceInfo->get("state");
  if (state) {
    o = aDeviceInfo->get("reachable");
    if (o) {
      updatePresenceState(o->boolValue());
    }
    LightBehaviourPtr l = getOutput<LightBehaviour>();
    if (l) {
      // on with brightness or off
      o = state->get("on");
      if (o && o->boolValue()) {
        // lamp is on, get brightness
        mCurrentlyOn = yes;
        o = state->get("bri");
        if (o) {
          double hb = o->int32Value(); // hue brightness from 1..254
          if (hb<1) hb=1;
          l->syncBrightnessFromHardware(hb/HUEAPI_FACTOR_BRIGHTNESS, false); // only sync if no new value pending already
        }
      }
      else {
        mCurrentlyOn = o ? no : undefined; // if no "on" field was included, consider undefined
        l->syncBrightnessFromHardware(0); // off
      }
      ColorLightBehaviourPtr cl = boost::dynamic_pointer_cast<ColorLightBehaviour>(l);
      if (cl) {
        // color information
        o = state->get("colormode");
        if (o) {
          string mode = o->stringValue();
          if (mode=="hs") {
            cl->colorMode = colorLightModeHueSaturation;
            o = state->get("hue");
            if (o) cl->hue->syncChannelValue((o->int32Value())/HUEAPI_FACTOR_HUE);
            o = state->get("sat");
            if (o) cl->saturation->syncChannelValue((o->int32Value())/HUEAPI_FACTOR_SATURATION);
          }
          else if (mode=="xy") {
            cl->colorMode = colorLightModeXY;
            o = state->get("xy");
            if (o) {
              JsonObjectPtr e = o->arrayGet(0);
              if (e) cl->cieX->syncChannelValue(e->doubleValue());
              e = o->arrayGet(1);
              if (e) cl->cieY->syncChannelValue(e->doubleValue());
            }
          }
          else if (mode=="ct") {
            cl->colorMode = colorLightModeCt;
            o = state->get("ct");
            if (o) cl->ct->syncChannelValue(o->int32Value());
          }
          else {
            cl->colorMode = colorLightModeNone;
          }
        }
      } // color
    } // light
  } // state
}



void HueDevice::syncChannelValues(SimpleCB aDoneCB)
{
  // query light attributes and state
  string url = string_format("/lights/%s", mLightID.c_str());
  hueComm().apiQuery(url.c_str(), boost::bind(&HueDevice::channelValuesReceived, this, aDoneCB, _1, _2));
}



void HueDevice::channelValuesReceived(SimpleCB aDoneCB, JsonObjectPtr aDeviceInfo, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // assign the channel values
    parseLightState(aDeviceInfo);
  } // no error
  // done
  if (aDoneCB) aDoneCB();
}




void HueDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  // - for lamps without unique ID:
  //   UUIDv5 with name = classcontainerinstanceid::bridgeUUID:huelightid
  // - for lamps with unique ID:
  //   UUIDv5 with name = hueUniqueID::uniqueID
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s;
  if (mUniqueID.empty()) {
    // we don't have an unique ID, identify relative to bridge's UUID
    s = mVdcP->vdcInstanceIdentifier();
    s += "::" + hueVdc().mBridgeIdentifier;
    s += ":" + mLightID;
  }
  else {
    // we have a unique ID for the lamp itself, identify trough that
    s = "hueUniqueID::";
    s += mUniqueID;
  }
  mDSUID.setNameInSpace(s, vdcNamespace);
}


string HueDevice::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- hue unique ID: %s", mUniqueID.c_str());
  return s;
}


#endif // ENABLE_HUE

