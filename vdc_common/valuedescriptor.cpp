//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL IFNOTREDUCEDFOOTPRINT(7)

#include <math.h>

#include "valuedescriptor.hpp"
#include "vdchost.hpp"

using namespace p44;


// MARK: - ValueDescriptor


ValueDescriptor::ValueDescriptor(const string aName, VdcValueType aValueType, ValueUnit aValueUnit, bool aHasDefault) :
  valueName(aName),
  valueType(aValueType),
  valueUnit(aValueUnit),
  hasValue(aHasDefault),
  isOptionalValue(!aHasDefault), // note that this is only the most common case, but setIsOptional make null values acceptable even when there is a default value
  readOnly(false),
  needsFetch(false),
  isDefaultValue(aHasDefault), // note that this is only most common case, but setIsDefault can be used to make even a null value default
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
  isDefaultValue = false;
  return gotValue;
}



bool ValueDescriptor::setChanged(bool aChanged)
{
  // update lastChange even if not technically changed, but only updated the first time apart from having a default value
  if (aChanged || lastChange==Never) {
    lastChange = MainLoop::currentMainLoop().now();
  }
  return aChanged;
}



bool ValueDescriptor::needsConformanceCheck(ApiValuePtr aApiValue, ErrorPtr &aError)
{
  if (!aApiValue) return false; // no value always conforms
  if (aApiValue->isNull()) {
    // check NULL value here, same for all types
    if (!isOptionalValue) {
      aError = Error::err<VdcApiError>(415, "Non-optional value, null not allowed");
      return false; // Error -> no type specific check needed any more
    }
    else {
      return false; // null value is ok -> no type specific check needed any more
    }
  }
  // not null, allow type specific check
  return true;
}



bool ValueDescriptor::invalidate()
{
  bool hadValue = hasValue;
  hasValue = false;
  return hadValue;
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
  if (valueType == valueType_boolean) {
    return v->boolValue() ? 1 : 0;
  }
  return v->int32Value();
}


bool ValueDescriptor::getBoolValue(bool aAsInternal, bool aPrevious)
{
  ApiValuePtr v = VdcHost::sharedVdcHost()->newApiValue();
  getValue(v, aAsInternal, aPrevious);
  return v->boolValue();
}



bool ValueDescriptor::setValue(ApiValuePtr aValue)
{
  if (!aValue || aValue->isNull()) {
    // setting NULL means invalidating
    return invalidate();
  }
  else if (valueType==valueType_numeric) {
    // numeric float type, set as double
    return setDoubleValue(aValue->doubleValue());
  }
  else if (valueType<valueType_integer || valueType==valueType_enumeration) {
    // numeric integer type or text enumeration (internally integer), set as integer
    return setInt32Value(aValue->int32Value());
  }
  else if (valueType == valueType_boolean) {
    // boolean type, implicitly converted to int
    return setInt32Value(aValue->boolValue() ? 1 : 0);
  }
  else {
    return setStringValue(aValue->stringValue());
  }
}




enum {
  type_key,
  unit_key,
  symbol_key,
  min_key,
  max_key,
  resolution_key,
  default_key,
  readonly_key,
  optional_key,
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
  static const PropertyDescription properties[numValueProperties] = {
    { "type", apivalue_string, type_key, OKEY(value_key) },
    { "siunit", apivalue_string, unit_key, OKEY(value_key) },
    { "symbol", apivalue_string, symbol_key, OKEY(value_key) },
    { "min", apivalue_double, min_key, OKEY(value_key) },
    { "max", apivalue_double, max_key, OKEY(value_key) },
    { "resolution", apivalue_double, resolution_key, OKEY(value_key) },
    { "default", apivalue_null, default_key, OKEY(value_key) },
    { "readonly", apivalue_bool, readonly_key, OKEY(value_key) },
    { "optional", apivalue_bool, optional_key, OKEY(value_key) },
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
      case type_key: aPropValue->setStringValue(valueTypeName(valueType)); return true;
      case unit_key: if (valueUnit!=valueUnit_none) { aPropValue->setStringValue(valueUnitName(valueUnit, false)); return true; } else return false;
      case symbol_key: if (valueUnit!=valueUnit_none) { aPropValue->setStringValue(valueUnitName(valueUnit, true)); return true; } else return false;
      case readonly_key: if (readOnly) { aPropValue->setBoolValue(readOnly); return true; } else return false; // show only when set (only for deviceProperties)
      case optional_key: if (!readOnly && isOptionalValue) { aPropValue->setBoolValue(isOptionalValue); return true; } else return false; // show only when writable AND optional
      case default_key: return (isDefaultValue ?  getValue(aPropValue, false) : false);
    }
  }
  return false;
}


