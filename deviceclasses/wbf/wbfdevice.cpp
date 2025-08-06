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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 6

#include "wbfdevice.hpp"

#if ENABLE_WBF

#include "wbfvdc.hpp"

using namespace p44;


// MARK: - WbfDevice


static const WbfSensorTypeInfo sensorInfos[] = {
  {  "temperature",  sensorType_temperature,     binInpType_none,        usage_room,       -40,     60,      0.025,        class_blue_climate, group_roomtemperature_control },
  {  "brightness",   sensorType_illumination,    binInpType_none,        usage_room,       0,       800000,  1,            class_yellow_light, group_yellow_light            },
  {  "illuminance",  sensorType_illumination,    binInpType_none,        usage_outdoors,   0,       800000,  1,            class_yellow_light, group_yellow_light            },
  {  "humidity",     sensorType_humidity,        binInpType_none,        usage_room,       0,       100,     0.025,        class_blue_climate, group_roomtemperature_control },
  {  "wind",         sensorType_wind_speed,      binInpType_none,        usage_outdoors,   0,       200,     0.025,        class_blue_climate, group_roomtemperature_control },
  {  "rain",         sensorType_none,            binInpType_rain,        usage_outdoors,   0,       1,       1,            class_blue_climate, group_roomtemperature_control },
  {  "hail",         sensorType_none,            binInpType_frost,       usage_outdoors,   0,       1,       1,            class_blue_climate, group_blue_ventilation        },
  {  nullptr,        sensorType_none,            binInpType_none,        usage_undefined }
};


static const WbfSensorTypeInfo* sensorTypeInfoByWbfType(const string& aType)
{
  const WbfSensorTypeInfo* siP = &sensorInfos[0];
  while (siP->wbfType) {
    if (aType==siP->wbfType) return siP;
    siP++;
  }
  return nullptr;
}




