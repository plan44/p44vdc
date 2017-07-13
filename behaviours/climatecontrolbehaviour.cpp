//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


// MARK: ===== ClimateControlScene (single value, for heating (and simple cooling) valves or other heating/cooling devices)


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
    case CLIMATE_ENABLE:
      sceneCmd = scene_cmd_climatecontrol_enable;
      sceneArea = 0; // not an area scene any more
      break;
    case CLIMATE_DISABLE:
      sceneCmd = scene_cmd_climatecontrol_disable;
      sceneArea = 0; // not an area scene any more
      break;
    case CLIMATE_VALVE_PROPHYLAXIS:
      sceneCmd = scene_cmd_climatecontrol_valve_prophylaxis;
      sceneArea = 0; // not an area scene any more
      break;
    default:
      break;
  }
  markClean(); // default values are always clean
}

// MARK: ===== ClimateDeviceSettings with default climate scenes factory


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


// MARK: ===== FanCoilUnitScene (specific for FCU behaviour)

FanCoilUnitScene::FanCoilUnitScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
  inherited(aSceneDeviceSettings, aSceneNo),
  powerState(false),
  operationMode(fcuOperatingMode_off)
{
}


void FanCoilUnitScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // set the base class scene defaults
  inherited::setDefaultSceneValues(aSceneNo);
  // simple fixed defaults
  // - heating scenes 0..5 set operating mode to heating
  // - cooling scenes 6..11 set operating mode to cooling
  if (aSceneNo>=0 && aSceneNo<=5) {
    // heating energy level
    powerState = aSceneNo!=0; // Scene 0 also turns off the device
    operationMode = fcuOperatingMode_heat;
  }
  else if (aSceneNo>=6 && aSceneNo<=11) {
    powerState = true;
    operationMode = fcuOperatingMode_cool;
  }
  else if (aSceneNo==30) {
    powerState = false;
    operationMode = fcuOperatingMode_off;
  }
  else if (aSceneNo==40) {
    powerState = true;
    operationMode = fcuOperatingMode_fan;
  }
  else if (aSceneNo==41) {
    powerState = true;
    operationMode = fcuOperatingMode_dry;
  }
  else if (aSceneNo==42) {
    powerState = true;
    operationMode = fcuOperatingMode_auto;
  }
  else {
    // all others: dontcare, but generally off
    powerState = false;
    operationMode = fcuOperatingMode_off;
    setDontCare(true);
  }
  markClean(); // default values are always clean
}


double FanCoilUnitScene::sceneValue(int aChannelIndex)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_fcu_operation_mode: return operationMode;
    case channeltype_power_state: return powerState ? 1 : 0;
  }
  return 0;
}


void FanCoilUnitScene::setSceneValue(int aChannelIndex, double aValue)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  ClimateControlBehaviourPtr ccb = boost::dynamic_pointer_cast<ClimateControlBehaviour>(getOutputBehaviour());
  switch (cb->getChannelType()) {
    case channeltype_fcu_operation_mode:
      setPVar(operationMode, (FcuOperationMode)aValue);
      break;
    case channeltype_power_state:
      setPVar(powerState, aValue>0);
      break;
  }
}


// MARK: ===== FanCoilUnitScene persistence

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
  aRow->getIfNotNull<bool>(aIndex++, powerState);
  aRow->getCastedIfNotNull<FcuOperationMode, int>(aIndex++, operationMode);
}


/// bind values to passed statement
void FanCoilUnitScene::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, powerState);
  aStatement.bind(aIndex++, (int)operationMode);
}


// MARK: ===== FanCoilUnitDeviceSettings with default FCU scenes factory


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





// MARK: ===== ClimateControlBehaviour


