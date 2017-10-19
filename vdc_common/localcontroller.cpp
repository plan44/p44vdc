//
//  Copyright (c) 2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 6

#include "localcontroller.hpp"

#include "buttonbehaviour.hpp"


#if ENABLE_LOCALCONTROLLER

using namespace p44;

// MARK: ===== ZoneDescriptor

ZoneDescriptor::ZoneDescriptor() :
  inheritedParams(VdcHost::sharedVdcHost()->getDsParamStore()),
  deviceCount(0)
{
}


ZoneDescriptor::~ZoneDescriptor()
{

}


void ZoneDescriptor::usedByDevice(DevicePtr aDevice, bool aInUse)
{
  deviceCount += (aInUse ? 1 : -1);
}


// MARK: ===== ZoneDescriptor persistence

const char *ZoneDescriptor::tableName()
{
  return "zoneDescriptors";
}


// primary key field definitions

static const size_t numZoneKeys = 1;

size_t ZoneDescriptor::numKeyDefs()
{
  // no parent id, zones are global
  return numZoneKeys;
}

const FieldDefinition *ZoneDescriptor::getKeyDef(size_t aIndex)
{
  static const FieldDefinition keyDefs[numZoneKeys] = {
    { "zoneId", SQLITE_INTEGER }, // uniquely identifies this zone
  };
  if (aIndex<numZoneKeys)
    return &keyDefs[aIndex];
  return NULL;
}


// data field definitions

static const size_t numZoneFields = 1;

size_t ZoneDescriptor::numFieldDefs()
{
  return inheritedParams::numFieldDefs()+numZoneFields;
}


const FieldDefinition *ZoneDescriptor::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numZoneFields] = {
    { "zoneName", SQLITE_TEXT }
  };
  if (aIndex<inheritedParams::numFieldDefs())
    return inheritedParams::getFieldDef(aIndex);
  aIndex -= inheritedParams::numFieldDefs();
  if (aIndex<numZoneFields)
    return &dataDefs[aIndex];
  return NULL;
}


void ZoneDescriptor::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inheritedParams::loadFromRowWithoutParentId(aRow, aIndex, aCommonFlagsP);
  // get zoneID
  zoneID = aRow->getWithDefault(aIndex++, 0);
  // the name
  zoneName = nonNullCStr(aRow->get<const char *>(aIndex++));
}


void ZoneDescriptor::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // - my own id
  aStatement.bind(aIndex++, zoneID);
  // - title
  aStatement.bind(aIndex++, zoneName.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
}


// MARK: ===== ZoneDescriptor property access implementation

enum {
  zoneName_key,
  deviceCount_key,
  numZoneProperties
};

static char zonedescriptor_key;



int ZoneDescriptor::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return numZoneProperties;
}


PropertyDescriptorPtr ZoneDescriptor::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numZoneProperties] = {
    { "name", apivalue_string, zoneName_key, OKEY(zonedescriptor_key) },
    { "devices", apivalue_uint64, deviceCount_key, OKEY(zonedescriptor_key) },
  };
  if (aParentDescriptor->isRootOfObject()) {
    // root level property of this object hierarchy
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


bool ZoneDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(zonedescriptor_key)) {
    if (aMode==access_read) {
      switch (aPropertyDescriptor->fieldKey()) {
        case zoneName_key: aPropValue->setStringValue(zoneName); return true;
        case deviceCount_key: aPropValue->setUint32Value(deviceCount); return true;
      }
    }
    else {
      switch (aPropertyDescriptor->fieldKey()) {
        case zoneName_key: setPVar(zoneName, aPropValue->stringValue()); return true;
      }
    }
  }
  return false;
}


// MARK: ===== ZoneList


ZoneDescriptorPtr ZoneList::getZoneById(DsZoneID aZoneId, bool aCreateNewIfNotExisting)
{
  ZoneDescriptorPtr zone;
  for (ZonesVector::iterator pos = zones.begin(); pos!=zones.end(); ++pos) {
    if ((*pos)->zoneID==aZoneId) {
      zone = *pos;
      break;
    }
  }
  if (!zone && aCreateNewIfNotExisting) {
    // create new zone descriptor on the fly
    zone = ZoneDescriptorPtr(new ZoneDescriptor);
    zone->zoneID = aZoneId;
    zone->zoneName = string_format("Zone #%d", aZoneId);
    zone->markClean(); // not modified yet, no need to save
    zones.push_back(zone);
  }
  return zone;
}


// MARK: ===== ZoneList persistence