WbfDevice::WbfDevice(WbfVdc *aVdcP, uint8_t aSubdeviceIndex, JsonObjectPtr aDevDesc, JsonObjectPtr aOutDesc, JsonObjectPtr aInputsArr, int& aInputsUsed) :
  inherited(aVdcP),
  mSubDeviceIndex(aSubdeviceIndex),
  mLoadId(-1),
  mHasWhiteChannel(false)
{
  DBGOLOG(LOG_INFO,
    "device descriptions to build device from: {\n \"devDesc\": %s\n, \"outDesc\": %s\n, \"inputsArr\": %s\n}",
    JsonObject::text(aDevDesc),
    JsonObject::text(aOutDesc),
    JsonObject::text(aInputsArr)
  );
  JsonObjectPtr o;
  // scan device generics
  string defaultName;
  int namesFound = 0;
  aInputsUsed = 0;
  JsonObjectPtr blockA = aDevDesc->get("a");
  JsonObjectPtr blockC = aDevDesc->get("c");
  // - the id + general device infos
  if (aDevDesc->get("id", o)) mWbfId = o->stringValue();
  if (blockC) {
    if (blockC->get("comm_name", o)) {
      mWbfCommNames = o->stringValue();
      defaultName = mWbfCommNames; // also use the front set comm name as last resort fallback for the default name
    }
    // add reference number of front set
    if (blockC->get("comm_ref", o)) mWbfCommRefs = o->stringValue();
    if (blockC->get("serial_nr", o)) mSerialNos = o->stringValue();
  }
  if (blockA) {
    if (blockA->get("comm_name", o)) {
      string cn = o->stringValue();
      if (cn.size()>0 && mWbfCommNames!=cn) mWbfCommNames += "/" + cn; // second name only if not same as first one
    }
    if (blockA->get("comm_ref", o)) {
      string cr = o->stringValue();
      if (cr.size()>0 && mWbfCommRefs!=cr) mWbfCommRefs += "/" + cr;
    }
    if (blockA->get("serial_nr", o)) mSerialNos += "/" + o->stringValue(); // second name only if not same as first one
  }
  // initialize last seen
  if (aDevDesc->get("last_seen", o)) mLastSeen = MainLoop::now()-(o->doubleValue()*Second);
  // - the output
  JsonObjectPtr loadState;
  if (aOutDesc) {
    // check the load, it determines the actual output
    JsonObjectPtr loadDesc = aOutDesc->get("load_info"); // our own field transporting the load associated with the output
    if (loadDesc) {
      if (loadDesc->get("id", o)) mLoadId = o->int32Value();
      // when we have a load, use its name
      if (loadDesc->get("name", o)) {
        namesFound++;
        defaultName = o->stringValue();
      }
      loadState = loadDesc->get("state");
      int appKind = 0;
      if (loadDesc->get("kind", o)) appKind = o->int32Value();
      if (loadDesc->get("type", o)) {
        string outType = o->stringValue();
        if (outType=="onoff") {
          // joker but light by default
          setColorClass(class_black_joker);
          installSettings(DeviceSettingsPtr(new LightDeviceSettings(*this)));
          LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*this));
          l->setGroupMembership(group_yellow_light, true); // put into light group by default
          l->setHardwareOutputConfig(outputFunction_switch, outputmode_binary, usage_undefined, false, -1);
          l->setHardwareName("on/off");
          addBehaviour(l);
        }
        else if (outType=="dim") {
          // joker but light by default
          setColorClass(class_black_joker);
          installSettings(DeviceSettingsPtr(new LightDeviceSettings(*this)));
          // - add simple single-channel light behaviour
          LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*this));
          l->setGroupMembership(group_yellow_light, true); // put into light group by default
          l->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, false, -1);
          l->setHardwareName("dimmer");
          addBehaviour(l);
        }
        else if (outType=="dali") {
          setColorClass(class_black_joker);
          // depends on what the state has
          ColorLightMode mode = colorLightModeNone; // default to dimmer
          mHasWhiteChannel = false;
          if (loadState) {
            if (loadState->get("red", o)) {
              // RGB(W)
              mode = colorLightModeRGBWA;
              if (loadState->get("white", o)) {
                mHasWhiteChannel = true;
              }
            }
            else if (loadState->get("ct")) {
              mode = colorLightModeCt;
            }
          }
          // now create the output
          switch (mode) {
            case colorLightModeRGBWA: {
              installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
              // - add multi-channel color light behaviour (which adds a number of auxiliary channels)
              RGBColorLightBehaviourPtr l = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this, false));
              l->setHardwareName("full color light");
              addBehaviour(l);
              break;
            }
            case colorLightModeCt: {
              installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
              // - add two-channel color light behaviour in CT only mode
              ColorLightBehaviourPtr l = ColorLightBehaviourPtr(new ColorLightBehaviour(*this, true));
              l->setHardwareName("color temperature light");
              addBehaviour(l);
              break;
            }
            default: {
              installSettings(DeviceSettingsPtr(new LightDeviceSettings(*this)));
              // - add simple single-channel light behaviour
              installSettings(DeviceSettingsPtr(new LightDeviceSettings(*this)));
              LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*this));
              l->setGroupMembership(group_yellow_light, true); // put into light group by default
              l->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, false, -1);
              l->setHardwareName("dimmer");
              addBehaviour(l);
              break;
            }
          }
        }
        else if (outType=="motor") {
          // - use shadow scene settings
          installSettings(DeviceSettingsPtr(new ShadowDeviceSettings(*this)));
          // - add shadow behaviour
          ShadowBehaviourPtr sb = ShadowBehaviourPtr(new ShadowBehaviour(*this, group_grey_shadow));
          sb->setHardwareOutputConfig(outputFunction_positional, outputmode_gradual, usage_undefined, false, -1);
          sb->setHardwareName("Motor");
          ShadowDeviceKind sk;
          // appKind: Motor:0, Venetian blinds:1, Roller shutters:2, Awnings:3
          switch (appKind) {
            default:
            case 0: sk = shadowdevice_jalousie; break;
            case 1: sk = shadowdevice_sunblind; break;
            case 2: sk = shadowdevice_rollerblind; break;
          }
          sb->setDeviceParams(sk, false, 0, 0, 0, false); // absolute movements
          addBehaviour(sb);
        }
      }
    }
  } // output
  if (!getOutput()) {
    // no output, just install minimal settings without scenes
    installSettings();
  }
  // process inputs (and delete those we picked)
  if (aInputsArr) {
    int iidx = 0;
    int buttonsTaken = 0;
    while (iidx<aInputsArr->arrayLength()) {
      JsonObjectPtr inpDesc = aInputsArr->arrayGet(iidx);
      string inputDesc;
      if (inpDesc->get("type", o)) inputDesc = o->stringValue();
      if (inpDesc->get("sub_type", o)) inputDesc += "/" + o->stringValue();
      JsonObjectPtr sensorInfo;
      JsonObjectPtr buttonInfo;
      if (inpDesc->get("sensor_info", sensorInfo)) {
        if (sensorInfo->get("channel", o)) inputDesc.insert(0, string_format("%d:", o->int32Value()));
        // this is a sensor (or binary input aka "bool" sensor)
        if (sensorInfo->get("type", o)) {
          const WbfSensorTypeInfo* sensorDesc = sensorTypeInfoByWbfType(o->stringValue());
          int sensorId = 0;
          if (sensorInfo->get("id", o)) sensorId = o->int32Value();
          if (sensorInfo->get("name", o) && namesFound==0) { defaultName = o->stringValue(); namesFound++; }
          if (sensorDesc) {
            if (sensorDesc->vdcSensorType!=sensorType_none) {
              SensorBehaviourPtr sb = SensorBehaviourPtr(new SensorBehaviour(*this, "")); // automatic id if not specified
              sb->setHardwareSensorConfig(sensorDesc->vdcSensorType, sensorDesc->usageHint, sensorDesc->min, sensorDesc->max, sensorDesc->resolution, 0, 0);
              sb->setGroup(sensorDesc->group);
              sb->setHardwareName(inputDesc);
              if (namesFound==0) { defaultName = inputDesc; namesFound++; }
              mPendingInputMappings[sensorId] = sb;
              addBehaviour(sb);
              aInputsArr->arrayDel(iidx); // delete this input from the list
              aInputsUsed++; // count it
              continue; // same index now has another input (or array exhausted)
            }
            else if (sensorDesc->dsInputType!=binInpType_none) {
              BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this, "")); // automatic id if not specified
              ib->setHardwareInputConfig(sensorDesc->dsInputType, sensorDesc->usageHint, true, 0, 0);
              ib->setGroup(sensorDesc->group);
              ib->setHardwareName(inputDesc);
              if (namesFound==0) { defaultName = inputDesc; namesFound++; }
              mPendingInputMappings[sensorId] = ib;
              addBehaviour(ib);
              aInputsArr->arrayDel(iidx); // delete this input from the list
              aInputsUsed++; // count it
              continue; // same index now has another input (or array exhausted)
            }
          }
        }
      } // if sensorinfo
      else if (inpDesc->get("button_info", buttonInfo)) {
        if (buttonInfo->get("channel", o)) inputDesc.insert(0, string_format("%d:", o->int32Value()));
        // for now, only add buttons which have a non-null ID, which are those meant to act as "smartbutton"
        // for now, only take ONE button (which can be a two-way rocker) per device
        if (buttonInfo->get("id", o) && !o->isType(json_type_null) && buttonsTaken==0) {
          if (buttonInfo->get("name", o) && namesFound==0) { defaultName = o->stringValue(); namesFound++; }
          int buttonId = o->int32Value();
          VdcButtonType bty = buttonType_single;
          if (buttonInfo->get("subtype", o) && o->stringValue()=="up down") bty = buttonType_2way;
          // non-null ID, is a smart button, pick it
          ButtonBehaviourPtr bb = ButtonBehaviourPtr(new ButtonBehaviour(*this, "")); // automatic id if not specified
          bb->setHardwareButtonConfig(0, bty, bty==buttonType_2way ? buttonElement_up : buttonElement_center, false, 1, 0);
          bb->setGroup(group_yellow_light); // pre-configure for light...
          bb->setFunction(buttonFunc_app); // ...but only as app button
          bb->setHardwareName(bty==buttonType_2way ? "up" : "button");
          if (namesFound==0) { defaultName = inputDesc; namesFound++; }
          // this is the primary behaviour, secondary button, if any, does not need to be registered
          mPendingInputMappings[buttonId] = bb;
          addBehaviour(bb);
          if (bty==buttonType_2way) {
            // need the other half, add the "down" element
            bb = ButtonBehaviourPtr(new ButtonBehaviour(*this, "")); // automatic id if not specified
            bb->setHardwareButtonConfig(0, buttonType_2way, buttonElement_down, false, 0, 0);
            bb->setGroup(group_yellow_light); // pre-configure for light
            bb->setHardwareName("down");
            addBehaviour(bb);
          }
          buttonsTaken++; // we've taken one
          aInputsArr->arrayDel(iidx); // delete this input from the list
          aInputsUsed++; // count it
          continue; // same index now has another input (or array exhausted)
        }
      }
      // input not eaten up, check next
      iidx++;
    } // while unprocessed inputs
  }
  // set the name
  initializeName(defaultName);
  // derive the dSUID
  deriveDsUid();
}


