//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2016-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "singledevice.hpp"
#include "simplescene.hpp"

#include "jsonvdcapi.hpp"

using namespace p44;


// MARK: - DeviceAction

DeviceAction::DeviceAction(SingleDevice &aSingleDevice, const string aId, const string aDescription, const string aTitle, const string aCategory) :
  singleDeviceP(&aSingleDevice),
  actionId(aId),
  actionDescription(aDescription),
  actionTitle(aTitle),
  actionCategory(aCategory)
{
  // install value list for parameters
  actionParams = ValueListPtr(new ValueList);
}


void DeviceAction::addParameter(ValueDescriptorPtr aValueDesc, bool aMandatory)
{
  aValueDesc->setIsOptional(!aMandatory); // even a null value is a default value except if parameter is mandatory
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
      if (Error::notOK(err)) {
        if (nonConformingAsNull() && !o->isNull()) {
          o = aParams->newNull(); // convert it to NULL
          err = (*pos)->conforms(o, true); // check again to see if NULL is conformant
          if (Error::notOK(err)) break; // NULL is not conformant -> error out
        }
        else {
          break; // param is not conformant -> error out
        }
      }
    }
    else {
      // caller did not supply this parameter, get default value (which might be NULL)
      o = aParams->newNull();
      if (!(*pos)->getValue(o)) {
        // there is no default value
        if (!(*pos)->isOptional()) {
          // a non-optional value can only be omitted in a call when there is a default value
          err = Error::err<VdcApiError>(415, "missing value for non-optional parameter");
          break;
        }
      }
      // add the default to the passed params
      aParams->add((*pos)->getName(), o);
    }
    ++pos;
  }
  if (Error::notOK(err)) {
    // rewrite error to include param name
    if (pos!=actionParams->values.end() && err->isDomain(VdcApiError::domain())) {
      err = Error::err<VdcApiError>(err->getErrorCode(), "parameter '%s': %s", (*pos)->getName().c_str(), err->text());
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


// MARK: - DeviceAction property access

enum {
  actiondescription_key,
  actiontitle_key,
  actionparams_key,
  actioncategory_key,
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
    { "title", apivalue_string, actiontitle_key, OKEY(deviceaction_key) },
    { "params", apivalue_object, actionparams_key, OKEY(deviceaction_key) },
    { "category", apivalue_string, actioncategory_key, OKEY(deviceaction_key) }
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
      case actiontitle_key:
        if (actionTitle.empty()) {
          return false; // empty title is not shown
        }
        else {
          aPropValue->setStringValue(actionTitle);
          return true;
        }
      case actioncategory_key:
        if (actionCategory.empty()) {
          return false;
        }
        else {
          aPropValue->setStringValue(actionCategory);
          return true;
        }
    }
  }
  return false;
}


// MARK: - DeviceActions container


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



// MARK: - DynamicDeviceActions container


void DynamicDeviceActions::addToModelUIDHash(string &aHashedString)
{
  // dynamic actions must not be part of ModelUID! -> NOP
}



bool DynamicDeviceActions::removeActionInternal(DeviceActionPtr aAction)
{
  if (!aAction) return false;
  for (ActionsVector::iterator pos = deviceActions.begin(); pos!=deviceActions.end(); ++pos) {
    if ((*pos)->actionId==aAction->getId()) {
      // already exists, remove it from container (might be the same object as aAction, or a different one with the same id)
      deviceActions.erase(pos);
      return true;
    }
  }
  return false;
}


void DynamicDeviceActions::addOrUpdateDynamicAction(DeviceActionPtr aAction)
{
  if (!aAction) return;
  // if action with same name already exists, remove it from the container first
  removeActionInternal(aAction);
  // (re)add the action now
  deviceActions.push_back(aAction);
  // report the change via push
  pushActionChange(aAction, false);
}

void DynamicDeviceActions::addOrUpdateDynamicActions(ActionsVector &aActions)
{
  ActionsVector resultVector;

  std::sort(aActions.begin(), aActions.end(), compareByIdAndTitle);
  std::sort(deviceActions.begin(), deviceActions.end(), compareByIdAndTitle);

  std::set_difference(
      aActions.begin(), aActions.end(),
      deviceActions.begin(), deviceActions.end(),
      back_inserter(resultVector),
      compareByIdAndTitle);

  std::for_each(resultVector.begin(), resultVector.end(), boost::bind(&DynamicDeviceActions::addOrUpdateDynamicAction, this, _1));
}


void DynamicDeviceActions::removeDynamicAction(DeviceActionPtr aAction)
{
  if (removeActionInternal(aAction)) {
    // actually deleted
    pushActionChange(aAction, true);
  }
}


void DynamicDeviceActions::removeDynamicActionsExcept(ActionsVector &aActions)
{
  ActionsVector resultVector;

  std::sort(aActions.begin(), aActions.end(), compareById);
  std::sort(deviceActions.begin(), deviceActions.end(), compareById);

  std::set_difference(
      deviceActions.begin(), deviceActions.end(),
      aActions.begin(), aActions.end(),
      back_inserter(resultVector),
      compareById);

  std::for_each(resultVector.begin(), resultVector.end(), boost::bind(&DynamicDeviceActions::removeDynamicAction, this, _1));
}


