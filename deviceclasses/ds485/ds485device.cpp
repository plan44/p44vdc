//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2024 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 7

#include "ds485device.hpp"

#if ENABLE_DS485DEVICES

#include "ds485vdc.hpp"

#include "outputbehaviour.hpp"
#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "colorlightbehaviour.hpp"
#include "shadowbehaviour.hpp"

#include <math.h>

using namespace p44;


Ds485Device::Ds485Device(Ds485Vdc *aVdcP, DsUid& aDsmDsUid, uint16_t aDevId, DsZoneID aZoneId) :
  inherited((Vdc *)aVdcP),
  mDs485Vdc(*aVdcP),
  mDsmDsUid(aDsmDsUid),
  mDevId(aDevId),
  mDS485ZoneId(aZoneId),
  mIsPresent(false),
  mNumOPC(0),
  mUpdatingCache(false),
  m16BitBuffer(0),
  mTracedScene(INVALID_SCENE_NO)
{
}


Ds485Device::~Ds485Device()
{
}


bool Ds485Device::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}


Ds485Vdc &Ds485Device::getDs485Vdc()
{
  return *(static_cast<Ds485Vdc *>(mVdcP));
}


string Ds485Device::deviceTypeIdentifier() const
{
  return "ds485";
}


string Ds485Device::modelName()
{
  return "dS terminal block"; // intentionally, old way to write dS
}


string Ds485Device::hardwareGUID()
{
  return "dsid:"+mDSUID.getDSIdString();
}


string Ds485Device::webuiURLString()
{
  return getVdc().webuiURLString();
}


string Ds485Device::vendorName()
{
  return "digitalSTROM"; // intentionally, old way to write dS
}


string Ds485Device::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- dSM: %s, devId=0x%04x, OPC=%d", mDsmDsUid.getString().c_str(), mDevId, mNumOPC);
  return s;
}