ErrorPtr ZoneList::load()
{
  ErrorPtr err;

  // create a template
  ZoneDescriptorPtr newZone = ZoneDescriptorPtr(new ZoneDescriptor());
  // get the query
  sqlite3pp::query *queryP = newZone->newLoadAllQuery(NULL);
  if (queryP==NULL) {
    // real error preparing query
    err = newZone->paramStore.error();
  }
  else {
    for (sqlite3pp::query::iterator row = queryP->begin(); row!=queryP->end(); ++row) {
      // got record
      // - load record fields into zone descriptor object
      int index = 0;
      newZone->loadFromRow(row, index, NULL);
      // - put custom action into container
      zones.push_back(newZone);
      // - fresh object for next row
      newZone = ZoneDescriptorPtr(new ZoneDescriptor());
    }
    delete queryP; queryP = NULL;
  }
  return err;
}


ErrorPtr ZoneList::save()
{
  ErrorPtr err;

  // save all elements (only dirty ones will be actually stored to DB)
  for (ZonesVector::iterator pos = zones.begin(); pos!=zones.end(); ++pos) {
    err = (*pos)->saveToStore(NULL, true); // multiple instances allowed, it's a *list*!
    if (!Error::isOK(err)) LOG(LOG_ERR,"Error saving zone %d: %s", (*pos)->zoneID, err->description().c_str());
  }
  return err;
}


// MARK: ===== ZoneList property access implementation

int ZoneList::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)zones.size();
}


static char zonelist_key;

PropertyDescriptorPtr ZoneList::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<zones.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = string_format("%hd", zones[aPropIndex]->zoneID);
    descP->propertyType = apivalue_object;
    descP->deletable = zones[aPropIndex]->deviceCount<=0; // zone is deletable when no device uses it
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(zonelist_key);
    return descP;
  }
  return PropertyDescriptorPtr();
}


PropertyDescriptorPtr ZoneList::getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor)
{
  PropertyDescriptorPtr p = inherited::getDescriptorByName(aPropMatch, aStartIndex, aDomain, aMode, aParentDescriptor);
  if (!p && aMode==access_write && isNamedPropSpec(aPropMatch)) {
    // writing to non-existing zone -> insert new zone
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = aPropMatch;
    descP->propertyType = apivalue_object;
    descP->deletable = true; // new zones are deletable
    descP->propertyFieldKey = zones.size(); // new zone will be appended, so index is current size
    descP->propertyObjectKey = OKEY(zonelist_key);
    DsZoneID newId = 0;
    if (sscanf(aPropMatch.c_str(), "%hd", &newId)!=1) {
      // not a valid zone ID, generate one
      newId = 22000; // arbitrary start number for locally generated zones
      while (getZoneById(newId, false)) {
        // already exists, use next
        newId++;
      }
    }
    getZoneById(newId, true); // creates the zone on the fly
    p = descP;
  }
  return p;
}


bool ZoneList::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(zonelist_key) && aMode==access_delete) {
    // only field-level access is deleting a zone
    ZoneDescriptorPtr dz = zones[aPropertyDescriptor->fieldKey()];
    dz->deleteFromStore(); // remove from store
    zones.erase(zones.begin()+aPropertyDescriptor->fieldKey()); // remove from container
    return true;
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


PropertyContainerPtr ZoneList::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(zonelist_key)) {
    return zones[aPropertyDescriptor->fieldKey()];
  }
  return NULL;
}



// MARK: ===== SceneDescriptor

SceneDescriptor::SceneDescriptor() :
  inheritedParams(VdcHost::sharedVdcHost()->getDsParamStore())
{
}


SceneDescriptor::~SceneDescriptor()
{
}


// MARK: ===== SceneDescriptor persistence

const char *SceneDescriptor::tableName()
{
  return "sceneDescriptors";
}


// primary key field definitions

static const size_t numSceneKeys = 1;

size_t SceneDescriptor::numKeyDefs()
{
  // no parent id, zones are global
  return numSceneKeys;
}

const FieldDefinition *SceneDescriptor::getKeyDef(size_t aIndex)
{
  static const FieldDefinition keyDefs[numSceneKeys] = {
    { "sceneNo", SQLITE_INTEGER }, // uniquely identifies this zone
  };
  if (aIndex<numSceneKeys)
    return &keyDefs[aIndex];
  return NULL;
}


// data field definitions

static const size_t numSceneFields = 1;

size_t SceneDescriptor::numFieldDefs()
{
  return inheritedParams::numFieldDefs()+numSceneFields;
}


const FieldDefinition *SceneDescriptor::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numSceneFields] = {
    { "sceneName", SQLITE_TEXT }
  };
  if (aIndex<inheritedParams::numFieldDefs())
    return inheritedParams::getFieldDef(aIndex);
  aIndex -= inheritedParams::numFieldDefs();
  if (aIndex<numSceneFields)
    return &dataDefs[aIndex];
  return NULL;
}