WbfDevice::~WbfDevice()
{
  // unregister ids
  wbfVdc().unregisterBehaviourMap(wbfVdc().mLoadsMap, getOutput());
  for (int i=0; i<numSensors(); i++) wbfVdc().unregisterBehaviourMap(wbfVdc().mSensorsMap, getSensor(i));
  for (int i=0; i<numInputs(); i++) wbfVdc().unregisterBehaviourMap(wbfVdc().mSensorsMap, getInput(i));
  for (int i=0; i<numButtons(); i++) wbfVdc().unregisterBehaviourMap(wbfVdc().mButtonsMap, getButton(i));
}



bool WbfDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}


string WbfDevice::getExtraInfo()
{
  return string_format("wbf ID: %s", mWbfId.c_str());
}



WbfVdc &WbfDevice::wbfVdc()
{
  return *(static_cast<WbfVdc *>(mVdcP));
}


WbfComm &WbfDevice::wbfComm()
{
  return (static_cast<WbfVdc *>(mVdcP))->mWbfComm;
}



void WbfDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // only now, when we are actually added to the vdc, register behaviours for websocket callbacks
  // Note: we can't do that at creation, because when our device is later recognized as a duplicate
  //   of an already existing device, it MUST NOT yet have overwritten that device's mappings!
  //   (because then the duplicate will get discarded)
  // - loads
  if (getOutput()) {
    wbfVdc().mLoadsMap[mLoadId] = getOutput();
    FOCUSOLOG("registered load id %d in loadsMap", mLoadId);
  }
  // - inputs (sensors and buttons)
  for (PartIdToBehaviourMap::iterator pos = mPendingInputMappings.begin(); pos!=mPendingInputMappings.end(); ++pos) {
    DsBehaviour* bP = pos->second.get();
    if (dynamic_cast<ButtonBehaviour*>(bP)) {
      wbfVdc().mButtonsMap[pos->first] = bP;
      FOCUSPOLOG(bP, "registered id %d in buttonsMap", pos->first);
    }
    if (dynamic_cast<SensorBehaviour*>(bP) || dynamic_cast<BinaryInputBehaviour*>(bP)) {
      wbfVdc().mSensorsMap[pos->first] = bP;
      FOCUSPOLOG(bP, "registered id %d in sensorsMap", pos->first);
    }
  }
  mPendingInputMappings.clear();
  aCompletedCB(ErrorPtr());
}


