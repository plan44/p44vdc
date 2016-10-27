//
//  Copyright (c) 2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include <math.h>

#include "singledevice.hpp"
#include "simplescene.hpp"

#include "jsonvdcapi.hpp"

using namespace p44;


// MARK: ===== ValueDescriptor


ValueDescriptor::ValueDescriptor(const string aName, VdcValueType aValueType, bool aHasDefault) :
  valueName(aName),
  valueType(aValueType),
  hasValue(aHasDefault),
  isDefault(aHasDefault),
  lastUpdate(Never),
  lastChange(Never)
{
}


bool ValueDescriptor::setLastUpdate(MLMicroSeconds aLastUpdate)
{
  if (aLastUpdate==Infinite)
    aLastUpdate = MainLoop::currentMainLoop().now();
  lastUpdate = aLastUpdate;
  bool gotValue = !hasValue; // if this is the first value update, consider value changed
  hasValue = true;
  isDefault = false;
  return gotValue;
}



bool ValueDescriptor::setChanged(bool aChanged)
{
  if (aChanged) {
    lastChange = MainLoop::currentMainLoop().now();
  }
  return (aChanged);
}



void ValueDescriptor::invalidate()
{
  hasValue = false;
}


string ValueDescriptor::getStringValue(bool aAsInternal, bool aPrevious)
{
  ApiValuePtr v = VdcHost::sharedVdcHost()->newApiValue();
  getValue(v, aAsInternal, aPrevious);
  return v->stringValue();
}


double ValueDescriptor::getDoubleValue(bool aAsInternal, bool aPrevious)
{
  ApiValuePtr v = VdcHost::sharedVdcHost()->newApiValue();
  getValue(v, aAsInternal, aPrevious);
  return v->doubleValue();
}


int32_t ValueDescriptor::getInt32Value(bool aAsInternal, bool aPrevious)
{
  ApiValuePtr v = VdcHost::sharedVdcHost()->newApiValue();
  getValue(v, aAsInternal, aPrevious);
  return v->int32Value();
}


bool ValueDescriptor::setValue(ApiValuePtr aValue)
{
  if (valueType<valueType_firstIntNum) {
    // numeric float type, set as double
    return setDoubleValue(aValue->doubleValue());
  }
  else if (valueType<valueType_firstNonNumeric || valueType==valueType_textenum) {
    // numeric integer type or textenum (internally integer), set as integer
    return setInt32Value(aValue->int32Value());
  }
  else {
    return setStringValue(aValue->stringValue());
  }
}




enum {
  valuetype_key,
  min_key,
  max_key,
  resolution_key,
  default_key,
  readonly_key,
  enumvalues_key,
  numValueProperties
};

static char value_key;
static char value_enumvalues_key;


int ValueDescriptor::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return numValueProperties;
}


PropertyDescriptorPtr ValueDescriptor::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // device level properties
  static const PropertyDescription properties[numValueProperties] = {
    { "valuetype", apivalue_uint64, valuetype_key, OKEY(value_key) },
    { "min", apivalue_double, min_key, OKEY(value_key) },
    { "max", apivalue_double, max_key, OKEY(value_key) },
    { "resolution", apivalue_double, resolution_key, OKEY(value_key) },
    { "default", apivalue_null, default_key, OKEY(value_key) },
    { "readonly", apivalue_bool, readonly_key, OKEY(value_key) },
    { "values", apivalue_object+propflag_container, enumvalues_key, OKEY(value_enumvalues_key) }
  };
  if (aParentDescriptor->isRootOfObject()) {
    // root level property of this object hierarchy
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


bool ValueDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(value_key) && aMode==access_read) {
    switch (aPropertyDescriptor->fieldKey()) {
      case valuetype_key: aPropValue->setUint16Value(valueType); return true;
      case readonly_key: if (readOnly) { aPropValue->setBoolValue(readOnly); return true; } else return false; // show only when set (only for deviceProperties)
      case default_key: return (isDefault ?  getValue(aPropValue, false) : false);
    }
  }
  return false;
}


// MARK: ===== NumericValueDescriptor


bool NumericValueDescriptor::setDoubleValue(double aValue)
{
  bool didChange = false; // assume no change
  if (setLastUpdate()) {
    // first time value is set - set both values and consider it a change
    previousValue = aValue;
    value = aValue;
    didChange = true;
  }
  if (value!=aValue) {
    // only changed values are considered a change
    previousValue = value;
    value = aValue;
    didChange = true;
  }
  return setChanged(didChange);
}


bool NumericValueDescriptor::updateDoubleValue(double aValue, double aMinChange)
{
  if (aMinChange<0) aMinChange = resolution/2;
  if (!hasValue || fabs(aValue - value) > aMinChange) {
    // change is large enough to actually update (or currently no value set at all)
    return setDoubleValue(aValue);
  }
  return false; // no change
}




bool NumericValueDescriptor::setInt32Value(int32_t aValue)
{
  return setDoubleValue(aValue);
}


ErrorPtr NumericValueDescriptor::conforms(ApiValuePtr aApiValue, bool aMakeInternal)
{
  ErrorPtr err;
  // check if value conforms
  ApiValueType vt = aApiValue->getType();
  if (valueType==valueType_bool) {
    // bool parameter, valuetype should be int or bool
    if (
      vt!=apivalue_bool &&
      vt!=apivalue_int64 &&
      vt!=apivalue_uint64
    ) {
      err = Error::err<VdcApiError>(415, "invalid boolean");
    }
  }
  else if (valueType<valueType_firstNonNumeric) {
    // check bounds
    double v = aApiValue->doubleValue();
    if (v<min || v>max) {
      err = Error::err<VdcApiError>(415, "number out of range");
    }
  }
  else {
    err = Error::err<VdcApiError>(415, "invalid number");
  }
  // everything else is not valid for numeric parameter
  return err;
}


bool NumericValueDescriptor::getValue(ApiValuePtr aApiValue, bool aAsInternal, bool aPrevious)
{
  if (!hasValue || !aApiValue) return false;
  double v = aPrevious ? previousValue : value;
  if (valueType==valueType_bool) {
    aApiValue->setType(apivalue_bool);
    aApiValue->setBoolValue(v);
  }
  else if (valueType==valueType_enum) {
    aApiValue->setType(apivalue_int64);
    aApiValue->setUint64Value(v);
  }
  else if (valueType>=valueType_firstIntNum) {
    aApiValue->setType(apivalue_int64);
    aApiValue->setInt64Value(v);
  }
  else {
    aApiValue->setType(apivalue_double);
    aApiValue->setDoubleValue(v);
  }
  return true;
}



