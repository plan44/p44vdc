//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "binaryinputbehaviour.hpp"

using namespace p44;

#define DSS_INPUT_MAX_PUSH_INTERVAL (55*Minute) // all inputs that have a aliveSignInterval!=Never must get pushed at least this often (otherwise dSS flags sensor red)

BinaryInputBehaviour::BinaryInputBehaviour(Device &aDevice, const string aId) :
  inherited(aDevice, aId),
  // persistent settings
  binInputGroup(group_black_variable),
  configuredInputType(binInpType_none),
  minPushInterval(2*Second), // don't push more often than every 2 seconds
  maxPushInterval(0),
  changesOnlyInterval(15*Minute), // report unchanged state updates max once every 15 minutes
  autoResetTo(-1), // no auto reset
  // state
  lastUpdate(Never),
  lastPush(Never),
  currentState(0)
{
  // set dummy default hardware default configuration (no known alive sign interval!)
  setHardwareInputConfig(binInpType_none, usage_undefined, true, 15*Second, 0);
}


BinaryInputBehaviour::~BinaryInputBehaviour()
{
}



const char *inputTypeIds[numVdcSensorTypes] = {
  "generic", ///< no system function
  "presence", ///< Presence
  "light", ///< Light
  "presence_in_darkness", ///< Presence in darkness
  "twilight", ///< twilight
  "motion", ///< motion
  "motion_in_darkness", ///< motion in darkness
  "smoke", ///< smoke
  "wind", ///< wind
  "rain", ///< rain
  "sun", ///< solar radiation (sun light above threshold)
  "thermostat", ///< thermostat (temperature below user-adjusted threshold)
  "low_battery", ///< device has low battery
  "window_open", ///< window is open
  "door_open", ///< door is open
  "window_handle", ///< TRI-STATE! Window handle, has extendedValue showing closed/open/tilted, bool value is just closed/open
  "garage_open", ///< garage door is open
  "sun_protection", ///< protect against too much sunlight
  "frost", ///< frost detector
  "heating_activated", ///< heating system activated
  "heating_changeover", ///< heating system change over (active=warm water, non active=cold water)
  "initializing", ///< can indicate when not all functions are ready yet
  "malfunction", ///< malfunction, device needs maintainance, cannot operate
  "service", ///< device needs service, but can still operate normally at the moment
};




void BinaryInputBehaviour::setHardwareInputConfig(DsBinaryInputType aInputType, VdcUsageHint aUsage, bool aReportsChanges, MLMicroSeconds aUpdateInterval, MLMicroSeconds aAliveSignInterval, int aAutoResetTo)
{
  hardwareInputType = aInputType;
  inputUsage = aUsage;
  reportsChanges = aReportsChanges;
  updateInterval = aUpdateInterval;
  autoResetTo = aAutoResetTo;
  aliveSignInterval = aAliveSignInterval;
  maxPushInterval = aliveSignInterval==Never ? Never : DSS_INPUT_MAX_PUSH_INTERVAL; // sensors without any update guarantee do not need to fake regular pushes
  // setup standard timeout to undefined state
  startInputTimeout(aliveSignInterval, undefined);
  // set default input mode to hardware type
  configuredInputType = hardwareInputType;
}


string BinaryInputBehaviour::getAutoId()
{
  return inputTypeIds[hardwareInputType];
}



InputState BinaryInputBehaviour::maxExtendedValue()
{
  if (configuredInputType==binInpType_windowHandle) return 2; // Window handle is tri-state
  return 1; // all others are binary so far
}


void BinaryInputBehaviour::startInputTimeout(MLMicroSeconds aTimeout, int aAfterTimeOutState)
{
  timeoutTicket.cancel();
  if (aTimeout!=Never) {
    // schedule invalidation or auto-reset
    timeoutTicket.executeOnce(boost::bind(&BinaryInputBehaviour::inputTimeout, this, aAfterTimeOutState), aTimeout);
  }
}


void BinaryInputBehaviour::inputTimeout(int aAfterTimeOutState)
{
  if (aAfterTimeOutState<0) {
    // consider invalid
    invalidateInputState();
  }
  else {
    // just set a state (e.g. motion sensors that only report motion, but no non-motion
    OLOG(LOG_INFO, "Auto-resetting input state after timeout now");
    updateInputState(aAfterTimeOutState);
  }
}