string WbfDevice::modelName()
{
  return string_format("%s (%s)", mWbfCommNames.c_str(), mWbfCommRefs.c_str());
}


string WbfDevice::hardwareGUID()
{
  return string_format("wbfid:%s", mWbfId.c_str());
}

string WbfDevice::modelVersion() const
{
  // TODO: implement
  return "";
}


string WbfDevice::vendorName()
{
  return "Feller";
};


int WbfDevice::opStateLevel()
{
  MLMicroSeconds seen = MainLoop::now()-mLastSeen;
  MLMicroSeconds good = 5*Minute;
  MLMicroSeconds bad = 4*Hour;
  return 100-(limited(seen, good, bad)-good)/(bad-good)*100;
}


string WbfDevice::getOpStateText()
{
  // TODO: implement
  return "";
}



#define PRESENT_WHEN_SEEN_EARLIER_THAN (10*Minute)

void WbfDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  wbfVdc().mWbfComm.apiQuery(string_format("/devices/%s", mWbfId.c_str()).c_str(), boost::bind(&WbfDevice::deviceInfoReceived, this, aPresenceResultHandler, _1, _2));
}


void WbfDevice::deviceInfoReceived(PresenceCB aPresenceResultHandler, JsonObjectPtr aDeviceInfo, ErrorPtr aError)
{
  JsonObjectPtr o;
  bool reachable = false;
  if (Error::isOK(aError) && aDeviceInfo) {
    if (aDeviceInfo->get("last_seen", o)) {
      mLastSeen = MainLoop::now()-(o->doubleValue()*Second);
      reachable = MainLoop::now()-mLastSeen < PRESENT_WHEN_SEEN_EARLIER_THAN;
    }
  }
  aPresenceResultHandler(reachable);
}