const char *valueTypeNames[numValueTypes] = {
  "unknown",
  "numeric",
  "integer",
  "boolean",
  "enumeration",
  "string",
};


string ValueDescriptor::valueTypeName(VdcValueType aValueType)
{
  if (aValueType>=numValueTypes) aValueType = valueType_unknown;
  return valueTypeNames[aValueType];
}


VdcValueType ValueDescriptor::stringToValueType(const string aValueTypeName)
{
  for (int i=0; i<numValueTypes; i++) {
    if (aValueTypeName == valueTypeNames[i]) {
      return (VdcValueType)i;
    }
  }
  return valueType_unknown;
}



// MARK: - NumericValueDescriptor


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


bool NumericValueDescriptor::setBoolValue(bool aValue)
{
  return setDoubleValue(aValue ? 1 : 0);
}



ErrorPtr NumericValueDescriptor::conforms(ApiValuePtr aApiValue, bool aMakeInternal)
{
  ErrorPtr err;
  // check if value conforms
  if (needsConformanceCheck(aApiValue, err)) {
    ApiValueType vt = aApiValue->getType();
    if (valueType==valueType_boolean) {
      // bool parameter, valuetype should be int or bool
      if (
        vt!=apivalue_bool &&
        vt!=apivalue_int64 &&
        vt!=apivalue_uint64
      ) {
        err = Error::err<VdcApiError>(415, "invalid boolean");
      }
    }
    else if (valueType==valueType_numeric || valueType==valueType_integer) {
      // check bounds
      double v = aApiValue->doubleValue();
      if (v<min || v>max) {
        err = Error::err<VdcApiError>(415, "number out of range");
      }
    }
    else {
      // everything else is not valid for numeric parameter
      err = Error::err<VdcApiError>(415, "invalid number");
    }
  }
  return err;
}


bool NumericValueDescriptor::getValue(ApiValuePtr aApiValue, bool aAsInternal, bool aPrevious)
{
  if (!hasValue || !aApiValue) return false;
  double v = aPrevious ? previousValue : value;
  if (valueType==valueType_boolean) {
    aApiValue->setType(apivalue_bool);
    aApiValue->setBoolValue(v);
  }
  else if (valueType==valueType_integer) {
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
          if (valueType==valueType_boolean) return false;
          aPropValue->setDoubleValue(min); return true;
        case max_key:
          if (valueType==valueType_boolean) return false;
          aPropValue->setDoubleValue(max); return true;
        case resolution_key:
          if (valueType==valueType_boolean) return false;
          aPropValue->setDoubleValue(resolution); return true;
      }
    }
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: - TextValueDescriptor

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
  if (needsConformanceCheck(aApiValue, err)) {
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


// MARK: - EnumList helper class

EnumList::EnumList(bool aWithValuesInDescription):
  valuesInDescription(aWithValuesInDescription)
{
}


void EnumList::addMapping(const char *aEnumText, EnumValue aEnumValue)
{
  enumDescs.push_back(EnumDesc(aEnumText, aEnumValue));
}


void EnumList::addEnumTexts(std::vector<const char*> aTexts)
{
  int i = 0;
  for(std::vector<const char*>::iterator it = aTexts.begin(); it!=aTexts.end(); it++) {
    addMapping(*it, i++);
  }
}


const char* EnumList::textForValue(EnumValue aValue, const char *aDefaultText) const
{
  for (EnumVector::const_iterator pos = enumDescs.begin(); pos!=enumDescs.end(); ++pos) {
    if (pos->second==aValue) {
      return pos->first.c_str();
    }
  }
  return aDefaultText;
}


EnumList::EnumValue EnumList::valueForText(const char *aText, bool aCaseSensitive, EnumList::EnumValue aDefaultValue) const
{
  string txt = aCaseSensitive ? aText : lowerCase(aText);
  for (EnumVector::const_iterator pos = enumDescs.begin(); pos!=enumDescs.end(); ++pos) {
    if (
      (aCaseSensitive && pos->first==txt) ||
      (!aCaseSensitive && lowerCase(pos->first) == txt)
    ) {
      // found
      return pos->second;
    }
  }
  return aDefaultValue;
}


EnumList::EnumValue EnumList::valueForText(const string &aText, bool aCaseSensitive, EnumList::EnumValue aDefaultValue) const
{
  return valueForText(aText.c_str(), aCaseSensitive, aDefaultValue);
}


int EnumList::numProps()
{
  return (int)enumDescs.size();
}

PropertyDescriptorPtr EnumList::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  DynamicPropertyDescriptor *descP = NULL;
  if (aPropIndex<numProps()) {
    descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = enumDescs[aPropIndex].first;
    descP->propertyType = valuesInDescription ? apivalue_uint64 : apivalue_null;
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = INSTANCE_OKEY(this); 
  }
  return descP;
}


