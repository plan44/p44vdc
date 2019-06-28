//
//  Copyright (c) 1-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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



// MARK: - HueDevice


HueDevice::HueDevice(HueVdc *aVdcP, const string &aLightID, bool aIsColor, bool aCTOnly, const string &aUniqueID) :
  inherited(aVdcP),
  lightID(aLightID),
  uniqueID(aUniqueID),
  hueCertified(undefined),
  reapplyMode(reapply_once)
{
  // hue devices are lights
  setColorClass(class_yellow_light);
  if (aIsColor || aCTOnly) {
    // color lamp
    // - use color light settings, which include a color scene table
    installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
    // - set the behaviour
    ColorLightBehaviourPtr cl = ColorLightBehaviourPtr(new ColorLightBehaviour(*this, aCTOnly));
    cl->setHardwareOutputConfig(aCTOnly ? outputFunction_ctdimmer : outputFunction_colordimmer, outputmode_gradual, usage_undefined, true, 8.5); // hue lights are always dimmable, one hue = 8.5W
    cl->setHardwareName(string_format("%s light #%s", aCTOnly ? "tunable white" : "color", lightID.c_str()));
    cl->initMinBrightness(DS_BRIGHTNESS_STEP); // min brightness
    addBehaviour(cl);
  }
  else {
    // dimmable lamp
    // - use normal light settings
    installSettings(DeviceSettingsPtr(new LightDeviceSettings(*this)));
    // - set the behaviour
    LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*this));
    l->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, true, 8.5); // hue lights are always dimmable, one hue = 8.5W
    l->setHardwareName(string_format("monochrome light #%s", lightID.c_str()));
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
  return string_format("Light #%s", lightID.c_str());
}



HueVdc &HueDevice::hueVdc()
{
  return *(static_cast<HueVdc *>(vdcP));
}


HueComm &HueDevice::hueComm()
{
  return (static_cast<HueVdc *>(vdcP))->hueComm;
}



void HueDevice::setName(const string &aName)
{
  string oldname = getName();
  inherited::setName(aName);
  if (getName()!=oldname) {
    // really changed, propagate to hue
    JsonObjectPtr params = JsonObject::newObj();
    params->add("name", JsonObject::newString(getName()));
    string url = string_format("/lights/%s", lightID.c_str());
    hueComm().apiAction(httpMethodPUT, url.c_str(), params, NULL);
  }
}



void HueDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // query light attributes and state
  string url = string_format("/lights/%s", lightID.c_str());
  hueComm().apiQuery(url.c_str(), boost::bind(&HueDevice::deviceStateReceived, this, aCompletedCB, aFactoryReset, _1, _2));
}


// TODO: once hue bridge 1.3 is common, this information could be read from the collection result
void HueDevice::deviceStateReceived(StatusCB aCompletedCB, bool aFactoryReset, JsonObjectPtr aDeviceInfo, ErrorPtr aError)
{
  if (Error::isOK(aError) && aDeviceInfo) {
    JsonObjectPtr o;
    // get model name from device (note: with 1.3 bridge and later this could be read at collection, but pre-1.3 needs this separate call)
    hueModel.clear();
    o = aDeviceInfo->get("type");
    if (o) {
      hueModel = o->stringValue();
    }
    o = aDeviceInfo->get("modelid");
    if (o) {
      hueModel += ": " + o->stringValue();
    }
    o = aDeviceInfo->get("swversion");
    if (o) {
      swVersion = o->stringValue();
    }
    // check capabilities
    o = aDeviceInfo->get("capabilities");
    if (o) {
      // certified state
      JsonObjectPtr o2 = o->get("certified");
      if (o2) {
        hueCertified = o2->boolValue() ? yes : no;
      }
    }
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
  if (!hueVdc().has_1_11_api) return false;
  // TODO: we only optimize scenes for now, not dimming
  if (aDeliveryState->optimizedType==ntfy_callscene) {
    // scenes are generally optimizable
    return true;
  }
  else if (aDeliveryState->optimizedType==ntfy_dimchannel) {
    // only brightness, saturation and hue dimming is optimizable for now
    return
      currentDimChannel && // actually prepared for dimming
      (currentDimChannel->getChannelType()==channeltype_brightness ||
       currentDimChannel->getChannelType()==channeltype_hue ||
       currentDimChannel->getChannelType()==channeltype_saturation);
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
      dimTicket.executeOnce(boost::bind(&HueDevice::syncChannelValues, this, SimpleCB()), 3*Second);
    }
  }
}


string HueDevice::modelName()
{
  return hueModel;
}


string HueDevice::hardwareGUID()
{
  if (!uniqueID.empty())
    return string_format("hueuid:%s", uniqueID.c_str());
  else
    return inherited::hardwareGUID();
}

string HueDevice::modelVersion() const
{
  return swVersion;
}


int HueDevice::opStateLevel()
{
  return hueCertified==no ? 80 : 100; // explicitly non-certified lights are given some negative points
}


string HueDevice::getOpStateText()
{
  string t;
  if (hueCertified==no) {
    t += "not certified";
  }
  return t;
}




void HueDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  // query the device
  string url = string_format("/lights/%s", lightID.c_str());
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



#define INITIAL_REAPPLY_DELAY (1*Second)
#define PERIODIC_REAPPLY_INTERVAL (30*Second)

void HueDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  reapplyTicket.cancel();
  reapplyCount = 0;
  if (applyLightState(aDoneCB, aForDimming, false)) {
    // actually applied something, schedule reapply if enabled and not dimming
    if (!aForDimming && reapplyMode!=reapply_none) {
      // initially re-apply shortly after
      reapplyTicket.executeOnce(boost::bind(&HueDevice::reapplyTimerHandler, this), INITIAL_REAPPLY_DELAY);
    }
  }
}