bool WbfDevice::canIdentifyToUser()
{
  return true; // all with buttons can, TODO: maybe not true for sensors and din-rail stuff
}



#define IDENTIFY_BLINK_PERIOD (700*MilliSecond)
#define DEFAULT_NUM_BLINKS 3

void WbfDevice::identifyToUser(MLMicroSeconds aDuration)
{
  if (aDuration<0) {
    mIdentifyTicket.cancel(); // stop it
  }
  else {
    int numBlinks = aDuration==Never ? DEFAULT_NUM_BLINKS : (int)(aDuration/IDENTIFY_BLINK_PERIOD)+1;
    identifyBlink(numBlinks);
  }
}


void WbfDevice::identifyBlink(int aRemainingBlinks)
{
  wbfVdc().mWbfComm.apiQuery(string_format("/devices/%s/ping", mWbfId.c_str()).c_str(), NoOP);
  aRemainingBlinks--;
  if (aRemainingBlinks>0) {
    mIdentifyTicket.executeOnce(boost::bind(&WbfDevice::identifyBlink, this, aRemainingBlinks), IDENTIFY_BLINK_PERIOD);
  }
}


// Main-Type  Sub-Type  Attr.
// ---------  --------  -----------------------------
// onoff                bri
// dim                  bri
// motor                level, tilt
// dali                 bri
// dali       tw        bri, ct
// dali       rgb       bri, red, green, blue, white

// Minimum and maximum values:
//
// Attr.    min.      max.
// bri      0         10000
// level    0         10000
// tilt     0         9
// ct       1000      20000
// red      0         255
// green    0         255
// blue     0         255
// white    0         255


void WbfDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  OutputBehaviourPtr ob = getOutput();
  LightBehaviourPtr lb = getOutput<LightBehaviour>();
  ColorLightBehaviourPtr clb = getOutput<ColorLightBehaviour>();
  ShadowBehaviourPtr sb = getOutput<ShadowBehaviour>();
  MLMicroSeconds transitionTime = 0;
  if (needsToApplyChannels(&transitionTime)) {
    // prepare a target state
    JsonObjectPtr targetState = JsonObject::newObj();
    if (sb) {
      // shadow
      targetState->add("level", JsonObject::newInt32((100-sb->mPosition->getChannelValue())*100)); // value is in 0..10000 range for 100%..0% window OPEN (in dS)
      targetState->add("tilt", JsonObject::newInt32(sb->mAngle->getChannelValue()/11.111111111)); // value is 0..9 range for 0..100% tilt
    }
    else {
      // light or plain output
      if (lb && lb->brightnessNeedsApplying()) {
        targetState->add("bri", JsonObject::newInt32(lb->brightnessForHardware(true)*100)); // value is in 0..10000 range for 0..100%
      }
      else {
        // just output, send default channel as bri
        targetState->add("bri", JsonObject::newInt32(ob->getChannelByType(channeltype_default)->getChannelValue()*100)); // value is in 0..10000 range for 0..100%
      }
      if (clb) {
        // color or ct light
        if (clb->isCtOnly()) {
          double mired = clb->getChannelByType(channeltype_colortemp)->getChannelValue();
          mired = 1000000 / (mired>0 ? mired : 100);
          targetState->add("ct", JsonObject::newInt32(mired));
        }
        else {
          RGBColorLightBehaviourPtr rgblb = getOutput<RGBColorLightBehaviour>();
          if (rgblb) {
            // full color light
            double r,g,b,w;
            if (mHasWhiteChannel) {
              rgblb->getRGBW(r, g, b, w, 255, true, false);
              targetState->add("white", JsonObject::newInt32(w));
            }
            else {
              rgblb->getRGB(r, g, b, 255, true, false);
            }
            targetState->add("red", JsonObject::newInt32(r));
            targetState->add("green", JsonObject::newInt32(g));
            targetState->add("blue", JsonObject::newInt32(b));
          }
        }
      }
    } // light or plain output
    // now send the new target state
    wbfVdc().mWbfComm.apiAction(
      WbfApiOperation::PUT,
      string_format("/loads/%d/target_state", mLoadId).c_str(),
      targetState,
      boost::bind(&WbfDevice::targetStateApplied, this, aDoneCB, _1, _2)
    );
  }
  else {
    // nothing to apply
    if (aDoneCB) aDoneCB();
  }
}


