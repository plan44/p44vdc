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

#include "binaryinputbehaviour.hpp"

using namespace p44;

#define DSS_INPUT_MAX_PUSH_INTERVAL (55*Minute) // all inputs that have a aliveSignInterval!=Never must get pushed at least this often (otherwise dSS flags sensor red)

BinaryInputBehaviour::BinaryInputBehaviour(Device &aDevice, const string aId) :
  inherited(aDevice, aId),
  // persistent settings
  mBinInputGroup(group_black_variable),
  mConfiguredInputType(binInpType_none),
  mMinPushInterval(2*Second), // don't push more often than every 2 seconds
  mMaxPushInterval(0),
  mChangesOnlyInterval(30*Minute), // report unchanged state updates max once every 30 minutes
  mAutoResetTo(-1), // no auto reset
  // state
  #if ENABLE_JSONBRIDGEAPI
  mBridgeExclusive(false),
  #endif
  mLastUpdate(Never),
  mLastPush(Never),
  mCurrentState(0)
{
  // set dummy default hardware default configuration (no known alive sign interval!)
  setHardwareInputConfig(binInpType_none, usage_undefined, true, 15*Second, 0);
}


BinaryInputBehaviour::~BinaryInputBehaviour()
{
}



const char *inputTypeIds[numBinaryInputTypes] = {
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
  mHardwareInputType = aInputType;
  mInputUsage = aUsage;
  mReportsChanges = aReportsChanges;
  mUpdateInterval = aUpdateInterval;
  mAutoResetTo = aAutoResetTo;
  mAliveSignInterval = aAliveSignInterval;
  mMaxPushInterval = mAliveSignInterval==Never ? Never : DSS_INPUT_MAX_PUSH_INTERVAL; // sensors without any update guarantee do not need to fake regular pushes
  // setup standard timeout to undefined state
  startInputTimeout(mAliveSignInterval, undefined);
  // set default input mode to hardware type
  mConfiguredInputType = mHardwareInputType;
}


string BinaryInputBehaviour::getAutoId()
{
  return inputTypeIds[mHardwareInputType];
}


bool BinaryInputBehaviour::isBridgeExclusive()
{
  #if ENABLE_JSONBRIDGEAPI
  return mDevice.isBridged() && mBridgeExclusive;
  #else
  return false;
  #endif
}


InputState BinaryInputBehaviour::maxExtendedValue()
{
  if (mConfiguredInputType==binInpType_windowHandle) return 2; // Window handle is tri-state
  return 1; // all others are binary so far
}


