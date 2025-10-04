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
#define FOCUSLOGLEVEL 6

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
  mUnappliedScene(INVALID_SCENE_NO)
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
        // device has been operated locally and has invoked LOCAL_ON or OFF scene on the device
        OLOG(LOG_NOTICE, "dS device locally switched %s -> trace new output state", aClickType==ct_local_on ? "ON" : "OFF");
        traceSceneCall(aClickType==ct_local_on ? LOCAL_ON : LOCAL_OFF);
        break;
      }
      case ct_local_stop: {
        // TODO: not 100% clear when DS uses this
        // local stop (of blinds movement? - for sure not of dimming, dim stop is not reported)
        OutputBehaviourPtr o = getOutput();
        if (o) {
          OLOG(LOG_NOTICE, "dS device output locally stopped -> trace actual output state");
          requestOutputValueUpdate();
        }
        break;
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



void Ds485Device::traceConfigValue(uint8_t aBank, uint8_t aOffs, uint8_t aByte, bool aIsAnswer)
{
  switch (aBank) {
    case 64: { // RAM
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
    } // case RAM
    case 128: { // Scenes
      SceneNo sceneNo = aOffs & 0x7F;
      SceneDeviceSettingsPtr scenes = getScenes();
      if (scenes && sceneNo<NUM_VALID_SCENES) {
        DsScenePtr scene = scenes->getScene(sceneNo);
        if (scene) {
          if (aOffs<0x80) {
            // scene value
            double newValue = (double)aByte*100/0xFF;
            scene->setSceneValue(0, newValue); // just first channel
            if (!mCachedScenes[sceneNo]) {
              mCachedScenes[sceneNo] = true; // consider cached now
              OLOG(LOG_INFO, "scene '%s' now considered cached, channel[0]=%.1f, dontCare=%d", VdcHost::sceneText(sceneNo, false).c_str(), newValue, scene->isDontCare());
            }
            if (sceneNo==mUnappliedScene) {
              mUnappliedScene = INVALID_SCENE_NO;
              traceSceneCall(sceneNo);
            }
          }
          else {
            // scene config
            // - bit0 = don't care
            scene->setDontCare(aByte & 0x01);
            // - bit1 = ignore local priority
            scene->setIgnoreLocalPriority(aByte & 0x02);
            // - bit2 = special behaviour (ignore)
            // - bit3 = blink
            SimpleScenePtr simplescene = boost::dynamic_pointer_cast<SimpleScene>(scene);
            if (simplescene) {
              simplescene->setPVar(simplescene->mEffect, aByte & 0x08 ? scene_effect_alert : scene_effect_smooth);
              // TODO: maybe also map dim time to effects
            }
            // - bit6/7 = 0:no change, 1..3: use dimtime0..2
          }
          if (scene->isDirty()) {
            OLOG(LOG_INFO,
              "scene '%s' changed from config: channel[0]=%.1f, dontCare=%d, ignoreLocalPriority=%d",
              VdcHost::sceneText(sceneNo, false).c_str(), scene->sceneValue(0), scene->isDontCare(), scene->ignoresLocalPriority()
            );
            scenes->updateScene(scene);
          }
          else {
            FOCUSOLOG(
              "scene '%s' confirmed from config as: channel[0]=%.1f, dontCare=%d, ignoreLocalPriority=%d",
              VdcHost::sceneText(sceneNo, false).c_str(), scene->sceneValue(0), scene->isDontCare(), scene->ignoresLocalPriority()
            );
          }
        }
      }
    } // case Scenes
    case 129: { // Shadow Position bits 7..4 of 16bit value
      SceneNo sceneNo = aOffs<<1;
      do {
        if (mCachedScenes[sceneNo]) {
          // we can refine only if we have alrerady cached the MSB
          uint8_t by = aByte & 0x0F;
          SceneDeviceSettingsPtr scenes = getScenes();
          if (scenes && sceneNo<NUM_VALID_SCENES) {
            ShadowScenePtr shadowscene = boost::dynamic_pointer_cast<ShadowScene>(scenes->getScene(sceneNo));
            if (shadowscene) {
              uint16_t val16 = shadowscene->sceneValue(0)*0xFFFF/100;
              val16 = (val16 & 0xFF00) + (by<<4) + by; // augment with new LSB, duplicated nibble (0xZ -> 0xZZ)
              double newValue = (double)val16*100/0xFFFF;
              shadowscene->setSceneValue(0, newValue);
              if (shadowscene->isDirty()) {
                OLOG(LOG_INFO, "scene '%s' position precision updated to %.1f", VdcHost::sceneText(sceneNo, false).c_str(), newValue);
                scenes->updateScene(shadowscene);
              }
              else {
                FOCUSOLOG("scene '%s' position precision confirmed at %.1f", VdcHost::sceneText(sceneNo, false).c_str(), newValue);
              }
            }
          }
        }
        aByte >>= 4; // Bits 0..3 are even scene's, 4..7 odd scene's
        sceneNo++; // next
      } while (sceneNo & 0x01); // continue only if next is odd
    }
    case 130: { // Shadow Scene Angles
      SceneNo sceneNo = aOffs;
      SceneDeviceSettingsPtr scenes = getScenes();
      if (scenes && sceneNo<NUM_VALID_SCENES) {
        ShadowScenePtr shadowscene = boost::dynamic_pointer_cast<ShadowScene>(scenes->getScene(sceneNo));
        if (shadowscene) {
          // bit 0 is reserved for direction with uncalibrated tilt, so ignored that here
          double newValue = (double)(aByte & 0xFE)*100/0xFF;
          shadowscene->setSceneValue(1, newValue); // just first channel
          if (shadowscene->isDirty()) {
            OLOG(LOG_INFO, "scene '%s' angle updated to %.1f", VdcHost::sceneText(sceneNo, false).c_str(), newValue);
            scenes->updateScene(shadowscene);
          }
          else {
            FOCUSOLOG("scene '%s' angle confirmed at %.1f", VdcHost::sceneText(sceneNo, false).c_str(), newValue);
          }
        }
      }
    } // case Shadow Scene Angles
  }
  confirmReadResult(aBank, aOffs);
}


void Ds485Device::trace8bitChannelChange(ChannelBehaviourPtr aChannelOrNullForDefault, uint8_t a8BitChannelValue, bool aTransitional)
{
  // duplicate MSB into LSB such that 8bit 0x00 -> 0x0000, 0xFF -> 0xFFFF, 0x7F -> 0x7F7F
  trace16bitChannelChange(aChannelOrNullForDefault, (uint16_t)(a8BitChannelValue<<8)+a8BitChannelValue, aTransitional);
}


void Ds485Device::trace16bitChannelChange(ChannelBehaviourPtr aChannelOrNullForDefault, uint16_t a16BitChannelValue, bool aTransitional)
{
  OutputBehaviourPtr o = getOutput();
  if (!aChannelOrNullForDefault) {
    if (o) aChannelOrNullForDefault = o->getChannelByType(channeltype_default);
  }
  if (o && aChannelOrNullForDefault) {
    // TODO: maybe evaluate aTransitional?
    double newValue = (double)a16BitChannelValue*100/0xFFFF;
    POLOG(aChannelOrNullForDefault, LOG_INFO,
      "got updated dS485 value: 16bit=0x%04x/%d 8bit=0x%02x/%d) = %.2f",
      a16BitChannelValue, a16BitChannelValue, a16BitChannelValue>>8, a16BitChannelValue>>8, newValue
    );
    aChannelOrNullForDefault->syncChannelValue(newValue);
    o->reportOutputState();
  }
}


#define SCENE_APPLY_RESULT_SAMPLE_DELAY (3*Second)

void Ds485Device::traceSceneCall(SceneNo aSceneNo)
{
  // check dimming first
  int area = 0;
  VdcDimMode dimMode = dimmode_stop;
  switch (aSceneNo) {
    // dim down
    case AREA_4_DEC: area++;
    case AREA_3_DEC: area++;
    case AREA_2_DEC: area++;
    case AREA_1_DEC: area++;
    case DEC_S:
      dimMode = dimmode_down;
      goto dim;
    // dim up
    case AREA_4_INC: area++;
    case AREA_3_INC: area++;
    case AREA_2_INC: area++;
    case AREA_1_INC: area++;
    case INC_S:
      dimMode = dimmode_up;
      goto dim;
    // dim stop
    case AREA_4_STOP_S: area++;
    case AREA_3_STOP_S: area++;
    case AREA_2_STOP_S: area++;
    case AREA_1_STOP_S: area++;
    case STOP_S:
      dimMode = dimmode_stop;
    dim: {
      traceDimChannel(channeltype_default, area, dimMode, LEGACY_DIM_STEP_TIMEOUT);
      // nothing more!
      return;
    }
    default: break;
  }
  // normal scene call
  if (mCachedScenes[aSceneNo]) {
    OLOG(LOG_INFO, "traceSceneCall '%s': taking scene value from cache to adjust local output channels", VdcHost::sceneText(aSceneNo, false).c_str());
    // we have the output value(s) cached that have been invoked with this scene
    // -> just simulate scene call to have our channel values adjusted
    mUpdatingCache = true; // prevent actual output update
    callScene(aSceneNo, true);
    mUpdatingCache = false;
  }
  else {
    // we do not yet have this scene cached -> request values
    OLOG(LOG_INFO, "traceSceneCall '%s': output values not yet cached, requesting scene config for adjusting local output channels later", VdcHost::sceneText(aSceneNo, false).c_str());
    requestSceneUpdate(aSceneNo);
    // mark call to this scene as not-yet-applied, so when scene update arrives, we can apply it
    mUnappliedScene = aSceneNo;
  }
}


void Ds485Device::traceDimChannel(DsChannelType aChannelType, int aArea, VdcDimMode aDimMode, MLMicroSeconds aAutoStopAfter)
{
  mDimTracingTimer.cancel();
  OutputBehaviourPtr o = getOutput();
  ChannelBehaviourPtr ch = o->getChannelByType(aChannelType);
  if (ch) {
    if (aDimMode==dimmode_stop) {
      // dimming stops, retrieve current output state
      POLOG(ch, LOG_INFO, "dimming STOPs, requesting output value update");
      requestOutputValueUpdate();
    }
    else {
      // dimming starts, just show, do nothing locally
      POLOG(ch, LOG_INFO, "starts dimming %s, autostop in %.1f s", aDimMode==dimmode_up ? "UP" : "DOWN", (double)aAutoStopAfter/Second);
      // schedule a sample well after automatic stop
      mDimTracingTimer.executeOnce(boost::bind(&Ds485Device::requestOutputValueUpdate, this), aAutoStopAfter*2);
    }
  }
}


void Ds485Device::requestSceneUpdate(SceneNo aSceneNo)
{
  if (getOutput()) {
    scheduleConfigRead(128, aSceneNo+0x80); // bank Scenes, scene config flags
    scheduleConfigRead(128, aSceneNo); // bank Scenes, scene values
    ShadowBehaviourPtr sh = getOutput<ShadowBehaviour>();
    if (sh) {
      if (sh->hasShadeBladeAngle()) {
        // also query the angle
        scheduleConfigRead(130, aSceneNo); // bank Shade blade angle scene values
      }
      // query more precise position
      scheduleConfigRead(129, aSceneNo>>1); // bank extension, stored in nibbles (two values in one byte)
    }
  }
}



void Ds485Device::requestOutputValueUpdate()
{
  if (getOutput()) {
    scheduleConfigRead(64, 0); // bank RAM, offset outputvalue
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
  VdcDimMode dimMode = dimmode_stop;
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
    case ZG(20): // action_dimming_stop, FIXME: not in dsm-api-const, only in dsm-api.xml
    {
      // all these cause output to change to a unknown value, so we need to get the output
      // state after the command completes, and update our local scene value along
      SceneNo scene;
      if ((aPli = Ds485Comm::payload_get8(aPayload, aPli, scene))==0) return;
      if (invalidate) {
        mCachedScenes[scene] = false;
        OLOG(LOG_NOTICE, "scene '%s' saved -> trigger updating chache", VdcHost::sceneText(scene, false).c_str());
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
    case ZG(ZONE_GROUP_ACTION_REQUEST_ACTION_OPC_INC):
    case DEV(DEVICE_ACTION_REQUEST_ACTION_OPC_INC):
      dimMode = dimmode_up;
      goto dim;
    case ZG(ZONE_GROUP_ACTION_REQUEST_ACTION_OPC_DEC):
    case DEV(DEVICE_ACTION_REQUEST_ACTION_OPC_DEC):
      dimMode = dimmode_down;
      goto dim;
    case ZG(ZONE_GROUP_ACTION_REQUEST_ACTION_OPC_STOP):
    case DEV(DEVICE_ACTION_REQUEST_ACTION_OPC_STOP):
      dimMode = dimmode_stop;
    dim: {
      uint8_t channelId;
      if ((aPli = Ds485Comm::payload_get8(aPayload, aPli, channelId))==0) return;
      traceDimChannel(channelId, 0, dimMode, MOC_DIM_STEP_TIMEOUT);
      break;
    }
  }
  return;
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
  return !mUpdatingCache; // actually apply only when this is not a cache update
  // TODO: we're completely lacking the mechanisms to apply local scene calls efficiently by invoking DS-side scenes for now
  // - this should be easy once we are sure our caching is complete, including the don't care flags.
  // - we can just forward the scene call to the device
  // - TODO: how would we detect and forward grouped scene calls?
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
      OLOG(LOG_WARNING, "DS native color light support not implemented (assumed not existing except on paper)");
    }
    else if (l && l->brightnessNeedsApplying()) {
      string payload;
      Ds485Comm::payload_append8(payload, l->brightnessForHardware(true)*0xFF/100);
      issueDeviceRequest(DEVICE_ACTION_REQUEST, DEVICE_ACTION_REQUEST_ACTION_SET_OUTVAL, payload);
      l->brightnessApplied();
    }
    else if (sb) {
      if (sb->mPosition->needsApplying()) {
        // set new target position
        uint16_t new16val = sb->mPosition->getChannelValue()*0xFFFF/100;
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
      if (sb->hasShadeBladeAngle() && sb->mAngle->needsApplying()) {
        // set new angle
        string payload;
        Ds485Comm::payload_append8(payload, 64); // Bank: RAM
        Ds485Comm::payload_append8(payload, 4); // Target lamella angle
        Ds485Comm::payload_append8(payload, sb->mAngle->getChannelValue()*0xFF/100);
        issueDeviceRequest(DEVICE_CONFIG, DEVICE_CONFIG_SET, payload);
        sb->mAngle->channelValueApplied();
      }
    }
    else {
      // simple unspecific output
      OutputBehaviourPtr o = getOutput();
      ChannelBehaviourPtr ch = o->getChannelByType(channeltype_default);
      string payload;
      Ds485Comm::payload_append8(payload, ch->getChannelValue()*0xFF/100);
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

void Ds485Device::scheduleConfigRead(uint8_t aBank, uint8_t aOffset)
{
  uint16_t bankOffs = (((uint16_t)aBank)<<8) + aOffset;
  bool wasEmpty = mPendingBankOffsReads.empty();
  mPendingBankOffsReads.insert(bankOffs);
  FOCUSOLOG("configRead: scheduleConfigRead: bank %u/offs %u, %zu reads now pending", aBank, aOffset, mPendingBankOffsReads.size());
  if (wasEmpty) {
    issueNextRead();
  }
}


#define DS485_CONFIG_READ_REPEAT_DELAY (5*Second)

void Ds485Device::confirmReadResult(uint8_t aBank, uint8_t aOffset)
{
  mReadRepeater.cancel();
  uint16_t bankOffs = (((uint16_t)aBank)<<8) + aOffset;
  auto pos = mPendingBankOffsReads.find(bankOffs);
  if (pos!=mPendingBankOffsReads.end()) {
    // this is one of the reads we were waiting for
    mPendingBankOffsReads.erase(pos);
    FOCUSOLOG("configRead: confirmReadResult: confirmed read of bank %u/offset %u, %zu more reads pending", aBank, aOffset, mPendingBankOffsReads.size());
    issueNextRead();
  }
  else if (!mPendingBankOffsReads.empty()) {
    // keep the request, retry a bit later
    FOCUSOLOG("configRead: confirmReadResult: bank %u/offs %u confirmed, is not of those %zu we need -> wait more and rely on retries", aBank, aOffset, mPendingBankOffsReads.size());
  }
}


void Ds485Device::issueNextRead()
{
  if (!mPendingBankOffsReads.empty()) {
    uint16_t bankOffs = *mPendingBankOffsReads.begin();
    uint8_t bank = bankOffs>>8;
    uint8_t offs = bankOffs&0xFF;
    FOCUSOLOG("configRead: issueNextRead: sending get for bank %u/offset %u", bank, offs);
    string payload;
    Ds485Comm::payload_append8(payload, bank);
    Ds485Comm::payload_append8(payload, offs);
    ErrorPtr err = issueDeviceRequest(DEVICE_CONFIG, DEVICE_CONFIG_GET, payload);
    if (Error::notOK(err)) {
      OLOG(LOG_WARNING, "configRead: error issuing device request: %s", err->text());
    }
    // have it repeat some time later
    mReadRepeater.executeOnce(boost::bind(&Ds485Device::issueNextRead, this), DS485_CONFIG_READ_REPEAT_DELAY);
  }
}



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