ClimateControlBehaviour::ClimateControlBehaviour(Device &aDevice, ClimateDeviceKind aKind, VdcHeatingSystemCapability aDefaultHeatingSystemCapability) :
  inherited(aDevice),
  climateDeviceKind(aKind),
  heatingSystemCapability(aDefaultHeatingSystemCapability),
  climateControlIdle(false), // assume valve active
  runProphylaxis(false), // no run scheduled
  zoneTemperatureUpdated(Never),
  zoneTemperatureSetPointUpdated(Never)
{
  // Note: there is no default group for climate, depends on application and must be set when instantiating the behaviour
  // - add the output channels
  ChannelBehaviourPtr ch;
  if (climateDeviceKind==climatedevice_simple) {
    // output channel is a simple unipolar heating/simple cooling valve. The power level can also be cooling in simple cooling
    powerLevel = ChannelBehaviourPtr(new PowerLevelChannel(*this));
    addChannel(powerLevel);
  }
  else if (climateDeviceKind==climatedevice_fancoilunit) {
    // power state is the main channel
    powerState = FlagChannelPtr(new FcuPowerStateChannel(*this));
    addChannel(powerState);
    // operation mode
    operationMode = IndexChannelPtr(new FcuOperationModeChannel(*this));
    addChannel(operationMode);
  }
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
        switch (heatingSystemCapability) {
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
        if (outputFunction!=outputFunction_bipolar_positional) {
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
    zoneTemperature = aValue;
    zoneTemperatureUpdated = MainLoop::currentMainLoop().now();
  }
  else if (aName=="TemperatureSetPoint") {
    zoneTemperatureSetPoint = aValue;
    zoneTemperatureSetPointUpdated = MainLoop::currentMainLoop().now();
  }
  return inherited::processControlValue(aName, aValue);
}


bool ClimateControlBehaviour::getZoneTemperatures(double &aCurrentTemperature, double &aTemperatureSetpoint)
{
  if (zoneTemperatureUpdated!=Never && zoneTemperatureSetPointUpdated!=Never) {
    aCurrentTemperature = zoneTemperature;
    aTemperatureSetpoint = zoneTemperatureSetPoint;
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
      // Assumption: virtual heating control devices (valves) do have group and mode setting...
      return yes;
    case modelFeature_heatingoutmode:
      // ...but not the more specific PWM and heating props
      return no;
    case modelFeature_valvetype:
      // only for heating valve devices
      return climateDeviceKind==climatedevice_simple ? yes : no;
    default:
      // not available at this level, ask base class
      return inherited::hasModelFeature(aFeatureIndex);
  }
}


void ClimateControlBehaviour::loadChannelsFromScene(DsScenePtr aScene)
{
  FanCoilUnitScenePtr fcuScene = boost::dynamic_pointer_cast<FanCoilUnitScene>(aScene);
  if (fcuScene) {
    // power state
    powerState->setChannelValueIfNotDontCare(aScene, fcuScene->powerState ? 1 : 0, 0, 0, true);
    // operation mode
    operationMode->setChannelValueIfNotDontCare(aScene, fcuScene->operationMode, 0, 0, true);
  }
  ClimateControlScenePtr valveScene = boost::dynamic_pointer_cast<ClimateControlScene>(aScene);
  if (valveScene) {
    // heating level
    powerLevel->setChannelValueIfNotDontCare(aScene, valveScene->value, 0, 0, true);
  }
}


void ClimateControlBehaviour::saveChannelsToScene(DsScenePtr aScene)
{
  FanCoilUnitScenePtr fcuScene = boost::dynamic_pointer_cast<FanCoilUnitScene>(aScene);
  if (fcuScene) {
    // power state
    fcuScene->setPVar(fcuScene->powerState, powerState->getChannelValue()>0);
    fcuScene->setSceneValueFlags(powerState->getChannelIndex(), valueflags_dontCare, false);
    // operation mode
    fcuScene->setPVar(fcuScene->operationMode, (FcuOperationMode)operationMode->getChannelValue());
    fcuScene->setSceneValueFlags(operationMode->getChannelIndex(), valueflags_dontCare, false);
  }
  ClimateControlScenePtr valveScene = boost::dynamic_pointer_cast<ClimateControlScene>(aScene);
  if (valveScene) {
    // heating level
    valveScene->setPVar(valveScene->value, powerLevel->getChannelValue());
    valveScene->setSceneValueFlags(powerLevel->getChannelIndex(), valueflags_dontCare, false);
  }
}




// apply scene
// - execute special climate commands
bool ClimateControlBehaviour::applyScene(DsScenePtr aScene)
{
  // check the special hardwired scenes
  if (climateDeviceKind==climatedevice_simple && isMember(group_roomtemperature_control)) {
    SceneCmd sceneCmd = aScene->sceneCmd;
    switch (sceneCmd) {
      case scene_cmd_climatecontrol_enable:
        // switch to winter mode
        climateControlIdle = false;
        return true;
      case scene_cmd_climatecontrol_disable:
        // switch to summer mode
        climateControlIdle = true;
        return true;
      case scene_cmd_climatecontrol_valve_prophylaxis:
        // valve prophylaxis
        runProphylaxis = true;
        return true;
      default:
        // all other scene calls are suppressed in group_roomtemperature_control
        return false;
    }
  }
  // other type of scene, let base class handle it
  return inherited::applyScene(aScene);
}


// MARK: ===== persistence


const char *ClimateControlBehaviour::tableName()
{
  return "ClimateOutputSettings";
}


// data field definitions

static const size_t numFields = 1;

size_t ClimateControlBehaviour::numFieldDefs()
{
  return inherited::numFieldDefs()+numFields;
}


const FieldDefinition *ClimateControlBehaviour::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "heatingSystemCapability", SQLITE_INTEGER },
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
  if (aCommonFlagsP) climateControlIdle = *aCommonFlagsP & outputflag_climateControlIdle;
  // get the fields
  aRow->getCastedIfNotNull<VdcHeatingSystemCapability, int>(aIndex++, heatingSystemCapability);
}


// bind values to passed statement
void ClimateControlBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  // encode the flags
  if (climateControlIdle) aCommonFlags |= outputflag_climateControlIdle;
  // bind
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, heatingSystemCapability);
}


// MARK: ===== property access


static char climatecontrol_key;

// settings properties

enum {
  heatingSystemCapability_key,
  numSettingsProperties
};


int ClimateControlBehaviour::numSettingsProps() { return inherited::numSettingsProps()+numSettingsProperties; }
const PropertyDescriptorPtr ClimateControlBehaviour::getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numSettingsProperties] = {
    { "heatingSystemCapability", apivalue_uint64, heatingSystemCapability_key+settings_key_offset, OKEY(climatecontrol_key) },
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
        // Settings properties
        case heatingSystemCapability_key+settings_key_offset: aPropValue->setUint8Value(heatingSystemCapability); return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case heatingSystemCapability_key+settings_key_offset: setPVar(heatingSystemCapability, (VdcHeatingSystemCapability)aPropValue->uint8Value()); return true;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}




// MARK: ===== description


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

