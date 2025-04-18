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

#include "climatecontrolbehaviour.hpp"
#include <math.h>

using namespace p44;


// MARK: - ClimateControlScene (single value, for heating (and simple cooling) valves or other heating/cooling devices)


ClimateControlScene::ClimateControlScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
  inherited(aSceneDeviceSettings, aSceneNo)
{
}


void ClimateControlScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // set the common simple scene defaults
  inherited::setDefaultSceneValues(aSceneNo);
  // Add special climate behaviour scene commands
  switch (aSceneNo) {
    case CLIMATE_HEAT_TEMP_OFF:
    case CLIMATE_HEAT_TEMP_COMFORT:
    case CLIMATE_HEAT_TEMP_ECO:
    case CLIMATE_HEAT_TEMP_NIGHT:
      mSceneCmd = scene_cmd_climatecontrol_mode_heating;
      mSceneArea = 0; // not an area scene any more
      break;
    case CLIMATE_HEAT_TEMP_NOTUSED:
    case CLIMATE_HEAT_TEMP_HOLIDAY:
      mSceneCmd = scene_cmd_climatecontrol_mode_protective_heating;
      mSceneArea = 0; // not an area scene any more
      break;
    case CLIMATE_COOL_TEMP_OFF:
    case CLIMATE_COOL_TEMP_COMFORT:
    case CLIMATE_COOL_TEMP_ECO:
    case CLIMATE_COOL_TEMP_NIGHT:
      mSceneCmd = scene_cmd_climatecontrol_mode_cooling;
      mSceneArea = 0; // not an area scene any more
      break;
    case CLIMATE_COOL_TEMP_NOTUSED:
    case CLIMATE_COOL_TEMP_HOLIDAY:
      mSceneCmd = scene_cmd_climatecontrol_mode_protective_cooling;
      mSceneArea = 0; // not an area scene any more
      break;
    case CLIMATE_COOL_PASSIVE_ON:
    case CLIMATE_COOL_PASSIVE_OFF:
      mSceneCmd = scene_cmd_climatecontrol_mode_passive_cooling;
      mSceneArea = 0; // not an area scene any more
      break;
    case CLIMATE_ENABLE:
      mSceneCmd = scene_cmd_climatecontrol_enable;
      mSceneArea = 0; // not an area scene any more
      break;
    case CLIMATE_DISABLE:
      mSceneCmd = scene_cmd_climatecontrol_disable;
      mSceneArea = 0; // not an area scene any more
      break;
    case CLIMATE_VALVE_PROPHYLAXIS:
      mSceneCmd = scene_cmd_climatecontrol_valve_prophylaxis;
      mSceneArea = 0; // not an area scene any more
      break;
    case CLIMATE_VALVE_OPEN:
      mSceneCmd = scene_cmd_climatecontrol_valve_service_open;
      mSceneArea = 0; // not an area scene any more
      break;
    case CLIMATE_VALVE_CLOSE:
      mSceneCmd = scene_cmd_climatecontrol_valve_service_close;
      mSceneArea = 0; // not an area scene any more
      break;
    default:
      break;
  }
  markClean(); // default values are always clean
}

// MARK: - ClimateDeviceSettings with default climate scenes factory


ClimateDeviceSettings::ClimateDeviceSettings(Device &aDevice) :
  inherited(aDevice)
{
}


DsScenePtr ClimateDeviceSettings::newDefaultScene(SceneNo aSceneNo)
{
  ClimateControlScenePtr climateControlScene = ClimateControlScenePtr(new ClimateControlScene(*this, aSceneNo));
  climateControlScene->setDefaultSceneValues(aSceneNo);
  // return it
  return climateControlScene;
}


// MARK: - FanCoilUnitScene (specific for FCU behaviour)

#if ENABLE_FCU_SUPPORT


FanCoilUnitScene::FanCoilUnitScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
  inherited(aSceneDeviceSettings, aSceneNo),
  mPowerState(powerState_off),
  mOperationMode(fcuOperatingMode_off)
{
}


void FanCoilUnitScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // set the base class scene defaults (NOT SimpleScene, so all sceneCmds are invoke, and no areas!)
  inherited::setDefaultSceneValues(aSceneNo);
  // All scenes are "invoke" or "off"  type, no specific scene commands needed
  // - heating scenes set operating mode to heating (off scene sets powerState to false, on scenes to true)
  // - cooling scenes set operating mode to cooling (off scene sets powerState to false, on scenes to true)
  switch (aSceneNo) {
    case CLIMATE_HEAT_TEMP_OFF:
      // heating off
      mPowerState = powerState_off;
      mOperationMode = fcuOperatingMode_heat;
      mSceneCmd = scene_cmd_off;
      break;
    case CLIMATE_HEAT_TEMP_COMFORT:
    case CLIMATE_HEAT_TEMP_ECO:
    case CLIMATE_HEAT_TEMP_NIGHT:
      mSceneCmd = scene_cmd_climatecontrol_mode_heating;
      goto heating;
    case CLIMATE_HEAT_TEMP_NOTUSED:
    case CLIMATE_HEAT_TEMP_HOLIDAY:
      mSceneCmd = scene_cmd_climatecontrol_mode_protective_heating;
    heating:
      // heating on
      mPowerState = powerState_on;
      mOperationMode = fcuOperatingMode_heat;
      break;
    case CLIMATE_COOL_TEMP_OFF:
      // cooling off
      mPowerState = powerState_off;
      mOperationMode = fcuOperatingMode_cool;
      mSceneCmd = scene_cmd_off;
      break;
    case CLIMATE_COOL_TEMP_COMFORT:
    case CLIMATE_COOL_TEMP_ECO:
    case CLIMATE_COOL_TEMP_NIGHT:
      mSceneCmd = scene_cmd_climatecontrol_mode_cooling;
      goto cooling;
    case CLIMATE_COOL_TEMP_NOTUSED:
    case CLIMATE_COOL_TEMP_HOLIDAY:
      mSceneCmd = scene_cmd_climatecontrol_mode_protective_cooling;
    cooling:
      // cooling on
      mPowerState = powerState_on;
      mOperationMode = fcuOperatingMode_cool;
      break;
    case CLIMATE_DISABLE:
      mPowerState = powerState_off;
      mOperationMode = fcuOperatingMode_off;
      mSceneCmd = scene_cmd_off;
      break;
    case CLIMATE_FAN_ONLY:
      mPowerState = powerState_on;
      mOperationMode = fcuOperatingMode_fan;
      break;
    case CLIMATE_DRY:
      mPowerState = powerState_on;
      mOperationMode = fcuOperatingMode_dry;
      break;
    case CLIMATE_AUTOMATIC:
      mPowerState = powerState_on;
      mOperationMode = fcuOperatingMode_auto;
      break;
    default:
      // all others: dontcare, but generally off
      mPowerState = powerState_off;
      mOperationMode = fcuOperatingMode_off;
      setDontCare(true);
      break;
  }
  markClean(); // default values are always clean
}


double FanCoilUnitScene::sceneValue(int aChannelIndex)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_fcu_operation_mode: return mOperationMode;
    case channeltype_power_state: return mPowerState ? 1 : 0;
  }
  return 0;
}


void FanCoilUnitScene::setSceneValue(int aChannelIndex, double aValue)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  ClimateControlBehaviourPtr ccb = boost::dynamic_pointer_cast<ClimateControlBehaviour>(getOutputBehaviour());
  switch (cb->getChannelType()) {
    case channeltype_fcu_operation_mode:
      setPVar(mOperationMode, (FcuOperationMode)aValue);
      break;
    case channeltype_power_state:
      setPVar(mPowerState, (DsPowerState)aValue);
      break;
  }
}


// MARK: - FanCoilUnitScene persistence

const char *FanCoilUnitScene::tableName()
{
  return "FCUScenes";
}

// data field definitions

static const size_t numFCUSceneFields = 2;

size_t FanCoilUnitScene::numFieldDefs()
{
  return inherited::numFieldDefs()+numFCUSceneFields;
}


const FieldDefinition *FanCoilUnitScene::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFCUSceneFields] = {
    { "powerState", SQLITE_INTEGER },
    { "operationMode", SQLITE_INTEGER }
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFCUSceneFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void FanCoilUnitScene::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the fields
  aRow->getCastedIfNotNull<DsPowerState, int>(aIndex++, mPowerState);
  aRow->getCastedIfNotNull<FcuOperationMode, int>(aIndex++, mOperationMode);
}


/// bind values to passed statement
void FanCoilUnitScene::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, mPowerState);
  aStatement.bind(aIndex++, (int)mOperationMode);
}