void BinaryInputBehaviour::startInputTimeout(MLMicroSeconds aTimeout, int aAfterTimeOutState)
{
  mTimeoutTicket.cancel();
  if (aTimeout!=Never) {
    // schedule invalidation or auto-reset
    mTimeoutTicket.executeOnce(boost::bind(&BinaryInputBehaviour::inputTimeout, this, aAfterTimeOutState), aTimeout);
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
  mLastUpdate = now;
  if (mAutoResetTo>=0 && aNewState!=mAutoResetTo) {
    // this update sets the output to a non-reset state -> set up auto reset
    startInputTimeout(mUpdateInterval, mAutoResetTo);
  }
  else {
    // just start the invalidation timeout
    startInputTimeout(mAliveSignInterval, -1);
  }
  bool changedState = aNewState!=mCurrentState;
  if (changedState) {
    // input state change is considered a (regular!) user action, have it checked globally first
    mDevice.getVdcHost().signalDeviceUserAction(mDevice, true);
    // Note: even if global identify handler processes this, still report state changes (otherwise upstream could get out of sync)
  }
  OLOG(changedState ? LOG_NOTICE : LOG_INFO, "reports %s state = %d", changedState ? "NEW" : "same", aNewState);
  // in all cases, binary input state changes must be forwarded long term
  // (but minPushInterval must "debounce" rapid intermediate changes)
  if (changedState || now>mLastPush+mChangesOnlyInterval) {
    // changed state or no update sent for more than changesOnlyInterval
    mCurrentState = aNewState;
    pushInput(changedState);
  }
  // notify listeners
  #if ENABLE_P44SCRIPT
  // send event
  sendValueEvent();
  #endif
}


bool BinaryInputBehaviour::pushInput(bool aChanged)
{
  MLMicroSeconds now = MainLoop::now();
  if (mLastPush==Never || now>mLastPush+mMinPushInterval) {
    // push the new value right now
    if (pushBehaviourState(!isBridgeExclusive(), true)) {
      mLastPush = now;
      OLOG(LOG_NOTICE, "successfully pushed state = %d", mCurrentState);
      if (hasDefinedState() && mMaxPushInterval!=Never) {
        // schedule re-push of defined state
        mUpdateTicket.executeOnce(boost::bind(&BinaryInputBehaviour::pushInput, this, false), mMaxPushInterval);
      }
      return true;
    }
    else if (mDevice.isPublicDS() || mDevice.isBridged()) {
      OLOG(LOG_NOTICE, "could not be pushed");
    }
  }
  else if (aChanged) {
    // cannot be pushed now, but final state of the input must be reported later
    OLOG(LOG_INFO, "input changes too quickly, push of final state will be pushed after minPushInterval");
    mUpdateTicket.executeOnceAt(boost::bind(&BinaryInputBehaviour::reportFinalState, this), mLastPush+mMinPushInterval);
  }
  return false;
}



void BinaryInputBehaviour::reportFinalState()
{
  // push the current value (after awaiting minPushInterval or after maxPushInterval has passed)
  mUpdateTicket.cancel();
  if (pushBehaviourState(!isBridgeExclusive(), true)) {
    OLOG(LOG_NOTICE, "now pushes current state (%d) after awaiting minPushInterval", mCurrentState);
    mLastPush = MainLoop::currentMainLoop().now();
  }
}


void BinaryInputBehaviour::invalidateInputState()
{
  if (hasDefinedState()) {
    // currently valid -> invalidate
    mLastUpdate = Never;
    //currentState = 0; // do NOT reset the state, it is better to use the last known state (for the valuesource value in p44scripts)
    mUpdateTicket.cancel();
    OLOG(LOG_NOTICE, "reports input state no longer available");
    // push invalidation (primitive clients not capable of NULL will at least see state==false)
    MLMicroSeconds now = MainLoop::now();
    // push the invalid state
    if (pushBehaviourState(true, true)) {
      mLastPush = now;
    }
    // notify listeners
    #if ENABLE_P44SCRIPT
    sendValueEvent();
    #endif
  }
}


bool BinaryInputBehaviour::hasDefinedState()
{
  return mLastUpdate!=Never;
}


void BinaryInputBehaviour::revalidateState()
{
  if (hasDefinedState() && (mAutoResetTo<0 || mUpdateInterval==Never || mCurrentState==mAutoResetTo)) {
    // re-arm invalidator (unless autoreset is pending)
    startInputTimeout(mAliveSignInterval, undefined);
  }
}


string BinaryInputBehaviour::getStatusText()
{
  if (hasDefinedState()) {
    return string_format("%d", mCurrentState);
  }
  return inherited::getStatusText();
}



#if ENABLE_P44SCRIPT
// MARK: - value source implementation

string BinaryInputBehaviour::getSourceId()
{
  return string_format("%s_I%s", mDevice.getDsUid().getString().c_str(), getId().c_str());
}


string BinaryInputBehaviour::getSourceName()
{
  // get device name or dSUID for context
  string n = mDevice.getAssignedName();
  if (n.empty()) {
    // use abbreviated dSUID instead
    string d = mDevice.getDsUid().getString();
    n = d.substr(0,8) + "..." + d.substr(d.size()-2,2);
  }
  // append behaviour description
  string_format_append(n, ": %s", getHardwareName().c_str());
  return n;
}


double BinaryInputBehaviour::getSourceValue()
{
  return mCurrentState;
}


MLMicroSeconds BinaryInputBehaviour::getSourceLastUpdate()
{
  return mLastUpdate;
}


int BinaryInputBehaviour::getSourceOpLevel()
{
  return mDevice.opStateLevel();
}

#endif // ENABLE_P44SCRIPT 


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
  aRow->getCastedIfNotNull<DsGroup, int>(aIndex++, mBinInputGroup);
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, mMinPushInterval);
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, mChangesOnlyInterval);
  aRow->getCastedIfNotNull<DsBinaryInputType, int>(aIndex++, mConfiguredInputType);
}