void WbfDevice::targetStateApplied(SimpleCB aDoneCB, JsonObjectPtr aApplyStateResult, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    allChannelsApplied();
  }
  else {
    OLOG(LOG_WARNING, "Error applying new target state: %s", Error::text(aError));
  }
  if (aDoneCB) aDoneCB();
}


void WbfDevice::syncChannelValues(SimpleCB aDoneCB)
{
  // query state of our load
  wbfVdc().mWbfComm.apiQuery(string_format("/loads/%d/state", mLoadId).c_str(), boost::bind(&WbfDevice::loadStateReceived, this, aDoneCB, _1, _2));
}


void WbfDevice::loadStateReceived(SimpleCB aDoneCB, JsonObjectPtr aLoadStateResult, ErrorPtr aError)
{
  FOCUSOLOG("received load state query result: error=%s, result=%s", Error::text(aError), JsonObject::text(aLoadStateResult));
  if (aLoadStateResult && Error::isOK(aError)) {
    mLastSeen = MainLoop::now(); // receiving state means seen now
    // extract the current channel values
    JsonObjectPtr state = aLoadStateResult->get("state");
    if (state && getOutput()) {
      handleLoadState(state, getOutput());
    }
  }
  // done
  if (aDoneCB) aDoneCB();
}


void WbfDevice::handleSensorState(JsonObjectPtr aState, DsBehaviourPtr aBehaviour)
{
  FOCUSOLOG("received sensor state: %s for behaviour '%s'", JsonObject::text(aState), aBehaviour->behaviourTypeIdentifier());
  mLastSeen = MainLoop::now(); // receiving sensor state means seen now
  JsonObjectPtr o;
  // {"id":177,"value":27.7}
  BinaryInputBehaviourPtr ib = dynamic_pointer_cast<BinaryInputBehaviour>(aBehaviour);
  if (ib) {
    if(aState->get("value", o)) {
      if (o->isType(json_type_null)) ib->invalidateInputState();
      else ib->updateInputState(o->boolValue()); // TODO: is it really a bool here?
    }
    return;
  }
  SensorBehaviourPtr sb = dynamic_pointer_cast<SensorBehaviour>(aBehaviour);
  if (sb) {
    if(aState->get("value", o)) {
      if (o->isType(json_type_null)) sb->invalidateSensorValue();
      else sb->updateSensorValue(o->doubleValue());
    }
    return;
  }
}


void WbfDevice::handleButtonCmd(JsonObjectPtr aCmd, DsBehaviourPtr aBehaviour)
{
  FOCUSOLOG("received button cmd: %s for behaviour '%s'", JsonObject::text(aCmd), aBehaviour->behaviourTypeIdentifier());
  mLastSeen = MainLoop::now(); // receiving button command means seen now
  JsonObjectPtr o;
  if (aCmd->get("event", o)) {
    // TODO: maybe there are also multi-clicks and press&hold?
    if (o->stringValue()=="click") {
      int targetButton = 0;
      if (numButtons()>1) {
        // could be the other button
        if (aCmd->get("type", o)) {
          #warning "Assumption, need to check with up/down smart button that actually sends events"
          if (o->stringValue()=="down") {
            targetButton = 1;
          }
        }
      }
      // inform the button
      getButton(targetButton)->injectClick(ct_tip_1x);
    }
  }
}



