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

#include "dsbehaviour.hpp"

#include "device.hpp"

using namespace p44;



// MARK: ===== DsBehaviour

DsBehaviour::DsBehaviour(Device &aDevice, const string aBehaviourId) :
  inheritedParams(aDevice.getVdcHost().getDsParamStore()),
  behaviourId(aBehaviourId),
  index(0),
  device(aDevice),
  hardwareName(""), // empty, will show behaviour ID by default
  hardwareError(hardwareError_none),
  hardwareErrorUpdated(p44::Never)
{
}


DsBehaviour::~DsBehaviour()
{
}


void DsBehaviour::setHardwareError(VdcHardwareError aHardwareError)
{
  if (aHardwareError!=hardwareError) {
    // error status has changed
    hardwareError = aHardwareError;
    hardwareErrorUpdated = MainLoop::now();
    // push the error status change
    pushBehaviourState();
  }
}


bool DsBehaviour::pushBehaviourState()
{
  VdcApiConnectionPtr api = device.getVdcHost().getSessionConnection();
  if (api) {
    ApiValuePtr query = api->newApiValue();
    query->setType(apivalue_object);
    ApiValuePtr subQuery = query->newValue(apivalue_object);
    subQuery->add(getApiId(api->getApiVersion()), subQuery->newValue(apivalue_null));
    query->add(string(getTypeName()).append("States"), subQuery);
    return device.pushNotification(query, ApiValuePtr(), VDC_API_DOMAIN, api->getApiVersion());
  }
  // could not push
  return false;
}


string DsBehaviour::getDbKey()
{
  return string_format("%s_%zu",device.dSUID.getString().c_str(),index);
}


ErrorPtr DsBehaviour::load()
{
  ErrorPtr err = loadFromStore(getDbKey().c_str());
  if (!Error::isOK(err)) BLOG(LOG_ERR,"Error loading behaviour %s: %s", shortDesc().c_str(), err->description().c_str());
  return err;
}


ErrorPtr DsBehaviour::save()
{
  ErrorPtr err = saveToStore(getDbKey().c_str(), false); // only one record per dbkey (=per device+behaviourindex)
  if (!Error::isOK(err)) BLOG(LOG_ERR,"Error saving behaviour %s: %s", shortDesc().c_str(), err->description().c_str());
  return err;
}


ErrorPtr DsBehaviour::forget()
{
  return deleteFromStore();
}


// MARK: ===== property access


string DsBehaviour::getApiId(int aApiVersion)
{
  if (aApiVersion>=3 && !behaviourId.empty()) {
    return behaviourId;
  }
  else {
    // no channel ID set, default to decimal string representation of channel type
    return string_format("%zu", getIndex());
  }
}





const char *DsBehaviour::getTypeName()
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
  error_key,
  numDsBehaviourStateProperties
};



static char dsBehaviour_Key;

int DsBehaviour::numLocalProps(PropertyDescriptorPtr aParentDescriptor)
{
  // Note: output does not have an intermediate level as there is only one
  // we need to get the fieldkey of the device level behaviour property of which this behaviour is a child or grandchild
  PropertyDescriptorPtr pdP = aParentDescriptor->parentDescriptor; // check parent of parent
  if (!pdP || pdP->objectKey()!=aParentDescriptor->objectKey()) {
    // if parent's parent is another object, there is no intermediate enumeration level (for buttons, binaryInputs, sensors), but field directly
    pdP = aParentDescriptor;
  }
  switch (pdP->fieldKey()) {
    case descriptions_key_offset: return numDescProps()+numDsBehaviourDescProperties;
    case settings_key_offset: return numSettingsProps(); // no settings on the DsBehaviour level
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
  PropertyDescriptorPtr pdP = aParentDescriptor->parentDescriptor; // check parent of parent
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
      // no settings at the DsBehaviour level
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
        // state
        case error_key+states_key_offset: aPropValue->setUint16Value(hardwareError); return true;
      }
    }
  }
  return inheritedProps::accessField(aMode, aPropValue, aPropertyDescriptor); // let base class handle it
}



// MARK: ===== description/shortDesc


string DsBehaviour::shortDesc()
{
  return getTypeName();
}


string DsBehaviour::description()
{
  string s = string_format("\n- behaviour hardware name: '%s'", getHardwareName().c_str());
  string_format_append(s, "\n- hardwareError: %d\n", hardwareError);
  return s;
}