bool DynamicDeviceActions::pushActionChange(DeviceActionPtr aAction, bool aRemoved)
{
  if (!aAction) return false;
  SingleDevice &sd = *aAction->singleDeviceP;
  VdcApiConnectionPtr api = sd.getVdcHost().getVdsmSessionConnection();
  SOLOG((sd), LOG_NOTICE, "%spushing: dynamic action '%s' was %s", api ? "" : "Not announced, not ", aAction->getId().c_str(), aRemoved ? "removed" : "added or changed");
  // try to push to connected vDC API client
  if (api) {
    // create query for description property to get pushed
    ApiValuePtr query = api->newApiValue();
    query->setType(apivalue_object);
    ApiValuePtr subQuery = query->newValue(apivalue_object);
    subQuery->add(aAction->getId(), subQuery->newValue(apivalue_null));
    query->add(string("dynamicActionDescriptions"), subQuery);
    // let pushNotification execute the query and
    return sd.pushNotification(api, query, ApiValuePtr(), aRemoved);
  }
  // no API, cannot not push
  return false;
}


void DynamicDeviceActions::updateDynamicActions(ActionsVector &aActions)
{
  removeDynamicActionsExcept(aActions);
  addOrUpdateDynamicActions(aActions);
}




// MARK: - CustomAction

ActionMacro::ActionMacro(SingleDevice &aSingleDevice) :
  singleDevice(aSingleDevice),
  flags(0)
{
  storedParams = ApiValuePtr(new JsonApiValue()); // must be JSON as we store params as JSON
  storedParams->setType(apivalue_object);
}


void ActionMacro::call(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  ErrorPtr err;

  // custom actions might exist that do not have a valid action (any more)
  if (!action) {
    LOG(LOG_ERR, "- action macro '%s' cannot be invoked because it is not based on a valid device action", actionId.c_str());
    aCompletedCB(Error::err<VdcApiError>(500, "action macro has no valid device action to call"));
    return;
  }
  // copy each of the stored params, unless same param is alreday in aParams (which means overridden)
  if (storedParams && storedParams->resetKeyIteration()) {
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
  LOG(LOG_INFO, "- action macro '%s' calls '%s':%s", actionId.c_str(), action->actionId.c_str(), aParams->description().c_str());
  action->call(aParams, aCompletedCB);
}



// MARK: - ActionMacro property access

enum {
  customactionaction_key,
  customactiontitle_key,
  customactiondesc_key,
  customactionparams_key,
  numCustomActionProperties
};

static char customaction_key;


int ActionMacro::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return numCustomActionProperties;
}


PropertyDescriptorPtr ActionMacro::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // device level properties
  static const PropertyDescription properties[numCustomActionProperties] = {
    { "action", apivalue_string, customactionaction_key, OKEY(customaction_key) },
    { "title", apivalue_string, customactiontitle_key, OKEY(customaction_key) },
    { "description", apivalue_string, customactiondesc_key, OKEY(customaction_key) },
    { "params", apivalue_null, customactionparams_key, OKEY(customaction_key) }
  };
  if (aParentDescriptor->isRootOfObject()) {
    // root level property of this object hierarchy
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


ErrorPtr ActionMacro::validateParams(ApiValuePtr aParams, ApiValuePtr aValidatedParams, bool aSkipInvalid)
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
      if (Error::notOK(err) && action->nonConformingAsNull() && !val->isNull()) {
        val = aParams->newNull(); // convert it to NULL
        err = vd->conforms(val, true); // check again to see if NULL is conformant
      }
    }
    if (Error::isOK(err)) {
      ApiValuePtr myParam = aValidatedParams->newValue(val->getType());
      *myParam = *val; // do a value copy because  API value types might be different
      aValidatedParams->add(key, myParam);
    }
    else {
      if (aSkipInvalid) continue; // just ignore, but continue checking others
      return TextError::err("invalid parameter '%s' for custom action '%s': %s", key.c_str(), actionId.c_str(), err->text());
    }
  }
  SOLOG(singleDevice, LOG_DEBUG, "validated params: %s", aValidatedParams ? aValidatedParams->description().c_str() : "<none>");
  return ErrorPtr(); // all parameters conform (or aSkipInvalid)
}


bool ActionMacro::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(customaction_key)) {
    if (aMode==access_read) {
      // read
      switch (aPropertyDescriptor->fieldKey()) {
        case customactionaction_key: aPropValue->setStringValue(action ? action->actionId : "INVALID"); return true;
        case customactiontitle_key: aPropValue->setStringValue(actionTitle); return true;
        case customactiondesc_key: {
          if (actionTitle.empty() && action)
            aPropValue->setStringValue(action->actionDescription);
          else
            aPropValue->setStringValue(actionTitle);
          return true;
        }
        case customactionparams_key: {
          // simply return the stored params object
          *aPropValue = *storedParams; // do a value copy because  API value types might be different
          return true;
        }
      }
    }
  }
  return inheritedProps::accessField(aMode, aPropValue, aPropertyDescriptor);
}


ErrorPtr ActionMacro::configureMacro(const string aDeviceActionId, JsonObjectPtr aParams)
{
  ErrorPtr err;
  // - look up device action this macro is based on
  action = singleDevice.deviceActions->getAction(aDeviceActionId);
  if (!action && aDeviceActionId.substr(0,4)=="std.") {
    // try without possibly historically present "std." prefix
    // (for custom actions created before separating standard actions from device actions)
    action = singleDevice.deviceActions->getAction(aDeviceActionId.substr(4));
  }
  if (action) {
    // get and validate params
    ApiValuePtr loadedParams;
    if (aParams) loadedParams = JsonApiValue::newValueFromJson(aParams);
    err = validateParams(loadedParams, storedParams, true);
  }
  else {
    // referring to unknown device action
    err = TextError::err("device action '%s' is unknown - cannot base macro '%s' on it", aDeviceActionId.c_str(), actionId.c_str());
  }
  return err;
}