bool NumericValueDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aMode==access_read) {
    // everything is read only
    if (aPropertyDescriptor->hasObjectKey(value_key)) {
      switch (aPropertyDescriptor->fieldKey()) {
        case min_key:
          if (valueType==valueType_bool) return false;
          aPropValue->setDoubleValue(min); return true;
        case max_key:
          if (valueType==valueType_bool) return false;
          aPropValue->setDoubleValue(max); return true;
        case resolution_key:
          if (valueType==valueType_bool) return false;
          aPropValue->setDoubleValue(resolution); return true;
      }
    }
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: ===== TextValueDescriptor

bool TextValueDescriptor::setStringValue(const string aValue)
{
  bool didChange = false; // assume no change
  if (setLastUpdate()) {
    // first time value is set - set both values and consider it a change
    previousValue = aValue;
    value = aValue;
    didChange = true;
  }
  if (!(value==aValue)) {
    // only changed values are considered a change
    previousValue = value;
    value = aValue;
    didChange = true;
  }
  return setChanged(didChange);
}


ErrorPtr TextValueDescriptor::conforms(ApiValuePtr aApiValue, bool aMakeInternal)
{
  ErrorPtr err;
  if (aApiValue) {
    // check if value conforms
    if (aApiValue->getType()!=apivalue_string) {
      err = Error::err<VdcApiError>(415, "invalid string");
    }
  }
  return err;
}



bool TextValueDescriptor::getValue(ApiValuePtr aApiValue, bool aAsInternal, bool aPrevious)
{
  if (!hasValue || !aApiValue) return false;
  aApiValue->setType(apivalue_string);
  aApiValue->setStringValue(value);
  return true;
}


// MARK: ===== EnumValueDescriptor


bool EnumValueDescriptor::setInt32Value(int32_t aValue)
{
  bool didChange = false; // assume no change
  if (setLastUpdate()) {
    // first time value is set - set both values and consider it a change
    previousValue = aValue;
    value = aValue;
    didChange = true;
  }
  if (value!=aValue) {
    // only changed values are considered a change
    previousValue = value;
    value = aValue;
    didChange = true;
  }
  return setChanged(didChange);
}



void EnumValueDescriptor::addEnum(const char *aEnumText, int aEnumValue, bool aIsDefault)
{
  enumDescs.push_back(EnumDesc(aEnumText, aEnumValue));
  if (aIsDefault) {
    value = aEnumValue; // also assign as default
    hasValue = true;
    isDefault = true;
  }
}


ErrorPtr EnumValueDescriptor::conforms(ApiValuePtr aApiValue, bool aMakeInternal)
{
  ErrorPtr err;
  if (aApiValue) {
    // check if value conforms
    if (aApiValue->getType()!=apivalue_string) {
      err = Error::err<VdcApiError>(415, "enum label must be string");
    }
    else {
      // must be one of the texts in the enum list
      string s = aApiValue->stringValue();
      for (EnumVector::iterator pos = enumDescs.begin(); pos!=enumDescs.end(); ++pos) {
        if (pos->first==s) {
          // found
          if (aMakeInternal) {
            aApiValue->setType(apivalue_uint64);
            aApiValue->setUint32Value(pos->second);
          }
          return err;
        }
      }
      err = Error::err<VdcApiError>(415, "invalid enum label");
    }
  }
  return err;
}


bool EnumValueDescriptor::getValue(ApiValuePtr aApiValue, bool aAsInternal, bool aPrevious)
{
  if (!hasValue || !aApiValue) return false;
  uint32_t v = aPrevious ? previousValue : value;
  if (aAsInternal) {
    aApiValue->setType(apivalue_uint64);
    aApiValue->setUint32Value(v);
    return true;
  }
  else {
    aApiValue->setType(apivalue_string);
    for (EnumVector::iterator pos = enumDescs.begin(); pos!=enumDescs.end(); ++pos) {
      if (pos->second==v) {
        aApiValue->setStringValue(pos->first);
        return true;
      }
    }
    return false;
  }
}


bool EnumValueDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aMode==access_read) {
    // everything is read only
    if (aPropertyDescriptor->hasObjectKey(value_enumvalues_key)) {
      // all enum list properties are NULL values...
      return true; // ...but they exist!
    }
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


PropertyContainerPtr EnumValueDescriptor::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->isArrayContainer() && aPropertyDescriptor->hasObjectKey(value_enumvalues_key)) {
    return PropertyContainerPtr(this); // handle enum values array myself
  }
  // unknown here
  return inherited::getContainer(aPropertyDescriptor, aDomain);
}


int EnumValueDescriptor::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(value_enumvalues_key)) {
    // number of enum values
    return (int)enumDescs.size();
  }
  return inherited::numProps(aDomain, aParentDescriptor);
}


PropertyDescriptorPtr EnumValueDescriptor::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(value_enumvalues_key)) {
    // enumvalues - distinct set of NULL values (only names count)
    if (aPropIndex<enumDescs.size()) {
      DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
      descP->propertyName = enumDescs[aPropIndex].first;
      descP->propertyType = apivalue_null;
      descP->propertyFieldKey = aPropIndex;
      descP->propertyObjectKey = OKEY(value_enumvalues_key);
      return descP;
    }
  }
  return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor);
}


// MARK: ===== ValueList

void ValueList::addValue(ValueDescriptorPtr aValueDesc)
{
  values.push_back(aValueDesc);
}


ValueDescriptorPtr ValueList::getValue(const string aName)
{
  for (ValuesVector::iterator pos = values.begin(); pos!=values.end(); ++pos) {
    if ((*pos)->valueName==aName) {
      // found
      return *pos;
    }
  }
  // not found
  return ValueDescriptorPtr();
}





int ValueList::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)values.size();
}


static char valueDescriptor_key;

PropertyDescriptorPtr ValueList::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<values.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = values[aPropIndex]->valueName;
    descP->propertyType = apivalue_object;
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(valueDescriptor_key);
    return descP;
  }
  return PropertyDescriptorPtr();
}


PropertyContainerPtr ValueList::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(valueDescriptor_key)) {
    return values[aPropertyDescriptor->fieldKey()];
  }
  return NULL;
}


// MARK: ===== DeviceAction

DeviceAction::DeviceAction(SingleDevice &aSingleDevice, const string aId, const string aDescription) :
  singleDeviceP(&aSingleDevice),
  actionId(aId),
  actionDescription(aDescription)
{
  // install value list for parameters
  actionParams = ValueListPtr(new ValueList);
}


void DeviceAction::addParameter(ValueDescriptorPtr aValueDesc)
{
  actionParams->addValue(aValueDesc);
}


void DeviceAction::call(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  ErrorPtr err;

  // for each parameter of the action, we either need a value in aParams or a default value
  ValueList::ValuesVector::iterator pos = actionParams->values.begin();
  while (pos!=actionParams->values.end()) {
    // check for override from passed params
    ApiValuePtr o = aParams->get((*pos)->getName());
    if (o) {
      // caller did supply this parameter
      err = (*pos)->conforms(o, true); // check and convert to internal (for text enums)
      if (!Error::isOK(err))
        break; // error
    }
    else {
      // caller did not supply this parameter, get default value
      o = aParams->newNull();
      if (!(*pos)->getValue(o)) {
        err = Error::err<VdcApiError>(415, "missing value for non-optional parameter");
        break;
      }
      // add the default to the passed params
      aParams->add((*pos)->getName(), o);
    }
    ++pos;
  }
  if (!Error::isOK(err)) {
    // rewrite error to include param name
    if (pos!=actionParams->values.end() && err->isDomain(VdcApiError::domain())) {
      err = Error::err<VdcApiError>(err->getErrorCode(), "parameter '%s': %s", (*pos)->getName().c_str(), err->description().c_str());
    }
    // parameter error, not executing action, call back with error
    if (aCompletedCB) aCompletedCB(err);
    return; // done
  }
  // parameters are ok, now invoking action implementation
  LOG(LOG_INFO, "- calling with expanded params: %s:%s", actionId.c_str(), aParams->description().c_str());
  performCall(aParams, aCompletedCB);
}



void DeviceAction::performCall(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  if (aCompletedCB) aCompletedCB(Error::err<VdcApiError>(501, "dummy action - not implemented"));
}