void SceneDescriptor::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inheritedParams::loadFromRowWithoutParentId(aRow, aIndex, aCommonFlagsP);
  // get zoneID
  sceneNo = (DsSceneNumber)aRow->getWithDefault(aIndex++, 0);
  // the name
  sceneName = nonNullCStr(aRow->get<const char *>(aIndex++));
}


void SceneDescriptor::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // - my own id
  aStatement.bind(aIndex++, sceneNo);
  // - title
  aStatement.bind(aIndex++, sceneName.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
}


// MARK: ===== ZoneDescriptor property access implementation

enum {
  sceneName_key,
  numSceneProperties
};

static char scenedescriptor_key;



int SceneDescriptor::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return numSceneProperties;
}


PropertyDescriptorPtr SceneDescriptor::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numSceneProperties] = {
    { "name", apivalue_string, zoneName_key, OKEY(scenedescriptor_key) }
  };
  if (aParentDescriptor->isRootOfObject()) {
    // root level property of this object hierarchy
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


bool SceneDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(scenedescriptor_key)) {
    if (aMode==access_read) {
      switch (aPropertyDescriptor->fieldKey()) {
        case sceneName_key: aPropValue->setStringValue(sceneName); return true;
      }
    }
    else {
      switch (aPropertyDescriptor->fieldKey()) {
        case zoneName_key: setPVar(sceneName, aPropValue->stringValue()); return true;
      }
    }
  }
  return false;
}


// MARK: ===== SceneList


SceneDescriptorPtr SceneList::getSceneByNo(DsSceneNumber aSceneNo, bool aCreateNewIfNotExisting)
{
  SceneDescriptorPtr scene;
  for (ScenesVector::iterator pos = scenes.begin(); pos!=scenes.end(); ++pos) {
    if ((*pos)->sceneNo==aSceneNo) {
      scene = *pos;
      break;
    }
  }
  if (!scene && aCreateNewIfNotExisting) {
    // create new scene descriptor on the fly
    scene = SceneDescriptorPtr(new SceneDescriptor);
    scene->sceneNo = aSceneNo;
    scene->sceneName = string_format("Scene #%d", aSceneNo);
    scene->markClean(); // not modified yet, no need to save
    scenes.push_back(scene);
  }
  return scene;
}


// MARK: ===== SceneList persistence

ErrorPtr SceneList::load()
{
  ErrorPtr err;

  // create a template
  SceneDescriptorPtr newScene = SceneDescriptorPtr(new SceneDescriptor());
  // get the query
  sqlite3pp::query *queryP = newScene->newLoadAllQuery(NULL);
  if (queryP==NULL) {
    // real error preparing query
    err = newScene->paramStore.error();
  }
  else {
    for (sqlite3pp::query::iterator row = queryP->begin(); row!=queryP->end(); ++row) {
      // got record
      // - load record fields into zone descriptor object
      int index = 0;
      newScene->loadFromRow(row, index, NULL);
      // - put custom action into container
      scenes.push_back(newScene);
      // - fresh object for next row
      newScene = SceneDescriptorPtr(new SceneDescriptor());
    }
    delete queryP; queryP = NULL;
  }
  return err;
}


ErrorPtr SceneList::save()
{
  ErrorPtr err;

  // save all elements (only dirty ones will be actually stored to DB)
  for (ScenesVector::iterator pos = scenes.begin(); pos!=scenes.end(); ++pos) {
    err = (*pos)->saveToStore(NULL, true); // multiple instances allowed, it's a *list*!
    if (!Error::isOK(err)) LOG(LOG_ERR,"Error saving scene %d: %s", (*pos)->sceneNo, err->description().c_str());
  }
  return err;
}


// MARK: ===== SceneList property access implementation

int SceneList::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)scenes.size();
}


static char scenelist_key;

PropertyDescriptorPtr SceneList::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<scenes.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = string_format("%u", scenes[aPropIndex]->sceneNo);
    descP->propertyType = apivalue_object;
    descP->deletable = true; // scene is deletable
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(scenelist_key);
    return descP;
  }
  return PropertyDescriptorPtr();
}


PropertyDescriptorPtr SceneList::getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor)
{
  PropertyDescriptorPtr p = inherited::getDescriptorByName(aPropMatch, aStartIndex, aDomain, aMode, aParentDescriptor);
  if (!p && aMode==access_write && isNamedPropSpec(aPropMatch)) {
    // writing to non-existing scene -> insert new scene
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = aPropMatch;
    descP->propertyType = apivalue_object;
    descP->deletable = true; // new scenes are deletable
    descP->propertyFieldKey = scenes.size(); // new zone will be appended, so index is current size
    descP->propertyObjectKey = OKEY(scenelist_key);
    uint16_t newSceneNo = MAX_SCENE_NO;
    if (sscanf(aPropMatch.c_str(), "%hd", &newSceneNo)==1) {
      if (newSceneNo<MAX_SCENE_NO) {
        getSceneByNo((DsSceneNumber)newSceneNo, true);
      }
      // valid new scene
      p = descP;
    }
  }
  return p;
}