// MARK: - FanCoilUnitDeviceSettings with default FCU scenes factory


FanCoilUnitDeviceSettings::FanCoilUnitDeviceSettings(Device &aDevice) :
  inherited(aDevice)
{
}


DsScenePtr FanCoilUnitDeviceSettings::newDefaultScene(SceneNo aSceneNo)
{
  FanCoilUnitScenePtr fcuControlScene = FanCoilUnitScenePtr(new FanCoilUnitScene(*this, aSceneNo));
  fcuControlScene->setDefaultSceneValues(aSceneNo);
  // return it
  return fcuControlScene;
}

#endif // ENABLE_FCU_SUPPORT



// MARK: - ClimateControlBehaviour


ClimateControlBehaviour::ClimateControlBehaviour(Device &aDevice, ClimateDeviceKind aKind, VdcHeatingSystemCapability aDefaultHeatingSystemCapability) :
  inherited(aDevice),
  climateDeviceKind(aKind),
  mHeatingSystemCapability(aDefaultHeatingSystemCapability),
  mHeatingSystemType(hstype_unknown),
  mClimateControlIdle(false), // assume valve active
  mClimateModeHeating(true),  // assume heating enabled
  mValveService(vs_none), // no valve service operation pending
  mForcedOffWakeMode(0), // not forced off
  mForceOffWakeSceneNo(0),
  mZoneTemperatureUpdated(Never),
  mZoneTemperature(0),
  mZoneTemperatureSetPointUpdated(Never),
  mZoneTemperatureSetPoint(0)
{
  // Note: there is no default group for climate, depends on application and must be set when instantiating the behaviour
  // - add the output channels
  ChannelBehaviourPtr ch;
  if (climateDeviceKind==climatedevice_simple) {
    // output channel is a simple unipolar heating/simple cooling valve. The power level can also be cooling in simple cooling
    mPowerLevel = ChannelBehaviourPtr(new PowerLevelChannel(*this));
    addChannel(mPowerLevel);
  }
  #if ENABLE_FCU_SUPPORT
  else if (climateDeviceKind==climatedevice_fancoilunit) {
    // power state is the main channel
    mPowerState = PowerStateChannelPtr(new PowerStateChannel(*this));
    addChannel(mPowerState);
    // operation mode
    mOperationMode = IndexChannelPtr(new FcuOperationModeChannel(*this));
    addChannel(mOperationMode);
  }
  #endif // ENABLE_FCU_SUPPORT
}



bool ClimateControlBehaviour::processControlValue(const string &aName, double aValue)
{
  if (aName=="heatingLevel" && climateDeviceKind==climatedevice_simple) {
    if (isMember(group_roomtemperature_control) && isEnabled()) {
      // if we have a heating/cooling power level channel, "heatingLevel" will control it
      ChannelBehaviourPtr cb = getChannelByType(channeltype_heating_power);
      if (cb) {
        // clip to -100..0..100 range
        if (aValue<-100) aValue = -100;
        else if (aValue>100) aValue = 100;
        // limit according to heatingSystemCapability setting
        switch (mHeatingSystemCapability) {
          case hscapability_heatingOnly:
            // 0..100
            if (aValue<0) aValue = 0; // ignore negatives
            break;
          case hscapability_coolingOnly:
            // -100..0
            if (aValue>0) aValue = 0; // ignore positives
            break;
          default:
          case hscapability_heatingAndCooling:
            // pass all values
            break;
        }
        // adapt to hardware capabilities
        if (mOutputFunction!=outputFunction_bipolar_positional) {
          // non-bipolar valves can only handle positive values, even for cooling
          aValue = fabs(aValue);
        }
        // apply now
        cb->setChannelValue(aValue, 0, true); // always apply
        return true; // needs apply
      }
    }
  }
  else if (aName=="TemperatureZone") {
    mZoneTemperature = aValue;
    mZoneTemperatureUpdated = MainLoop::currentMainLoop().now();
    return checkForcedOffWake();
  }
  else if (aName=="TemperatureSetPoint") {
    mZoneTemperatureSetPoint = aValue;
    mZoneTemperatureSetPointUpdated = MainLoop::currentMainLoop().now();
    return checkForcedOffWake();
  }
  return inherited::processControlValue(aName, aValue);
}