void BinaryInputBehaviour::updateInputState(InputState aNewState)
{
  if (aNewState>maxExtendedValue()) aNewState = maxExtendedValue(); // make sure state does not exceed expectation
  // always update age, even if value itself may not have changed
  MLMicroSeconds now = MainLoop::now();
  lastUpdate = now;
  if (autoResetTo>=0 && aNewState!=autoResetTo) {
    // this update sets the output to a non-reset state -> set up auto reset
    startInputTimeout(updateInterval, autoResetTo);
  }
  else {
    // just start the invalidation timeout
    startInputTimeout(aliveSignInterval, -1);
  }
  bool changedState = aNewState!=currentState;
  if (changedState) {
    // input state change is considered a (regular!) user action, have it checked globally first
    device.getVdcHost().signalDeviceUserAction(device, true);
    // Note: even if global identify handler processes this, still report state changes (otherwise upstream could get out of sync)
  }
  OLOG(changedState ? LOG_NOTICE : LOG_INFO, "reports %s state = %d", changedState ? "NEW" : "same", aNewState);
  // in all cases, binary input state changes must be forwarded long term
  // (but minPushInterval must "debounce" rapid intermediate changes)
  if (changedState || now>lastPush+changesOnlyInterval) {
    // changed state or no update sent for more than changesOnlyInterval
    currentState = aNewState;
    pushInput(changedState);
  }
  // notify listeners
  #if ENABLE_P44SCRIPT
  // send event
  sendValueEvent();
  #else
  // notify listeners
  notifyListeners(changedState ? valueevent_changed : valueevent_confirmed);
  #endif
}


bool BinaryInputBehaviour::pushInput(bool aChanged)
{
  MLMicroSeconds now = MainLoop::now();
  if (lastPush==Never || now>lastPush+minPushInterval) {
    // push the new value right now
    if (pushBehaviourState()) {
      lastPush = now;
      OLOG(LOG_NOTICE, "successfully pushed state = %d", currentState);
      if (hasDefinedState() && maxPushInterval!=Never) {
        // schedule re-push of defined state
        updateTicket.executeOnce(boost::bind(&BinaryInputBehaviour::pushInput, this, false), maxPushInterval);
      }
      return true;
    }
    else if (device.isPublicDS()) {
      OLOG(LOG_NOTICE, "could not be pushed");
    }
  }
  else if (aChanged) {
    // cannot be pushed now, but final state of the input must be reported later
    OLOG(LOG_INFO, "input changes too quickly, push of final state will be pushed after minPushInterval");
    updateTicket.executeOnceAt(boost::bind(&BinaryInputBehaviour::reportFinalState, this), lastPush+minPushInterval);
  }
  return false;
}



void BinaryInputBehaviour::reportFinalState()
{
  // push the current value (after awaiting minPushInterval or after maxPushInterval has passed)
  updateTicket.cancel();
  if (pushBehaviourState()) {
    OLOG(LOG_NOTICE, "now pushes current state (%d) after awaiting minPushInterval", currentState);
    lastPush = MainLoop::currentMainLoop().now();
  }
}


void BinaryInputBehaviour::invalidateInputState()
{
  if (hasDefinedState()) {
    // currently valid -> invalidate
    lastUpdate = Never;
    //currentState = 0; // do NOT reset the state, it is better to use the last known state (for the valuesource value in p44scripts)
    updateTicket.cancel();
    OLOG(LOG_NOTICE, "reports input state no longer available");
    // push invalidation (primitive clients not capable of NULL will at least see state==false)
    MLMicroSeconds now = MainLoop::now();
    // push the invalid state
    if (pushBehaviourState()) {
      lastPush = now;
    }
    // notify listeners
    #if ENABLE_P44SCRIPT
    sendValueEvent();
    #else
    notifyListeners(valueevent_changed);
    #endif
  }
}


bool BinaryInputBehaviour::hasDefinedState()
{
  return lastUpdate!=Never;
}


void BinaryInputBehaviour::revalidateState()
{
  if (hasDefinedState() && (autoResetTo<0 || updateInterval==Never || currentState==autoResetTo)) {
    // re-arm invalidator (unless autoreset is pending)
    startInputTimeout(aliveSignInterval, undefined);
  }
}