bool EnumList::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aMode==access_read) {
    // everything is read only
    if (valuesInDescription) {
      // show numeric value
      aPropValue->setUint32Value(enumDescs[aPropertyDescriptor->fieldKey()].second);
      return true;
    }
    else {
      // just null
      return true; // but exists!
    }
  }
  return false;
}


// MARK: - EnumValueDescriptor


EnumValueDescriptor::EnumValueDescriptor(const string aName, bool aNoInternalValue) :
  inherited(aName, valueType_enumeration, valueUnit_none, false),
  noInternalValue(aNoInternalValue)
{
  enumList = EnumListPtr(new EnumList(false));
}


bool EnumValueDescriptor::setDoubleValue(double aValue)
{
  // double can also be used to set enum by integer
  return setInt32Value((int32_t)aValue);
}


bool EnumValueDescriptor::setBoolValue(bool aValue)
{
  // bool can also be used to set enums with only two choices, would allow things like "yes"/"no" or "enabled"/"disabled".
  return setInt32Value(aValue ? 1 : 0);
}


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


bool EnumValueDescriptor::setStringValue(const string aEnumText)
{
  EnumList::EnumValue v = enumList->valueForText(aEnumText, true);
  if (v==EnumList::unknownEnum) return false;
  else return setInt32Value(v);
}


bool EnumValueDescriptor::setStringValueCaseInsensitive(const string& aValue)
{
  EnumList::EnumValue v = enumList->valueForText(aValue, false);
  if (v==EnumList::unknownEnum) return false;
  else return setInt32Value(v);
}


EnumValueDescriptorPtr EnumValueDescriptor::create(const char* aName, std::vector<const char*> aValues)
{
  EnumValueDescriptorPtr desc = EnumValueDescriptorPtr(new EnumValueDescriptor(aName, true));
  desc->enumList->addEnumTexts(aValues);
  return desc;
}


void EnumValueDescriptor::addEnum(const char *aEnumText, int aEnumValue, bool aIsDefault)
{
  enumList->addMapping(aEnumText, aEnumValue);
  if (aIsDefault) {
    value = aEnumValue; // also assign as default
    hasValue = true;
    isDefaultValue = true;
  }
}


ErrorPtr EnumValueDescriptor::conforms(ApiValuePtr aApiValue, bool aMakeInternal)
{
  ErrorPtr err;
  if (needsConformanceCheck(aApiValue, err)) {
    // check if value conforms
    if (aApiValue->getType()!=apivalue_string) {
      err = Error::err<VdcApiError>(415, "enum label must be string");
    }
    else {
      // must be one of the texts in the enum list
      EnumList::EnumValue v = enumList->valueForText(aApiValue->stringValue());
      if (v!=EnumList::unknownEnum) {
        // found
        if (aMakeInternal && !noInternalValue) {
          aApiValue->setType(apivalue_uint64);
          aApiValue->setUint32Value(v);
        }
        return err;
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
  if (aAsInternal && !noInternalValue) {
    aApiValue->setType(apivalue_uint64);
    aApiValue->setUint32Value(v);
    return true;
  }
  else {
    aApiValue->setType(apivalue_string);
    const char* t = enumList->textForValue(v);
    if (t) {
      aApiValue->setStringValue(t);
      return true;
    }
    return false;
  }
}


bool EnumValueDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(INSTANCE_OKEY(enumList.get()))) {
    return enumList->accessField(aMode, aPropValue, aPropertyDescriptor);
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


PropertyContainerPtr EnumValueDescriptor::getContainer(const PropertyDescriptorPtr aPropertyDescriptor, int &aDomain)
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
    return enumList->numProps();
  }
  return inherited::numProps(aDomain, aParentDescriptor);
}


PropertyDescriptorPtr EnumValueDescriptor::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(value_enumvalues_key)) {
    // enumvalues - distinct set of NULL values (only names count)
    return enumList->getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor);
  }
  return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor);
}


// MARK: - ValueList

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


PropertyContainerPtr ValueList::getContainer(const PropertyDescriptorPtr aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(valueDescriptor_key)) {
    return values[aPropertyDescriptor->fieldKey()];
  }
  return NULL;
}
