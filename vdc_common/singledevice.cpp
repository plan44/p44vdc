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


#include "singledevice.hpp"

using namespace p44;


// MARK: ===== ValueDescriptor

enum {
  type_key,
  min_key,
  max_key,
  resolution_key,
  default_key,
  enumvalues_key,
  numParamProperties
};

static char param_key;
static char param_enumvalues_key;


int ValueDescriptor::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return numParamProperties;
}


PropertyDescriptorPtr ValueDescriptor::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // device level properties
  static const PropertyDescription properties[numParamProperties] = {
    { "type", apivalue_uint64, type_key, OKEY(param_key) },
    { "min", apivalue_double, min_key, OKEY(param_key) },
    { "max", apivalue_double, max_key, OKEY(param_key) },
    { "resolution", apivalue_double, max_key, OKEY(param_key) },
    { "default", apivalue_null, max_key, OKEY(param_key) },
    { "values", apivalue_object+propflag_container, enumvalues_key, OKEY(param_enumvalues_key) }
  };
  if (!aParentDescriptor) {
    // root level property of this object hierarchy
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


bool ValueDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(param_key) && aMode==access_read) {
    switch (aPropertyDescriptor->fieldKey()) {
      case type_key: aPropValue->setUint16Value(valueType); return true;
    }
  }
  return false;
}



// MARK: ===== NumericValueDescriptor

bool NumericValueDescriptor::conforms(ApiValuePtr aApiValue)
{
  if (!aApiValue && hasValue) return true; // no value provided, but there is a default value -> conforms
  // check if value conforms
  ApiValueType vt = aApiValue->getType();
  if (valueType==valueType_bool) {
    // bool parameter, valuetype should be int or bool
    return
      vt==apivalue_bool ||
      vt==apivalue_int64 ||
      vt==apivalue_uint64;
  }
  else if (valueType<valueType_firstNonNumeric) {
    // check bounds
    double v = aApiValue->doubleValue();
    return v>=min && v<=max;
  }
  // everything else is not valid for numeric parameter
  return false;
}


double NumericValueDescriptor::doubleValue(ApiValuePtr aApiValue)
{
  if (!aApiValue) return value;
  return aApiValue->doubleValue();
}


int NumericValueDescriptor::intValue(ApiValuePtr aApiValue)
{
  if (!aApiValue) return value;
  if (valueType==valueType_bool)
    return aApiValue->boolValue(); // needed to catch real bools
  // all other numerics 
  return aApiValue->doubleValue(); // most generic, will catch ints and uints as well
}