string BinaryInputBehaviour::getStatusText()
{
  if (hasDefinedState()) {
    return string_format("%d", currentState);
  }
  return inherited::getStatusText();
}



// MARK: - value source implementation


string BinaryInputBehaviour::getSourceId()
{
  return string_format("%s_I%s", device.getDsUid().getString().c_str(), getId().c_str());
}


string BinaryInputBehaviour::getSourceName()
{
  // get device name or dSUID for context
  string n = device.getAssignedName();
  if (n.empty()) {
    // use abbreviated dSUID instead
    string d = device.getDsUid().getString();
    n = d.substr(0,8) + "..." + d.substr(d.size()-2,2);
  }
  // append behaviour description
  string_format_append(n, ": %s", getHardwareName().c_str());
  return n;
}


double BinaryInputBehaviour::getSourceValue()
{
  return currentState;
}


MLMicroSeconds BinaryInputBehaviour::getSourceLastUpdate()
{
  return lastUpdate;
}


int BinaryInputBehaviour::getSourceOpLevel()
{
  return device.opStateLevel();
}


// MARK: - persistence implementation


// SQLIte3 table name to store these parameters to
const char *BinaryInputBehaviour::tableName()
{
  return "BinaryInputSettings";
}


// data field definitions

static const size_t numFields = 4;

size_t BinaryInputBehaviour::numFieldDefs()
{
  return inherited::numFieldDefs()+numFields;
}


const FieldDefinition *BinaryInputBehaviour::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "dsGroup", SQLITE_INTEGER }, // Note: don't call a SQL field "group"!
    { "minPushInterval", SQLITE_INTEGER },
    { "changesOnlyInterval", SQLITE_INTEGER },
    { "configuredInputType", SQLITE_INTEGER },
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void BinaryInputBehaviour::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the fields
  aRow->getCastedIfNotNull<DsGroup, int>(aIndex++, binInputGroup);
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, minPushInterval);
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, changesOnlyInterval);
  aRow->getCastedIfNotNull<DsBinaryInputType, int>(aIndex++, configuredInputType);
}


// bind values to passed statement
void BinaryInputBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, binInputGroup);
  aStatement.bind(aIndex++, (long long int)minPushInterval);
  aStatement.bind(aIndex++, (long long int)changesOnlyInterval);
  aStatement.bind(aIndex++, (long long int)configuredInputType);
}



// MARK: - property access

static char binaryInput_key;

// description properties

enum {
  hardwareInputType_key,
  inputUsage_key,
  reportsChanges_key,
  updateInterval_key,
  aliveSignInterval_key,
  maxPushInterval_key,
  numDescProperties
};


int BinaryInputBehaviour::numDescProps() { return numDescProperties; }
const PropertyDescriptorPtr BinaryInputBehaviour::getDescDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numDescProperties] = {
    { "sensorFunction", apivalue_uint64, hardwareInputType_key+descriptions_key_offset, OKEY(binaryInput_key) },
    { "inputUsage", apivalue_uint64, inputUsage_key+descriptions_key_offset, OKEY(binaryInput_key) },
    { "inputType", apivalue_bool, reportsChanges_key+descriptions_key_offset, OKEY(binaryInput_key) },
    { "updateInterval", apivalue_double, updateInterval_key+descriptions_key_offset, OKEY(binaryInput_key) },
    { "aliveSignInterval", apivalue_double, aliveSignInterval_key+descriptions_key_offset, OKEY(binaryInput_key) },
    { "maxPushInterval", apivalue_double, maxPushInterval_key+descriptions_key_offset, OKEY(binaryInput_key) },
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// settings properties

enum {
  group_key,
  minPushInterval_key,
  changesOnlyInterval_key,
  configuredInputType_key,
  numSettingsProperties
};


int BinaryInputBehaviour::numSettingsProps() { return numSettingsProperties; }
const PropertyDescriptorPtr BinaryInputBehaviour::getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numSettingsProperties] = {
    { "group", apivalue_uint64, group_key+settings_key_offset, OKEY(binaryInput_key) },
    { "minPushInterval", apivalue_double, minPushInterval_key+settings_key_offset, OKEY(binaryInput_key) },
    { "changesOnlyInterval", apivalue_double, changesOnlyInterval_key+settings_key_offset, OKEY(binaryInput_key) },
    { "sensorFunction", apivalue_uint64, configuredInputType_key+settings_key_offset, OKEY(binaryInput_key) },
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}