// MARK: ===== DeviceAction property access

enum {
  actiondescription_key,
  actionparams_key,
  numActionProperties
};

static char deviceaction_key;


int DeviceAction::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return numActionProperties;
}


PropertyDescriptorPtr DeviceAction::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // device level properties
  static const PropertyDescription properties[numActionProperties] = {
    { "description", apivalue_string, actiondescription_key, OKEY(deviceaction_key) },
    { "params", apivalue_object, actionparams_key, OKEY(deviceaction_key) }
  };
  if (aParentDescriptor->isRootOfObject()) {
    // root level property of this object hierarchy
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


PropertyContainerPtr DeviceAction::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(deviceaction_key)) {
    if (aPropertyDescriptor->fieldKey()==actionparams_key) {
      return actionParams;
    }
  }
  return NULL;
}


bool DeviceAction::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(deviceaction_key) && aMode==access_read) {
    switch (aPropertyDescriptor->fieldKey()) {
      case actiondescription_key: aPropValue->setStringValue(actionDescription); return true;
    }
  }
  return false;
}



// MARK: ===== DeviceActions container


void DeviceActions::addToModelUIDHash(string &aHashedString)
{
  for (ActionsVector::iterator pos = deviceActions.begin(); pos!=deviceActions.end(); ++pos) {
    aHashedString += ':';
    aHashedString += (*pos)->actionId;
  }
}


int DeviceActions::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)deviceActions.size();
}


static char actions_key;

PropertyDescriptorPtr DeviceActions::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<deviceActions.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = deviceActions[aPropIndex]->actionId;
    descP->propertyType = apivalue_object;
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(actions_key);
    return descP;
  }
  return PropertyDescriptorPtr();
}


PropertyContainerPtr DeviceActions::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(actions_key)) {
    return deviceActions[aPropertyDescriptor->fieldKey()];
  }
  return NULL;
}


DeviceActionPtr DeviceActions::getAction(const string aActionId)
{
  for (ActionsVector::iterator pos = deviceActions.begin(); pos!=deviceActions.end(); ++pos) {
    if ((*pos)->actionId==aActionId) {
      // found
      return *pos;
    }
  }
  // not found
  return DeviceActionPtr();
}


bool DeviceActions::call(const string aActionId, ApiValuePtr aParams, StatusCB aCompletedCB)
{
  DeviceActionPtr a = getAction(aActionId);
  if (a) {
    // action exists
    a->call(aParams, aCompletedCB);
    return true; // done, callback will finish it
  }
  return false;
}


void DeviceActions::addAction(DeviceActionPtr aAction)
{
  deviceActions.push_back(aAction);
}



// MARK: ===== CustomAction

CustomAction::CustomAction(SingleDevice &aSingleDevice) :
  singleDevice(aSingleDevice),
  inheritedParams(aSingleDevice.getVdcHost().getDsParamStore()),
  flags(0)
{
  storedParams = ApiValuePtr(new JsonApiValue()); // must be JSON as we store params as JSON
  storedParams->setType(apivalue_object);
}


void CustomAction::call(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  ErrorPtr err;

  // copy each of the stored params, unless same param is alreday in aParams (which means overridden)
  if (storedParams->resetKeyIteration()) {
    string key;
    ApiValuePtr val;
    while (storedParams->nextKeyValue(key, val)) {
      // already exists in aParams?
      if (!aParams->get(key)) {
        // this param was not overridden by an argument to call()
        // -> add it to the arguments we'll pass on
        ApiValuePtr pval = aParams->newNull();
        *pval = *val; // do a value copy because  API value types might be different
        aParams->add(key, pval);
      }
    }
  }
  // parameters collected, now invoke actual device action
  LOG(LOG_INFO, "- custom action %s calls %s:%s", actionId.c_str(), action->actionId.c_str(), aParams->description().c_str());
  action->call(aParams, aCompletedCB);
}



// MARK: ===== CustomAction property access

enum {
  customactionaction_key,
  customactiontitle_key,
  customactionparams_key,
  numCustomActionProperties
};

static char customaction_key;


int CustomAction::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return numCustomActionProperties;
}


PropertyDescriptorPtr CustomAction::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // device level properties
  static const PropertyDescription properties[numCustomActionProperties] = {
    { "action", apivalue_string, customactionaction_key, OKEY(customaction_key) },
    { "title", apivalue_string, customactiontitle_key, OKEY(customaction_key) },
    { "params", apivalue_null, customactionparams_key, OKEY(customaction_key) }
  };
  if (aParentDescriptor->isRootOfObject()) {
    // root level property of this object hierarchy
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


ErrorPtr CustomAction::validateParams(ApiValuePtr aParams, ApiValuePtr aValidatedParams, bool aSkipInvalid)
{
  // rebuild params
  aValidatedParams->clear();
  // check to-be-stored parameters
  if (!aParams || aParams->isNull())
    return ErrorPtr(); // NULL is ok and means no params
  if (!aParams->isType(apivalue_object)) {
    return TextError::err("params needs to be an object");
  }
  // go through params
  aParams->resetKeyIteration();
  string key;
  ApiValuePtr val;
  while(aParams->nextKeyValue(key, val)) {
    ErrorPtr err;
    ValueDescriptorPtr vd;
    if (action) {
      vd = action->actionParams->getValue(key);
      if (!vd) {
        if (aSkipInvalid) continue; // just ignore, but continue checking others
        return TextError::err("parameter '%s' unknown for action '%s'", key.c_str(), action->actionId.c_str());
      }
    }
    if (vd) {
      // param with that name exists
      err = vd->conforms(val, false);
    }
    if (Error::isOK(err)) {
      ApiValuePtr myParam = aValidatedParams->newValue(val->getType());
      *myParam = *val; // do a value copy because  API value types might be different
      aValidatedParams->add(key, myParam);
    }
    else {
      if (aSkipInvalid) continue; // just ignore, but continue checking others
      return TextError::err("invalid parameter '%s': %s", key.c_str(), err->description().c_str());
    }
  }
  SALOG(singleDevice, LOG_DEBUG, "validated params: %s", aValidatedParams ? aValidatedParams->description().c_str() : "<none>");
  return ErrorPtr(); // all parameters conform (or aSkipInvalid)
}


bool CustomAction::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(customaction_key)) {
    if (aMode==access_read) {
      // read
      switch (aPropertyDescriptor->fieldKey()) {
        case customactionaction_key: aPropValue->setStringValue(action ? action->actionId : "INVALID"); return true;
        case customactiontitle_key: aPropValue->setStringValue(actionTitle); return true;
        case customactionparams_key: {
          // simply return the stored params object
          *aPropValue = *storedParams; // do a value copy because  API value types might be different
          return true;
        }
      }
    }
    else {
      // write
      switch (aPropertyDescriptor->fieldKey()) {
        case customactionaction_key: {
          // assign new action
          DeviceActionPtr a = singleDevice.deviceActions->getAction(aPropValue->stringValue());
          if (a) {
            // action exists
            action = a; // assign it
            markDirty();
            // clean parameters to conform with new action
            ApiValuePtr newParams = storedParams->newValue(apivalue_object);
            validateParams(storedParams, newParams, true); // just skip invalid ones
            storedParams = newParams; // use cleaned-up set of params
            return true;
          }
          SALOG(singleDevice, LOG_ERR, "there is no deviceAction called '%s'", aPropValue->stringValue().c_str());
          return false;
        }
        case customactiontitle_key: {
          setPVar(actionTitle, aPropValue->stringValue());
          return true;
        }
        case customactionparams_key: {
          ApiValuePtr newParams = storedParams->newValue(apivalue_object);
          ErrorPtr err = validateParams(aPropValue, newParams, false);
          if (Error::isOK(err)) {
            // assign
            markDirty();
            storedParams = newParams;
            return true;
          }
          // error
          SALOG(singleDevice, LOG_ERR, "writing 'params' failed: %s", err->description().c_str());
          return false;
        }
      }
    }
    return false;
  }
  return inheritedProps::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: ===== CustomAction persistence