void WbfDevice::handleLoadState(JsonObjectPtr aState, DsBehaviourPtr aBehaviour)
{
  FOCUSOLOG("received load state: %s for behaviour '%s'", JsonObject::text(aState), aBehaviour->behaviourTypeIdentifier());
  mLastSeen = MainLoop::now(); // receiving state means seen now
  JsonObjectPtr o;
  // {"bri":500,"flags":{"short_circuit":0,"fading":1,"noise":0,"direction":0,"rx_error":0}}
  OutputBehaviourPtr ob = dynamic_pointer_cast<OutputBehaviour>(aBehaviour);
  if (ob) {
    ChannelBehaviourPtr mainChannel = ob->getChannelByType(channeltype_default);
    double mainValue = 0;
    if (aState->get("bri", o)) mainValue = o->doubleValue()/100; // value is in 0..10000 range for 0..100%
    FOCUSOLOG("- mainvalue from 'bri' = %.1f, channel '%s' %s apply pending", mainValue, mainChannel->getName(), mainChannel->needsApplying() ? "HAS" : "no");
    // check details
    ShadowBehaviourPtr sb = dynamic_pointer_cast<ShadowBehaviour>(aBehaviour);
    if (sb) {
      bool isMoving = false;
      if (aState->get("moving", o)) isMoving = o->stringValue()!="stop"; // "up" or "down"
      if (aState->get("level", o)) mainValue = 100-(o->doubleValue()/100); // value is in 0..10000 range for 100%..0% window OPEN (in dS)
      if (isMoving) {
        // transitional
        mainChannel->reportChannelProgress(mainValue);
      }
      else {
        // final
        mainChannel->syncChannelValue(mainValue);
        if (sb->mAngle) {
          if (aState->get("tilt", o)) {
            double tiltvalue = o->doubleValue()*11.111111111; // value is 0..9 range for 0..100% tilt
            sb->mAngle->syncChannelValue(tiltvalue);
          }
        }
      }
      return;
    } // shadow
    LightBehaviourPtr lb = dynamic_pointer_cast<LightBehaviour>(aBehaviour);
    if (lb) {
      lb->syncBrightnessFromHardware(mainValue);
      ColorLightBehaviourPtr clb = dynamic_pointer_cast<ColorLightBehaviour>(lb);
      if (clb) {
        if (clb->isCtOnly()) {
          // color temperature light
          if (aState->get("ct", o)) {
            double ctvalue = o->doubleValue();
            ctvalue = 1000000/(ctvalue>1000 ? ctvalue : 1000); // value is color temp in K 1000..20000, we need mireds = 1E6/ct
            clb->mCt->syncChannelValue(ctvalue);
          }
        }
        else {
          // full color light
          RGBColorLightBehaviourPtr rgblb = dynamic_pointer_cast<RGBColorLightBehaviour>(clb);
          if (rgblb) {
            double r=0, g=0, b=0;
            if (aState->get("red", o)) r = o->doubleValue();
            if (aState->get("green", o)) g = o->doubleValue();
            if (aState->get("blue", o)) b = o->doubleValue();
            if (mHasWhiteChannel) {
              // RGBW
              double w=0;
              if (aState->get("white", o)) w = o->doubleValue();
              rgblb->setRGBW(r, g, b, w, 255, true); // brightness is separate
            }
            else {
              // RGB
              rgblb->setRGB(r, g, b, 255, true); // brightness is separate
            }
          }
        }
      }
      return;
    } // light
    // just a generic output
    ob->getChannelByType(channeltype_default)->syncChannelValue(mainValue);
    return;
  }
}


bool WbfDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  const char *colorediconname = "wbf";
  const char *iconname = NULL;
  if (getOutput()) {
    iconname = nullptr;
    switch (getOutput()->getOutputFunction()) {
      case outputFunction_colordimmer: iconname = "wbf_color"; break;
      case outputFunction_ctdimmer: iconname = "wbf_ct"; break;
      case outputFunction_dimmer:
        if (getOutput()->isMember(group_yellow_light)) iconname = "wbf_dim";
        break;
      case outputFunction_positional: iconname = "wbf_motor"; break;
      default: break;
    }
  }
  else {
    if (numButtons()>0) colorediconname = "wbf_btn";
    else if (numSensors()>0 || numInputs()>0) iconname = "wbf_sens";
  }
  if (iconname && getIcon(iconname, aIcon, aWithData, aResolutionPrefix)) return true;
  if (getClassColoredIcon(colorediconname, getDominantColorClass(), aIcon, aWithData, aResolutionPrefix)) return true;
  return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}



void WbfDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  // - UUIDv5 with name = wbfUniqueId::uniqueID
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s;
  // we have a unique ID for the entire device (a+c blocks)
  s = "wbfUniqueId::";
  s += mWbfId;
  mDSUID.setNameInSpace(s, vdcNamespace);
  mDSUID.setSubdeviceIndex(mSubDeviceIndex);
}


string WbfDevice::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- wiser device ID: %s", mWbfId.c_str());
  if (mLoadId>=0) string_format_append(s, "\n- output load id: %d", mLoadId);
  return s;
}


#endif // ENABLE_WBF