bool NumericValueDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aMode==access_read) {
    // everything is read only
    if (aPropertyDescriptor->hasObjectKey(param_key)) {
      switch (aPropertyDescriptor->fieldKey()) {
        case min_key: aPropValue->setDoubleValue(min); return true;
        case max_key: aPropValue->setDoubleValue(max); return true;
        case resolution_key: aPropValue->setDoubleValue(resolution); return true;
        case default_key: {
          if (!hasValue) return false; // no (default) value, don't show it
          if (valueType<valueType_firstIntNum) {
            aPropValue->setType(apivalue_double);
            aPropValue->setDoubleValue(value);
          }
          else if (valueType==valueType_bool) {
            aPropValue->setType(apivalue_bool);
            aPropValue->setBoolValue(value);
          }
          else {
            aPropValue->setType(apivalue_int64);
            aPropValue->setInt64Value(value);
          }
        }
      }
    }
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: ===== TextValueDescriptor

bool TextValueDescriptor::conforms(ApiValuePtr aApiValue)
{
  if (!aApiValue && hasValue)
    return true; // no value provided, but there is a default value -> conforms
  // check if value conforms
  return aApiValue->getType()==apivalue_string;
}


string TextValueDescriptor::stringValue(ApiValuePtr aApiValue)
{
  if (!aApiValue) return value;
  return aApiValue->stringValue();
}


bool TextValueDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(param_key) && aMode==access_read) {
    switch (aPropertyDescriptor->fieldKey()) {
      case default_key: {
        if (!hasValue) return false; // no (default) value, don't show it
        aPropValue->setType(apivalue_string);
        aPropValue->setStringValue(value);
        return true;
      }
    }
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: ===== EnumValueDescriptor


void EnumValueDescriptor::addEnum(const char *aEnumText, int aEnumValue, bool aIsDefault)
{
  enumDescs.push_back(EnumDesc(aEnumText, aEnumValue));
  if (aIsDefault) value = aEnumValue; // also assign as default
}


bool EnumValueDescriptor::conforms(ApiValuePtr aApiValue)
{
  if (!aApiValue && hasValue) return true; // no value provided, but there is a default value -> conforms
  // check if value conforms
  if (aApiValue->getType()!=apivalue_string) return false; // must be string
  // must be one of the texts in the enum list
  string s = aApiValue->stringValue();
  for (EnumVector::iterator pos = enumDescs.begin(); pos!=enumDescs.end(); ++pos) {
    if (pos->first==s) return true;
  }
  return false;
}


int EnumValueDescriptor::intValue(ApiValuePtr aApiValue)
{
  if (!aApiValue) return value;
  // look up enum value
  string s = aApiValue->stringValue();
  for (EnumVector::iterator pos = enumDescs.begin(); pos!=enumDescs.end(); ++pos) {
    if (pos->first==s) return pos->second;
  }
  return 0;
}


bool EnumValueDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aMode==access_read) {
    // everything is read only
    if (aPropertyDescriptor->hasObjectKey(param_key)) {
      switch (aPropertyDescriptor->fieldKey()) {
        case default_key: {
          if (!hasValue) return false; // no (default) value, don't show it
          aPropValue->setType(apivalue_uint64);
          aPropValue->setUint32Value(value);
          return true;
        }
      }
    }
    else if (aPropertyDescriptor->hasObjectKey(param_enumvalues_key)) {
      // all enum list properties are NULL values...
      return true; // ...but they exist!
    }
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


PropertyContainerPtr EnumValueDescriptor::getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->isArrayContainer() && aPropertyDescriptor->hasObjectKey(param_enumvalues_key)) {
    return PropertyContainerPtr(this); // handle enum values array myself
  }
  // unknown here
  return inherited::getContainer(aPropertyDescriptor, aDomain);
}


int EnumValueDescriptor::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (HAS_OKEY(aParentDescriptor, param_enumvalues_key)) {
    // number of enum values
    return (int)enumDescs.size();
  }
  return inherited::numProps(aDomain, aParentDescriptor);
}


PropertyDescriptorPtr EnumValueDescriptor::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (HAS_OKEY(aParentDescriptor, param_enumvalues_key)) {
    // enumvalues - distinct set of NULL values (only names count)
    if (aPropIndex<enumDescs.size()) {
      DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
      descP->propertyName = enumDescs[aPropIndex].first;
      descP->propertyType = apivalue_null;
      descP->propertyFieldKey = aPropIndex;
      descP->propertyObjectKey = OKEY(param_enumvalues_key);
      return descP;
    }
  }
  return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor);
}


// MARK: ===== ValueList

void ValueList::addValueDescriptor(const string aValueName, ValueDescriptorPtr aValueDesc)
{
  values.push_back(ValueEntry(aValueName, aValueDesc));
}


int ValueList::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)values.size();
}


static char values_key;

PropertyDescriptorPtr ValueList::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // enumvalues - distinct set of NULL values (only names count)
  if (aPropIndex<values.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = values[aPropIndex].first;
    descP->propertyType = apivalue_object;
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(values_key);
    return descP;
  }
  return PropertyDescriptorPtr();
}


PropertyContainerPtr ValueList::getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(values_key)) {
    return values[aPropertyDescriptor->fieldKey()].second;
  }
  return NULL;
}