const char *CustomAction::tableName()
{
  return "customActions";
}



// primary key field definitions

static const size_t numKeys = 1;

size_t CustomAction::numKeyDefs()
{
  return inheritedParams::numKeyDefs()+numKeys;
}

const FieldDefinition *CustomAction::getKeyDef(size_t aIndex)
{
  static const FieldDefinition keyDefs[numKeys] = {
    { "customActionId", SQLITE_TEXT }, // parent's key plus this one identifies this custom action among all saved custom actions of all devices
  };
  if (aIndex<inheritedParams::numKeyDefs())
    return inheritedParams::getKeyDef(aIndex);
  aIndex -= inheritedParams::numKeyDefs();
  if (aIndex<numKeys)
    return &keyDefs[aIndex];
  return NULL;
}


// data field definitions

static const size_t numFields = 4;

size_t CustomAction::numFieldDefs()
{
  return inheritedParams::numFieldDefs()+numFields;
}


const FieldDefinition *CustomAction::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "title", SQLITE_TEXT },
    { "actionId", SQLITE_TEXT },
    { "paramsJSON", SQLITE_TEXT },
    { "flags", SQLITE_INTEGER }
  };
  if (aIndex<inheritedParams::numFieldDefs())
    return inheritedParams::getFieldDef(aIndex);
  aIndex -= inheritedParams::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


void CustomAction::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inheritedParams::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the custom action id (my own)
  actionId = nonNullCStr(aRow->get<const char *>(aIndex++));
  // the title
  actionTitle = nonNullCStr(aRow->get<const char *>(aIndex++));
  // the action this custom action refers to
  string baseAction = nonNullCStr(aRow->get<const char *>(aIndex++));
  // the params
  string jsonparams = nonNullCStr(aRow->get<const char *>(aIndex++));
  // flags
  flags = aRow->get<int>(aIndex++);
  // - look it up
  action = singleDevice.deviceActions->getAction(baseAction);
  if (action) {
    // get and validate params
    JsonObjectPtr j = JsonObject::objFromText(jsonparams.c_str());
    ApiValuePtr loadedParams = JsonApiValue::newValueFromJson(j);
    validateParams(loadedParams, storedParams, true);
  }
  else {
    // this is an invalid custom action
    SALOG(singleDevice, LOG_ERR, "invalid custom action - refers to non-existing action '%s'", baseAction.c_str())
  }
}


void CustomAction::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  // - my own id
  aStatement.bind(aIndex++, actionId.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  // - title
  aStatement.bind(aIndex++, actionTitle.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  if (action) {
    // - the device action this custom action refers to
    aStatement.bind(aIndex++, action->actionId.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
    // - the parameters
    JsonObjectPtr j = boost::dynamic_pointer_cast<JsonApiValue>(storedParams)->jsonObject();
    aStatement.bind(aIndex++, j->c_strValue(), false); // not static!
  }
  else {
    aStatement.bind(aIndex++); // no action -> NULL
    aStatement.bind(aIndex++); // no params -> NULL
  }
  aStatement.bind(aIndex++, (int)flags);
}


// MARK: ===== CustomActions container


int CustomActions::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)customActions.size();
}


static char customactions_key;

PropertyDescriptorPtr CustomActions::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<customActions.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = customActions[aPropIndex]->actionId;
    descP->propertyType = apivalue_object;
    descP->deletable = true; // custom actions are deletable
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(customactions_key);
    return descP;
  }
  return PropertyDescriptorPtr();
}


PropertyDescriptorPtr CustomActions::getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor)
{
  PropertyDescriptorPtr p = inherited::getDescriptorByName(aPropMatch, aStartIndex, aDomain, aMode, aParentDescriptor);
  if (!p && aMode==access_write && isNamedPropSpec(aPropMatch)) {
    // writing to non-existing custom action -> insert new action
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = aPropMatch;
    descP->propertyType = apivalue_object;
    descP->deletable = true; // custom actions are deletable
    descP->propertyFieldKey = customActions.size();
    descP->propertyObjectKey = OKEY(customactions_key);
    CustomActionPtr a = CustomActionPtr(new CustomAction(singleDevice));
    a->actionId = aPropMatch;
    customActions.push_back(a);
    p = descP;
  }
  return p;
}



bool CustomActions::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(customactions_key) && aMode==access_delete) {
    // only field-level access is deleting a custom action
    CustomActionPtr da = customActions[aPropertyDescriptor->fieldKey()];
    da->deleteFromStore(); // remove from store
    customActions.erase(customActions.begin()+aPropertyDescriptor->fieldKey()); // remove from container
    return true;
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}



PropertyContainerPtr CustomActions::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(customactions_key)) {
    return customActions[aPropertyDescriptor->fieldKey()];
  }
  return NULL;
}


bool CustomActions::call(const string aActionId, ApiValuePtr aParams, StatusCB aCompletedCB)
{
  for (CustomActionsVector::iterator pos = customActions.begin(); pos!=customActions.end(); ++pos) {
    if ((*pos)->actionId==aActionId) {
      // action exists
      (*pos)->call(aParams, aCompletedCB);
      return true; // done, callback will finish it
    }
  }
  // custom action does not exist
  return false; // we might want to try standard action
}



ErrorPtr CustomActions::load()
{
  ErrorPtr err;

  // custom actions are stored by dSUID
  string parentID = singleDevice.dSUID.getString();
  // create a template
  CustomActionPtr newAction = CustomActionPtr(new CustomAction(singleDevice));
  // get the query
  sqlite3pp::query *queryP = newAction->newLoadAllQuery(parentID.c_str());
  if (queryP==NULL) {
    // real error preparing query
    err = newAction->paramStore.error();
  }
  else {
    for (sqlite3pp::query::iterator row = queryP->begin(); row!=queryP->end(); ++row) {
      // got record
      // - load record fields into custom action object
      int index = 0;
      newAction->loadFromRow(row, index, NULL);
      // - put custom action into container
      customActions.push_back(newAction);
      // - fresh object for next row
      newAction = CustomActionPtr(new CustomAction(singleDevice));
    }
    delete queryP; queryP = NULL;
    // Now check for default settings from files
    loadActionsFromFiles();
  }
  return err;
}


ErrorPtr CustomActions::save()
{
  ErrorPtr err;

  // custom actions are stored by dSUID
  string parentID = singleDevice.dSUID.getString();
  // save all elements of the map (only dirty ones will be actually stored to DB
  for (CustomActionsVector::iterator pos = customActions.begin(); pos!=customActions.end(); ++pos) {
    err = (*pos)->saveToStore(parentID.c_str(), true); // multiple children of same parent allowed
    if (!Error::isOK(err)) SALOG(singleDevice, LOG_ERR,"Error saving custom action '%s': %s", (*pos)->actionId.c_str(), err->description().c_str());
  }
  return err;
}


