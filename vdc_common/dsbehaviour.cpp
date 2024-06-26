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

#include "dsbehaviour.hpp"

#include "device.hpp"

using namespace p44;



// MARK: - DsBehaviour

DsBehaviour::DsBehaviour(Device &aDevice, const string aBehaviourId) :
  inheritedParams(aDevice.getVdcHost().getDsParamStore()),
  mBehaviourId(aBehaviourId),
  mIndex(0),
  mDevice(aDevice),
  mHardwareName(""), // empty, will show behaviour ID by default
  mColorClass(class_undefined), // inherit color from device
  mHardwareError(hardwareError_none),
  mHardwareErrorUpdated(p44::Never)
{
}


DsBehaviour::~DsBehaviour()
{
}


DsClass DsBehaviour::getColorClass()
{
  if (mColorClass!=class_undefined) return mColorClass;
  // no specific color set on the behaviour level: try to derive from group
  DsGroup group = getGroup();
  if (group!=group_undefined) return Device::colorClassFromGroup(group);
  // no group set on behaviour level: use colorClass of device
  return mDevice.getColorClass();
}


void DsBehaviour::setHardwareError(VdcHardwareError aHardwareError)
{
  if (aHardwareError!=mHardwareError) {
    // error status has changed
    mHardwareError = aHardwareError;
    mHardwareErrorUpdated = MainLoop::now();
    // push the error status change to dS and bridges
    pushBehaviourState(true, true);
  }
}


bool DsBehaviour::pushBehaviourState(bool aDS, bool aBridges)
{
  bool requestedPushDone = true;

  if (aDS) {
    // push to vDC API
    VdcApiConnectionPtr api = mDevice.getVdcHost().getVdsmSessionConnection();
    if (api) {
      ApiValuePtr q = api->newApiValue();
      q = q->wrapNull(getApiId(api->getApiVersion()))->wrapAs(string(getTypeName()).append("States"));
      if (!mDevice.pushNotification(api, q, ApiValuePtr())) requestedPushDone = false;
    }
    else {
      requestedPushDone = false;
    }
  }
  #if ENABLE_JSONBRIDGEAPI
  if (aBridges && mDevice.isBridged()) {
    // push to bridges
    VdcApiConnectionPtr api = mDevice.getVdcHost().getBridgeApi();
    if (api) {
      ApiValuePtr q = api->newApiValue();
      q = q->wrapNull(getApiId(api->getApiVersion()))->wrapAs(string(getTypeName()).append("States"));
      if (!mDevice.pushNotification(api, q, ApiValuePtr())) requestedPushDone = false;
    }
    else {
      requestedPushDone = false;
    }
  }
  #endif
  // true if requested pushes are done or irrelevant (e.g. bridge push requested w/o bridging enabled at all)
  return requestedPushDone;
}


string DsBehaviour::getDbKey()
{
  return string_format("%s_%zu",mDevice.mDSUID.getString().c_str(),mIndex);
}


ErrorPtr DsBehaviour::load()
{
  ErrorPtr err = loadFromStore(getDbKey().c_str());
  if (Error::notOK(err)) OLOG(LOG_ERR,"Error loading behaviour %s: %s", shortDesc().c_str(), err->text());
  return err;
}


ErrorPtr DsBehaviour::save()
{
  ErrorPtr err = saveToStore(getDbKey().c_str(), false); // only one record per dbkey (=per device+behaviourindex)
  if (Error::notOK(err)) OLOG(LOG_ERR,"Error saving behaviour %s: %s", shortDesc().c_str(), err->text());
  return err;
}


ErrorPtr DsBehaviour::forget()
{
  return deleteFromStore();
}


// MARK: - persistence implementation

/// Note: we do NOT define a `tableName()`, each specific behaviour has its own table

static const size_t numFields = 1;

size_t DsBehaviour::numFieldDefs()
{
  return inheritedParams::numFieldDefs()+numFields;
}


const FieldDefinition *DsBehaviour::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "colorClass", SQLITE_INTEGER },
  };
  if (aIndex<inheritedParams::numFieldDefs())
    return inheritedParams::getFieldDef(aIndex);
  aIndex -= inheritedParams::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void DsBehaviour::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inheritedParams::loadFromRow(aRow, aIndex, NULL); // no common flags in base class
  // get the fields
  aRow->getCastedIfNotNull<DsClass, int>(aIndex++, mColorClass); // if not present in DB, leave it untouched (class_undefined)
}