// MARK: - CustomAction

CustomAction::CustomAction(SingleDevice &aSingleDevice) :
  inherited(aSingleDevice),
  inheritedParams(aSingleDevice.getVdcHost().getDsParamStore())
{
}


const char *CustomAction::tableName()
{
  return "customActions";
}


// MARK: - CustomAction persistence

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
  // - configure it
  JsonObjectPtr j = JsonObject::objFromText(jsonparams.c_str());
  ErrorPtr err = configureMacro(baseAction, j);
  if (Error::notOK(err)) {
    SOLOG(singleDevice, LOG_ERR, "error loading custom action: %s", err->text());
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


// MARK: - CustomAction property access

bool CustomAction::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(customaction_key)) {
    if (aMode==access_read) {
      // read is implemented in common base class
      return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
    }
    else {
      // write is only available in customAction
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
          SOLOG(singleDevice, LOG_ERR, "there is no deviceAction called '%s'", aPropValue->stringValue().c_str());
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
          SOLOG(singleDevice, LOG_ERR, "writing 'params' failed: %s", err->text());
          return false;
        }
      }
    }
    return false;
  }
  return inheritedProps::accessField(aMode, aPropValue, aPropertyDescriptor);
}




// MARK: - CustomActions container

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
    descP->createdNew = true;
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
  string parentID = singleDevice.mDSUID.getString();
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
  }
  return err;
}


ErrorPtr CustomActions::save()
{
  ErrorPtr err;

  // custom actions are stored by dSUID
  string parentID = singleDevice.mDSUID.getString();
  // save all elements of the map (only dirty ones will be actually stored to DB
  for (CustomActionsVector::iterator pos = customActions.begin(); pos!=customActions.end(); ++pos) {
    err = (*pos)->saveToStore(parentID.c_str(), true); // multiple children of same parent allowed
    if (Error::notOK(err)) SOLOG(singleDevice, LOG_ERR,"Error saving custom action '%s': %s", (*pos)->actionId.c_str(), err->text());
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


// MARK: - StandardAction

StandardAction::StandardAction(SingleDevice &aSingleDevice, const string aId, const string aTitle) :
  inherited(aSingleDevice)
{
  actionId = aId;
  actionTitle = aTitle;
}

void StandardAction::updateParameterValue(const string& aName, JsonObjectPtr aValue)
{
  JsonObjectPtr params = boost::dynamic_pointer_cast<JsonApiValue>(storedParams)->jsonObject();
  params->add(aName.c_str(), aValue);

}


// MARK: - StandardActions container


void StandardActions::addStandardAction(StandardActionPtr aAction)
{
  standardActions.push_back(aAction);
}

void StandardActions::addToModelUIDHash(string &aHashedString)
{
  for (StandardActionsVector::iterator pos = standardActions.begin(); pos!=standardActions.end(); ++pos) {
    aHashedString += ':';
    aHashedString += (*pos)->actionId;
  }
}



int StandardActions::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)standardActions.size();
}


static char standardactions_key;

PropertyDescriptorPtr StandardActions::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<standardActions.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = standardActions[aPropIndex]->actionId;
    descP->propertyType = apivalue_object;
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(standardactions_key);
    return descP;
  }
  return PropertyDescriptorPtr();
}


PropertyContainerPtr StandardActions::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(standardactions_key)) {
    return standardActions[aPropertyDescriptor->fieldKey()];
  }
  return NULL;
}


bool StandardActions::call(const string aActionId, ApiValuePtr aParams, StatusCB aCompletedCB)
{
  for (StandardActionsVector::iterator pos = standardActions.begin(); pos!=standardActions.end(); ++pos) {
    if ((*pos)->actionId==aActionId) {
      // action exists
      (*pos)->call(aParams, aCompletedCB);
      return true; // done, callback will finish it
    }
  }
  // custom action does not exist
  return false; // we might want to try device action directly
}


// MARK: - DeviceStateParams

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





// MARK: - DeviceState

DeviceState::DeviceState(SingleDevice &aSingleDevice, const string aStateId, const string aDescription, ValueDescriptorPtr aStateDescriptor, DeviceStateWillPushCB aWillPushHandler) :
  singleDeviceP(&aSingleDevice),
  stateId(aStateId),
  stateDescriptor(aStateDescriptor),
  stateDescription(aDescription),
  willPushHandler(aWillPushHandler),
  updateInterval(0),
  lastPush(Never)
{
  stateDescriptor->setIsOptional(false); // never "optional" (NULL exists as state value in general, but means: not known)
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
  VdcApiConnectionPtr api = singleDeviceP->getVdcHost().getVdsmSessionConnection();
  SOLOG((*singleDeviceP), LOG_NOTICE, "%spushing: state '%s' changed to '%s'", api ? "" : "Not announced, not ", stateId.c_str(), stateDescriptor->getStringValue().c_str());
  // update for every push attempt, as this are "events"
  lastPush = MainLoop::currentMainLoop().now();
  // collect additional events to push
  if (willPushHandler) {
    willPushHandler(DeviceStatePtr(this), aEventList);
  }
  // try to push to connected vDC API client
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
      SOLOG((*singleDeviceP), LOG_NOTICE, "- pushing event '%s' along with state change", (*pos)->eventId.c_str());
    }
    return singleDeviceP->pushNotification(api, query, events, VDC_API_DOMAIN);
  }
  else {
    for (DeviceEventsList::iterator pos=aEventList.begin(); pos!=aEventList.end(); ++pos) {
      SOLOG((*singleDeviceP), LOG_NOTICE, "- event '%s' not pushed", (*pos)->eventId.c_str());
    }
  }
  // no API, cannot not push
  return false;
}