bool ClimateControlBehaviour::checkForcedOffWake()
{
  #if ENABLE_FCU_SUPPORT
  if (mPowerState && mForcedOffWakeMode!=0 && mPowerState->getIndex()==powerState_forcedOff) {
    // this is a FCU in forced off power state, and must wake when temperature crosses set point
    double temp, setpoint;
    if (getZoneTemperatures(temp, setpoint)) {
      if (mForcedOffWakeMode*temp < mForcedOffWakeMode*setpoint) {
        // need to wake the device from powerState_forcedOff
        OLOG(LOG_NOTICE, "waking FCU from forced off mode by applying scene #%d - because in a protective mode and temperature requires action", mForceOffWakeSceneNo);
        DsScenePtr wakeScene = mDevice.getScenes()->getScene(mForceOffWakeSceneNo);
        if (wakeScene) {
          // bypass our local implementation, just invoke
          return inherited::performApplySceneToChannels(wakeScene, scene_cmd_invoke);
        }
      }
    }
  }
  #endif
  return false; // no output channel change by default
}



bool ClimateControlBehaviour::getZoneTemperatures(double &aCurrentTemperature, double &aTemperatureSetpoint)
{
  if (mZoneTemperatureUpdated!=Never && mZoneTemperatureSetPointUpdated!=Never) {
    aCurrentTemperature = mZoneTemperature;
    aTemperatureSetpoint = mZoneTemperatureSetPoint;
    return true; // values available
  }
  return false; // no values
}




Tristate ClimateControlBehaviour::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // now check for climate control behaviour level features
  switch (aFeatureIndex) {
    case modelFeature_blink:
      // heating outputs can't blink
      return no;
    case modelFeature_heatinggroup:
      // Only simple heating control devices (valves) do have heating group setting
      return climateDeviceKind==climatedevice_simple ? yes : no;
    case modelFeature_heatingoutmode:
      // ...but not the more specific PWM and heating props
      return no;
    case modelFeature_valvetype:
      // only for heating valve devices
      return climateDeviceKind==climatedevice_simple ? yes : no;
    case modelFeature_outmodegeneric:
      if (climateDeviceKind==climatedevice_fancoilunit) return no; // FCU output cannot be disabled
      else goto use_default;
    default:
    use_default:
      // not available at this level, ask base class
      return inherited::hasModelFeature(aFeatureIndex);
  }
}


void ClimateControlBehaviour::loadChannelsFromScene(DsScenePtr aScene)
{
  #if ENABLE_FCU_SUPPORT
  FanCoilUnitScenePtr fcuScene = boost::dynamic_pointer_cast<FanCoilUnitScene>(aScene);
  if (fcuScene) {
    // power state
    mPowerState->setChannelValueIfNotDontCare(aScene, fcuScene->mPowerState ? 1 : 0, 0, 0, true);
    // operation mode
    mOperationMode->setChannelValueIfNotDontCare(aScene, fcuScene->mOperationMode, 0, 0, true);
  }
  #endif // ENABLE_FCU_SUPPORT
  ClimateControlScenePtr valveScene = boost::dynamic_pointer_cast<ClimateControlScene>(aScene);
  if (valveScene) {
    // heating level
    mPowerLevel->setChannelValueIfNotDontCare(aScene, valveScene->value, 0, 0, true);
  }
}


void ClimateControlBehaviour::saveChannelsToScene(DsScenePtr aScene)
{
  #if ENABLE_FCU_SUPPORT
  FanCoilUnitScenePtr fcuScene = boost::dynamic_pointer_cast<FanCoilUnitScene>(aScene);
  if (fcuScene) {
    // power state
    fcuScene->setPVar(fcuScene->mPowerState, (DsPowerState)mPowerState->getChannelValue());
    fcuScene->setSceneValueFlags(mPowerState->getChannelIndex(), valueflags_dontCare, false);
    // operation mode
    fcuScene->setPVar(fcuScene->mOperationMode, (FcuOperationMode)mOperationMode->getChannelValue());
    fcuScene->setSceneValueFlags(mOperationMode->getChannelIndex(), valueflags_dontCare, false);
  }
  #endif // ENABLE_FCU_SUPPORT
  ClimateControlScenePtr valveScene = boost::dynamic_pointer_cast<ClimateControlScene>(aScene);
  if (valveScene) {
    // heating level
    valveScene->setPVar(valveScene->value, mPowerLevel->getChannelValue());
    valveScene->setSceneValueFlags(mPowerLevel->getChannelIndex(), valueflags_dontCare, false);
  }
}