// bind values to passed statement
void BinaryInputBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, mBinInputGroup);
  aStatement.bind(aIndex++, (long long int)mMinPushInterval);
  aStatement.bind(aIndex++, (long long int)mChangesOnlyInterval);
  aStatement.bind(aIndex++, (long long int)mConfiguredInputType);
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
    { "inputType", apivalue_bool, reportsChanges_key+descriptions_key_offset, OKEY(binaryInput_key) }, // reports changes or not
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
  #if ENABLE_JSONBRIDGEAPI
  bridgeExclusive_key,
  #endif
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
    #if ENABLE_JSONBRIDGEAPI
    { "x-p44-bridgeExclusive", apivalue_bool, bridgeExclusive_key+settings_key_offset, OKEY(binaryInput_key) },
    #endif
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
          aPropValue->setUint8Value(mHardwareInputType);
          return true;
        case inputUsage_key+descriptions_key_offset:
          aPropValue->setUint8Value(mInputUsage);
          return true;
        case reportsChanges_key+descriptions_key_offset: // aka "inputType", 1=reporting, 0=needs polling
          aPropValue->setUint8Value(mReportsChanges ? 1 : 0);
          return true;
        case updateInterval_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)mUpdateInterval/Second);
          return true;
        case aliveSignInterval_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)mAliveSignInterval/Second);
          return true;
        case maxPushInterval_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)mMaxPushInterval/Second);
          return true;
        // Settings properties
        case group_key+settings_key_offset:
          aPropValue->setUint16Value(mBinInputGroup);
          return true;
        case minPushInterval_key+settings_key_offset:
          aPropValue->setDoubleValue((double)mMinPushInterval/Second);
          return true;
        case changesOnlyInterval_key+settings_key_offset:
          aPropValue->setDoubleValue((double)mChangesOnlyInterval/Second);
          return true;
        case configuredInputType_key+settings_key_offset: // aka "sensorFunction"
          aPropValue->setUint8Value(mConfiguredInputType);
          return true;
        #if ENABLE_JSONBRIDGEAPI
        case bridgeExclusive_key+settings_key_offset:
          if (!mDevice.isBridged()) return false; // hide when not bridged
          aPropValue->setBoolValue(mBridgeExclusive);
          return true;
        #endif
        // States properties
        case value_key+states_key_offset:
          // value
          if (!hasDefinedState())
            aPropValue->setNull();
          else
            aPropValue->setBoolValue(mCurrentState>=1); // all states > 0 are considered "true" for the basic state
          return true;
        case extendedValue_key+states_key_offset:
          // extended value
          if (maxExtendedValue()>1) {
            // this is a multi-state input, show the actual state as "extendedValue"
            if (!hasDefinedState())
              aPropValue->setNull();
            else
              aPropValue->setUint8Value(mCurrentState);
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
            aPropValue->setDoubleValue((double)(MainLoop::now()-mLastUpdate)/Second);
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case group_key+settings_key_offset:
          setPVar(mBinInputGroup, (DsGroup)aPropValue->int32Value());
          return true;
        case minPushInterval_key+settings_key_offset:
          setPVar(mMinPushInterval, (MLMicroSeconds)(aPropValue->doubleValue()*Second));
          return true;
        case changesOnlyInterval_key+settings_key_offset:
          setPVar(mChangesOnlyInterval, (MLMicroSeconds)(aPropValue->doubleValue()*Second));
          return true;
        case configuredInputType_key+settings_key_offset: // aka "sensorFunction"
          setPVar(mConfiguredInputType, (DsBinaryInputType)aPropValue->int32Value());
          return true;
        #if ENABLE_JSONBRIDGEAPI
        case bridgeExclusive_key+settings_key_offset:
          // volatile, does not make settings dirty
          mBridgeExclusive = aPropValue->boolValue();
          return true;
        #endif
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
  string_format_append(s, "\n- binary input type: %d, reportsChanges=%d, interval: %lld mS", mHardwareInputType, mReportsChanges, mUpdateInterval/MilliSecond);
  string_format_append(s, "\n- minimal interval between pushes: %lld mS, aliveSignInterval: %lld mS", mMinPushInterval/MilliSecond, mAliveSignInterval/MilliSecond);
  s.append(inherited::description());
  return s;
}