// MARK: - DeviceState property access

enum {
  statedescription_key,
  updateInterval_key,
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
    { "updateInterval", apivalue_double, updateInterval_key, OKEY(devicestatedesc_key) },
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
        case statedescription_key:
          aPropValue->setStringValue(stateDescription);
          return true;
        case updateInterval_key:
          aPropValue->setDoubleValue((double)updateInterval/Second);
          return true;
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




// MARK: - DeviceStates container



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



// MARK: - DeviceEvent


DeviceEvent::DeviceEvent(SingleDevice &aSingleDevice, const string aEventId, const string aDescription) :
  singleDeviceP(&aSingleDevice),
  eventId(aEventId),
  eventDescription(aDescription)
{
}


// MARK: - DeviceEvent property access

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




// MARK: - DeviceEvents container


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
  VdcApiConnectionPtr api = singleDeviceP->getVdcHost().getVdsmSessionConnection();
  if (!aEventList.empty()) {
    // add events
    SOLOG((*singleDeviceP), LOG_NOTICE, "%spushing: independent event(s):", api ? "" : "Not announced, not ");
    if (api) {
      ApiValuePtr events;
      for (DeviceEventsList::iterator pos=aEventList.begin(); pos!=aEventList.end(); ++pos) {
        if (!events) {
          events = api->newApiValue();
          events->setType(apivalue_object);
        }
        ApiValuePtr event = api->newApiValue();
        event->setType(apivalue_null); // for now, events don't have any properties, so it's just named NULL values
        events->add((*pos)->eventId, event);
        SOLOG((*singleDeviceP), LOG_NOTICE, "- event '%s'", (*pos)->eventId.c_str());
      }
      return singleDeviceP->pushNotification(api, ApiValuePtr(), events);
    }
    else {
      for (DeviceEventsList::iterator pos=aEventList.begin(); pos!=aEventList.end(); ++pos) {
        SOLOG((*singleDeviceP), LOG_NOTICE, "- event '%s' not pushed", (*pos)->eventId.c_str());
      }

    }
  }
  return false; // no events to push
}




// MARK: - DeviceProperties container


void DeviceProperties::addProperty(ValueDescriptorPtr aPropertyDesc, bool aReadOnly, bool aNeedsFetch, bool aNullAllowed)
{
  aPropertyDesc->setReadOnly(aReadOnly);
  aPropertyDesc->setNeedsFetch(aNeedsFetch);
  aPropertyDesc->setIsOptional(aNullAllowed); // properties are never "optional"
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
  VdcApiConnectionPtr api = singleDeviceP->getVdcHost().getVdsmSessionConnection();
  if (api) {
    // create query for device property to get pushed
    ApiValuePtr query = api->newApiValue();
    query->setType(apivalue_object);
    ApiValuePtr subQuery = query->newValue(apivalue_object);
    subQuery->add(aPropertyDesc->getName(), subQuery->newValue(apivalue_null));
    query->add(string("deviceProperties"), subQuery);
    return singleDeviceP->pushNotification(api, query, ApiValuePtr());
  }
  return false; // cannot push
}


// MARK: - DeviceProperties property access


static char devicepropertydesc_key;
static char deviceproperty_key;


PropertyDescriptorPtr DeviceProperties::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  PropertyDescriptorPtr p = inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor);
  if (aParentDescriptor->hasObjectKey(deviceproperty_key)) {
    DynamicPropertyDescriptor *dp = static_cast<DynamicPropertyDescriptor *>(p.get());
    // access via deviceProperties, we directly want to see the values
    dp->propertyType = apivalue_null; // switch to leaf (of variable type)
    dp->deletable = true; // some properties might be assigned NULL to "delete", "invalidate" or "reset" them in some way (properties that do not allow null will complain at conforms())
    dp->needsReadPrep = values[aPropIndex]->doesNeedFetch(); // for values, we might need a fetch
  }
  return p;
}


void DeviceProperties::prepareAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor, StatusCB aPreparedCB)
{
  if (aMode==access_read) {
    ValueDescriptorPtr val = values[aPropertyDescriptor->fieldKey()];
    if (propertyFetchHandler) {
      propertyFetchHandler(val, aPreparedCB);
      return;
    }
  }
  // nothing to do here, let inherited handle it
  inherited::prepareAccess(aMode, aPropertyDescriptor, aPreparedCB);
}