// apply scene
// - execute special climate commands
bool ClimateControlBehaviour::performApplySceneToChannels(DsScenePtr aScene, SceneCmd aSceneCmd)
{
  // check the special hardwired scenes
  if (climateDeviceKind==climatedevice_simple) {
    // simple climate control device
    // - scene commands that are independent of group
    switch (aSceneCmd) {
      case scene_cmd_climatecontrol_valve_prophylaxis:
        // valve prophylaxis
        mValveService = vs_prophylaxis;
        return true;
      case scene_cmd_climatecontrol_valve_service_open:
        // valve service
        mValveService = vs_fullyopen;
        return true;
      case scene_cmd_climatecontrol_valve_service_close:
        // valve service
        mValveService = vs_fullyclose;
        return true;
      default:
        // group specific or default handling
        break;
    }
    if (isMember(group_roomtemperature_control)) {
      // scene commands active in room temperature (group 48) only
      switch (aSceneCmd) {
        case scene_cmd_climatecontrol_enable:
          // switch to winter mode
          mClimateControlIdle = false;
          return true;
        case scene_cmd_climatecontrol_disable:
          // switch to summer mode
          mClimateControlIdle = true;
          return true;
        case scene_cmd_climatecontrol_mode_heating:
        case scene_cmd_climatecontrol_mode_protective_heating:
          // switch to heating mode
          mClimateModeHeating = true;
          return true;
        case scene_cmd_climatecontrol_mode_cooling:
        case scene_cmd_climatecontrol_mode_protective_cooling:
        case scene_cmd_climatecontrol_mode_passive_cooling:
          // switch to cooling mode (active or passive)
          mClimateModeHeating = false;
          return true;
        default:
          // all other scene calls are suppressed in group_roomtemperature_control
          return false;
      }
    }
    else {
      // scene commands active in other groups
      switch (aSceneCmd) {
        case scene_cmd_climatecontrol_mode_heating:
        case scene_cmd_climatecontrol_mode_protective_heating:
        case scene_cmd_climatecontrol_mode_cooling:
        case scene_cmd_climatecontrol_mode_protective_cooling:
        case scene_cmd_climatecontrol_mode_passive_cooling:
          // treat these normally, just invoke power level
          aSceneCmd = scene_cmd_invoke;
        default:
          break;
      }
    }
  }
  #if ENABLE_FCU_SUPPORT
  else if (climateDeviceKind==climatedevice_fancoilunit && isMember(group_roomtemperature_control)) {
    // FAN coil unit: standard case is that scenes are invoked normally, but some exceptions exist
    // - in powerState_forcedOff, scene calls are suppressed
    // - some scenes switch internal state for temperature-triggered wake from powerState_forcedOff
    switch (aSceneCmd) {
      case scene_cmd_climatecontrol_mode_protective_heating:
        // enables waking from powerState_forcedOff when temperature falls below set point
        OLOG(LOG_INFO, "Entering protective heating mode - wake on temperature drop enabled");
        mForcedOffWakeMode = 1;
        mForceOffWakeSceneNo = aScene->mSceneNo;
        aSceneCmd = scene_cmd_invoke;
        break;
      case scene_cmd_climatecontrol_mode_protective_cooling:
        // enables waking from powerState_forcedOff when temperature rises above set point
        OLOG(LOG_INFO, "Entering protective cooling mode - wake on temperature rise enabled");
        mForcedOffWakeMode = -1;
        mForceOffWakeSceneNo = aScene->mSceneNo;
        aSceneCmd = scene_cmd_invoke; // otherwise, treat like invoke
        break;
      case scene_cmd_climatecontrol_mode_heating:
      case scene_cmd_climatecontrol_mode_cooling:
      case scene_cmd_climatecontrol_mode_passive_cooling:
        // disable waking from powerState_forcedOff
        mForcedOffWakeMode = 0;
        mForceOffWakeSceneNo = 0;
        aSceneCmd = scene_cmd_invoke; // otherwise, treat like invoke
        break;
      default:
        // all other scene calls passed on as-is
        break;
    }
    if (mPowerState->getIndex()==powerState_forcedOff) {
      // no scene calls in forced off mode
      OLOG(LOG_INFO, "FCU is in forced off state, scene calls are suppressed");
      return false;
    }
  }
  #endif
  // let base class handle it now
  return inherited::performApplySceneToChannels(aScene, aSceneCmd);
}