ErrorPtr CustomActions::forget()
{
  ErrorPtr err;
  for (CustomActionsVector::iterator pos = customActions.begin(); pos!=customActions.end(); ++pos) {
    err = (*pos)->deleteFromStore();
  }
  return err;
}


bool CustomActions::isDirty()
{
  for (CustomActionsVector::iterator pos = customActions.begin(); pos!=customActions.end(); ++pos) {
    if ((*pos)->isDirty()) return true;
  }
  return false;
}


void CustomActions::markClean()
{
  for (CustomActionsVector::iterator pos = customActions.begin(); pos!=customActions.end(); ++pos) {
    (*pos)->markClean();
  }
}


void CustomActions::loadActionsFromFiles()
{
  string dir = singleDevice.getVdcHost().getPersistentDataDir();
  const int numLevels = 4;
  string levelids[numLevels];
  // Level strategy: most specialized will be active, unless lower levels specify explicit override
  // - Baselines are hardcoded defaults plus settings (already) loaded from persistent store
  // - Level 0 are actions related to the device instance (dSUID)
  // - Level 1 are actions related to the device type (deviceTypeIdentifier())
  // - Level 2 are actions related to the device class/version (deviceClass()_deviceClassVersion())
  // - Level 3 are actions related to the vDC (vdcClassIdentifier())
  levelids[0] = "vdsd_" + singleDevice.getDsUid().getString();
  levelids[1] = string(singleDevice.deviceTypeIdentifier()) + "_device";
  levelids[2] = string_format("%s_%d_class", singleDevice.deviceClass().c_str(), singleDevice.deviceClassVersion());
  levelids[3] = singleDevice.vdcP->vdcClassIdentifier();
  for(int i=0; i<numLevels; ++i) {
    // try to open config file
    string fn = dir+"actions_"+levelids[i]+".csv";
    string line;
    int lineNo = 0;
    FILE *file = fopen(fn.c_str(), "r");
    if (!file) {
      int syserr = errno;
      if (syserr!=ENOENT) {
        // file not existing is ok, all other errors must be reported
        SALOG(singleDevice, LOG_ERR, "failed opening file '%s' - %s", fn.c_str(), strerror(syserr));
      }
      // don't process, try next
      SALOG(singleDevice, LOG_DEBUG, "loadActionsFromFiles: tried '%s' - not found", fn.c_str());
    }
    else {
      // file opened
      SALOG(singleDevice, LOG_DEBUG, "loadActionsFromFiles: found '%s' - processing", fn.c_str());
      while (string_fgetline(file, line)) {
        lineNo++;
        // skip empty lines and those starting with #, allowing to format and comment CSV
        if (line.empty() || line[0]=='#') {
          // skip this line
          continue;
        }
        string f;
        const char *p = line.c_str();
        // first field is action id
        bool overridden = false;
        if (nextCSVField(p, f)) {
          const char *fp = f.c_str();
          if (!*fp) continue; // empty actionid field -> invalid line
          // check override prefix
          if (*fp=='!') {
            ++fp;
            overridden = true;
          }
          // get action id
          string actionId = fp;
          if (actionId.size()==0) {
            SALOG(singleDevice, LOG_ERR, "%s:%d - missing activity name", fn.c_str(), lineNo);
            continue; // no valid scene number -> invalid line
          }
          // check if this action already exists
          CustomActionPtr a;
          for (CustomActionsVector::iterator pos = customActions.begin(); pos!=customActions.end(); ++pos) {
            if ((*pos)->actionId==actionId) {
              // action already exists
              if (!overridden) continue; // action already exists (user setting or more specialized level) -> dont apply
              a = (*pos);
            }
          }
          if (!a) {
            a = CustomActionPtr(new CustomAction(singleDevice));
            a->actionId = actionId;
            customActions.push_back(a);
          }
          // process rest of CSV line as property name/value pairs
          a->readPropsFromCSV(VDC_API_DOMAIN, false, p, fn.c_str(), lineNo);
          // these changes are NOT to be made persistent in DB!
          a->markClean();
          // put scene into table
          SALOG(singleDevice, LOG_INFO, "Custom action '%s' %sloaded from config file %s", actionId.c_str(), overridden ? "(with override) " : "", fn.c_str());
        }
      }
      fclose(file);
    }
  }
}



// MARK: ===== DeviceStateParams

static char devicestatedesc_key;
static char devicestate_key;


PropertyDescriptorPtr DeviceStateParams::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  PropertyDescriptorPtr p = inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor);
  if (aParentDescriptor->hasObjectKey(devicestate_key)) {
    // access via deviceStates, we directly want to see the values
    static_cast<DynamicPropertyDescriptor *>(p.get())->propertyType = apivalue_null; // switch to leaf (of variable type)
  }
  return p;
}


bool DeviceStateParams::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->parentDescriptor->hasObjectKey(devicestate_key) && aMode==access_read) {
    // fieldkey is the param index, just get that param's value
    if (!values[aPropertyDescriptor->fieldKey()]->getValue(aPropValue))
      aPropValue->setNull(); // unset param will just report NULL
    return true; // param values are always reported
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}





// MARK: ===== DeviceState

DeviceState::DeviceState(SingleDevice &aSingleDevice, const string aStateId, const string aDescription, ValueDescriptorPtr aStateDescriptor, DeviceStateWillPushCB aWillPushHandler) :
  singleDeviceP(&aSingleDevice),
  stateId(aStateId),
  stateDescriptor(aStateDescriptor),
  stateDescription(aDescription),
  willPushHandler(aWillPushHandler),
  lastPush(Never)
{
}


bool DeviceState::push()
{
  DeviceEventsList emptyList;
  return pushWithEvents(emptyList);
}


bool DeviceState::pushWithEvent(DeviceEventPtr aEvent)
{
  DeviceEventsList eventList;
  eventList.push_back(aEvent);
  return pushWithEvents(eventList);
}


bool DeviceState::pushWithEvents(DeviceEventsList aEventList)
{
  SALOG((*singleDeviceP), LOG_NOTICE, "pushing: state '%s' changed to '%s'", stateId.c_str(), stateDescriptor->getStringValue().c_str());
  // update for every push attempt, as this are "events"
  lastPush = MainLoop::currentMainLoop().now();
  // collect additional events to push
  if (willPushHandler) {
    willPushHandler(DeviceStatePtr(this), aEventList);
  }
  // try to push to connected vDC API client
  VdcApiConnectionPtr api = singleDeviceP->getVdcHost().getSessionConnection();
  if (api) {
    // create query for state property to get pushed
    ApiValuePtr query = api->newApiValue();
    query->setType(apivalue_object);
    ApiValuePtr subQuery = query->newValue(apivalue_object);
    subQuery->add(stateId, subQuery->newValue(apivalue_null));
    query->add(string("deviceStates"), subQuery);
    // add events, if any
    ApiValuePtr events;
    for (DeviceEventsList::iterator pos=aEventList.begin(); pos!=aEventList.end(); ++pos) {
      if (!events) {
        events = api->newApiValue();
        events->setType(apivalue_object);
      }
      ApiValuePtr event = api->newApiValue();
      event->setType(apivalue_null); // for now, events don't have any properties, so it's just named NULL values
      events->add((*pos)->eventId, event);
      SALOG((*singleDeviceP), LOG_NOTICE, "- pushing event '%s' along with state change", (*pos)->eventId.c_str());
    }
    return singleDeviceP->pushNotification(query, events, VDC_API_DOMAIN);
  }
  // no API, cannot not push
  return false;
}