bool SceneList::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(scenelist_key) && aMode==access_delete) {
    // only field-level access is deleting a zone
    SceneDescriptorPtr ds = scenes[aPropertyDescriptor->fieldKey()];
    ds->deleteFromStore(); // remove from store
    scenes.erase(scenes.begin()+aPropertyDescriptor->fieldKey()); // remove from container
    return true;
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


PropertyContainerPtr SceneList::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(scenelist_key)) {
    return scenes[aPropertyDescriptor->fieldKey()];
  }
  return NULL;
}



// MARK: ===== LocalController

LocalController::LocalController(VdcHost &aVdcHost) :
  vdcHost(aVdcHost)
{
  localZones.isMemberVariable();
  localScenes.isMemberVariable();
}


LocalController::~LocalController()
{

}



void LocalController::processGlobalEvent(VdchostEvent aActivity)
{
  FOCUSLOG("processGlobalEvent: event = %d", (int)aActivity);
}


bool LocalController::processButtonClick(ButtonBehaviour &aButtonBehaviour, DsClickType aClickType)
{
  FOCUSLOG("processButtonClick: clicktype=%d, device = %s", (int)aClickType, aButtonBehaviour.shortDesc().c_str());
  return false; // not handled so far
}


void LocalController::deviceAdded(DevicePtr aDevice)
{
  FOCUSLOG("deviceAdded: device = %s", aDevice->shortDesc().c_str());
  // make sure this device's zone exists in the global list
  ZoneDescriptorPtr deviceZone = localZones.getZoneById(aDevice->getZoneID(), true);
  deviceZone->usedByDevice(aDevice, true);
}


void LocalController::deviceRemoved(DevicePtr aDevice)
{
  FOCUSLOG("deviceRemoved: device = %s", aDevice->shortDesc().c_str());
  // TODO: remove zone usage - however this can be done only if we also track changing zones in devices
  //   so we leave it for now, meaning that zones can be deleted only after restart
}


void LocalController::startRunning()
{
  FOCUSLOG("startRunning");
}


ErrorPtr LocalController::load()
{
  ErrorPtr err;
  err = localZones.load();
  if (!Error::isOK(err)) LOG(LOG_ERR, "could not load localZones: %s", err->description().c_str());
  err = localScenes.load();
  if (!Error::isOK(err)) LOG(LOG_ERR, "could not load localScenes: %s", err->description().c_str());
  return err;
}


ErrorPtr LocalController::save()
{
  ErrorPtr err;
  err = localZones.save();
  if (!Error::isOK(err)) LOG(LOG_ERR, "could not save localZones: %s", err->description().c_str());
  err = localScenes.save();
  if (!Error::isOK(err)) LOG(LOG_ERR, "could not save localScenes: %s", err->description().c_str());
  return err;
}



// MARK: ===== LocalController property access

static char triggerlist_key;

enum {
  // singledevice level properties
  zones_key,
  scenes_key,
  triggers_key,
  numLocalControllerProperties
};

static char localcontroller_key;


int LocalController::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // properties are only visible when single device is enabled (i.e. deviceActions exist)
  if (aParentDescriptor->isRootOfObject()) {
    // Accessing properties at the Device (root) level
    return inherited::numProps(aDomain, aParentDescriptor)+numLocalControllerProperties;
  }
  return inherited::numProps(aDomain, aParentDescriptor); // only the inherited ones
}


PropertyDescriptorPtr LocalController::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // device level properties
  static const PropertyDescription properties[numLocalControllerProperties] = {
    // common device properties
    { "zones", apivalue_object, zones_key, OKEY(zonelist_key) },
    { "scenes", apivalue_object, scenes_key, OKEY(scenelist_key) },
    { "triggers", apivalue_object, triggers_key, OKEY(triggerlist_key) },
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


PropertyContainerPtr LocalController::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->parentDescriptor->isRootOfObject()) {
    switch (aPropertyDescriptor->fieldKey()) {
      case zones_key:
        return ZoneListPtr(&localZones);
      case scenes_key:
        return SceneListPtr(&localScenes);
//      case triggers_key:
//        return TriggerListPtr(&localTriggers);
    }
  }
  // unknown here
  return inherited::getContainer(aPropertyDescriptor, aDomain);
}




#endif // ENABLE_LOCALCONTROLLER
