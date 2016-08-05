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
    { "resolution", apivalue_double, resolution_key, OKEY(param_key) },
    { "default", apivalue_null, default_key, OKEY(param_key) },
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
      err = ErrorPtr(new VdcApiError(415, "invalid boolean"));
    }
  }
  else if (valueType<valueType_firstNonNumeric) {
    // check bounds
    double v = aApiValue->doubleValue();
    if (v<min || v>max) {
      err = ErrorPtr(new VdcApiError(415, "number out of range"));
    }
  }
  else {
    err = ErrorPtr(new VdcApiError(415, "invalid number"));
  }
  // everything else is not valid for numeric parameter
  return err;
}


bool NumericValueDescriptor::getValue(ApiValuePtr aApiValue, bool aAsInternal)
{
  if (!hasValue || !aApiValue) return false;
  if (valueType==valueType_bool) {
    aApiValue->setType(apivalue_bool);
    aApiValue->setBoolValue(value);
  }
  else if (valueType==valueType_enum) {
    aApiValue->setType(apivalue_int64);
    aApiValue->setUint64Value(value);
  }
  else if (valueType>=valueType_firstIntNum) {
    aApiValue->setType(apivalue_int64);
    aApiValue->setInt64Value(value);
  }
  else {
    aApiValue->setType(apivalue_double);
    aApiValue->setDoubleValue(value);
  }
  return true;
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
        case default_key: return getValue(aPropValue);
      }
    }
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: ===== TextValueDescriptor

ErrorPtr TextValueDescriptor::conforms(ApiValuePtr aApiValue, bool aMakeInternal)
{
  ErrorPtr err;
  if (aApiValue) {
    // check if value conforms
    if (aApiValue->getType()!=apivalue_string) {
      err = ErrorPtr(new VdcApiError(415, "invalid string"));
    }
  }
  return err;
}



bool TextValueDescriptor::getValue(ApiValuePtr aApiValue, bool aAsInternal)
{
  if (!hasValue || !aApiValue) return false;
  aApiValue->setType(apivalue_string);
  aApiValue->setStringValue(value);
  return true;
}


bool TextValueDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(param_key) && aMode==access_read) {
    switch (aPropertyDescriptor->fieldKey()) {
      case default_key: return getValue(aPropValue);
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


ErrorPtr EnumValueDescriptor::conforms(ApiValuePtr aApiValue, bool aMakeInternal)
{
  ErrorPtr err;
  if (aApiValue) {
    // check if value conforms
    if (aApiValue->getType()!=apivalue_string) {
      err = ErrorPtr(new VdcApiError(415, "enum label must be string"));
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
      err = ErrorPtr(new VdcApiError(415, "invalid enum label"));
    }
  }
  return err;
}


bool EnumValueDescriptor::getValue(ApiValuePtr aApiValue, bool aAsInternal)
{
  if (!hasValue || !aApiValue) return false;
  if (aAsInternal) {
    aApiValue->setType(apivalue_uint64);
    aApiValue->setUint32Value(value);
    return true;
  }
  else {
    aApiValue->setType(apivalue_string);
    for (EnumVector::iterator pos = enumDescs.begin(); pos!=enumDescs.end(); ++pos) {
      if (pos->second==value) {
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


// MARK: ===== DeviceAction

DeviceAction::DeviceAction(SingleDevice &aSingleDevice, const string aDescription) :
  singleDeviceP(&aSingleDevice),
  actionDescription(aDescription)
{
  // install value list for parameters
  actionParams = ValueListPtr(new ValueList);
}


void DeviceAction::addParameter(const string aParameterName, ValueDescriptorPtr aValueDesc)
{
  actionParams->addValueDescriptor(aParameterName, aValueDesc);
}


void DeviceAction::call(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  ErrorPtr err;

  // for each parameter of the action, we either need a value in aParams or a default value
  ValueList::ValuesVector::iterator pos = actionParams->values.begin();
  while (pos!=actionParams->values.end()) {
    // check for override from passed params
    ApiValuePtr o = aParams->get(pos->first);
    if (o) {
      // caller did supply this parameter
      err = pos->second->conforms(o, true); // check and convert to internal (for text enums)
      if (!Error::isOK(err))
        break; // error
    }
    else {
      // called did not supply this parameter, get default value
      o = aParams->newNull();
      if (!pos->second->getValue(o)) {
        err = ErrorPtr(new VdcApiError(415, "missing value for non-optional parameter"));
        break;
      }
      // add the default to the passed params
      aParams->add(pos->first, o);
    }
    ++pos;
  }
  if (!Error::isOK(err)) {
    // rewrite error to include param name
    if (pos!=actionParams->values.end() && err->isDomain(VdcApiError::domain())) {
      err = ErrorPtr(new VdcApiError(err->getErrorCode(), string_format("parameter '%s': %s", pos->first.c_str(), err->description().c_str())));
    }
    // parameter error, not executing action, call back with error
    if (aCompletedCB) aCompletedCB(err);
    return; // done
  }
  // parameters are ok, now invoking action implementation
  LOG(LOG_INFO, "- calling with expanded params: %s", aParams->description().c_str());
  performCall(aParams, aCompletedCB);
}



void DeviceAction::performCall(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  if (aCompletedCB) aCompletedCB(ErrorPtr(new VdcApiError(501, "dummy action - not implemented")));
}


// MARK: ===== DeviceAction property access

enum {
  description_key,
  params_key,
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
    { "description", apivalue_string, description_key, OKEY(deviceaction_key) },
    { "params", apivalue_object, params_key, OKEY(deviceaction_key) }
  };
  if (!aParentDescriptor) {
    // root level property of this object hierarchy
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


PropertyContainerPtr DeviceAction::getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(deviceaction_key)) {
    if (aPropertyDescriptor->fieldKey()==params_key) {
      return actionParams;
    }
  }
  return NULL;
}


bool DeviceAction::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(deviceaction_key) && aMode==access_read) {
    switch (aPropertyDescriptor->fieldKey()) {
      case description_key: aPropValue->setStringValue(actionDescription); return true;
    }
  }
  return false;
}


// MARK: ===== DeviceActions container


int DeviceActions::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)deviceActions.size();
}


static char actions_key;

PropertyDescriptorPtr DeviceActions::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // enumvalues - distinct set of NULL values (only names count)
  if (aPropIndex<deviceActions.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = deviceActions[aPropIndex].first;
    descP->propertyType = apivalue_object;
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(actions_key);
    return descP;
  }
  return PropertyDescriptorPtr();
}