void HueDevice::reapplyTimerHandler()
{
  reapplyTicket = 0;
  reapplyCount++;
  ALOG(reapplyCount>1 ? LOG_DEBUG : LOG_INFO, "Re-applying values to hue (%d. time) to make sure light actually is udpated", reapplyCount);
  applyLightState(NULL, false, true);
  if (reapplyMode==reapply_periodic) {
    // re-apply periodically -> schedule next
    reapplyTicket.executeOnce(boost::bind(&HueDevice::reapplyTimerHandler, this), PERIODIC_REAPPLY_INTERVAL);
  }
}




bool HueDevice::applyLightState(SimpleCB aDoneCB, bool aForDimming, bool aAnyway)
{
  // Update of light state needed
  LightBehaviourPtr l = getOutput<LightBehaviour>();
  if (l) {
    if (!aAnyway && !needsToApplyChannels()) {
      // NOP for this call
      channelValuesSent(l, aDoneCB, JsonObjectPtr(), ErrorPtr());
      return false; // no changes
    }
    ColorLightBehaviourPtr cl = getOutput<ColorLightBehaviour>();
    MLMicroSeconds transitionTime = 0; // undefined so far
    // build hue API light state
    string url = string_format("/lights/%s/state", lightID.c_str());
    JsonObjectPtr newState = JsonObject::newObj();
    // brightness is always re-applied unless it's dimming
    bool lightIsOn = true; // assume on
    if (aAnyway || !aForDimming || l->brightness->needsApplying()) {
      Brightness b = l->brightnessForHardware();
      transitionTime = l->transitionTimeToNewBrightness();
      if (b<DS_BRIGHTNESS_STEP) {
        // light off, no other parameters
        newState->add("on", JsonObject::newBool(false));
        lightIsOn = false;
      }
      else {
        // light on
        newState->add("on", JsonObject::newBool(true));
        newState->add("bri", JsonObject::newInt32(b*HUEAPI_FACTOR_BRIGHTNESS+HUEAPI_OFFSET_BRIGHTNESS+0.5)); // DS_BRIGHTNESS_STEP..100 -> 1..254
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
            if (aAnyway || !aForDimming || cl->hue->needsApplying()) {
              if (transitionTime==0) transitionTime = cl->hue->transitionTimeToNewValue();
              newState->add("hue", JsonObject::newInt32(cl->hue->getChannelValue()*HUEAPI_FACTOR_HUE+0.5));
            }
            if (aAnyway || !aForDimming || cl->saturation->needsApplying()) {
              if (transitionTime==0) transitionTime = cl->saturation->transitionTimeToNewValue();
              newState->add("sat", JsonObject::newInt32(cl->saturation->getChannelValue()*HUEAPI_FACTOR_SATURATION+0.5));
            }
            break;
          }
          case colorLightModeXY: {
            // x,y are always applied together
            if (aAnyway || cl->cieX->needsApplying() || cl->cieY->needsApplying()) {
              if (transitionTime==0) transitionTime = cl->cieX->transitionTimeToNewValue();
              if (transitionTime==0) transitionTime = cl->cieY->transitionTimeToNewValue();
              JsonObjectPtr xyArr = JsonObject::newArray();
              xyArr->arrayAppend(JsonObject::newDouble(cl->cieX->getChannelValue()));
              xyArr->arrayAppend(JsonObject::newDouble(cl->cieY->getChannelValue()));
              newState->add("xy", xyArr);
            }
            break;
          }
          case colorLightModeCt: {
            if (aAnyway || cl->ct->needsApplying()) {
              if (transitionTime==0) transitionTime = cl->ct->transitionTimeToNewValue();
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
    if (LOGENABLED(LOG_INFO) && (!aForDimming || LOGENABLED(LOG_DEBUG))) {
      ALOG(LOG_INFO, "sending new light state: light is %s, brightness=%0.0f, transition in %d mS", lightIsOn ? "ON" : "OFF", l->brightness->getChannelValue(), (int)(transitionTime/MilliSecond));
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
    // use transition time from (1/10 = 100mS second resolution)
    newState->add("transitiontime", JsonObject::newInt64(transitionTime/(100*MilliSecond)));
    hueComm().apiAction(httpMethodPUT, url.c_str(), newState, boost::bind(&HueDevice::channelValuesSent, this, l, aDoneCB, _1, _2));
  }
  return true;
}



void HueDevice::channelValuesSent(LightBehaviourPtr aLightBehaviour, SimpleCB aDoneCB, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (!reapplyTicket || reapplyCount>1) {
    // synchronize actual channel values as hue delivers them back, but only if not first re-apply still pending
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
              if (!val->boolValue()) {
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
        o = state->get("bri");
        if (o) {
          double hb = o->int32Value(); // hue brightness from 1..254
          if (hb<1) hb=1;
          l->syncBrightnessFromHardware(hb/HUEAPI_FACTOR_BRIGHTNESS, false); // only sync if no new value pending already
        }
      }
      else {
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
  string url = string_format("/lights/%s", lightID.c_str());
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
  if (uniqueID.empty()) {
    // we don't have an unique ID, identify relative to bridge's UUID
    s = vdcP->vdcInstanceIdentifier();
    s += "::" + hueVdc().bridgeUuid;
    s += ":" + lightID;
  }
  else {
    // we have a unique ID for the lamp itself, identify trough that
    s = "hueUniqueID::";
    s += uniqueID;
  }
  dSUID.setNameInSpace(s, vdcNamespace);
}


string HueDevice::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- hue unique ID: %s", uniqueID.c_str());
  return s;
}


#endif // ENABLE_HUE