// MARK: ===== SingleDevice

SingleDevice::SingleDevice(Vdc *aVdcP) :
  inherited(aVdcP)
{
  // TODO: create actions & states
  // create device properties
  // FIXME: %%% test only, later this will be a more specific object
  deviceProperties = ValueListPtr(new ValueList);
  deviceProperties->addValueDescriptor("testDbl", ValueDescriptorPtr(new NumericValueDescriptor(valueType_temperature, -10, 40, 0.5, true, 21)));
  deviceProperties->addValueDescriptor("testText", ValueDescriptorPtr(new TextValueDescriptor(false)));
  EnumValueDescriptor *ev = new EnumValueDescriptor(true);
  ev->addEnum("piff", 1);
  ev->addEnum("paff", 2);
  ev->addEnum("puff", 42, true);
  deviceProperties->addValueDescriptor("testEnum", ev);
}


SingleDevice::~SingleDevice()
{
}


// MARK: ===== SingleDevice persistence

ErrorPtr SingleDevice::load()
{
  // TODO: add loading custom actions
  return inherited::load();
}


ErrorPtr SingleDevice::save()
{
  // TODO: add saving custom actions
  return inherited::save();
}


ErrorPtr SingleDevice::forget()
{
  // TODO: implement
  return inherited::forget();
}


bool SingleDevice::isDirty()
{
  // TODO: check custom actions
  return inherited::isDirty();
}


void SingleDevice::markClean()
{
  // TODO: check custom actions
  inherited::markClean();
}


void SingleDevice::loadSettingsFromFiles()
{
  // TODO: load predefined custom actions
  inherited::loadSettingsFromFiles();
}

// MARK: ===== SingleDevice API calls

ErrorPtr SingleDevice::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="callDeviceAction") {
    string action;
    respErr = checkStringParam(aParams, "name", action);
    if (Error::isOK(respErr)) {
      ApiValuePtr actionParams = aParams->get("params");
      if (!actionParams) {
        // always pass params, even if empty
        actionParams = aParams->newObject();
      }
      // now call the action
      respErr = callDeviceAction(action, actionParams);
    }
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}



ErrorPtr SingleDevice::callDeviceAction(const string aActionName, ApiValuePtr aActionParams)
{
  ErrorPtr respErr;
  // TODO: implement it!
  respErr =  ErrorPtr(new VdcApiError(404, "unknown device action"));
  return respErr;
}



// MARK: ===== SingleDevice property access


enum {
  // singledevice level properties
  devicePropertyDescriptions_key,
  numSimpleDeviceProperties
};

static char singledevice_key;


int SingleDevice::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (!aParentDescriptor) {
    // Accessing properties at the Device (root) level
    return inherited::numProps(aDomain, aParentDescriptor)+numSimpleDeviceProperties;
  }
  return 0; // none
}


PropertyDescriptorPtr SingleDevice::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // device level properties
  static const PropertyDescription properties[numSimpleDeviceProperties] = {
    // common device properties
    { "devicePropertyDescriptions", apivalue_object, devicePropertyDescriptions_key, OKEY(singledevice_key) }
  };
  // C++ object manages different levels, check aParentDescriptor
  if (!aParentDescriptor) {
    // root level - accessing properties on the Device level
    int n = inherited::numProps(aDomain, aParentDescriptor);
    if (aPropIndex<n)
      return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
    aPropIndex -= n; // rebase to 0 for my own first property
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


PropertyContainerPtr SingleDevice::getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  // containers are elements from the behaviour arrays
  if (aPropertyDescriptor->hasObjectKey(singledevice_key)) {
    if (aPropertyDescriptor->fieldKey()==devicePropertyDescriptions_key)
      return deviceProperties;
  }
  // unknown here
  return inherited::getContainer(aPropertyDescriptor, aDomain);
}