bool DeviceProperties::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->parentDescriptor->hasObjectKey(deviceproperty_key)) {
    // fieldkey is the property index, just get that property's value
    ValueDescriptorPtr val = values[aPropertyDescriptor->fieldKey()];
    if (aMode==access_read) {
      // read
      if (!val->getValue(aPropValue))
        aPropValue->setNull(); // unset param will just report NULL
      return true; // property values are always reported
    }
    else if (!val->isReadOnly()) {
      // write
      ErrorPtr err = val->conforms(aPropValue, true);
      if (Error::notOK(err)) {
        SOLOG((*singleDeviceP), LOG_ERR, "Cannot set property '%s': %s", val->getName().c_str(), err->text());
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



// MARK: - ActionOutputBehaviour

ActionOutputBehaviour::ActionOutputBehaviour(Device &aDevice) :
  inherited(aDevice)
{
  // does not have a classic output with channels, so configure it as custom/disabled
  setHardwareOutputConfig(outputFunction_custom, outputmode_disabled, usage_undefined, false, -1);
}


Tristate ActionOutputBehaviour::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // now check for light behaviour level features
  switch (aFeatureIndex) {
    case modelFeature_outmodegeneric:
    case modelFeature_outvalue8:
    case modelFeature_blink:
      // suppress classic output mode features
      return no;
    default:
      // not available at output level
      return undefined;
  }
}





// MARK: - SingleDevice


SingleDevice::SingleDevice(Vdc *aVdcP, bool aEnableAsSingleDevice) :
  inherited(aVdcP)
{
  if (aEnableAsSingleDevice) {
    enableAsSingleDevice();
  }
}


void SingleDevice::enableAsSingleDevice()
{
  // create actions
  if (!deviceActions) deviceActions = DeviceActionsPtr(new DeviceActions);
  // create dynamic actions
  if (!dynamicDeviceActions) dynamicDeviceActions = DynamicDeviceActionsPtr(new DynamicDeviceActions);
  // create custom actions
  if (!customActions) customActions = CustomActionsPtr(new CustomActions(*this));
  // create states
  if (!deviceStates) deviceStates = DeviceStatesPtr(new DeviceStates);
  // create events
  if (!deviceEvents) deviceEvents = DeviceEventsPtr(new DeviceEvents(*this));
  // create device properties
  if (!deviceProperties) deviceProperties = DevicePropertiesPtr(new DeviceProperties(*this));
}


void SingleDevice::enableStandardActions()
{
  enableAsSingleDevice();
  // create standard actions
  if (!standardActions) standardActions = StandardActionsPtr(new StandardActions(*this));
}



SingleDevice::~SingleDevice()
{
}


void SingleDevice::addToModelUIDHash(string &aHashedString)
{
  // action names
  inherited::addToModelUIDHash(aHashedString);
  if (deviceActions) deviceActions->addToModelUIDHash(aHashedString);
  if (standardActions) standardActions->addToModelUIDHash(aHashedString);
  // Note: dynamic device actions are NOT part of the hash!
  if (deviceStates) deviceStates->addToModelUIDHash(aHashedString);
  if (deviceEvents) deviceEvents->addToModelUIDHash(aHashedString);
  if (deviceProperties) deviceProperties->addToModelUIDHash(aHashedString);
}





// MARK: - SingleDevice persistence

ErrorPtr SingleDevice::load()
{
  ErrorPtr err;
  if (customActions) customActions->load();
  if (Error::isOK(err)) {
    err = inherited::load();
  }
  return err;
}


ErrorPtr SingleDevice::save()
{
  ErrorPtr err = inherited::save();
  if (customActions && Error::isOK(err)) {
    err = customActions->save();
  }
  return err;
}


ErrorPtr SingleDevice::forget()
{
  ErrorPtr err = inherited::forget();
  if (customActions) err = customActions->forget();
  return err;
}


bool SingleDevice::isDirty()
{
  return inherited::isDirty() || (customActions && customActions->isDirty());
}


void SingleDevice::markClean()
{
  inherited::markClean();
  if (customActions) customActions->markClean();
}


// MARK: - SingleDevice API calls

void SingleDevice::call(const string aActionId, ApiValuePtr aParams, StatusCB aCompletedCB)
{
  if (!customActions->call(aActionId, aParams, aCompletedCB)) {
    if (!standardActions || !standardActions->call(aActionId, aParams, aCompletedCB)) {
      if (!dynamicDeviceActions->call(aActionId, aParams, aCompletedCB)) {
        if (!deviceActions->call(aActionId, aParams, aCompletedCB)) {
          // action does not exist, call back with error
          if (aCompletedCB) {
            aCompletedCB(Error::err<VdcApiError>(501, "action '%s' does not exist", aActionId.c_str()));
          }
        }
      }
    }
  }
}


ErrorPtr SingleDevice::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (deviceActions && aMethod=="invokeDeviceAction") {
    // recognizes method only if there are any actions
    string actionid;
    respErr = checkStringParam(aParams, "id", actionid);
    if (Error::notOK(respErr))
      return respErr;
    ApiValuePtr actionParams = aParams->get("params");
    if (!actionParams) {
      // always pass params, even if empty
      actionParams = aParams->newObject();
    }
    // now call the action
    OLOG(LOG_NOTICE, "invokeDeviceAction: %s:%s", actionid.c_str(), actionParams->description().c_str());
    call(actionid, actionParams, boost::bind(&SingleDevice::invokeDeviceActionComplete, this, aRequest, _1));
    // callback will create the response when done
    return ErrorPtr(); // do not return anything now
  }
  return inherited::handleMethod(aRequest, aMethod, aParams);
}


void SingleDevice::invokeDeviceActionComplete(VdcApiRequestPtr aRequest, ErrorPtr aError)
{
  OLOG(LOG_NOTICE, "- call completed with status: %s", Error::isOK(aError) ? "OK" : aError->text());
  methodCompleted(aRequest, aError);
}


// MARK: - Singledevice scene command handling

bool SingleDevice::prepareSceneCall(DsScenePtr aScene)
{
  SimpleCmdScenePtr cs = boost::dynamic_pointer_cast<SimpleCmdScene>(aScene);
  bool continueApply = true;
  if (cs) {
    // execute custom scene commands
    if (!cs->mCommand.empty()) {
      // special case: singledevice also directly executes non-prefixed commands,
      string cmd, cmdargs;
      bool isDeviceAction = false;
      if (keyAndValue(cs->mCommand, cmd, cmdargs, ':')) {
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
            cmdargs = cs->mCommand; // entire string as device action
          }
        }
      }
      else {
        // no prefix at all -> default to deviceaction anyway
        isDeviceAction = true;
        cmdargs = cs->mCommand; // entire string as device action
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
        OLOG(LOG_NOTICE, "invoking action via scene %d command: %s:%s", aScene->mSceneNo, actionid.c_str(), actionParams->description().c_str());
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
    OLOG(LOG_INFO, "scene invoked command complete");
  }
  else {
    OLOG(LOG_ERR, "scene invoked command returned error: %s", aError->text());
  }
}



// MARK: - SingleDevice property access


enum {
  // singledevice level properties
  deviceActionDescriptions_key,
  dynamicActionDescriptions_key,
  customActions_key,
  standardActions_key,
  deviceStateDescriptions_key,
  deviceStates_key,
  deviceEventDescriptions_key,
  devicePropertyDescriptions_key,
  deviceProperties_key,
  numSingleDeviceProperties
};

static char singledevice_key;


int SingleDevice::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // properties are only visible when single device is enabled (i.e. deviceActions exist)
  if (aParentDescriptor->isRootOfObject() && deviceActions) {
    // Accessing properties at the Device (root) level
    return inherited::numProps(aDomain, aParentDescriptor)+numSingleDeviceProperties;
  }
  return inherited::numProps(aDomain, aParentDescriptor); // only the inherited ones
}