// MARK: ===== DeviceState property access

enum {
  statedescription_key,
  statetype_key,
  numStatesDescProperties
};

enum {
  state_key,
  age_key,
  changed_key,
  numStatesProperties
};


int DeviceState::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // check access path, depends on how we access the state (description or actual state)
  if (aParentDescriptor->parentDescriptor->hasObjectKey(devicestatedesc_key))
    return numStatesDescProperties;
  else if (aParentDescriptor->parentDescriptor->hasObjectKey(devicestate_key))
    return numStatesProperties;
  return 0;
}


PropertyDescriptorPtr DeviceState::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription descproperties[numStatesDescProperties] = {
    { "description", apivalue_string, statedescription_key, OKEY(devicestatedesc_key) },
    { "value", apivalue_object, statetype_key, OKEY(devicestatedesc_key) },
  };
  static const PropertyDescription properties[numStatesProperties] = {
    { "value", apivalue_null, state_key, OKEY(devicestate_key) },
    { "age", apivalue_double, age_key, OKEY(devicestate_key) },
    { "changed", apivalue_double, changed_key, OKEY(devicestate_key) },
  };
  // check access path, depends on how we access the state (description or actual state)
  if (aParentDescriptor->parentDescriptor->hasObjectKey(devicestatedesc_key)) {
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&descproperties[aPropIndex], aParentDescriptor));
  }
  else if (aParentDescriptor->parentDescriptor->hasObjectKey(devicestate_key)) {
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


PropertyContainerPtr DeviceState::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(devicestatedesc_key)) {
    if (aPropertyDescriptor->fieldKey()==statetype_key) {
      return stateDescriptor;
    }
  }
  return NULL;
}


bool DeviceState::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aMode==access_read) {
    if (aPropertyDescriptor->hasObjectKey(devicestatedesc_key)) {
      switch (aPropertyDescriptor->fieldKey()) {
        case statedescription_key: aPropValue->setStringValue(stateDescription); return true;
      }
    }
    else if (aPropertyDescriptor->hasObjectKey(devicestate_key)) {
      switch (aPropertyDescriptor->fieldKey()) {
        case state_key:
          if (!stateDescriptor) return false; // no state
          if (!stateDescriptor->getValue(aPropValue))
            aPropValue->setNull();
          return true;
        case age_key: {
          MLMicroSeconds lu = stateDescriptor->getLastUpdate();
          if (lu==Never)
            aPropValue->setNull();
          else
            aPropValue->setDoubleValue((double)(MainLoop::now()-lu)/Second);
          return true;
        }
        case changed_key: {
          MLMicroSeconds lc = stateDescriptor->getLastChange();
          if (lc==Never)
            aPropValue->setNull();
          else
            aPropValue->setDoubleValue((double)(MainLoop::now()-lc)/Second);
          return true;
        }
      }
    }
  }
  return false;
}




// MARK: ===== DeviceStates container



void DeviceStates::addToModelUIDHash(string &aHashedString)
{
  for (StatesVector::iterator pos = deviceStates.begin(); pos!=deviceStates.end(); ++pos) {
    aHashedString += ':';
    aHashedString += (*pos)->stateId;
  }
}


int DeviceStates::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)deviceStates.size();
}


static char states_key;

PropertyDescriptorPtr DeviceStates::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<deviceStates.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = deviceStates[aPropIndex]->stateId;
    descP->propertyType = apivalue_object;
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(states_key);
    return descP;
  }
  return PropertyDescriptorPtr();
}


PropertyContainerPtr DeviceStates::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(states_key)) {
    return deviceStates[aPropertyDescriptor->fieldKey()];
  }
  return NULL;
}


void DeviceStates::addState(DeviceStatePtr aState)
{
  deviceStates.push_back(aState);
}



DeviceStatePtr DeviceStates::getState(const string aStateId)
{
  for (StatesVector::iterator pos = deviceStates.begin(); pos!=deviceStates.end(); ++pos) {
    if ((*pos)->stateId==aStateId) {
      // found
      return *pos;
    }
  }
  // not found
  return DeviceStatePtr();
}



// MARK: ===== DeviceEvent


DeviceEvent::DeviceEvent(SingleDevice &aSingleDevice, const string aEventId, const string aDescription) :
  singleDeviceP(&aSingleDevice),
  eventId(aEventId),
  eventDescription(aDescription)
{
}


// MARK: ===== DeviceEvent property access

enum {
  eventdescription_key,
  numEventDescProperties
};

static char deviceeventdesc_key;


int DeviceEvent::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // check access path, depends on how we access the state (description or actual state)
  if (aParentDescriptor->parentDescriptor->hasObjectKey(deviceeventdesc_key))
    return numEventDescProperties;
  return 0;
}


PropertyDescriptorPtr DeviceEvent::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription descproperties[numEventDescProperties] = {
    { "description", apivalue_string, eventdescription_key, OKEY(deviceeventdesc_key) },
  };
  // check access path, depends on how we access the event (Note: for now we only have description, so it's not strictly needed yet here)
  if (aParentDescriptor->parentDescriptor->hasObjectKey(deviceeventdesc_key)) {
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&descproperties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


PropertyContainerPtr DeviceEvent::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(deviceeventdesc_key)) {
    // TODO: here we would add descriptions of event params once we introduce them
//    if (aPropertyDescriptor->fieldKey()==eventparams_key) {
//      return eventDescriptors;
//    }
  }
  return NULL;
}


bool DeviceEvent::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aMode==access_read) {
    if (aPropertyDescriptor->hasObjectKey(deviceeventdesc_key)) {
      switch (aPropertyDescriptor->fieldKey()) {
        case eventdescription_key: aPropValue->setStringValue(eventDescription); return true;
      }
    }
  }
  return false;
}




// MARK: ===== DeviceEvents container


void DeviceEvents::addToModelUIDHash(string &aHashedString)
{
  for (EventsVector::iterator pos = deviceEvents.begin(); pos!=deviceEvents.end(); ++pos) {
    aHashedString += ':';
    aHashedString += (*pos)->eventId;
  }
}


int DeviceEvents::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)deviceEvents.size();
}


static char events_key;

PropertyDescriptorPtr DeviceEvents::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<deviceEvents.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = deviceEvents[aPropIndex]->eventId;
    descP->propertyType = apivalue_object;
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(events_key);
    return descP;
  }
  return PropertyDescriptorPtr();
}


PropertyContainerPtr DeviceEvents::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(events_key)) {
    return deviceEvents[aPropertyDescriptor->fieldKey()];
  }
  return NULL;
}


void DeviceEvents::addEvent(DeviceEventPtr aEvent)
{
  deviceEvents.push_back(aEvent);
}



DeviceEventPtr DeviceEvents::getEvent(const string aEventId)
{
  for (EventsVector::iterator pos = deviceEvents.begin(); pos!=deviceEvents.end(); ++pos) {
    if ((*pos)->eventId==aEventId) {
      // found
      return *pos;
    }
  }
  // not found
  return DeviceEventPtr();
}


bool DeviceEvents::pushEvent(const string aEventId)
{
  DeviceEventPtr ev = getEvent(aEventId);
  if (ev) {
    // push it
    return pushEvent(ev);
  }
  return false; // no event, nothing pushed
}