// state properties

enum {
  value_key,
  extendedValue_key,
  age_key,
  numStateProperties
};


int BinaryInputBehaviour::numStateProps() { return numStateProperties; }
const PropertyDescriptorPtr BinaryInputBehaviour::getStateDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numStateProperties] = {
    { "value", apivalue_bool, value_key+states_key_offset, OKEY(binaryInput_key) },
    { "extendedValue", apivalue_uint64, extendedValue_key+states_key_offset, OKEY(binaryInput_key) },
    { "age", apivalue_double, age_key+states_key_offset, OKEY(binaryInput_key) },
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// access to all fields
bool BinaryInputBehaviour::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(binaryInput_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Description properties
        case hardwareInputType_key+descriptions_key_offset: // aka "hardwareSensorFunction"
          aPropValue->setUint8Value(hardwareInputType);
          return true;
        case inputUsage_key+descriptions_key_offset:
          aPropValue->setUint8Value(inputUsage);
          return true;
        case reportsChanges_key+descriptions_key_offset: // aka "inputType"
          aPropValue->setUint8Value(reportsChanges ? 1 : 0);
          return true;
        case updateInterval_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)updateInterval/Second);
          return true;
        case aliveSignInterval_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)aliveSignInterval/Second);
          return true;
        case maxPushInterval_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)maxPushInterval/Second);
          return true;
        // Settings properties
        case group_key+settings_key_offset:
          aPropValue->setUint16Value(binInputGroup);
          return true;
        case minPushInterval_key+settings_key_offset:
          aPropValue->setDoubleValue((double)minPushInterval/Second);
          return true;
        case changesOnlyInterval_key+settings_key_offset:
          aPropValue->setDoubleValue((double)changesOnlyInterval/Second);
          return true;
        case configuredInputType_key+settings_key_offset: // aka "sensorFunction"
          aPropValue->setUint8Value(configuredInputType);
          return true;
        // States properties
        case value_key+states_key_offset:
          // value
          if (!hasDefinedState())
            aPropValue->setNull();
          else
            aPropValue->setBoolValue(currentState>=1); // all states > 0 are considered "true" for the basic state
          return true;
        case extendedValue_key+states_key_offset:
          // extended value
          if (maxExtendedValue()>1) {
            // this is a multi-state input, show the actual state as "extendedValue"
            if (!hasDefinedState())
              aPropValue->setNull();
            else
              aPropValue->setUint8Value(currentState);
          }
          else {
            // simple binary input, do not show the extended state
            return false; // property invisible
          }
          return true;
        case age_key+states_key_offset:
          // age
          if (!hasDefinedState())
            aPropValue->setNull();
          else
            aPropValue->setDoubleValue((double)(MainLoop::now()-lastUpdate)/Second);
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case group_key+settings_key_offset:
          setPVar(binInputGroup, (DsGroup)aPropValue->int32Value());
          return true;
        case minPushInterval_key+settings_key_offset:
          setPVar(minPushInterval, (MLMicroSeconds)(aPropValue->doubleValue()*Second));
          return true;
        case changesOnlyInterval_key+settings_key_offset:
          setPVar(changesOnlyInterval, (MLMicroSeconds)(aPropValue->doubleValue()*Second));
          return true;
        case configuredInputType_key+settings_key_offset: // aka "sensorFunction"
          setPVar(configuredInputType, (DsBinaryInputType)aPropValue->int32Value());
          return true;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}



// MARK: - description/shortDesc


string BinaryInputBehaviour::description()
{
  string s = string_format("%s behaviour", shortDesc().c_str());
  string_format_append(s, "\n- binary input type: %d, reportsChanges=%d, interval: %lld mS", hardwareInputType, reportsChanges, updateInterval/MilliSecond);
  string_format_append(s, "\n- minimal interval between pushes: %lld mS, aliveSignInterval: %lld mS", minPushInterval/MilliSecond, aliveSignInterval/MilliSecond);
  s.append(inherited::description());
  return s;
}