// bind values to passed statement
void DsBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  aStatement.bind(aIndex++, mColorClass);
}


// MARK: - property access


string DsBehaviour::getApiId(int aApiVersion) const
{
  if (aApiVersion>=3 && !mBehaviourId.empty()) {
    return mBehaviourId;
  }
  else {
    // no behaviour ID set, default to decimal string representation of behaviour index
    return string_format("%zu", getIndex());
  }
}


const char *DsBehaviour::getTypeName() const
{
  // Note: this must be the prefix for the xxxDescriptions, xxxSettings and xxxStates properties
  switch (getType()) {
    case behaviour_button : return "buttonInput";
    case behaviour_binaryinput : return "binaryInput";
    case behaviour_output : return "output";
    case behaviour_sensor : return "sensor";
    case behaviour_actionOutput : return "actionOutput"; // probably will never have actionOutputDescription/Settings/State
    default: return "<undefined>";
  }
}


enum {
  name_key,
  type_key,
  dsIndex_key,
  behaviourType_key,
  numDsBehaviourDescProperties
};

enum {
  colorClass_key,
  logLevelOffset_key,
  numDsBehaviourSettingsProperties
};

enum {
  error_key,
  numDsBehaviourStateProperties
};



static char dsBehaviour_Key;

int DsBehaviour::numLocalProps(PropertyDescriptorPtr aParentDescriptor)
{
  // Note: output does not have an intermediate level as there is only one
  // we need to get the fieldkey of the device level behaviour property of which this behaviour is a child or grandchild
  PropertyDescriptorPtr pdP = aParentDescriptor->mParentDescriptor; // check parent of parent
  if (!pdP || pdP->objectKey()!=aParentDescriptor->objectKey()) {
    // if parent's parent is another object, there is no intermediate enumeration level (for buttons, binaryInputs, sensors), but field directly
    pdP = aParentDescriptor;
  }
  switch (pdP->fieldKey()) {
    case descriptions_key_offset: return numDescProps()+numDsBehaviourDescProperties;
    case settings_key_offset: return numSettingsProps()+numDsBehaviourSettingsProperties;
    case states_key_offset: return numStateProps()+numDsBehaviourStateProperties;
    default: break;
  }
  return 0;
}


int DsBehaviour::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return inheritedProps::numProps(aDomain, aParentDescriptor)+numLocalProps(aParentDescriptor);
}