bool Ds485Device::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getClassColoredIcon("ds485", getDominantColorClass(), aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


void Ds485Device::addedAndInitialized()
{
  // re-apply the zoneID we got from DS now, overriding possibly different zoneID from local persistence
  if (mDS485ZoneId!=getZoneID()) {
    OLOG(LOG_WARNING, "ds485 scanning changes zoneId from %d to %d", getZoneID(), mDS485ZoneId);
    setZoneID(mDS485ZoneId);
  }
  updatePresenceState(mIsPresent);
  // request output states
  requestOutputValueUpdate();
  // request sensor states
  for (uint8_t sidx = 0; sidx<mSensorInfos.size(); ++sidx) {
    requestSensorValueUpdate(sidx);
  }
  // request binary input states
  uint8_t iidx = 0;
  BinaryInputBehaviourPtr ib;
  while ((ib = getInput(iidx))) {
    requestInputValueUpdate(iidx);
    iidx++;
  }
}


// MARK: - message processing

void Ds485Device::handleDeviceUpstreamMessage(bool aIsSensor, uint8_t aKeyNo, DsClickType aClickType)
{
  if (aIsSensor) {
    // TODO: sensor
  }
  else {
    // button
    switch (aClickType) {
      case ct_local_off:
      case ct_local_on: {
        // device has been operated locally
        OutputBehaviourPtr o = getOutput();
        if (o) {
          OLOG(LOG_NOTICE, "dS device output locally switched, update output state");
          ChannelBehaviourPtr ch = o->getChannelByType(channeltype_default);
          if (ch) ch->syncChannelValueBool(aClickType==ct_local_on);
          o->reportOutputState();
        }
      }
      case ct_local_stop: {
        // TODO: not 100% clear when DS uses this
        // local stop (of blinds movement? - for sure not of dimming, dim stop is not reported)
        OutputBehaviourPtr o = getOutput();
        if (o) {
          OLOG(LOG_NOTICE, "dS device output locally stopped -> request actual output state");
          requestOutputValueUpdate();
        }
      }
      default: {
        // forward to button, if any
        ButtonBehaviourPtr b = getButton(0);
        if (b) {
          OLOG(LOG_NOTICE, "dS device button click received: clicktype=%s", ButtonBehaviour::clickTypeName(aClickType).c_str());
          b->injectClick(aClickType);
        }
        break;
      }
    }
  }
}


static double standardConv(uint16_t aValue, SensorBehaviourPtr aSensorBehaviour)
{
  return aSensorBehaviour->getMin()+(aValue*aSensorBehaviour->getResolution());
}

static double logConv(uint16_t aValue, SensorBehaviourPtr aSensorBehaviour)
{
  // lux = 10^(engineeringvalue/800)
  return pow(10, (double)aValue/800.0);
}


static const DsSensorTypeInfo sensorInfo[] = {
  // Internal:
  // dSTy  internal,  vdcTy                       usage             min      max      resolution    convfunc        colorclass          group
  {  3,    true,      sensorType_power,           usage_undefined,  0,       4092,    4,            &standardConv,  class_black_joker,  group_black_variable }, // zone power
  {  4,    true,      sensorType_power,           usage_undefined,  0,       4095,    1,            &standardConv,  class_black_joker,  group_black_variable }, // output power
  {  5,    true,      sensorType_current,         usage_undefined,  0,       4.095,   0.001,        &standardConv,  class_black_joker,  group_black_variable }, // output current
  {  6,    true,      sensorType_energy,          usage_undefined,  0,       40.95,   0.01,         &standardConv,  class_black_joker,  group_black_variable }, // energy counter
  {  61,   true,      sensorType_temperature,     usage_undefined,  -55,     125,     1,            &standardConv,  class_black_joker,  group_black_variable }, // chip temperature
  {  64,   true,      sensorType_current,         usage_undefined,  0,       16.380,  0.004,        &standardConv,  class_black_joker,  group_black_variable }, // output current of device
  {  65,   true,      sensorType_power,           usage_undefined,  0,       4095,    1,            &standardConv,  class_black_joker,  group_black_variable }, // output power in VA
  // public:
  // dSTy  internal,  vdcTy                       usage             min      max      resolution    convfunc
  {  9,    false,     sensorType_temperature,     usage_room,       -43.15,  59.225,  0.025,        &standardConv,  class_blue_climate, group_roomtemperature_control },
  {  10,   false,     sensorType_temperature,     usage_outdoors,   -43.15,  59.225,  0.025,        &standardConv,  class_blue_climate, group_roomtemperature_control },
  {  11,   false,     sensorType_illumination,    usage_room,       0,       131447,  1,            &logConv,       class_yellow_light, group_yellow_light            },
  {  12,   false,     sensorType_illumination,    usage_outdoors,   0,       131447,  1,            &logConv,       class_yellow_light, group_yellow_light            },
  {  13,   false,     sensorType_humidity,        usage_room,       0,       100,     0.025,        &standardConv,  class_blue_climate, group_roomtemperature_control },
  {  14,   false,     sensorType_humidity,        usage_outdoors,   0,       100,     0.025,        &standardConv,  class_blue_climate, group_roomtemperature_control },
  {  15,   false,     sensorType_air_pressure,    usage_outdoors,   200,     1024,    0.25,         &standardConv,  class_blue_climate, group_roomtemperature_control }, // FIXME: really correct?
  {  18,   false,     sensorType_wind_speed,      usage_outdoors,   0,       102.3,   0.025,        &standardConv,  class_blue_climate, group_roomtemperature_control },
  {  19,   false,     sensorType_wind_direction,  usage_outdoors,   0,       360,     1,            &standardConv,  class_blue_climate, group_roomtemperature_control },
  {  20,   false,     sensorType_precipitation,   usage_outdoors,   0,       102.3,   0.025,        &standardConv,  class_blue_climate, group_roomtemperature_control },
  {  21,   false,     sensorType_gas_CO2,         usage_outdoors,   0,       131447,  1,            &logConv,       class_blue_climate, group_blue_ventilation        },
  // terminator
  {  0,    true,      sensorType_none,            usage_undefined,  0,       0,       0,            NoOP }
};


const DsSensorTypeInfo* Ds485Device::sensorTypeInfoByDsType(uint8_t aDsSensorType)
{
  const DsSensorTypeInfo* siP = &sensorInfo[0];
  while (siP->convFunc) {
    if (siP->dsSensorType==aDsSensorType) return siP;
    siP++;
  }
  return nullptr;
}


void Ds485Device::setSensorInfoAtIndex(int aIndex, DsSensorInstanceInfo aInstanceInfo)
{
  if (mSensorInfos.size()<=aIndex) {
    mSensorInfos.resize(aIndex+1, DsSensorInstanceInfo());
  }
  mSensorInfos[aIndex] = aInstanceInfo;
}


void Ds485Device::processSensorValue12Bit(uint8_t aSensorIndex, uint16_t a12BitSensorValue)
{
  // Note: passed DS sensor index is not the same as our behaviour index, because not all sensors get mapped
  if (aSensorIndex>=mSensorInfos.size()) return; // fatal, index too high
  DsSensorInstanceInfo& si = mSensorInfos[aSensorIndex];
  SensorBehaviourPtr s = si.mSensorBehaviour;
  if (si.mSensorTypeInfoP && s) {
    double v = si.mSensorTypeInfoP->convFunc(a12BitSensorValue, s);
    s->updateSensorValue(v);
  }
}


void Ds485Device::processBinaryInputValue(uint8_t aBinaryInputIndex, uint8_t aBinaryInputValue)
{
  BinaryInputBehaviourPtr i = getInput(aBinaryInputIndex);
  if (i) {
    i->updateInputState(aBinaryInputValue);
  }
}



void Ds485Device::traceConfigValue(uint8_t aBank, uint8_t aOffs, uint8_t aByte)
{
  switch (aBank) {
    case 64: // RAM
      switch (aOffs) {
        case 0: {
          trace8bitChannelChange(nullptr, aByte, false);
          break;
        }
        case 2: // lobyte
        case 3: // hibyte
        case 4:
        case 5:
        case 6: // lobyte
        case 7: { // hibyte
          ShadowBehaviourPtr sb = getOutput<ShadowBehaviour>();
          if (sb) {
            // is shadow
            bool transitional = false;
            switch (aOffs) {
              case 6: // current
              case 2: // target
                m16BitBuffer = aByte; // lower byte of target or actual position
                break;
              case 7: // current
                transitional = true;
              case 3: { // target
                m16BitBuffer |= ((uint16_t)aByte)<<8; // upper byte of target or actual position
                // position is first channel
                ChannelBehaviourPtr ch = getOutput()->getChannelByIndex(0);
                trace16bitChannelChange(ch, m16BitBuffer, transitional);
                m16BitBuffer = 0;
                break;
              }
              case 5: // current lamella
                transitional = true;
              case 4: // target lamella
                // lamella is second channel
                trace8bitChannelChange(getOutput()->getChannelByIndex(1), aByte, transitional);
                break;
            }
          }
          // TODO: blue and green blocks have the relevant values in other offsets
          break;
        }
      }
      break;
  }
}


void Ds485Device::trace8bitChannelChange(ChannelBehaviourPtr aChannelOrNullForDefault, uint8_t a8BitChannelValue, bool aTransitional)
{
  trace16bitChannelChange(aChannelOrNullForDefault, (uint16_t)a8BitChannelValue<<8, aTransitional);
}


void Ds485Device::trace16bitChannelChange(ChannelBehaviourPtr aChannelOrNullForDefault, uint16_t a16BitChannelValue, bool aTransitional)
{
  OutputBehaviourPtr o = getOutput();
  if (!aChannelOrNullForDefault) {
    if (o) aChannelOrNullForDefault = o->getChannelByType(channeltype_default);
  }
  if (o && aChannelOrNullForDefault) {
    // TODO: maybe evaluate aTransitional?
    double newValue = (double)a16BitChannelValue*100/255/0x100;
    POLOG(aChannelOrNullForDefault, LOG_INFO,
      "got updated dS485 value: 16bit=0x%04x/%d 8bit=0x%02x/%d) = %.2f",
      a16BitChannelValue, a16BitChannelValue, a16BitChannelValue>>8, a16BitChannelValue>>8, newValue
    );
    aChannelOrNullForDefault->syncChannelValue(newValue);
    o->reportOutputState();
    // update local scene's value
    SceneDeviceSettingsPtr scenes = getScenes();
    if (scenes && mTracedScene!=INVALID_SCENE_NO) {
      OLOG(LOG_INFO, "tracing scene '%s' ongoing - store new channel value if applicable", VdcHost::sceneText(mTracedScene, false).c_str());
      DsScenePtr scene = scenes->getScene(mTracedScene);
      if (scene && !scene->isDontCare()) {
        SceneCmd c = scene->mSceneCmd;
        if (
          c==scene_cmd_invoke ||
          c==scene_cmd_off ||
          c==scene_cmd_min ||
          c==scene_cmd_max
        ) {
          // traced channel value originates from this scene -> update local value
          POLOG(aChannelOrNullForDefault, LOG_INFO, "updating value to %.1f in scene '%s'", newValue, VdcHost::sceneText(mTracedScene, false).c_str());
          scene->setSceneValue(aChannelOrNullForDefault->getChannelIndex(), newValue);
          mCachedScenes[mTracedScene] = true; // we are in sync now
          if (scene->isDirty()) {
            scenes->updateScene(scene);
            POLOG(aChannelOrNullForDefault, LOG_NOTICE, "changed to new value %.1f in scene '%s'", newValue, VdcHost::sceneText(mTracedScene, false).c_str());
          }
        }
      }
    }
  }
  mTracedScene = INVALID_SCENE_NO;
}


#define SCENE_APPLY_RESULT_SAMPLE_DELAY (3*Second)

void Ds485Device::traceSceneCall(SceneNo aSceneNo)
{
  if (mCachedScenes[aSceneNo]) {
    OLOG(LOG_INFO, "traceSceneCall '%s': taking scene value from cache to adjust local output channels", VdcHost::sceneText(aSceneNo, false).c_str());
    // we have the output value(s) cached that have been invoked with this scene
    // -> just simulate scene call to have our channel values adjusted
    mUpdatingCache = true; // prevent actual output update
    callScene(aSceneNo, true);
    mUpdatingCache = false;
  }
  else {
    // we do not yet have a cached value
    // -> wait a little for output to settle, then read back current value from device
    OLOG(LOG_INFO, "traceSceneCall '%s': output values not yet cached, schedule output value sampling", VdcHost::sceneText(aSceneNo, false).c_str());
    mTracingTimer.executeOnce(boost::bind(&Ds485Device::startTracingFor, this, aSceneNo), SCENE_APPLY_RESULT_SAMPLE_DELAY);
  }
}


void Ds485Device::requestOutputValueUpdate()
{
  if (getOutput()) {
    string payload;
    Ds485Comm::payload_append8(payload, 64); // bank RAM
    Ds485Comm::payload_append8(payload, 0); // offset outputvalue
    issueDeviceRequest(DEVICE_CONFIG, DEVICE_CONFIG_GET, payload);
  }
}


void Ds485Device::requestSensorValueUpdate(uint8_t aDsSensorIndex)
{
  if (aDsSensorIndex>=mSensorInfos.size()) return; // fatal, index too high
  DsSensorInstanceInfo& si = mSensorInfos[aDsSensorIndex];
  SensorBehaviourPtr s = si.mSensorBehaviour;
  if (s) {
    // request value of this sensor
    string payload;
    Ds485Comm::payload_append8(payload, aDsSensorIndex);
    issueDeviceRequest(DEVICE_SENSOR, DEVICE_SENSOR_GET_VALUE, payload);
  }
}


void Ds485Device::requestInputValueUpdate(uint8_t aDsInputIndex)
{
  // Ds and Vdc input indices map 1:1
  BinaryInputBehaviourPtr i = getInput(aDsInputIndex);
  if (i) {
    // request value of this input
    // FIXME: seems we can't trigger this, but maybe we can somehow
  }
}


void Ds485Device::processActionRequest(uint16_t aFlaggedModifier, const string aPayload, size_t aPli)
{
  bool invalidate = false;
  switch (aFlaggedModifier) {
    case DEV(DEVICE_ACTION_REQUEST_ACTION_SAVE_SCENE):
    case ZG(ZONE_GROUP_ACTION_REQUEST_ACTION_SAVE_SCENE):
      // saving scene must invalidate scene value cache
      invalidate = true;
    case DEV(DEVICE_ACTION_REQUEST_ACTION_CALL_SCENE):
    case DEV(DEVICE_ACTION_REQUEST_ACTION_FORCE_CALL_SCENE):
    case ZG(ZONE_GROUP_ACTION_REQUEST_ACTION_CALL_SCENE):
    case ZG(ZONE_GROUP_ACTION_REQUEST_ACTION_FORCE_CALL_SCENE):
    case ZG(ZONE_GROUP_ACTION_REQUEST_ACTION_CALL_SCENE_MIN):
    case ZG(ZONE_GROUP_ACTION_REQUEST_ACTION_LOCAL_STOP):
    case ZG(20): // FIXME: not in dsm-api-const, only in dsm-api.xml
    {
      // all these cause output to change to a unknown value, so we need to get the output
      // state after the command completes, and update our local scene value along
      SceneNo scene;
      if ((aPli = Ds485Comm::payload_get8(aPayload, aPli, scene))==0) return;
      if (invalidate) {
        mCachedScenes[scene] = false;
        OLOG(LOG_NOTICE, "scene '%s' saved -> trigger updating chache", VdcHost::sceneText(mTracedScene, false).c_str());
        // traceSceneCall will need to actually trace down the current, now saved value
      }
      traceSceneCall(scene);
      break;
    }
    case DEV(DEVICE_ACTION_REQUEST_ACTION_SET_OUTVAL):
    case ZG(ZONE_GROUP_ACTION_REQUEST_ACTION_SET_OUTVAL):
    {
      uint8_t outval;
      if ((aPli = Ds485Comm::payload_get8(aPayload, aPli, outval))==0) return;
      trace8bitChannelChange(nullptr, outval, false);
      break;
    }
  }
  return;
}



void Ds485Device::startTracingFor(SceneNo aSceneNo)
{
  // trigger request for current value
  OLOG(LOG_INFO, "query output values for updating scene '%s'", VdcHost::sceneText(aSceneNo, false).c_str());
  mTracedScene = aSceneNo;
  requestOutputValueUpdate();
  // - traceChannelChange will get the actual scene value
}


void Ds485Device::processPropertyRequest(uint16_t aFlaggedModifier, const string aPayload, size_t aPli)
{
  ButtonBehaviourPtr b = getButton(0);
  switch (aFlaggedModifier) {
    case DEV(DEVICE_PROPERTIES_SET_NAME): {
      string newname;
      if ((aPli = Ds485Comm::payload_getString(aPayload, aPli, 21, newname))==0) return;
      setName(newname); // update and propagate to bridges
      break;
    }
    case DEV(DEVICE_PROPERTIES_SET_ZONE): {
      uint16_t zoneId;
      if ((aPli = Ds485Comm::payload_get16(aPayload, aPli, zoneId))==0) return;
      setZoneID(zoneId); // update and propagate to bridges
      break;
    }
    case DEV(DEVICE_PROPERTIES_SET_BUTTON_ACTIVE_GROUP): {
      uint8_t buttonGroup;
      if ((aPli = Ds485Comm::payload_get8(aPayload, aPli, buttonGroup))==0) return;
      if (b) b->setGroup((DsGroup)buttonGroup);
      break;
    }
    case DEV(DEVICE_PROPERTIES_SET_BUTTON_SET_OUTPUT_CHANNEL): {
      uint8_t channelId;
      if ((aPli = Ds485Comm::payload_get8(aPayload, aPli, channelId))==0) return;
      if (b) b->setChannel((DsChannelType)channelId);
      break;
    }
    case DEV(DEVICE_PROPERTIES_SET_BUTTON_SET_LOCAL_PRIORITY): {
      uint8_t localprio;
      if ((aPli = Ds485Comm::payload_get8(aPayload, aPli, localprio))==0) return;
      if (b) b->setSetsLocalPriority(localprio);
      break;
    }
    case DEV(DEVICE_PROPERTIES_SET_BUTTON_SET_NO_COMING_HOME_CALL): {
      uint8_t preventPresent;
      if ((aPli = Ds485Comm::payload_get8(aPayload, aPli, preventPresent))==0) return;
      if (b) b->setCallsPresent(!preventPresent);
      break;
    }
  }
  return;
}



// MARK: - output

void Ds485Device::identifyToUser(MLMicroSeconds aDuration)
{
  issueDeviceRequest(DEVICE_ACTION_REQUEST, DEVICE_ACTION_REQUEST_ACTION_BLINK);
}


bool Ds485Device::prepareSceneApply(DsScenePtr aScene)
{
  // prevent applying when just updating cache
  if (mUpdatingCache) {
    OLOG(LOG_INFO, "NOT applying scene values - just updating cache");
    // just consider all channels already applied, which is true because
    // this callScene run was triggered by monitoring an actual
    // dS485 scene call of which we knew we have the values cached.
    // So we did not retrieve and sync channels, but fake-apply the scene
    allChannelsApplied();
    getOutput()->reportOutputState();
  }
  return !mUpdatingCache;
}


void Ds485Device::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  LightBehaviourPtr l = getOutput<LightBehaviour>();
  ColorLightBehaviourPtr cl = getOutput<ColorLightBehaviour>();
  ShadowBehaviourPtr sb = getOutput<ShadowBehaviour>();
  MLMicroSeconds transitionTime = 0;
  if (needsToApplyChannels(&transitionTime)) {
    if (cl) {
      // TODO: handle color
    }
    else if (l && l->brightnessNeedsApplying()) {
      string payload;
      Ds485Comm::payload_append8(payload, l->brightnessForHardware(true)*255/100);
      issueDeviceRequest(DEVICE_ACTION_REQUEST, DEVICE_ACTION_REQUEST_ACTION_SET_OUTVAL, payload);
      l->brightnessApplied();
    }
    else if (sb) {
      if (sb->mPosition->needsApplying()) {
        // set new target position
        uint16_t new16val = sb->mPosition->getChannelValue()*255*0x100/100;
        string payload;
        // hi byte
        Ds485Comm::payload_append8(payload, 64); // Bank: RAM
        Ds485Comm::payload_append8(payload, 3); // Offset 3: hi byte
        Ds485Comm::payload_append8(payload, new16val>>8);
        issueDeviceRequest(DEVICE_CONFIG, DEVICE_CONFIG_SET, payload);
        // lo byte
        payload.clear();
        Ds485Comm::payload_append8(payload, 64); // Bank: RAM
        Ds485Comm::payload_append8(payload, 2); // Offset 2: lo byte
        Ds485Comm::payload_append8(payload, new16val & 0xFF);
        issueDeviceRequest(DEVICE_CONFIG, DEVICE_CONFIG_SET, payload);
        sb->mPosition->channelValueApplied();
      }
      if (sb->mAngle && sb->mAngle->needsApplying()) {
        // set new angle
        string payload;
        Ds485Comm::payload_append8(payload, 64); // Bank: RAM
        Ds485Comm::payload_append8(payload, 4); // Target lamella angle
        Ds485Comm::payload_append8(payload, sb->mAngle->getChannelValue()*255/100);
        issueDeviceRequest(DEVICE_CONFIG, DEVICE_CONFIG_SET, payload);
        sb->mAngle->channelValueApplied();
      }
    }
    else {
      // simple unspecific output
      OutputBehaviourPtr o = getOutput();
      ChannelBehaviourPtr ch = o->getChannelByType(channeltype_default);
      string payload;
      Ds485Comm::payload_append8(payload, ch->getChannelValue()*255/100);
      issueDeviceRequest(DEVICE_ACTION_REQUEST, DEVICE_ACTION_REQUEST_ACTION_SET_OUTVAL, payload);
      ch->channelValueApplied();
    }
  }
  // confirm done
  if (aDoneCB) aDoneCB();
}