PropertyDescriptorPtr SingleDevice::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // device level properties
  static const PropertyDescription properties[numSingleDeviceProperties] = {
    // common device properties
    { "deviceActionDescriptions", apivalue_object, deviceActionDescriptions_key, OKEY(singledevice_key) },
    { "dynamicActionDescriptions", apivalue_object, dynamicActionDescriptions_key, OKEY(singledevice_key) },
    { "customActions", apivalue_object, customActions_key, OKEY(singledevice_key) },
    { "standardActions", apivalue_object, standardActions_key, OKEY(singledevice_key) },
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
      case dynamicActionDescriptions_key:
        return dynamicDeviceActions;
      case customActions_key:
        return customActions;
      case standardActions_key:
        return standardActions;
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


// MARK: - dynamic configuration of single devices via JSON


ErrorPtr SingleDevice::actionFromJSON(DeviceActionPtr &aAction, JsonObjectPtr aJSONConfig, const string aActionId, const string aDescription, const string aCategory)
{
  // base class just creates a unspecific action
  aAction = DeviceActionPtr(new DeviceAction(*this, aActionId, aDescription, "", aCategory));
  return ErrorPtr();
}


ErrorPtr SingleDevice::dynamicActionFromJSON(DeviceActionPtr &aAction, JsonObjectPtr aJSONConfig, const string aActionId, const string aDescription, const string aTitle, const string aCategory)
{
  // base class just creates a unspecific action
  aAction = DeviceActionPtr(new DeviceAction(*this, aActionId, aDescription, aTitle, aCategory));
  return ErrorPtr();
}

ErrorPtr SingleDevice::parameterFromJSON(ValueDescriptorPtr &aParameter, JsonObjectPtr aJSONConfig, const string aParamName)
{
  return parseValueDesc(aParameter, aJSONConfig, aParamName);
}

ErrorPtr SingleDevice::stateFromJSON(DeviceStatePtr &aState, JsonObjectPtr aJSONConfig, const string aStateId, const string aDescription, ValueDescriptorPtr aStateDescriptor)
{
  // base class just creates a unspecific state without push handler
  aState = DeviceStatePtr(new DeviceState(*this, aStateId, aDescription, aStateDescriptor, NoOP));
  return ErrorPtr();
}


ErrorPtr SingleDevice::eventFromJSON(DeviceEventPtr &aEvent, JsonObjectPtr aJSONConfig, const string aEventId, const string aDescription)
{
  // base class just creates a unspecific event
  aEvent = DeviceEventPtr(new DeviceEvent(*this, aEventId, aDescription));
  return ErrorPtr();
}


ErrorPtr SingleDevice::propertyFromJSON(ValueDescriptorPtr &aProperty, JsonObjectPtr aJSONConfig, const string aPropName)
{
  return parseValueDesc(aProperty, aJSONConfig, aPropName);
}



ErrorPtr SingleDevice::addActionFromJSON(bool aDynamic, JsonObjectPtr aJSONConfig, const string aActionId, bool aPush)
{
  JsonObjectPtr o;
  ErrorPtr err;
  DeviceActionPtr a;
  string desc = aActionId; // default to name
  string category;
  if (aJSONConfig && aJSONConfig->get("description", o)) desc = o->stringValue();
  if (aJSONConfig && aJSONConfig->get("category", o)) category = o->stringValue();
  if (aDynamic) {
    // dynamic action
    if (!aJSONConfig || !aJSONConfig->get("title", o)) {
      return TextError::err("Dynamic action must have a title");
    }
    string title = o->stringValue();
    err = dynamicActionFromJSON(a, aJSONConfig, aActionId, desc, title, category);
  }
  else {
    // standard action
    err = actionFromJSON(a, aJSONConfig, aActionId, desc, category);
  }
  if (Error::notOK(err) || !a) return err;
  // check for params
  if (aJSONConfig && aJSONConfig->get("params", o)) {
    string pname;
    JsonObjectPtr param;
    o->resetKeyIteration();
    while (o->nextKeyValue(pname, param)) {
      ValueDescriptorPtr p;
      ErrorPtr err = parameterFromJSON(p, param, pname);
      if (Error::notOK(err)) return err;
      if (!p) continue; // if the parsing was ok, but no parameter was created do not add it to the action
      bool optional = !(p->isDefault()); // by default, no default value means the value is optional
      JsonObjectPtr o3;
      if (param->get("optional", o3)) {
        optional = o3->boolValue();
      }
      a->addParameter(p, !optional);
    }
  }
  if (aDynamic) {
    if (aPush)
      dynamicDeviceActions->addOrUpdateDynamicAction(a); // at runtime change
    else
      dynamicDeviceActions->addAction(a); // normal add, no push when added at device creation time
  }
  else {
    deviceActions->addAction(a);
  }
  return err;
}


ErrorPtr SingleDevice::standardActionsFromJSON(JsonObjectPtr aJSONConfig)
{
  JsonObjectPtr o;
  ErrorPtr err;

  // check for standard device actions
  if (aJSONConfig->get("autoaddstandardactions", o) && o->boolValue()) {
    autoAddStandardActions();
  }
  if (aJSONConfig->get("standardactions", o)) {
    enableStandardActions(); // must behave as a single device with standard actions
    string actionId;
    string actionTitle;
    JsonObjectPtr stdActionConfig;
    o->resetKeyIteration();
    while (o->nextKeyValue(actionId, stdActionConfig)) {
      JsonObjectPtr o2;
      if (!stdActionConfig->get("action", o2)) {
        err = TextError::err("missing 'action' parameter");
      }
      else {
        string baseAction = o2->stringValue();
        string title;
        if (stdActionConfig->get("title", o2)) {
          title = o2->stringValue();
        }
        StandardActionPtr a = StandardActionPtr(new StandardAction(*this, actionId, title));
        JsonObjectPtr p = stdActionConfig->get("params");
        err = a->configureMacro(baseAction, p);
        if (Error::isOK(err)) {
          standardActions->addStandardAction(a);
        }
      }
      if (Error::notOK(err)) return err->withPrefix("Error creating standard action '%s': ", actionId.c_str());
    }
  }
  return ErrorPtr();
}


void SingleDevice::autoAddStandardActions()
{
  autoAddStandardActions(deviceActions->deviceActions);
}

void SingleDevice::autoAddStandardActions(const DeviceActions::ActionsVector& aDeviceActions)
{
  enableStandardActions(); // must behave as a single device with standard actions
  for (DeviceActions::ActionsVector::const_iterator pos = aDeviceActions.begin(); pos!=aDeviceActions.end(); ++pos) {
    string id = (*pos)->getId();
    StandardActionPtr a = StandardActionPtr(new StandardAction(*this, "std." + id));
    a->configureMacro(id, getParametersFromActionDefaults(*pos));
    standardActions->addStandardAction(a);
  }
}

JsonObjectPtr SingleDevice::getParametersFromActionDefaults(DeviceActionPtr aAction)
{
  JsonObjectPtr json = JsonObject::newObj();
  ValueListPtr params = aAction->getActionParams();
  for(ValueList::ValuesVector::iterator it = params->values.begin(); it < params->values.end(); it++) {
    JsonApiValuePtr v = new JsonApiValue();
    if ((*it)->getValue(v)) {
      json->add((*it)->getName().c_str(), v->jsonObject());
    }
  }

  return json;
}


ErrorPtr SingleDevice::configureFromJSON(JsonObjectPtr aJSONConfig)
{
  JsonObjectPtr o;
  ErrorPtr err;

  // check for single device actions
  for (int dynamic=0; dynamic<=1; dynamic++) {
    if (aJSONConfig->get(dynamic ? "dynamicactions" : "actions", o)) {
      enableAsSingleDevice(); // must behave as a single device
      string actionId;
      JsonObjectPtr actionConfig;
      o->resetKeyIteration();
      while (o->nextKeyValue(actionId, actionConfig)) {
        err = addActionFromJSON(dynamic, actionConfig, actionId, false);
        if (Error::notOK(err)) return err->withPrefix("Error creating action '%s': ", actionId.c_str());
      }
    }
  }
  // check for single device states
  if (aJSONConfig->get("states", o)) {
    enableAsSingleDevice(); // must behave as a single device
    string stateId;
    JsonObjectPtr stateConfig;
    o->resetKeyIteration();
    while (o->nextKeyValue(stateId, stateConfig)) {
      JsonObjectPtr o2;
      string desc = stateId; // default to name
      if (stateConfig && stateConfig->get("description", o2)) desc = o2->stringValue();
      ValueDescriptorPtr v;
      err = parseValueDesc(v, stateConfig, "state");
      if (Error::notOK(err)) return err->withPrefix("Error in 'state' of '%s': ", stateId.c_str());
      // create the state
      DeviceStatePtr s;
      err = stateFromJSON(s, stateConfig, stateId, desc, v);
      if (Error::notOK(err)) return err->withPrefix("Error creating state '%s': ", stateId.c_str());
      deviceStates->addState(s);
    }
  }
  // check for single device events
  if (aJSONConfig->get("events", o)) {
    enableAsSingleDevice(); // must behave as a single device
    string eventId;
    JsonObjectPtr eventConfig;
    o->resetKeyIteration();
    while (o->nextKeyValue(eventId, eventConfig)) {
      // create the event
      JsonObjectPtr o2;
      string desc = eventId; // default to name
      if (eventConfig && eventConfig->get("description", o2)) desc = o2->stringValue();
      DeviceEventPtr e;
      err = eventFromJSON(e, eventConfig, eventId, desc);
      if (Error::notOK(err)) return err->withPrefix("Error creating event '%s': ", eventId.c_str());
      deviceEvents->addEvent(e);
    }
  }
  // check for single device properties
  if (aJSONConfig->get("properties", o)) {
    enableAsSingleDevice(); // must behave as a single device
    string propId;
    JsonObjectPtr propConfig;
    o->resetKeyIteration();
    while (o->nextKeyValue(propId, propConfig)) {
      // create the property (is represented by a ValueDescriptor)
      bool readonly = false;
      JsonObjectPtr o2;
      if (propConfig && propConfig->get("readonly", o2))
        readonly = o2->boolValue();
      ValueDescriptorPtr p;
      err = propertyFromJSON(p, propConfig, propId);
      if (Error::notOK(err)) {
        return err;
      }
      deviceProperties->addProperty(p, readonly);
    }
  }
  // successful
  return ErrorPtr();
}


ErrorPtr SingleDevice::updateDynamicActionFromJSON(const string aActionId, JsonObjectPtr aJSONConfig)
{
  if (!aJSONConfig || aJSONConfig->isType(json_type_null)) {
    dynamicDeviceActions->removeDynamicAction(dynamicDeviceActions->getAction(aActionId));
  }
  else {
    // add or change
    return addActionFromJSON(true, aJSONConfig, aActionId, true);
  }
  return ErrorPtr();
}



// MARK: - misc utils

ErrorPtr p44::parseValueDesc(ValueDescriptorPtr &aValueDesc, JsonObjectPtr aJSONConfig, const string aParamName)
{
  JsonObjectPtr o;
  if (!aJSONConfig || !aJSONConfig->get("type", o))
    return TextError::err("Need to specify value 'type'");
  VdcValueType vt = ValueDescriptor::stringToValueType(o->stringValue());
  if (vt==valueType_unknown)
    return TextError::err("Unknown value type '%s'", o->stringValue().c_str());
  JsonObjectPtr def = aJSONConfig->get("default");
  // value type is defined
  switch (vt) {
    default:
    case valueType_string:
      aValueDesc = ValueDescriptorPtr(new TextValueDescriptor(aParamName, (bool)def, def ? def->stringValue() : ""));
      break;
    case valueType_boolean:
      aValueDesc = ValueDescriptorPtr(new NumericValueDescriptor(aParamName, valueType_boolean, valueUnit_none, 0, 1, 1, (bool)def, def ? def->boolValue() : false));
      break;
    case valueType_numeric:
    case valueType_integer: {
      // can have an unit optionally
      ValueUnit u = valueUnit_none;
      if (aJSONConfig->get("siunit", o)) {
        u = stringToValueUnit(o->stringValue());
        if (u==unit_unknown)
          return TextError::err("Unknown siunit '%s'", o->stringValue().c_str());
      }
      // must have min, max
      double min,max;
      double resolution = 1;
      if (!aJSONConfig->get("min", o))
        return TextError::err("Numeric values need to have 'min'");
      min = o->doubleValue();
      if (!aJSONConfig->get("max", o))
        return TextError::err("Numeric values need to have 'max'");
      max = o->doubleValue();
      if (aJSONConfig->get("resolution", o)) {
        resolution = o->doubleValue();
      }
      else if (vt!=valueType_integer)
        return TextError::err("Numeric values need to have 'resolution'");
      aValueDesc = ValueDescriptorPtr(new NumericValueDescriptor(aParamName, vt, u, min, max, resolution, (bool)def, def ? def->doubleValue() : false));
      break;
    }
    case valueType_enumeration: {
      if (!aJSONConfig->get("values", o) || !o->isType(json_type_array))
        return TextError::err("Need to specify enumeration 'values' array");
      EnumValueDescriptor *en = new EnumValueDescriptor(aParamName);
      fillEnumDescriptor(*o, *en);
      aValueDesc = ValueDescriptorPtr(en);
      break;
    }
  }
  return ErrorPtr();
}

void p44::fillEnumDescriptor(JsonObject& aValues, EnumValueDescriptor& aEnumDesc)
{
  for (int i=0; i<aValues.arrayLength(); i++) {
    string e = aValues.arrayGet(i)->stringValue();
    bool isdefault = false;
    if (e.size()>0 && e[0]=='!') {
      isdefault = true;
      e.erase(0,1);
    }
    aEnumDesc.addEnum(e.c_str(), i, isdefault);
  }
}