PropertyDescriptorPtr DsBehaviour::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription descProperties[numDsBehaviourDescProperties] = {
    { "name", apivalue_string, name_key+descriptions_key_offset, OKEY(dsBehaviour_Key) },
    { "type", apivalue_string, type_key+descriptions_key_offset, OKEY(dsBehaviour_Key) },
    { "dsIndex", apivalue_uint64, dsIndex_key+descriptions_key_offset, OKEY(dsBehaviour_Key) },
    { "x-p44-behaviourType", apivalue_string, behaviourType_key+descriptions_key_offset, OKEY(dsBehaviour_Key) },
  };
  static const PropertyDescription settingsProperties[numDsBehaviourSettingsProperties] = {
    { "x-p44-logLevelOffset", apivalue_int64, logLevelOffset_key+settings_key_offset, OKEY(dsBehaviour_Key) },
    { "colorClass", apivalue_uint64, colorClass_key+settings_key_offset, OKEY(dsBehaviour_Key) },
  };
  static const PropertyDescription stateProperties[numDsBehaviourStateProperties] = {
    { "error", apivalue_uint64, error_key+states_key_offset, OKEY(dsBehaviour_Key) },
  };
  int n = inheritedProps::numProps(aDomain, aParentDescriptor);
  if (aPropIndex<n)
    return inheritedProps::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
  aPropIndex -= n; // rebase to 0 for my own first property
  if (aPropIndex>=numLocalProps(aParentDescriptor))
    return NULL;
  // we need to get the fieldkey of the device level behaviour property of which this behaviour is a child or grandchild
  PropertyDescriptorPtr pdP = aParentDescriptor->mParentDescriptor; // check parent of parent
  if (!pdP || pdP->objectKey()!=aParentDescriptor->objectKey()) {
    // if parent's parent is another object, there is no intermediate enumeration level (for buttons, binaryInputs, sensors), but field directly
    pdP = aParentDescriptor;
  }
  switch (pdP->fieldKey()) {
    case descriptions_key_offset:
      // check for generic description properties
      if (aPropIndex<numDsBehaviourDescProperties)
        return PropertyDescriptorPtr(new StaticPropertyDescriptor(&descProperties[aPropIndex], aParentDescriptor));
      aPropIndex -= numDsBehaviourDescProperties;
      // check type-specific descriptions
      return getDescDescriptorByIndex(aPropIndex, aParentDescriptor);
    case settings_key_offset:
      // check for generic settings properties
      if (aPropIndex<numDsBehaviourSettingsProperties)
        return PropertyDescriptorPtr(new StaticPropertyDescriptor(&settingsProperties[aPropIndex], aParentDescriptor));
      aPropIndex -= numDsBehaviourSettingsProperties;
      // check type-specific settings
      return getSettingsDescriptorByIndex(aPropIndex, aParentDescriptor);
    case states_key_offset:
      // check for generic state properties
      if (aPropIndex<numDsBehaviourStateProperties)
        return PropertyDescriptorPtr(new StaticPropertyDescriptor(&stateProperties[aPropIndex], aParentDescriptor));
      aPropIndex -= numDsBehaviourStateProperties;
      // check type-specific states
      return getStateDescriptorByIndex(aPropIndex, aParentDescriptor);
    default:
      return NULL;
  }
}


bool DsBehaviour::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(dsBehaviour_Key)) {
    if (aMode==access_read) {
      // Read
      switch (aPropertyDescriptor->fieldKey()) {
        // descriptions
        case name_key+descriptions_key_offset: aPropValue->setStringValue(getHardwareName()); return true;
        case type_key+descriptions_key_offset: aPropValue->setStringValue(getTypeName()); return true;
        case dsIndex_key+descriptions_key_offset: aPropValue->setUint64Value(getIndex()); return true;
        case behaviourType_key+descriptions_key_offset: aPropValue->setStringValue(behaviourTypeIdentifier()); return true;
        // settings
        case logLevelOffset_key+settings_key_offset: { int o=getLocalLogLevelOffset(); if (o==0) return false; else aPropValue->setInt32Value(o); return true; }
        case colorClass_key+settings_key_offset: aPropValue->setUint16Value(getColorClass()); return true;
        // state
        case error_key+states_key_offset: aPropValue->setUint16Value(mHardwareError); return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case logLevelOffset_key+settings_key_offset:
          setLogLevelOffset(aPropValue->int32Value());
          return true;
        case colorClass_key+settings_key_offset:
          setPVar(mColorClass, (DsClass)aPropValue->uint16Value());
          return true;
      }
    }
  }
  return inheritedProps::accessField(aMode, aPropValue, aPropertyDescriptor); // let base class handle it
}



// MARK: - description/shortDesc/logging


string DsBehaviour::shortDesc()
{
  return getTypeName();
}


string DsBehaviour::description()
{
  string s = string_format("\n- behaviour hardware name: '%s'", getHardwareName().c_str());
  string_format_append(s, "\n- hardwareError: %d\n", mHardwareError);
  return s;
}


int DsBehaviour::getLogLevelOffset()
{
  if (logLevelOffset==0) {
    // no own offset - inherit device's
    return mDevice.getLogLevelOffset();
  }
  return inheritedProps::getLogLevelOffset();
}


string DsBehaviour::logContextPrefix()
{
  return string_format("%s: %s[%zu] %s '%s'", mDevice.logContextPrefix().c_str(), getTypeName(), getIndex(), getApiId(3).c_str(), getHardwareName().c_str());
}



string DsBehaviour::contextName() const
{
  return mDevice.contextName() + "/" + getApiId(3);
}


string DsBehaviour::contextType() const
{
  return mDevice.contextType() + "/" + getTypeName();
}


string DsBehaviour::contextId() const
{
  return string_format("#%zu", getIndex());
}