bool DeviceEvents::pushEvent(DeviceEventPtr aEvent)
{
  DeviceEventsList events;
  events.push_back(aEvent);
  return pushEvents(events);
}


bool DeviceEvents::pushEvents(DeviceEventsList aEventList)
{
  // try to push to connected vDC API client
  VdcApiConnectionPtr api = singleDeviceP->getVdcHost().getSessionConnection();
  if (api && !aEventList.empty()) {
    // add events
    SALOG((*singleDeviceP), LOG_NOTICE, "pushing: independent event(s):");
    ApiValuePtr events;
    for (DeviceEventsList::iterator pos=aEventList.begin(); pos!=aEventList.end(); ++pos) {
      if (!events) {
        events = api->newApiValue();
        events->setType(apivalue_object);
      }
      ApiValuePtr event = api->newApiValue();
      event->setType(apivalue_null); // for now, events don't have any properties, so it's just named NULL values
      events->add((*pos)->eventId, event);
      SALOG((*singleDeviceP), LOG_NOTICE, "- event '%s'", (*pos)->eventId.c_str());
    }
    return singleDeviceP->pushNotification(ApiValuePtr(), events, VDC_API_DOMAIN);
  }
  return false; // no events to push
}




// MARK: ===== DeviceProperties container


void DeviceProperties::addProperty(ValueDescriptorPtr aPropertyDesc, bool aReadOnly)
{
  aPropertyDesc->setReadOnly(aReadOnly);
  values.push_back(aPropertyDesc);
}


void DeviceProperties::addToModelUIDHash(string &aHashedString)
{
  for (ValuesVector::iterator pos = values.begin(); pos!=values.end(); ++pos) {
    aHashedString += ':';
    aHashedString += (*pos)->getName();
  }
}



ValueDescriptorPtr DeviceProperties::getProperty(const string aPropertyId)
{
  return getValue(aPropertyId);
}


bool DeviceProperties::pushProperty(ValueDescriptorPtr aPropertyDesc)
{
  // try to push to connected vDC API client
  VdcApiConnectionPtr api = singleDeviceP->getVdc().getSessionConnection();
  if (api) {
    // create query for device property to get pushed
    ApiValuePtr query = api->newApiValue();
    query->setType(apivalue_object);
    ApiValuePtr subQuery = query->newValue(apivalue_object);
    subQuery->add(aPropertyDesc->getName(), subQuery->newValue(apivalue_null));
    query->add(string("deviceProperties"), subQuery);
    return singleDeviceP->pushNotification(query, ApiValuePtr(), VDC_API_DOMAIN);
  }
  return false; // cannot push
}


// MARK: ===== DeviceProperties property access


static char devicepropertydesc_key;
static char deviceproperty_key;


PropertyDescriptorPtr DeviceProperties::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  PropertyDescriptorPtr p = inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor);
  if (aParentDescriptor->hasObjectKey(deviceproperty_key)) {
    // access via deviceProperties, we directly want to see the values
    static_cast<DynamicPropertyDescriptor *>(p.get())->propertyType = apivalue_null; // switch to leaf (of variable type)
  }
  return p;
}


bool DeviceProperties::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->parentDescriptor->hasObjectKey(deviceproperty_key)) {
    ValueDescriptorPtr val = values[aPropertyDescriptor->fieldKey()];
    if (aMode==access_read) {
      // read: fieldkey is the property index, just get that property's value
      if (!val->getValue(aPropValue))
        aPropValue->setNull(); // unset param will just report NULL
      return true; // property values are always reported
    }
    else if (!val->isReadOnly()) {
      // write: fieldkey is the property index, write that property's value
      ErrorPtr err = val->conforms(aPropValue, true);
      if (!Error::isOK(err)) {
        SALOG((*singleDeviceP), LOG_ERR, "Cannot set property '%s': %s", val->getName().c_str(), err->description().c_str());
        return false;
      }
      else {
        // write property now
        if (val->setValue(aPropValue)) {
          // value has changed -> trigger change handler
          if (propertyChangeHandler) propertyChangeHandler(val);
        }
        return true;
      }
    }
    return false;
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}



// MARK: ===== SingleDevice


SingleDevice::SingleDevice(Vdc *aVdcP) :
  inherited(aVdcP)
{
  // create actions
  deviceActions = DeviceActionsPtr(new DeviceActions);
  // create custom actions
  customActions = CustomActionsPtr(new CustomActions(*this));
  // create states
  deviceStates = DeviceStatesPtr(new DeviceStates);
  // create events
  deviceEvents = DeviceEventsPtr(new DeviceEvents(*this));
  // create device properties
  deviceProperties = DevicePropertiesPtr(new DeviceProperties(*this));
}


SingleDevice::~SingleDevice()
{
}


void SingleDevice::addToModelUIDHash(string &aHashedString)
{
  // action names
  inherited::addToModelUIDHash(aHashedString);
  deviceActions->addToModelUIDHash(aHashedString);
  deviceStates->addToModelUIDHash(aHashedString);
  deviceProperties->addToModelUIDHash(aHashedString);
}





// MARK: ===== SingleDevice persistence

ErrorPtr SingleDevice::load()
{
  // load the custom actions first (so saved ones will be there when loadFromFiles occurs at inherited::load()
  ErrorPtr err = customActions->load();
  if (Error::isOK(err)) {
    err = inherited::load();
  }
  return err;
}


ErrorPtr SingleDevice::save()
{
  ErrorPtr err = inherited::save();
  if (Error::isOK(err)) {
    err = customActions->save();
  }
  return err;
}


ErrorPtr SingleDevice::forget()
{
  inherited::forget();
  return customActions->forget();
}


bool SingleDevice::isDirty()
{
  return inherited::isDirty() || customActions->isDirty();
}


void SingleDevice::markClean()
{
  inherited::markClean();
  customActions->markClean();
}


void SingleDevice::loadSettingsFromFiles()
{
  inherited::loadSettingsFromFiles();
  customActions->loadActionsFromFiles();
}



// MARK: ===== SingleDevice API calls

void SingleDevice::call(const string aActionId, ApiValuePtr aParams, StatusCB aCompletedCB)
{
  if (!customActions->call(aActionId, aParams, aCompletedCB)) {
    if (!deviceActions->call(aActionId, aParams, aCompletedCB)) {
      // action does not exist, call back with error
      if (aCompletedCB) {
        aCompletedCB(Error::err<VdcApiError>(501, "action '%s' does not exist", aActionId.c_str()));
      }
    }
  }
}


ErrorPtr SingleDevice::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="invokeDeviceAction") {
    string actionid;
    respErr = checkStringParam(aParams, "id", actionid);
    if (!Error::isOK(respErr))
      return respErr;
    ApiValuePtr actionParams = aParams->get("params");
    if (!actionParams) {
      // always pass params, even if empty
      actionParams = aParams->newObject();
    }
    // now call the action
    ALOG(LOG_NOTICE, "invokeDeviceAction: %s:%s", actionid.c_str(), actionParams->description().c_str());
    call(actionid, actionParams, boost::bind(&SingleDevice::invokeDeviceActionComplete, this, aRequest, _1));
    // callback will create the response when done
    return ErrorPtr(); // do not return anything now
  }
  return inherited::handleMethod(aRequest, aMethod, aParams);
}