PropertyContainerPtr DeviceActions::getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(actions_key)) {
    return deviceActions[aPropertyDescriptor->fieldKey()].second;
  }
  return NULL;
}


void DeviceActions::call(const string aAction, ApiValuePtr aParams, StatusCB aCompletedCB)
{
  for (ActionsVector::iterator pos = deviceActions.begin(); pos!=deviceActions.end(); ++pos) {
    if (pos->first==aAction) {
      // call it
      pos->second->call(aParams, aCompletedCB);
      return; // done, callback will finish it
    }
  }
  // action does not exist
  if (aCompletedCB) {
    aCompletedCB(ErrorPtr(new VdcApiError(501, string_format("standard action '%s' not implemented", aAction.c_str()))));
  }
}


void DeviceActions::addAction(const string aName, DeviceActionPtr aAction)
{
  deviceActions.push_back(ActionEntry(aName, aAction));
}


// MARK: ===== DeviceProperties


void DeviceProperties::addProperty(const string aName, ValueDescriptorPtr aPropertyDesc)
{
  values.push_back(ValueEntry(aName, aPropertyDesc));
}





// MARK: ===== SingleDevice


SingleDevice::SingleDevice(Vdc *aVdcP) :
  inherited(aVdcP)
{
  // create actions
  deviceActions = DeviceActionsPtr(new DeviceActions);
  // create device properties
  deviceProperties = DevicePropertiesPtr(new DeviceProperties);
  // TODO: create custome actions & states
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
      ALOG(LOG_NOTICE, "callDeviceAction: %s:%s", action.c_str(), actionParams->description().c_str());
      // TODO: check custom actions first
      deviceActions->call(action, actionParams, boost::bind(&SingleDevice::actionCallComplete, this, aRequest, _1));
      // callback will create the response
      return ErrorPtr(); // do not return anything now
    }
  }
  return inherited::handleMethod(aRequest, aMethod, aParams);
}


void SingleDevice::actionCallComplete(VdcApiRequestPtr aRequest, ErrorPtr aError)
{
  ALOG(LOG_NOTICE, "- call completed with status %s", Error::isOK(aError) ? "OK" : aError->description().c_str());
  aRequest->sendStatus(aError);
}



// MARK: ===== SingleDevice property access


enum {
  // singledevice level properties
  deviceActions_key,
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
    { "deviceActions", apivalue_object, deviceActions_key, OKEY(singledevice_key) },
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
    switch (aPropertyDescriptor->fieldKey()) {
      case deviceActions_key:
        return deviceActions;
      case devicePropertyDescriptions_key:
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