// MARK: - persistence


const char *ClimateControlBehaviour::tableName()
{
  return "ClimateOutputSettings";
}


// data field definitions

static const size_t numFields = 2;

size_t ClimateControlBehaviour::numFieldDefs()
{
  return inherited::numFieldDefs()+numFields;
}


const FieldDefinition *ClimateControlBehaviour::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "heatingSystemCapability", SQLITE_INTEGER },
    { "heatingSystemType", SQLITE_INTEGER },
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void ClimateControlBehaviour::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  // get the data
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // decode the common flags
  if (aCommonFlagsP) mClimateControlIdle = *aCommonFlagsP & climateoutputflag_controlIdle;
  // get the fields
  aRow->getCastedIfNotNull<VdcHeatingSystemCapability, int>(aIndex++, mHeatingSystemCapability);
  aRow->getCastedIfNotNull<VdcHeatingSystemType, int>(aIndex++, mHeatingSystemType);
}


// bind values to passed statement
void ClimateControlBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  // encode the flags
  if (mClimateControlIdle) aCommonFlags |= climateoutputflag_controlIdle;
  // bind superclass' fields, which includes commonFlags
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, mHeatingSystemCapability);
  aStatement.bind(aIndex++, mHeatingSystemType);
}


// MARK: - property access


static char climatecontrol_key;

// description properties

enum {
  activeCoolingMode_key,
  numDescProperties
};

// settings properties

enum {
  heatingSystemCapability_key,
  heatingSystemType_key,
  numSettingsProperties
};


int ClimateControlBehaviour::numDescProps() { return inherited::numDescProps()+numDescProperties; }
const PropertyDescriptorPtr ClimateControlBehaviour::getDescDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
    static const PropertyDescription properties[numDescProperties] = {
      { "activeCoolingMode", apivalue_bool, activeCoolingMode_key+descriptions_key_offset, OKEY(climatecontrol_key) },
    };
    int n = inherited::numDescProps();
    if (aPropIndex<n)
      return inherited::getDescDescriptorByIndex(aPropIndex, aParentDescriptor);
    aPropIndex -= n;
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}

int ClimateControlBehaviour::numSettingsProps() { return inherited::numSettingsProps()+numSettingsProperties; }
const PropertyDescriptorPtr ClimateControlBehaviour::getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numSettingsProperties] = {
    { "heatingSystemCapability", apivalue_uint64, heatingSystemCapability_key+settings_key_offset, OKEY(climatecontrol_key) },
    { "heatingSystemType", apivalue_uint64, heatingSystemType_key+settings_key_offset, OKEY(climatecontrol_key) },
  };
  int n = inherited::numSettingsProps();
  if (aPropIndex<n)
    return inherited::getSettingsDescriptorByIndex(aPropIndex, aParentDescriptor);
  aPropIndex -= n;
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// access to all fields

bool ClimateControlBehaviour::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(climatecontrol_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Description properties
        case activeCoolingMode_key+descriptions_key_offset:
          aPropValue->setBoolValue(climateDeviceKind==climatedevice_fancoilunit); // FCUs can cool actively
          return true;
        // Settings properties
        case heatingSystemCapability_key+settings_key_offset:
          aPropValue->setUint8Value(mHeatingSystemCapability);
          return true;
        case heatingSystemType_key+settings_key_offset:
          aPropValue->setUint8Value(mHeatingSystemType);
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case heatingSystemCapability_key+settings_key_offset:
          setPVar(mHeatingSystemCapability, (VdcHeatingSystemCapability)aPropValue->uint8Value());
          return true;
        case heatingSystemType_key+settings_key_offset:
          setPVar(mHeatingSystemType, (VdcHeatingSystemType)aPropValue->uint8Value());
          return true;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}




// MARK: - description


string ClimateControlBehaviour::shortDesc()
{
  return string("ClimateControl");
}


string ClimateControlBehaviour::description()
{
  string s = string_format("%s behaviour (in %s mode)", shortDesc().c_str(), isClimateControlIdle() ? "idle" : "active");
  s.append(inherited::description());
  return s;
}