void SingleDevice::invokeDeviceActionComplete(VdcApiRequestPtr aRequest, ErrorPtr aError)
{
  ALOG(LOG_NOTICE, "- call completed with status %s", Error::isOK(aError) ? "OK" : aError->description().c_str());
  aRequest->sendStatus(aError);
}


// MARK: ===== Singledevice scene command handling

bool SingleDevice::prepareSceneCall(DsScenePtr aScene)
{
  SimpleCmdScenePtr cs = boost::dynamic_pointer_cast<SimpleCmdScene>(aScene);
  bool continueApply = true;
  if (cs) {
    // execute custom scene commands
    if (!cs->command.empty()) {
      // special case: singledevice also directly executes non-prefixed commands,
      string cmd, cmdargs;
      bool isDeviceAction = false;
      if (keyAndValue(cs->command, cmd, cmdargs, ':')) {
        if (cmd==SCENECMD_DEVICE_ACTION) {
          // prefixed
          isDeviceAction = true;
        }
        else {
          // has a xxxx: prefix, but not "deviceaction"
          if (cmd.find_first_of(".-_")!=string::npos) {
            // prefix contains things that can't be a prefix -> assume entire string is a deviceAction
            // Note: dS internally used actions are prefixed with "std." or "cust.", so these always work as direct deviceActions
            isDeviceAction = true;
            cmdargs = cs->command; // entire string as device action
          }
        }
      }
      else {
        // no prefix at all -> default to deviceaction anyway
        isDeviceAction = true;
        cmdargs = cs->command; // entire string as device action
      }
      if (isDeviceAction) {
        // Syntax: actionid[:<JSON object with params>]
        ApiValuePtr actionParams;
        string actionid;
        string jsonparams;
        JsonObjectPtr j;
        if (keyAndValue(cmdargs, actionid, jsonparams)) {
          // substitute placeholders that might be in the JSON
          cs->substitutePlaceholders(jsonparams);
          // make Json object of it
          j = JsonObject::objFromText(jsonparams.c_str());
        }
        else {
          // no params, just actions
          actionid = cmdargs;
        }
        if (!j) {
          // make an API value out of it
          j = JsonObject::newObj();
        }
        actionParams = JsonApiValue::newValueFromJson(j);
        ALOG(LOG_NOTICE, "invoking action via scene %d command: %s:%s", aScene->sceneNo, actionid.c_str(), actionParams->description().c_str());
        call(actionid, actionParams, boost::bind(&SingleDevice::sceneInvokedActionComplete, this, _1));
        return false; // do not continue applying
      }
    }
  }
  // prepared ok
  return continueApply;
}


void SingleDevice::sceneInvokedActionComplete(ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    ALOG(LOG_INFO, "scene invoked command complete");
  }
  else {
    ALOG(LOG_ERR, "scene invoked command returned error: %s", aError->description().c_str());
  }
}





// MARK: ===== SingleDevice property access


enum {
  // singledevice level properties
  deviceActionDescriptions_key,
  customActions_key,
  deviceStateDescriptions_key,
  deviceStates_key,
  deviceEventDescriptions_key,
  devicePropertyDescriptions_key,
  deviceProperties_key,
  numSimpleDeviceProperties
};

static char singledevice_key;


int SingleDevice::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->isRootOfObject()) {
    // Accessing properties at the Device (root) level
    return inherited::numProps(aDomain, aParentDescriptor)+numSimpleDeviceProperties;
  }
  return inherited::numProps(aDomain, aParentDescriptor); // only the inherited ones
}


PropertyDescriptorPtr SingleDevice::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // device level properties
  static const PropertyDescription properties[numSimpleDeviceProperties] = {
    // common device properties
    { "deviceActionDescriptions", apivalue_object, deviceActionDescriptions_key, OKEY(singledevice_key) },
    { "customActions", apivalue_object, customActions_key, OKEY(singledevice_key) },
    { "deviceStateDescriptions", apivalue_object, deviceStateDescriptions_key, OKEY(devicestatedesc_key) },
    { "deviceStates", apivalue_object, deviceStates_key, OKEY(devicestate_key) },
    { "deviceEventDescriptions", apivalue_object, deviceEventDescriptions_key, OKEY(deviceeventdesc_key) },
    { "devicePropertyDescriptions", apivalue_object, devicePropertyDescriptions_key, OKEY(devicepropertydesc_key) },
    { "deviceProperties", apivalue_object, deviceProperties_key, OKEY(deviceproperty_key) }
  };
  // C++ object manages different levels, check aParentDescriptor
  if (aParentDescriptor->isRootOfObject()) {
    // root level - accessing properties on the Device level
    int n = inherited::numProps(aDomain, aParentDescriptor);
    if (aPropIndex<n)
      return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
    aPropIndex -= n; // rebase to 0 for my own first property
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor);
}


PropertyContainerPtr SingleDevice::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->parentDescriptor->isRootOfObject()) {
    switch (aPropertyDescriptor->fieldKey()) {
      case deviceActionDescriptions_key:
        return deviceActions;
      case customActions_key:
        return customActions;
      case deviceStateDescriptions_key:
      case deviceStates_key:
        return deviceStates;
      case deviceEventDescriptions_key:
        return deviceEvents;
      case devicePropertyDescriptions_key:
      case deviceProperties_key:
        return deviceProperties;
    }
  }
  // unknown here
  return inherited::getContainer(aPropertyDescriptor, aDomain);
}



// MARK: ======= misc utils

ErrorPtr p44::substitutePlaceholders(string &aString, ValueLookupCB aValueLookupCB)
{
  ErrorPtr err;
  size_t p = 0;
  // Syntax of placeholders:
  //   @{var[*ff][+|-oo][%frac]}
  //   ff is an optional float factor to scale the channel value
  //   oo is an float offset to apply
  //   frac are number of fractional digits to use in output
  while ((p = aString.find("@{",p))!=string::npos) {
    size_t e = aString.find("}",p+2);
    if (e==string::npos) {
      // syntactically incorrect, no closing "}"
      err = TextError::err("unterminated placeholder: %s", aString.c_str()+p);
      break;
    }
    string v = aString.substr(p+2,e-2-p);
    // process operations
    double chfactor = 1;
    double choffset = 0;
    int numFracDigits = 0;
    bool calc = false;
    size_t varend = string::npos;
    size_t i = 0;
    while (true) {
      i = v.find_first_of("*+-%",i);
      if (varend==string::npos) {
        varend = i==string::npos ? v.size() : i;
      }
      if (i==string::npos) break; // no more factors, offsets or format specs
      // factor and/or offset
      calc = true;
      double dd;
      if (sscanf(v.c_str()+i+1, "%lf", &dd)==1) {
        switch (v[i]) {
          case '*' : chfactor *= dd; break;
          case '+' : choffset += dd; break;
          case '-' : choffset -= dd; break;
          case '%' : numFracDigits = dd; break;
        }
      }
      i++;
    }
    // process variable
    string rep = v.substr(0, varend);
    if (aValueLookupCB) {
      aValueLookupCB(rep);
    }
    // apply calculations if any
    if (calc) {
      // parse as double
      double dv;
      if (sscanf(rep.c_str(), "%lf", &dv)==1) {
        // got double value, apply calculations
        dv = dv * chfactor + choffset;
        // render back to string
        rep = string_format("%.*lf", numFracDigits, dv);
      }
    }
    // replace, even if rep is empty
    aString.replace(p, e-p+1, rep);
    p+=rep.size();
  }
  return err;
}