// MARK: - local method/notification handling

ErrorPtr Ds485Device::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  // TODO: no speical handling yet
  return inherited::handleMethod(aRequest, aMethod, aParams);
}


void Ds485Device::handleNotification(const string &aNotification, ApiValuePtr aParams, StatusCB aExaminedCB)
{
  // TODO: no speical handling yet
  inherited::handleNotification(aNotification, aParams, aExaminedCB);
}


void Ds485Device::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // TODO: no speical handling yet
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


// MARK: - ds485 helpers


ErrorPtr Ds485Device::issueDeviceRequest(uint8_t aCommand, uint8_t aModifier, const string& aMorePayload)
{
  string payload;
  Ds485Comm::payload_append16(payload, mDevId);
  payload.append(aMorePayload);
  return issueDsmRequest(aCommand, aModifier, payload);
}


ErrorPtr Ds485Device::issueDsmRequest(uint8_t aCommand, uint8_t aModifier, const string& aPayload)
{
  return mDs485Vdc.mDs485Comm.issueRequest(mDsmDsUid, aCommand, aModifier, aPayload);
}


void Ds485Device::executeDsmQuery(QueryCB aQueryCB, MLMicroSeconds aTimeout, uint8_t aCommand, uint8_t aModifier, const string& aPayload)
{
  mDs485Vdc.mDs485Comm.executeQuery(aQueryCB, aTimeout, mDsmDsUid, aCommand, aModifier, aPayload);
}



#endif // ENABLE_DS485DEVICES
