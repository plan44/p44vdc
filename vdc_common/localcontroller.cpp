//
//  Copyright (c) 2017-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "jsonvdcapi.hpp"

#include "outputbehaviour.hpp"
#include "buttonbehaviour.hpp"
#include "simplescene.hpp"

#include "timeutils.hpp"

#if ENABLE_LOCALCONTROLLER

using namespace p44;


// MARK: - ZoneState

ZoneState::ZoneState() :
  lastGlobalScene(INVALID_SCENE_NO),
  lastDim(dimmode_stop),
  lastLightScene(INVALID_SCENE_NO)
{
  for (SceneArea i=0; i<=num_areas; ++i) {
    lightOn[i] = false;
    shadesOpen[i] = false;
  }
}


bool ZoneState::stateFor(int aGroup, int aArea)
{
  switch(aGroup) {
    case group_yellow_light : return lightOn[aArea];
    case group_grey_shadow : return shadesOpen[aArea];
    default: return false;
  }
}


void ZoneState::setStateFor(int aGroup, int aArea, bool aState)
{
  switch(aGroup) {
    case group_yellow_light : lightOn[aArea] = aState;
    case group_grey_shadow : shadesOpen[aArea] = aState;
  }
}



// MARK: - ZoneDescriptor

ZoneDescriptor::ZoneDescriptor() :
  inheritedParams(VdcHost::sharedVdcHost()->getDsParamStore())
{
}


ZoneDescriptor::~ZoneDescriptor()
{
}


void ZoneDescriptor::usedByDevice(DevicePtr aDevice, bool aInUse)
{
  if (zoneID==zoneId_global) return; // global zone always contains all devices, no need to maintain a list
  for (DeviceVector::iterator pos = devices.begin(); pos!=devices.end(); ++pos) {
    if (*pos==aDevice) {
      if (aInUse) return; // already here -> NOP
      // not in use any more, remove it
      devices.erase(pos);
      return;
    }
  }
  // not yet in my list
  if (aInUse) {
    devices.push_back(aDevice);
  }
}


DsGroupMask ZoneDescriptor::getZoneGroups()
{
  DsGroupMask zoneGroups = 0;
  if (zoneID==zoneId_global) return 0; // groups are not relevant in zone0
  for (DeviceVector::iterator pos = devices.begin(); pos!=devices.end(); ++pos) {
    OutputBehaviourPtr ob = (*pos)->getOutput();
    if (ob) zoneGroups |= ob->groupMemberships();
  }
  return zoneGroups;
}


SceneIdsVector ZoneDescriptor::getZoneScenes(DsGroup aForGroup, SceneKind aRequiredKinds, SceneKind aForbiddenKinds)
{
  SceneIdsVector zoneScenes;
  // create list of scenes
  const SceneKindDescriptor *sceneKindP = NULL;
  if (zoneID==zoneId_global) {
    // global scenes
    aRequiredKinds |= scene_global;
    sceneKindP = globalScenes;
  }
  else {
    // room scenes
    aRequiredKinds |= scene_room;
    sceneKindP = roomScenes;
  }
  aForbiddenKinds &= ~aRequiredKinds; // required ones must be allowed
  while (sceneKindP && sceneKindP->no!=INVALID_SCENE_NO) {
    // get identifier
    SceneIdentifier si(*sceneKindP, zoneID, aForGroup);
    SceneKind k = sceneKindP->kind;
    // look up in user-defined scenes
    SceneDescriptorPtr userscene = LocalController::sharedLocalController()->localScenes.getScene(si);
    SceneKind forbiddenKinds = aForbiddenKinds;
    if (userscene) {
      si.name = userscene->getSceneName();
      if (!si.name.empty()) {
        k |= scene_usernamed;
        forbiddenKinds &= ~(scene_extended|scene_area); // usernamed overrides extended/area exclusion
      }
    }
    if (
      ((k & aRequiredKinds)==aRequiredKinds) &&
      ((k & forbiddenKinds)==0)
    ) {
      zoneScenes.push_back(si);
    }
    sceneKindP++;
  }
  return zoneScenes;
}


size_t ZoneDescriptor::devicesInZone() const
{
  if (zoneID==zoneId_global) {
    return LocalController::sharedLocalController()->totalDevices();
  }
  else {
    return devices.size();
  }
}





// MARK: - ZoneDescriptor persistence

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


// MARK: - ZoneDescriptor property access implementation

static char zonedevices_container_key;
static char zonedevice_key;

enum {
  zoneName_key,
  deviceCount_key,
  zoneDevices_key,
  numZoneProperties
};

static char zonedescriptor_key;

int ZoneDescriptor::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(zonedevices_container_key)) {
    return (int)devicesInZone();
  }
  return numZoneProperties;
}



PropertyDescriptorPtr ZoneDescriptor::getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(zonedevices_container_key)) {
    // accessing one of the zone's devices by numeric index
    return getDescriptorByNumericName(
      aPropMatch, aStartIndex, aDomain, aParentDescriptor,
      OKEY(zonedevice_key)
    );
  }
  // None of the containers within Device - let base class handle vdc-Level properties
  return inherited::getDescriptorByName(aPropMatch, aStartIndex, aDomain, aMode, aParentDescriptor);
}


PropertyContainerPtr ZoneDescriptor::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->isArrayContainer()) {
    // local container (e.g. all devices of this zone)
    return PropertyContainerPtr(this); // handle myself
  }
  else if (aPropertyDescriptor->hasObjectKey(zonedevice_key)) {
    // - get device
    PropertyContainerPtr container = devices[aPropertyDescriptor->fieldKey()];
    return container;
  }
  // unknown here
  return NULL;
}



PropertyDescriptorPtr ZoneDescriptor::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numZoneProperties] = {
    { "name", apivalue_string, zoneName_key, OKEY(zonedescriptor_key) },
    { "deviceCount", apivalue_uint64, deviceCount_key, OKEY(zonedescriptor_key) },
    { "devices", apivalue_object+propflag_needsreadprep+propflag_needswriteprep+propflag_container+propflag_nowildcard, zoneDevices_key, OKEY(zonedevices_container_key) },
  };
  if (aParentDescriptor->isRootOfObject()) {
    // root level property of this object hierarchy
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}



void ZoneDescriptor::prepareAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor, StatusCB aPreparedCB)
{
  if (aPropertyDescriptor->hasObjectKey(zonedevices_container_key) && zoneID==zoneId_global) {
    // for global zone: create temporary list of all devices
    LocalController::sharedLocalController()->vdcHost.createDeviceList(devices);
  }
  // in any case: let inherited handle the callback
  inherited::prepareAccess(aMode, aPropertyDescriptor, aPreparedCB);
}


void ZoneDescriptor::finishAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(zonedevices_container_key) && zoneID==zoneId_global) {
    // list is only temporary
    devices.clear();
  }
}



bool ZoneDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(zonedescriptor_key)) {
    if (aMode==access_read) {
      switch (aPropertyDescriptor->fieldKey()) {
        case zoneName_key: aPropValue->setStringValue(zoneName); return true;
        case deviceCount_key: aPropValue->setUint64Value(devicesInZone()); return true;
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


// MARK: - ZoneList


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
    zone->zoneName = aZoneId==0 ? "[global]" : string_format("Zone #%d", aZoneId);
    zone->markClean(); // not modified yet, no need to save
    zones.push_back(zone);
  }
  return zone;
}


ZoneDescriptorPtr ZoneList::getZoneByName(const string aZoneName)
{
  int numName = -1;
  sscanf(aZoneName.c_str(), "%d", &numName);
  for (ZonesVector::iterator pos = zones.begin(); pos!=zones.end(); ++pos) {
    if (strucmp((*pos)->getName().c_str(),aZoneName.c_str())==0) {
      return *pos;
    }
    else if (numName>=0 && numName==(*pos)->getZoneId()) {
      return *pos;
    }
  }
  return ZoneDescriptorPtr();
}


int ZoneList::getZoneIdByName(const string aZoneNameOrId)
{
  ZoneDescriptorPtr zone = getZoneByName(aZoneNameOrId);
  if (zone) return zone->getZoneId();
  int zi;
  if (sscanf(aZoneNameOrId.c_str(), "%d", &zi)==1) return zi;
  return -1;
}



// MARK: - ZoneList persistence

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
      // - load record fields into object
      int index = 0;
      newZone->loadFromRow(row, index, NULL);
      // - put custom action into container
      zones.push_back(newZone);
      // - fresh object for next row
      newZone = ZoneDescriptorPtr(new ZoneDescriptor());
    }
    delete queryP; queryP = NULL;
    // make sure we have a global (appartment) zone
    getZoneById(0, true);
  }
  return err;
}


ErrorPtr ZoneList::save()
{
  ErrorPtr err;

  // save all elements (only dirty ones will be actually stored to DB)
  for (ZonesVector::iterator pos = zones.begin(); pos!=zones.end(); ++pos) {
    err = (*pos)->saveToStore(NULL, true); // multiple instances allowed, it's a *list*!
    if (Error::notOK(err)) LOG(LOG_ERR,"Error saving zone %d: %s", (*pos)->zoneID, err->text());
  }
  return err;
}


// MARK: - ZoneList property access implementation

int ZoneList::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)zones.size();
}


static char zonelist_key;

PropertyDescriptorPtr ZoneList::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<zones.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = string_format("%hu", zones[aPropIndex]->zoneID);
    descP->propertyType = apivalue_object;
    descP->deletable = zones[aPropIndex]->devices.size()==0; // zone is deletable when no device uses it
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
    descP->propertyType = apivalue_object;
    descP->deletable = true; // new zones are deletable
    descP->propertyFieldKey = zones.size(); // new zone will be appended, so index is current size
    descP->propertyObjectKey = OKEY(zonelist_key);
    DsZoneID newId = 0;
    if (sscanf(aPropMatch.c_str(), "%hu", &newId)!=1) {
      // not a valid zone ID, generate one
      newId = 22000; // arbitrary start number for locally generated zones
      while (getZoneById(newId, false)) {
        // already exists, use next
        newId++;
      }
    }
    getZoneById(newId, true); // creates the zone on the fly
    descP->propertyName = string_format("%hu", newId);
    descP->createdNew = true;
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



// MARK: - SceneIdentifier

SceneIdentifier::SceneIdentifier()
{
  sceneKindP = NULL;
  sceneNo = INVALID_SCENE_NO;
  zoneID = scene_global;
  group = group_undefined;
}


SceneIdentifier::SceneIdentifier(const SceneKindDescriptor &aSceneKind, DsZoneID aZone, DsGroup aGroup)
{
  sceneKindP = &aSceneKind;
  sceneNo = sceneKindP->no;
  zoneID = aZone;
  group = aGroup;
}


SceneIdentifier::SceneIdentifier(SceneNo aNo, DsZoneID aZone, DsGroup aGroup)
{
  sceneNo = aNo;
  zoneID = aZone;
  group = aGroup;
  deriveSceneKind();
}


SceneIdentifier::SceneIdentifier(const string aStringId)
{
  uint16_t tmpSceneNo = INVALID_SCENE_NO;
  uint16_t tmpZoneID = scene_global;
  uint16_t tmpGroup = group_undefined;
  sscanf(aStringId.c_str(), "%hu_%hu_%hu", &tmpSceneNo, &tmpZoneID, &tmpGroup);
  sceneNo = tmpSceneNo;
  zoneID = tmpZoneID;
  group = (DsGroup)tmpGroup;
  deriveSceneKind();
}


string SceneIdentifier::stringId() const
{
  return string_format("%hu_%hu_%hu", (uint16_t)sceneNo, (uint16_t)zoneID, (uint16_t)group);
}



string SceneIdentifier::getActionName() const
{
  return sceneKindP ? sceneKindP->actionName : string_format("scene %d", sceneNo);
}


string SceneIdentifier::getName() const
{
  return name;
}




bool SceneIdentifier::deriveSceneKind()
{
  const SceneKindDescriptor *sk = sceneNo>=START_APARTMENT_SCENES ? globalScenes : roomScenes;
  while (sk->no<MAX_SCENE_NO) {
    if (sk->no==sceneNo) {
      sceneKindP = sk;
      return true;
    }
    sk++;
  }
  sceneKindP = NULL; // unknown
  return false;
}



// MARK: - SceneDescriptor

SceneDescriptor::SceneDescriptor() :
  inheritedParams(VdcHost::sharedVdcHost()->getDsParamStore())
{
}


SceneDescriptor::~SceneDescriptor()
{
}


// MARK: - SceneDescriptor persistence

const char *SceneDescriptor::tableName()
{
  return "sceneDescriptors";
}


// primary key field definitions

static const size_t numSceneKeys = 3;

size_t SceneDescriptor::numKeyDefs()
{
  // no parent id, zones are global
  return numSceneKeys;
}

const FieldDefinition *SceneDescriptor::getKeyDef(size_t aIndex)
{
  static const FieldDefinition keyDefs[numSceneKeys] = {
    { "sceneNo", SQLITE_INTEGER },
    { "sceneZone", SQLITE_INTEGER },
    { "sceneGroup", SQLITE_INTEGER },
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
  // get key fields
  sceneId.sceneNo = aRow->getCastedWithDefault<SceneNo, int>(aIndex++, 0);
  sceneId.zoneID = aRow->getCastedWithDefault<DsZoneID, int>(aIndex++, 0);
  sceneId.group = aRow->getCastedWithDefault<DsGroup, int>(aIndex++, group_undefined);
  sceneId.deriveSceneKind();
  // the name
  sceneId.name = nonNullCStr(aRow->get<const char *>(aIndex++));
}


void SceneDescriptor::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // - my own id
  aStatement.bind(aIndex++, sceneId.sceneNo);
  aStatement.bind(aIndex++, sceneId.zoneID);
  aStatement.bind(aIndex++, sceneId.group);
  // - title
  aStatement.bind(aIndex++, sceneId.name.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
}


// MARK: - SceneDescriptor property access implementation

enum {
  sceneNo_key,
  sceneName_key,
  sceneAction_key,
  sceneZoneID_key,
  sceneGroup_key,
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
    { "sceneNo", apivalue_uint64, sceneNo_key, OKEY(scenedescriptor_key) },
    { "name", apivalue_string, sceneName_key, OKEY(scenedescriptor_key) },
    { "action", apivalue_string, sceneAction_key, OKEY(scenedescriptor_key) },
    { "zoneID", apivalue_uint64, sceneZoneID_key, OKEY(scenedescriptor_key) },
    { "group", apivalue_uint64, sceneGroup_key, OKEY(scenedescriptor_key) }
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
        case sceneNo_key: aPropValue->setUint16Value(getSceneNo()); return true;
        case sceneName_key: aPropValue->setStringValue(getSceneName()); return true;
        case sceneAction_key: aPropValue->setStringValue(getActionName()); return true;
        case sceneZoneID_key: aPropValue->setUint16Value(sceneId.zoneID); return true;
        case sceneGroup_key: aPropValue->setUint8Value(sceneId.group); return true;
      }
    }
    else {
      switch (aPropertyDescriptor->fieldKey()) {
        case sceneName_key: setPVar(sceneId.name, aPropValue->stringValue()); return true;
      }
    }
  }
  return false;
}


// MARK: - SceneList


SceneDescriptorPtr SceneList::getSceneByName(const string aSceneName)
{
  SceneDescriptorPtr scene;
  for (int i = 0; i<scenes.size(); ++i) {
    SceneDescriptorPtr sc = scenes[i];
    if (strucmp(sc->sceneId.name.c_str(), aSceneName.c_str())==0) {
      scene = sc;
      break;
    }
  }
  return scene;
}


SceneNo SceneList::getSceneIdByKind(const string aSceneKindName)
{
  const SceneKindDescriptor* skP = roomScenes;
  for (int i=0; i<2; i++) {
    while (skP->no!=INVALID_SCENE_NO) {
      if (strucmp(aSceneKindName.c_str(), skP->actionName)==0) {
        return skP->no;
      }
      skP++;
    }
    // try globals
    skP = globalScenes;
  }
  // try just using integer
  int sceneNo;
  if (sscanf(aSceneKindName.c_str(), "%d", &sceneNo)==1) {
    if (sceneNo>=0 && sceneNo<MAX_SCENE_NO) return sceneNo;
  }
  return INVALID_SCENE_NO;
}



SceneDescriptorPtr SceneList::getScene(const SceneIdentifier &aSceneId, bool aCreateNewIfNotExisting, size_t *aSceneIndexP)
{
  SceneDescriptorPtr scene;
  for (int i = 0; i<scenes.size(); ++i) {
    SceneDescriptorPtr sc = scenes[i];
    if (sc->sceneId.sceneNo==aSceneId.sceneNo && sc->sceneId.zoneID==aSceneId.zoneID && sc->sceneId.group==aSceneId.group) {
      scene = sc;
      if (aSceneIndexP) *aSceneIndexP = i;
      break;
    }
  }
  if (!scene && aCreateNewIfNotExisting && aSceneId.sceneNo<MAX_SCENE_NO) {
    // create new scene descriptor
    scene = SceneDescriptorPtr(new SceneDescriptor);
    scene->sceneId = aSceneId;
    if (scene->sceneId.deriveSceneKind()) {
      scene->markClean(); // not modified yet, no need to save
      if (aSceneIndexP) *aSceneIndexP = scenes.size();
      scenes.push_back(scene);
    }
  }
  return scene;
}


// MARK: - SceneList persistence

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
      // - load record fields into object
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
    if (Error::notOK(err)) LOG(LOG_ERR,"Error saving scene %d: %s", (*pos)->sceneId.sceneNo, err->text());
  }
  return err;
}


// MARK: - SceneList property access implementation

int SceneList::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)scenes.size();
}


static char scenelist_key;

PropertyDescriptorPtr SceneList::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<scenes.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = scenes[aPropIndex]->getStringID();
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
    // writing to non-existing scene -> try to insert new scene
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = aPropMatch;
    descP->createdNew = true;
    descP->propertyType = apivalue_object;
    descP->deletable = true; // new scenes are deletable
    descP->propertyObjectKey = OKEY(scenelist_key);
    size_t si;
    if (getScene(SceneIdentifier(aPropMatch), true, &si)) {
      // valid new scene
      descP->propertyFieldKey = si; // the scene's index
      p = descP;
    }
    else {
      delete descP;
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



// MARK: - Trigger

Trigger::Trigger() :
  inheritedParams(VdcHost::sharedVdcHost()->getDsParamStore()),
  triggerId(0),
  #if ENABLE_P44SCRIPT
  triggerCondition("condition", this, boost::bind(&Trigger::handleTrigger, this, _1), onGettingTrue, Never, expression+keepvars+synchronously+concurrently), // concurrently+keepvars: because action might still be running in this context
  triggerAction(sourcecode+regular, "action", this),
  #else
  triggerCondition(*this, &VdcHost::sharedVdcHost()->geolocation),
  triggerAction(*this, &VdcHost::sharedVdcHost()->geolocation),
  #endif
  conditionMet(undefined)
{
  #if ENABLE_P44SCRIPT
  valueMapper.isMemberVariable();
  triggerContext = triggerCondition.domain()->newContext(); // common context for condition and action
  triggerContext->registerMemberLookup(&valueMapper); // allow context to access the mapped values
  triggerCondition.setSharedMainContext(triggerContext);
  triggerAction.setSharedMainContext(triggerContext);
  #else
  triggerCondition.isMemberVariable();
  triggerCondition.setContextInfo("condition", this);
  triggerAction.isMemberVariable();
  triggerAction.setContextInfo("action", this);
  triggerCondition.setEvaluationResultHandler(boost::bind(&Trigger::triggerEvaluationExecuted, this, _1));
  #endif
}


Trigger::~Trigger()
{
}


string Trigger::logContextPrefix()
{
  return string_format("Trigger '%s'", name.c_str());
}



// MARK: - Trigger condition evaluation

#if ENABLE_EXPRESSIONS

TriggerExpressionContext::TriggerExpressionContext(Trigger &aTrigger, const GeoLocation* aGeoLocationP) :
  inherited(aGeoLocationP),
  trigger(aTrigger)
{
}


bool TriggerExpressionContext::valueLookup(const string &aName, ExpressionValue &aResult)
{
  ExpressionValue res;
  if (trigger.valueMapper.valueLookup(aResult, aName)) return true;
  return inherited::valueLookup(aName, aResult);
}



ErrorPtr Trigger::checkAndFire(EvalMode aEvalMode)
{
  return triggerCondition.triggerEvaluation(aEvalMode);
}


void Trigger::triggerEvaluationExecuted(ExpressionValue aEvaluationResult)
{
  Tristate newState = undefined;
  if (aEvaluationResult.isValue()) {
    newState = aEvaluationResult.boolValue() ? yes : no;
  }
  if (newState!=conditionMet) {
    OLOG(LOG_NOTICE, "condition changes to %s based on values: %s",
      newState==yes ? "TRUE" : (newState==no ? "FALSE" : "undefined"),
      valueMapper.shortDesc().c_str()
    );
    conditionMet = newState;
    if (conditionMet==yes) {
      // a trigger fire is an activity
      LocalController::sharedLocalController()->signalActivity();
      // trigger when state goes from not met to met.
      stopActions(); // abort previous actions
      executeActions(boost::bind(&Trigger::triggerActionExecuted, this, _1));
    }
  }
}


void Trigger::triggerActionExecuted(ExpressionValue aEvaluationResult)
{
  if (aEvaluationResult.isOK()) {
    OLOG(LOG_NOTICE, "actions executed successfully, result: %s", aEvaluationResult.stringValue().c_str());
  }
  else {
    OLOG(LOG_ERR, "actions did not execute successfully: %s", aEvaluationResult.error()->text());
  }
}

void Trigger::reCheckTimed()
{
  varParseTicket.cancel();
  checkAndFire(evalmode_timed);
}

#endif // ENABLE_EXPRESSIONS


#define REPARSE_DELAY (30*Second)

void Trigger::parseVarDefs()
{
  varParseTicket.cancel();
  #if ENABLE_P44SCRIPT
  bool foundall = valueMapper.parseMappingDefs(triggerVarDefs, NULL); // use EventSource/EventSink notification
  #else
  bool foundall = valueMapper.parseMappingDefs(
    triggerVarDefs,
    boost::bind(&Trigger::dependentValueNotification, this, _1, _2)
  );
  #endif
  if (!foundall) {
    // schedule a re-parse later
    varParseTicket.executeOnce(boost::bind(&Trigger::parseVarDefs, this), REPARSE_DELAY);
  }
  else if (LocalController::sharedLocalController()->devicesReady) {
    // do not run checks (and fire triggers too early) before devices are reported initialized
    #if ENABLE_P44SCRIPT
    triggerCondition.compileAndInit();
    #else
    checkAndFire(evalmode_initial);
    #endif
  }
}


void Trigger::processGlobalEvent(VdchostEvent aActivity)
{
  if (aActivity==vdchost_devices_initialized) {
    // good chance we'll get everything resolved now
    parseVarDefs();
  }
  else if (aActivity==vdchost_timeofday_changed) {
    // change in local time
    if (!varParseTicket) {
      // Note: if variable re-parsing is already scheduled, this will re-evaluate anyway
      //   Otherwise: have condition re-evaluated (because it possibly contains references to local time)
      #if ENABLE_P44SCRIPT
      triggerCondition.nextEvaluationNotLaterThan(MainLoop::now()+REPARSE_DELAY);
      #else
      varParseTicket.executeOnce(boost::bind(&Trigger::reCheckTimed, this), REPARSE_DELAY);
      #endif
    }
  }
}


#if ENABLE_P44SCRIPT

// MARK: - Trigger actions execution

void Trigger::handleTrigger(ScriptObjPtr aResult)
{
  // note: is a onGettingTrue trigger, no further result evaluation needed
  OLOG(LOG_NOTICE, "triggers based on values (and maybe timing): %s",
    valueMapper.shortDesc().c_str()
  );
  // launch action (but let trigger evaluation IN SAME CONTEXT actually finish first)
  MainLoop::currentMainLoop().executeNow(boost::bind(&Trigger::executeTriggerAction, this));
}


void Trigger::executeTriggerAction()
{
  // a trigger fire is an activity
  LocalController::sharedLocalController()->signalActivity();
  triggerAction.run(stopall, boost::bind(&Trigger::triggerActionExecuted, this, _1), Infinite);
}


void Trigger::triggerActionExecuted(ScriptObjPtr aResult)
{
  if (!aResult->isErr()) {
    OLOG(LOG_NOTICE, "actions executed successfully, result: %s", aResult->stringValue().c_str());
  }
  else {
    OLOG(LOG_ERR, "actions did not execute successfully: %s", aResult->errorValue()->text());
  }
}


// MARK: - Trigger API method handlers

ErrorPtr Trigger::handleCheckCondition(VdcApiRequestPtr aRequest)
{
  ApiValuePtr checkResult = aRequest->newApiValue();
  checkResult->setType(apivalue_object);
  ApiValuePtr mappingInfo = checkResult->newObject();
  parseVarDefs(); // reparse
  if (valueMapper.getMappedSourcesInfo(mappingInfo)) {
    checkResult->add("varDefs", mappingInfo);
  }
  // Condition
  ApiValuePtr cond = checkResult->newObject();
  ScriptObjPtr res = triggerCondition.run(initial|synchronously, NULL, 2*Second);
  cond->add("expression", checkResult->newString(triggerCondition.getSource().c_str()));
  if (!res->isErr()) {
    cond->add("result", cond->newScriptValue(res));
    cond->add("text", cond->newString(res->defined() ? res->stringValue() : res->getAnnotation()));
    OLOG(LOG_INFO, "condition '%s' -> %s", triggerCondition.getSource().c_str(), res->stringValue().c_str());
  }
  else {
    cond->add("error", cond->newString(res->errorValue()->getErrorMessage()));
    SourceCursor* cursor = res->cursor();
    if (cursor) {
      cond->add("at", cond->newUint64(cursor->textpos()));
      cond->add("line", cond->newUint64(cursor->lineno()));
      cond->add("char", cond->newUint64(cursor->charpos()));
    }
  }
  checkResult->add("condition", cond);
  // return the result
  aRequest->sendResult(checkResult);
  return ErrorPtr();
}


ErrorPtr Trigger::handleTestActions(VdcApiRequestPtr aRequest)
{
  triggerAction.run(stopall, boost::bind(&Trigger::testTriggerActionExecuted, this, aRequest, _1), Infinite);
  return ErrorPtr(); // will send result later
}


void Trigger::testTriggerActionExecuted(VdcApiRequestPtr aRequest, ScriptObjPtr aResult)
{
  ApiValuePtr testResult = aRequest->newApiValue();
  testResult->setType(apivalue_object);
  if (!aResult->isErr()) {
    testResult->add("result", testResult->newScriptValue(aResult));
  }
  else {
    testResult->add("error", testResult->newString(aResult->errorValue()->getErrorMessage()));
    SourceCursor* cursor = aResult->cursor();
    if (cursor) {
      testResult->add("at", testResult->newUint64(cursor->textpos()));
      testResult->add("line", testResult->newUint64(cursor->lineno()));
      testResult->add("char", testResult->newUint64(cursor->charpos()));
    }
  }
  aRequest->sendResult(testResult);
}


void Trigger::stopActions()
{
  triggerContext->abort(stopall, new ErrorValue(ScriptError::Aborted, "trigger action stopped"));
}

#else

// MARK: - Trigger actions execution

TriggerActionContext::TriggerActionContext(Trigger &aTrigger, const GeoLocation* aGeoLocationP) :
  inherited(aGeoLocationP),
  trigger(aTrigger)
{
}


bool TriggerActionContext::abort(bool aDoCallBack)
{
  if (httpAction) {
    httpAction->cancelRequest();
  }
  return inherited::abort(aDoCallBack);
}


bool TriggerActionContext::valueLookup(const string &aName, ExpressionValue &aResult)
{
  ExpressionValue res;
  if (trigger.valueMapper.valueLookup(aResult, aName)) return true;
  return inherited::valueLookup(aName, aResult);
}


bool TriggerActionContext::evaluateAsyncFunction(const string &aFunc, const FunctionArguments &aArgs, bool &aNotYielded)
{
  #if EXPRESSION_SCRIPT_SUPPORT
  if (HttpComm::evaluateAsyncHttpFunctions(this, aFunc, aArgs, aNotYielded, &httpAction)) {
    return true;
  }
  else if (aFunc=="trigger" && aArgs.size()==1) {
    // trigger('triggername')
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], false); // return error from argument
    TriggerPtr targetTrigger = LocalController::sharedLocalController()->localTriggers.getTriggerByName(aArgs[0].stringValue());
    if (!targetTrigger) {
      return throwError(ExpressionError::NotFound, "No trigger named '%s' found", aArgs[0].stringValue().c_str());
    }
    else if (targetTrigger==&trigger) {
      return throwError(ExpressionError::CyclicReference, "Cannot recursively call trigger '%s'", aArgs[0].stringValue().c_str());
    }
    targetTrigger->executeActions(boost::bind(&TriggerActionContext::triggerFuncExecuted, this, _1));
    aNotYielded = false; // yielded to other trigger's action
    return true;
  }
  else if (aFunc=="switchcontext" && aArgs.size()==1) {
    // switchcontext('device_with_output')
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], false); // return error from argument
    DevicePtr device = VdcHost::sharedVdcHost()->getDeviceByNameOrDsUid(aArgs[0].stringValue());
    OutputBehaviourPtr output;
    if (device) output = device->getOutput();
    if (!output) {
      return throwError(ExpressionError::NotFound, "No device with output named '%s' found", aArgs[0].stringValue().c_str());
    }
    // continue execution in a different context
    aNotYielded = chainContext(output->sceneScriptContext, boost::bind(&TriggerActionContext::triggerFuncExecuted, this, _1));
    return true;
  }
  #endif
  return inherited::evaluateAsyncFunction(aFunc, aArgs, aNotYielded);
}


void TriggerActionContext::triggerFuncExecuted(ExpressionValue aEvaluationResult)
{
  continueWithAsyncFunctionResult(aEvaluationResult);
}


bool TriggerActionContext::evaluateFunction(const string &aFunc, const FunctionArguments &aArgs, ExpressionValue &aResult)
{
  if (aFunc=="scene" && (aArgs.size()>=1 || aArgs.size()<=2)) {
    // scene(name)
    // scene(name, transition_time)
    // scene(id, zone)
    // scene(id, zone, transition_time)
    // scene(id, zone, transition_time, group)
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    int ai = 1;
    int zoneid = -1; // none specified
    SceneNo sceneNo = INVALID_SCENE_NO;
    MLMicroSeconds transitionTime = Infinite; // use scene's standard time
    DsGroup group = group_yellow_light; // default to light
    if (aArgs.size()>1 && aArgs[1].isString()) {
      // second param is a zone
      // - ..so first one must be a scene number or name
      sceneNo = LocalController::sharedLocalController()->localScenes.getSceneIdByKind(aArgs[0].stringValue());
      if (sceneNo==INVALID_SCENE_NO) return throwError(ExpressionError::NotFound, "Scene '%s' not found", aArgs[0].stringValue().c_str());
      // - check zone
      ZoneDescriptorPtr zone = LocalController::sharedLocalController()->localZones.getZoneByName(aArgs[1].stringValue());
      if (!zone) return throwError(ExpressionError::NotFound, "Zone '%s' not found", aArgs[1].stringValue().c_str());
      zoneid = zone->getZoneId();
      ai++;
    }
    else {
      // first param is a named scene that includes the zone
      SceneDescriptorPtr scene = LocalController::sharedLocalController()->localScenes.getSceneByName(aArgs[0].stringValue());
      if (!scene) return throwError(ExpressionError::NotFound, "scene '%s' not found", aArgs[0].stringValue().c_str());
      zoneid = scene->getZoneID();
      sceneNo = scene->getSceneNo();
    }
    if (aArgs.size()>ai) {
      if (aArgs[ai].notValue()) return errorInArg(aArgs[ai], aResult); // return error/null from argument
      transitionTime = aArgs[ai].numValue()*Second;
      if (transitionTime<0) transitionTime = Infinite; // use default
      ai++;
      if (aArgs.size()>ai) {
        if (aArgs[ai].notValue()) return errorInArg(aArgs[ai], aResult); // return error/null from argument
        const GroupDescriptor* gdP = LocalController::groupInfoByName(aArgs[ai].stringValue());
        if (!gdP) return throwError(ExpressionError::NotFound, "unknown group '%s'", aArgs[ai].stringValue().c_str());
        group = gdP->no;
      }
    }
    // execute the scene
    LocalController::sharedLocalController()->callScene(sceneNo, zoneid, group, transitionTime);
  }
  else if (aFunc=="set" && (aArgs.size()>=2 || aArgs.size()<=5)) {
    // set(zone_or_device, value)
    // set(zone_or_device, value)
    // set(zone_or_device, value, transitiontime)
    // set(zone_or_device, value, transitiontime, channelid)
    // set(zone, value, transitiontime, channelid, group)
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    if (aArgs[1].notValue()) return errorInArg(aArgs[1], aResult); // return error/null from argument
    double value = aArgs[1].numValue();
    // - optional transitiontime
    MLMicroSeconds transitionTime = Infinite; // use scene's standard time
    if (aArgs.size()>2) {
      if (!aArgs[2].isNull()) {
        if (aArgs[2].notValue()) return errorInArg(aArgs[2], aResult); // return error/null from argument
        transitionTime = aArgs[2].numValue()*Second;
        if (transitionTime<0) transitionTime = Infinite; // use default
      }
    }
    // - optional channelid
    string channelId = "0"; // default channel
    if (aArgs.size()>3) {
      if (!aArgs[3].isNull()) {
        if (aArgs[3].notValue()) return errorInArg(aArgs[3], aResult); // return error/null from argument
        channelId = aArgs[3].stringValue();
      }
    }
    // get zone or device
    if (ZoneDescriptorPtr zone = LocalController::sharedLocalController()->localZones.getZoneByName(aArgs[0].stringValue())) {
      // - might have an optional group argument
      DsGroup group = group_yellow_light; // default to light
      if (aArgs.size()>4) {
        if (!aArgs[4].isOK()) return errorInArg(aArgs[4], aResult); // return error/null from argument
        const GroupDescriptor* gdP = LocalController::groupInfoByName(aArgs[4].stringValue());
        if (!gdP) return throwError(ExpressionError::NotFound, "unknown group '%s'", aArgs[4].stringValue().c_str());
        group = gdP->no;
      }
      LocalController::sharedLocalController()->setOutputChannelValues(zone->getZoneId(), group, channelId, value, transitionTime);
    }
    else if (DevicePtr device = VdcHost::sharedVdcHost()->getDeviceByNameOrDsUid(aArgs[0].stringValue())) {
      if (aArgs.size()>4) { abortWithSyntaxError("group cannot be specified for setting single device's output"); return true; }
      NotificationAudience audience;
      VdcHost::sharedVdcHost()->addTargetToAudience(audience, device);
      LocalController::sharedLocalController()->setOutputChannelValues(audience, channelId, value, transitionTime);
    }
    else {
      return throwError(ExpressionError::NotFound, "no zone or device named '%s' found", aArgs[0].stringValue().c_str());
    }
  }
  else {
    return inherited::evaluateFunction(aFunc, aArgs, aResult);
  }
  return true; // found
}

#define LEGACY_ACTIONS_REWRITING 1

bool Trigger::executeActions(EvaluationResultCB aCallback)
{
  ErrorPtr err;
  #if LEGACY_ACTIONS_REWRITING
  const char *p = triggerAction.getCode();
  while (*p && (*p==' ' || *p=='\t')) p++;
  if (strucmp(p, "scene:", 6)==0 || strucmp(p, "set:", 4)==0) {
    // Legacy Syntax
    //  actions = <action> [ ; <action> [ ; ...]]
    //  action = <cmd>:<params>
    string actionScript;
    string action;
    while (nextPart(p, action, ';')) {
      string cmd;
      string params;
      OLOG(LOG_WARNING, "- start rewriting action '%s'", action.c_str());
      if (!keyAndValue(action, cmd, params, ':')) cmd = action; // could be action only
      // replace @{expr} by expr, which will work unless the expression does not contain ","
      size_t i = 0;
      while ((i = action.find("@{",i))!=string::npos) {
        size_t e = action.find("}",i+2);
        string expr = action.substr(i+2,e==string::npos ? e : e-2-i);
        action.replace(i, e-i+1, expr);
        p+=expr.size();
      }
      if (cmd=="scene") {
        // scene:<name>[,<transitionTime>] -> scene("<name>"[,transitionTime>]);
        actionScript += "scene(";
        const char *p2 = params.c_str();
        string s;
        if (nextPart(p2, s, ',')) {
          actionScript += "\"" + s + "\""; // name
          if (nextPart(p2, s, ',')) {
            actionScript += ", " + s; // transition time
          }
        }
        actionScript += "); ";
      }
      else if (cmd=="set") {
        // set:<zone>,<value>[,<transitionTime>[,<channelid>[,<group>]]]
        actionScript += "set(";
        const char *p2 = params.c_str();
        string s;
        if (nextPart(p2, s, ',')) {
          actionScript += "\"" + s + "\""; // mandatory zone name
          if (nextPart(p2, s, ',')) {
            actionScript += ", " + s; // mandatory output value
            if (nextPart(p2, s, ',')) actionScript += ", " + s; // optional transition time
            if (nextPart(p2, s, ',')) actionScript += ", " + s; // optional channel id
            if (nextPart(p2, s, ',')) actionScript += ", " + s; // optional group
          }
          actionScript += "); ";
        }
      }
    } // while legacy actions
    OLOG(LOG_WARNING, "rewritten legacy actions: '%s' -> '%s'", triggerAction.getCode(), actionScript.c_str());
    triggerAction.setCode(actionScript);
    markDirty();
  }
  #endif // LEGACY_ACTIONS_REWRITING
  // run script
  return triggerAction.execute(true, aCallback);
}


void Trigger::stopActions()
{
  triggerAction.abort(false);
}



// MARK: - Trigger API method handlers


void Trigger::dependentValueNotification(ValueSource &aValueSource, ValueListenerEvent aEvent)
{
  if (aEvent==valueevent_removed) {
    // a value has been removed, update my map
    parseVarDefs();
  }
  else {
    OLOG(LOG_INFO, "value source '%s' reports value %f -> re-evaluating trigger condition", aValueSource.getSourceName().c_str(), aValueSource.getSourceValue());
    checkAndFire(evalmode_externaltrigger);
  }
}



ErrorPtr Trigger::handleCheckCondition(VdcApiRequestPtr aRequest)
{
  ApiValuePtr checkResult = aRequest->newApiValue();
  checkResult->setType(apivalue_object);
  ApiValuePtr mappingInfo = checkResult->newObject();
  parseVarDefs(); // reparse
  if (valueMapper.getMappedSourcesInfo(mappingInfo)) {
    checkResult->add("varDefs", mappingInfo);
  }
  // Condition
  ApiValuePtr cond = checkResult->newObject();
  ExpressionValue res;
  res = triggerCondition.evaluateSynchronously(evalmode_initial);
  cond->add("expression", checkResult->newString(triggerCondition.getCode()));
  if (res.isOK()) {
    cond->add("result", cond->newExpressionValue(res));
    cond->add("text", cond->newString(res.stringValue()));
    OLOG(LOG_INFO, "condition '%s' -> %s", triggerCondition.getCode(), res.stringValue().c_str());
  }
  else {
    cond->add("error", checkResult->newString(res.error()->getErrorMessage()));
    cond->add("at", cond->newUint64(triggerCondition.getPos()));
  }
  checkResult->add("condition", cond);
  // return the result
  aRequest->sendResult(checkResult);
  return ErrorPtr();
}


ErrorPtr Trigger::handleTestActions(VdcApiRequestPtr aRequest)
{
  triggerAction.abort(true); // abort previous ones, calling back (to finish request that possibly has started the script)
  executeActions(boost::bind(&Trigger::testTriggerActionExecuted, this, aRequest, _1));
  return ErrorPtr(); // will send result later
}


void Trigger::testTriggerActionExecuted(VdcApiRequestPtr aRequest, ExpressionValue aEvaluationResult)
{
  ApiValuePtr testResult = aRequest->newApiValue();
  testResult->setType(apivalue_object);
  if (aEvaluationResult.isOK()) {
    testResult->add("result", testResult->newExpressionValue(aEvaluationResult));
  }
  else {
    testResult->add("error", testResult->newString(aEvaluationResult.error()->getErrorMessage()));
    testResult->add("at", testResult->newUint64(triggerAction.getPos()));
  }
  aRequest->sendResult(testResult);
}

#endif // ENABLE_EXPRESSIONS



// MARK: - Trigger persistence

const char *Trigger::tableName()
{
  return "triggers";
}


// primary key field definitions

static const size_t numTriggerKeys = 1;

size_t Trigger::numKeyDefs()
{
  // no parent id, triggers are global
  return numTriggerKeys;
}

const FieldDefinition *Trigger::getKeyDef(size_t aIndex)
{
  static const FieldDefinition keyDefs[numTriggerKeys] = {
    { "triggerId", SQLITE_INTEGER }
  };
  if (aIndex<numTriggerKeys)
    return &keyDefs[aIndex];
  return NULL;
}


// data field definitions

static const size_t numTriggerFields = 4;

size_t Trigger::numFieldDefs()
{
  return inheritedParams::numFieldDefs()+numTriggerFields;
}


const FieldDefinition *Trigger::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numTriggerFields] = {
    { "triggerName", SQLITE_TEXT },
    { "triggerCondition", SQLITE_TEXT },
    { "triggerActions", SQLITE_TEXT }, // note: only historically: triggerActionsS (plural)
    { "triggerVarDefs", SQLITE_TEXT }
  };
  if (aIndex<inheritedParams::numFieldDefs())
    return inheritedParams::getFieldDef(aIndex);
  aIndex -= inheritedParams::numFieldDefs();
  if (aIndex<numTriggerFields)
    return &dataDefs[aIndex];
  return NULL;
}


void Trigger::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inheritedParams::loadFromRowWithoutParentId(aRow, aIndex, aCommonFlagsP);
  // get key fields
  triggerId = aRow->getWithDefault<int>(aIndex++, 0);
  // the fields
  name = nonNullCStr(aRow->get<const char *>(aIndex++));
  #if ENABLE_P44SCRIPT
  triggerCondition.setTriggerSource(nonNullCStr(aRow->get<const char *>(aIndex++)));
  triggerAction.setSource(nonNullCStr(aRow->get<const char *>(aIndex++)));
  #else
  triggerCondition.setCode(nonNullCStr(aRow->get<const char *>(aIndex++)));
  triggerAction.setCode(nonNullCStr(aRow->get<const char *>(aIndex++)));
  #endif
  triggerVarDefs = nonNullCStr(aRow->get<const char *>(aIndex++));
  // initiate evaluation, first vardefs and eventually trigger expression to get timers started
  parseVarDefs();
}


void Trigger::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // - my own id
  aStatement.bind(aIndex++, triggerId);
  // the fields
  aStatement.bind(aIndex++, name.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  #if ENABLE_P44SCRIPT
  aStatement.bind(aIndex++, triggerCondition.getSource().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, triggerAction.getSource().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  #else
  aStatement.bind(aIndex++, triggerCondition.getCode(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, triggerAction.getCode(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  #endif
  aStatement.bind(aIndex++, triggerVarDefs.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
}


// MARK: - Trigger property access implementation

enum {
  triggerName_key,
  triggerCondition_key,
  triggerVarDefs_key,
  triggerAction_key,
  logLevelOffset_key,
  numTriggerProperties
};

static char trigger_key;



int Trigger::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return numTriggerProperties;
}


PropertyDescriptorPtr Trigger::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numTriggerProperties] = {
    { "name", apivalue_string, triggerName_key, OKEY(trigger_key) },
    { "condition", apivalue_string, triggerCondition_key, OKEY(trigger_key) },
    { "varDefs", apivalue_string, triggerVarDefs_key, OKEY(trigger_key) },
    { "action", apivalue_string, triggerAction_key, OKEY(trigger_key) },
    { "logLevelOffset", apivalue_int64, logLevelOffset_key, OKEY(trigger_key) },
  };
  if (aParentDescriptor->isRootOfObject()) {
    // root level property of this object hierarchy
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


bool Trigger::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(trigger_key)) {
    if (aMode==access_read) {
      switch (aPropertyDescriptor->fieldKey()) {
        case triggerName_key: aPropValue->setStringValue(name); return true;
        case triggerVarDefs_key: aPropValue->setStringValue(triggerVarDefs); return true;
        #if ENABLE_P44SCRIPT
        case triggerCondition_key: aPropValue->setStringValue(triggerCondition.getSource().c_str()); return true;
        case triggerAction_key: aPropValue->setStringValue(triggerAction.getSource().c_str()); return true;
        #else
        case triggerCondition_key: aPropValue->setStringValue(triggerCondition.getCode()); return true;
        case triggerAction_key: aPropValue->setStringValue(triggerAction.getCode()); return true;
        #endif
        case logLevelOffset_key: aPropValue->setInt32Value(getLocalLogLevelOffset()); return true;
      }
    }
    else {
      switch (aPropertyDescriptor->fieldKey()) {
        case triggerName_key:
          setPVar(name, aPropValue->stringValue());
          return true;
        case triggerVarDefs_key:
          if (setPVar(triggerVarDefs, aPropValue->stringValue())) {
            parseVarDefs(); // changed variable mappings, re-parse them
          }
          return true;
        #if ENABLE_P44SCRIPT
        case triggerCondition_key:
          if (triggerCondition.setTriggerSource(aPropValue->stringValue(), true)) {
            markDirty();
          }
          return true;
        case triggerAction_key: if (triggerAction.setSource(aPropValue->stringValue())) markDirty(); return true;
        #else
        case triggerCondition_key:
          if (triggerCondition.setCode(aPropValue->stringValue())) {
            markDirty();
            checkAndFire(evalmode_initial);
          }
          return true;
        case triggerAction_key: if (triggerAction.setCode(aPropValue->stringValue())) markDirty(); return true;
        #endif
        case logLevelOffset_key: setLogLevelOffset(aPropValue->int32Value()); return true;
      }
    }
  }
  return false;
}



// MARK: - TriggerList


TriggerPtr TriggerList::getTrigger(int aTriggerId, bool aCreateNewIfNotExisting, size_t *aTriggerIndexP)
{
  TriggerPtr trig;
  int highestId = 0;
  size_t tidx = 0;
  for (tidx=0; tidx<triggers.size(); ++tidx) {
    int tid = triggers[tidx]->triggerId;
    if (aTriggerId!=0 && tid==aTriggerId) {
      // found
      break;
    }
    if (tid>=highestId) highestId = tid;
  }
  if (tidx>=triggers.size() && aCreateNewIfNotExisting) {
    TriggerPtr newTrigger = TriggerPtr(new Trigger);
    newTrigger->triggerId = highestId+1;
    triggers.push_back(newTrigger);
  }
  if (tidx<triggers.size()) {
    trig = triggers[tidx];
    if (aTriggerIndexP) *aTriggerIndexP = tidx;
  }
  return trig;
}


TriggerPtr TriggerList::getTriggerByName(const string aTriggerName)
{
  for (TriggersVector::iterator pos = triggers.begin(); pos!=triggers.end(); ++pos) {
    if (strucmp((*pos)->name.c_str(),aTriggerName.c_str())==0) {
      return *pos;
    }
  }
  return TriggerPtr();
}



// MARK: - TriggerList persistence

ErrorPtr TriggerList::load()
{
  ErrorPtr err;

  // create a template
  TriggerPtr newTrigger = TriggerPtr(new Trigger());
  // get the query
  sqlite3pp::query *queryP = newTrigger->newLoadAllQuery(NULL);
  if (queryP==NULL) {
    // real error preparing query
    err = newTrigger->paramStore.error();
  }
  else {
    for (sqlite3pp::query::iterator row = queryP->begin(); row!=queryP->end(); ++row) {
      // got record
      // - load record fields into object
      int index = 0;
      newTrigger->loadFromRow(row, index, NULL);
      // - put custom action into container
      triggers.push_back(newTrigger);
      // - fresh object for next row
      newTrigger = TriggerPtr(new Trigger);
    }
    delete queryP; queryP = NULL;
  }
  return err;
}


ErrorPtr TriggerList::save()
{
  ErrorPtr err;

  // save all elements (only dirty ones will be actually stored to DB)
  for (TriggersVector::iterator pos = triggers.begin(); pos!=triggers.end(); ++pos) {
    err = (*pos)->saveToStore(NULL, true); // multiple instances allowed, it's a *list*!
    if (Error::notOK(err)) LOG(LOG_ERR,"Error saving trigger %d: %s", (*pos)->triggerId, err->text());
  }
  return err;
}


// MARK: - TriggerList property access implementation

int TriggerList::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)triggers.size();
}


static char triggerlist_key;

PropertyDescriptorPtr TriggerList::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<triggers.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = string_format("%d",triggers[aPropIndex]->triggerId);
    descP->propertyType = apivalue_object;
    descP->deletable = true; // trigger is deletable
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(triggerlist_key);
    return descP;
  }
  return PropertyDescriptorPtr();
}


PropertyDescriptorPtr TriggerList::getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor)
{
  PropertyDescriptorPtr p = inherited::getDescriptorByName(aPropMatch, aStartIndex, aDomain, aMode, aParentDescriptor);
  if (!p && aMode==access_write) {
    // writing to non-existing trigger id (usually 0) -> insert new trigger
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyType = apivalue_object;
    descP->deletable = true; // new scenes are deletable
    descP->propertyObjectKey = OKEY(triggerlist_key);
    size_t ti;
    int newId = 0;
    sscanf(aPropMatch.c_str(), "%d", &newId); // use specified new id, otherwise use 0
    TriggerPtr trg = getTrigger(newId, true, &ti);
    if (trg) {
      // valid new trigger
      descP->propertyFieldKey = ti; // the scene's index
      descP->propertyName = string_format("%d", trg->triggerId);
      descP->createdNew = true;
      p = descP;
    }
    else {
      delete descP;
    }
  }
  return p;
}


bool TriggerList::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(triggerlist_key) && aMode==access_delete) {
    // only field-level access is deleting a zone
    TriggerPtr ds = triggers[aPropertyDescriptor->fieldKey()];
    ds->deleteFromStore(); // remove from store
    triggers.erase(triggers.begin()+aPropertyDescriptor->fieldKey()); // remove from container
    return true;
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


PropertyContainerPtr TriggerList::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(triggerlist_key)) {
    return triggers[aPropertyDescriptor->fieldKey()];
  }
  return NULL;
}


void TriggerList::processGlobalEvent(VdchostEvent aActivity)
{
  for (TriggersVector::iterator pos = triggers.begin(); pos!=triggers.end(); ++pos) {
    (*pos)->processGlobalEvent(aActivity);
  }
}



// MARK: - LocalController

LocalController::LocalController(VdcHost &aVdcHost) :
  vdcHost(aVdcHost),
  devicesReady(false)
{
  localZones.isMemberVariable();
  localScenes.isMemberVariable();
  localTriggers.isMemberVariable();
  #if ENABLE_P44SCRIPT
  StandardScriptingDomain::sharedDomain().registerMemberLookup(LocalControllerLookup::sharedLookup());
  #endif
}


LocalController::~LocalController()
{
}


LocalControllerPtr LocalController::sharedLocalController()
{
  LocalControllerPtr lc = VdcHost::sharedVdcHost()->getLocalController();
  assert(lc); // must exist at this point
  return lc;
}


void LocalController::signalActivity()
{
  vdcHost.signalActivity();
}


void LocalController::processGlobalEvent(VdchostEvent aActivity)
{
  if (aActivity==vdchost_devices_initialized) {
    // from now on, triggers can/should fire
    devicesReady = true;
  }
  if (aActivity>=vdchost_redistributed_events) {
    // only process events that should be redistributed to all objects
    LOG(LOG_INFO, ">>> localcontroller starts processing global event %d", (int)aActivity);
    localTriggers.processGlobalEvent(aActivity);
    LOG(LOG_INFO, ">>> localcontroller done processing event %d", (int)aActivity);
  }
}


bool LocalController::processButtonClick(ButtonBehaviour &aButtonBehaviour, DsClickType aClickType)
{
  LocalController::sharedLocalController()->signalActivity(); // button clicks are activity
  FOCUSLOG("processButtonClick: clicktype=%d, device = %s", (int)aClickType, aButtonBehaviour.shortDesc().c_str());
  // defaults
  DsGroup group = aButtonBehaviour.buttonGroup;
  DsChannelType channelType = channeltype_default;
  DsZoneID zoneID = zoneId_global;
  // possible actions
  bool doDim = false;
  SceneNo sceneToCall = INVALID_SCENE_NO;
  // determine what to do
  VdcDimMode direction = dimmode_stop; // none known
  switch (aButtonBehaviour.buttonMode) {
    case buttonMode_standard:
    case buttonMode_turbo:
      direction = dimmode_stop;
      break;
    case buttonMode_rockerDown_pairWith0:
    case buttonMode_rockerDown_pairWith1:
    case buttonMode_rockerDown_pairWith2:
    case buttonMode_rockerDown_pairWith3:
      direction = dimmode_down;
      break;
    case buttonMode_rockerUp_pairWith0:
    case buttonMode_rockerUp_pairWith1:
    case buttonMode_rockerUp_pairWith2:
    case buttonMode_rockerUp_pairWith3:
      direction = dimmode_up;
      break;
    case buttonMode_inactive:
    default:
      return true; // button inactive or unknown -> NOP, but handled
  }
  // evaluate function
  int area = 0;
  bool global = false;
  SceneNo sceneOffclick = INVALID_SCENE_NO;
  SceneNo scene1click = INVALID_SCENE_NO;
  SceneNo scene2click = INVALID_SCENE_NO;
  SceneNo scene3click = INVALID_SCENE_NO;
  SceneNo scene4click = INVALID_SCENE_NO;
  if (aButtonBehaviour.buttonFunc==buttonFunc_app) {
    return false; // we do not handle app buttons
  }
  else if (group==group_black_variable) {
    switch (aButtonBehaviour.buttonFunc) {
      case buttonFunc_alarm:
        scene1click = ALARM1;
        global = true;
        break;
      case buttonFunc_panic:
        scene1click = PANIC;
        global = true;
        break;
      case buttonFunc_leave:
        scene1click = ABSENT;
        global = true;
        break;
      case buttonFunc_doorbell:
        scene1click = BELL1;
        global = true;
        break;
      default:
        break;
    }
  }
  else {
    switch (aButtonBehaviour.buttonFunc) {
      case buttonFunc_area1_preset0x:
        area = 1;
        scene1click = AREA_1_ON;
        sceneOffclick = AREA_1_OFF;
        goto preset0x;
      case buttonFunc_area2_preset0x:
        area = 2;
        scene1click = AREA_2_ON;
        sceneOffclick = AREA_2_OFF;
        goto preset0x;
      case buttonFunc_area3_preset0x:
        area = 3;
        scene1click = AREA_3_ON;
        sceneOffclick = AREA_3_OFF;
        goto preset0x;
      case buttonFunc_area4_preset0x:
        area = 4;
        scene1click = AREA_4_ON;
        sceneOffclick = AREA_4_OFF;
        goto preset0x;
      case buttonFunc_area1_preset1x:
        area = 1;
        scene1click = AREA_1_ON;
        sceneOffclick = AREA_1_OFF;
        goto preset1x;
      case buttonFunc_area2_preset2x:
        area = 2;
        scene1click = AREA_2_ON;
        sceneOffclick = AREA_2_OFF;
        goto preset2x;
      case buttonFunc_area3_preset3x:
        area = 3;
        scene1click = AREA_3_ON;
        sceneOffclick = AREA_3_OFF;
        goto preset3x;
      case buttonFunc_area4_preset4x:
        area = 4;
        scene1click = AREA_4_ON;
        sceneOffclick = AREA_4_OFF;
        goto preset4x;
      case buttonFunc_room_preset0x:
        scene1click = ROOM_ON;
        sceneOffclick = ROOM_OFF;
      preset0x:
        scene2click = PRESET_2;
        scene3click = PRESET_3;
        scene4click = PRESET_4;
        break;
      case buttonFunc_room_preset1x:
        scene1click = PRESET_11;
        sceneOffclick = ROOM_OFF;
      preset1x:
        scene2click = PRESET_12;
        scene3click = PRESET_13;
        scene4click = PRESET_14;
        break;
      case buttonFunc_room_preset2x:
        scene1click = PRESET_21;
        sceneOffclick = ROOM_OFF;
      preset2x:
        scene2click = PRESET_22;
        scene3click = PRESET_23;
        scene4click = PRESET_24;
        break;
      case buttonFunc_room_preset3x:
        scene1click = PRESET_31;
        sceneOffclick = ROOM_OFF;
      preset3x:
        scene2click = PRESET_32;
        scene3click = PRESET_33;
        scene4click = PRESET_34;
        break;
      case buttonFunc_room_preset4x:
        scene1click = PRESET_41;
        sceneOffclick = ROOM_OFF;
      preset4x:
        scene2click = PRESET_42;
        scene3click = PRESET_43;
        scene4click = PRESET_44;
        break;
      default:
        break;
    }
  }
  if (global) {
    // global scene
    zoneID = zoneId_global;
    group = group_undefined;
    direction = dimmode_up; // always "on"
    switch (aClickType) {
      case ct_tip_1x:
      case ct_click_1x:
        sceneToCall = scene1click;
        break;
      default:
        return true; // unknown click -> ignore, but handled
    }
  }
  else {
    // room scene
    zoneID = aButtonBehaviour.device.getZoneID();
    channelType = aButtonBehaviour.buttonChannel;
    ZoneDescriptorPtr zone = localZones.getZoneById(zoneID, false);
    if (!zone) return false; // button in a non-local zone, cannot handle
    if (group!=group_yellow_light && group!=group_grey_shadow) return true; // NOP because we don't support anything except light and shadow for now, but handled
    // evaluate click
    if (aClickType==ct_hold_start) {
      // start dimming if not off (or if it is specifically the up-key of a rocker)
      if (!zone->zoneState.stateFor(group, area)) {
        // light is currently off
        if (direction==dimmode_up) {
          // holding specific up-key can start dimming even if light was off
          doDim = true;
        }
        else {
          // long press while off, and not specifically up: deep off
          sceneToCall = DEEP_OFF;
        }
      }
      else {
        // light is on, can dim
        doDim = true;
      }
      if (doDim && direction==dimmode_stop) {
        // single button, no explicit direction -> use inverse of last dim
        direction = zone->zoneState.lastDim==dimmode_up ? dimmode_down : dimmode_up;
      }
    }
    else if (aClickType==ct_hold_end) {
      // stop dimming
      direction = dimmode_stop;
      doDim = true;
    }
    else {
      // - not hold or release
      SceneNo sceneOnClick = INVALID_SCENE_NO;
      switch (aClickType) {
        case ct_tip_1x:
        case ct_click_1x:
          sceneOnClick = scene1click;
          break;
        case ct_tip_2x:
        case ct_click_2x:
          sceneOnClick = scene2click;
          direction = dimmode_up;
          break;
        case ct_tip_3x:
        case ct_click_3x:
          sceneOnClick = scene3click;
          direction = dimmode_up;
          break;
        case ct_tip_4x:
          sceneOnClick = scene4click;
          direction = dimmode_up;
          break;
        default:
          return true; // unknown click -> ignore, but handled
      }
      if (direction==dimmode_stop) {
        // single button, no explicit direction
        direction = zone->zoneState.stateFor(group,area) ? dimmode_down : dimmode_up;
      }
      // local
      if (direction==dimmode_up) {
        // calling a preset
        sceneToCall = sceneOnClick;
      }
      else {
        // calling an off scene
        sceneToCall = sceneOffclick;
      }
    }
  }
  // now perform actions
  if (sceneToCall!=INVALID_SCENE_NO) {
    callScene(sceneToCall, zoneID, group);
    return true; // handled
  }
  else if (doDim) {
    // deliver
    NotificationAudience audience;
    vdcHost.addToAudienceByZoneAndGroup(audience, zoneID, group);
    JsonApiValuePtr params = JsonApiValuePtr(new JsonApiValue);
    params->setType(apivalue_object);
    // - define audience
    params->add("zone_id", params->newUint64(zoneID));
    params->add("group", params->newUint64(group));
    string method = "dimChannel";
    params->add("mode", params->newInt64(direction));
    params->add("autostop", params->newBool(false)); // prevent stop dimming event w/o repeating command
    params->add("channel", params->newUint64(channelType));
    params->add("area", params->newUint64(area));
    // - deliver
    vdcHost.deliverToAudience(audience, VdcApiConnectionPtr(), method, params);
    return true; // handled
  }
  else {
    return true; // NOP, but handled
  }
  return false; // not handled so far
}


void LocalController::callScene(SceneIdentifier aScene, MLMicroSeconds aTransitionTimeOverride)
{
  callScene(aScene.sceneNo, aScene.zoneID, aScene.group, aTransitionTimeOverride);
}


void LocalController::callScene(SceneNo aSceneNo, DsZoneID aZone, DsGroup aGroup, MLMicroSeconds aTransitionTimeOverride)
{
  NotificationAudience audience;
  vdcHost.addToAudienceByZoneAndGroup(audience, aZone, aGroup);
  callScene(aSceneNo, audience, aTransitionTimeOverride);
}


void LocalController::callScene(SceneNo aSceneNo, NotificationAudience &aAudience, MLMicroSeconds aTransitionTimeOverride)
{
  JsonApiValuePtr params = JsonApiValuePtr(new JsonApiValue);
  params->setType(apivalue_object);
  // { "notification":"callScene", "zone_id":0, "group":1, "scene":5, "force":false }
  // Note: we don't need the zone/group params, these are defined by the audience already
  string method = "callScene";
  params->add("scene", params->newUint64(aSceneNo));
  params->add("force", params->newBool(false));
  if (aTransitionTimeOverride!=Infinite) {
    params->add("transitionTime", params->newDouble((double)aTransitionTimeOverride/Second));
  }
  // - deliver
  vdcHost.deliverToAudience(aAudience, VdcApiConnectionPtr(), method, params);
}


void LocalController::setOutputChannelValues(DsZoneID aZone, DsGroup aGroup, string aChannelId, double aValue, MLMicroSeconds aTransitionTimeOverride)
{
  NotificationAudience audience;
  vdcHost.addToAudienceByZoneAndGroup(audience, aZone, aGroup);
  setOutputChannelValues(audience, aChannelId, aValue, aTransitionTimeOverride);
}


void LocalController::setOutputChannelValues(NotificationAudience &aAudience, string aChannelId, double aValue, MLMicroSeconds aTransitionTimeOverride)
{
  JsonApiValuePtr params = JsonApiValuePtr(new JsonApiValue);
  params->setType(apivalue_object);
  // { "notification":"setOutputChannelValue", "zone_id":0, "group":1, "value":50, "channelId":"brightness", "transitionTime":20 }
  string method = "setOutputChannelValue";
  params->add("value", params->newDouble(aValue));
  params->add("channelId", params->newString(aChannelId));
  if (aTransitionTimeOverride!=Infinite) {
    params->add("transitionTime", params->newDouble((double)aTransitionTimeOverride/Second));
  }
  // - deliver
  vdcHost.deliverToAudience(aAudience, VdcApiConnectionPtr(), method, params);
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
  ZoneDescriptorPtr deviceZone = localZones.getZoneById(aDevice->getZoneID(), false);
  if (deviceZone) deviceZone->usedByDevice(aDevice, false);
}


void LocalController::deviceChangesZone(DevicePtr aDevice, DsZoneID aFromZone, DsZoneID aToZone)
{
  FOCUSLOG("deviceChangesZone: device = %s, zone %d -> %d", aDevice->shortDesc().c_str(), aFromZone, aToZone);
  if (aFromZone!=aToZone) {
    // - remove from old
    ZoneDescriptorPtr deviceZone = localZones.getZoneById(aFromZone, false);
    if (deviceZone) deviceZone->usedByDevice(aDevice, false);
    // - add to new (and create it in case it is new)
    deviceZone = localZones.getZoneById(aToZone, true);
    deviceZone->usedByDevice(aDevice, true);
  }
}


void LocalController::deviceWillApplyNotification(DevicePtr aDevice, NotificationDeliveryState &aDeliveryState)
{
  ZoneDescriptorPtr zone = localZones.getZoneById(aDevice->getZoneID(), false);
  if (zone && aDevice->getOutput()) {
    DsGroupMask affectedGroups = aDevice->getOutput()->groupMemberships();
    if (aDeliveryState.optimizedType==ntfy_callscene) {
      // scene call
      for (int g = group_undefined; affectedGroups; affectedGroups>>=1, ++g) {
        if (affectedGroups & 1) {
          SceneIdentifier calledScene(aDeliveryState.contentId, zone->getZoneId(), (DsGroup)g);
          // general
          int area = SimpleScene::areaForScene(calledScene.sceneNo);
          if (calledScene.getKindFlags()&scene_off) {
            // is a off scene (area or not), cancels the local priority
            aDevice->getOutput()->setLocalPriority(false);
          }
          else if (area!=0) {
            // is area on scene, set local priority in the device
            aDevice->setLocalPriority(calledScene.sceneNo);
          }
          if (calledScene.getKindFlags()&scene_global) {
            zone->zoneState.lastGlobalScene = calledScene.sceneNo;
          }
          // group specific
          if (g==group_yellow_light) {
            zone->zoneState.lastLightScene = calledScene.sceneNo;
          }
          zone->zoneState.setStateFor(g, area, !(calledScene.getKindFlags()&scene_off));
          if (calledScene.sceneNo==DEEP_OFF) {
            // force areas off as well
            zone->zoneState.setStateFor(g, 1, false);
            zone->zoneState.setStateFor(g, 2, false);
            zone->zoneState.setStateFor(g, 3, false);
            zone->zoneState.setStateFor(g, 4, false);
          }
        }
      }
    }
    else if (aDeliveryState.optimizedType==ntfy_dimchannel) {
      // dimming
      if (aDeliveryState.actionVariant!=dimmode_stop) {
        zone->zoneState.lastDimChannel = (DsChannelType)aDeliveryState.actionParam;
        zone->zoneState.lastDim = (VdcDimMode)aDeliveryState.actionVariant;
      }
    }
    LOG(LOG_INFO,
      "Zone '%s' (%d) state updated: lastLightScene:%d, lastGlobalScene:%d, lightOn=%d/areas1234=%d%d%d%d, shadesOpen=%d/%d%d%d%d",
      zone->getName().c_str(), zone->getZoneId(),
      zone->zoneState.lastLightScene, zone->zoneState.lastGlobalScene,
      zone->zoneState.lightOn[0],
      zone->zoneState.lightOn[1], zone->zoneState.lightOn[2], zone->zoneState.lightOn[3], zone->zoneState.lightOn[4],
      zone->zoneState.shadesOpen[0],
      zone->zoneState.shadesOpen[1], zone->zoneState.shadesOpen[2], zone->zoneState.shadesOpen[3], zone->zoneState.shadesOpen[4]
    );
  }
}




size_t LocalController::totalDevices() const
{
  return vdcHost.dSDevices.size();
}


void LocalController::startRunning()
{
  FOCUSLOG("startRunning");
}



ErrorPtr LocalController::load()
{
  ErrorPtr err;
  err = localZones.load();
  if (Error::notOK(err)) LOG(LOG_ERR, "could not load localZones: %s", err->text());
  err = localScenes.load();
  if (Error::notOK(err)) LOG(LOG_ERR, "could not load localScenes: %s", err->text());
  err = localTriggers.load();
  if (Error::notOK(err)) LOG(LOG_ERR, "could not load localTriggers: %s", err->text());
  return err;
}


ErrorPtr LocalController::save()
{
  ErrorPtr err;
  err = localZones.save();
  if (Error::notOK(err)) LOG(LOG_ERR, "could not save localZones: %s", err->text());
  err = localScenes.save();
  if (Error::notOK(err)) LOG(LOG_ERR, "could not save localScenes: %s", err->text());
  err = localTriggers.save();
  if (Error::notOK(err)) LOG(LOG_ERR, "could not save localTriggers: %s", err->text());
  return err;
}


// MARK: - LocalController specific root (vdchost) level method handling


static const GroupDescriptor groupInfos[] = {
  { group_undefined,               group_global,      "undefined",      0x000000 },
  { group_yellow_light,            group_standard,    "light",          0xFFFF00 },
  { group_grey_shadow,             group_standard,    "shadow",         0x999999 },
  { group_blue_heating,            group_standard,    "heating",        0x0000FF },
  { group_cyan_audio,              group_standard,    "audio",          0x00FFFF },
  { group_magenta_video,           group_standard,    "video",          0xFF00FF },
  { group_red_security,            group_global,      "security",       0xFF0000 },
  { group_green_access,            group_global,      "access",         0x00FF00 },
  { group_black_variable,          group_application, "joker",          0x000000 },
  { group_blue_cooling,            group_standard,    "cooling",        0x0000FF },
  { group_blue_ventilation,        group_standard,    "ventilation",    0x0000FF },
  { group_blue_windows,            group_standard,    "windows",        0x0000FF },
  { group_blue_air_recirculation,  group_controller,  "air recirculation", 0x0000FF },
  { group_roomtemperature_control, group_controller,  "room temperature control", 0x0000FF },
  { group_ventilation_control,     group_controller,  "ventilation control", 0x0000FF },
  { group_undefined,               0 /* terminator */,"" }
};


const GroupDescriptor* LocalController::groupInfo(DsGroup aGroup)
{
  const GroupDescriptor *giP = groupInfos;
  while (giP && giP->kind!=0) {
    if (aGroup==giP->no) {
      return giP;
    }
    giP++;
  }
  return NULL;
}


const GroupDescriptor* LocalController::groupInfoByName(const string aGroupName)
{
  const GroupDescriptor *giP = groupInfos;
  while (giP && giP->kind!=0) {
    if (strucmp(aGroupName.c_str(), giP->name)==0) {
      return giP;
    }
    giP++;
  }
  return NULL;
}



DsGroupMask LocalController::standardRoomGroups(DsGroupMask aGroups)
{
  return aGroups & (
    (1ll<<group_yellow_light) |
    (1ll<<group_grey_shadow) |
    (1ll<<group_blue_heating) |
    (1ll<<group_cyan_audio) |
    (1ll<<group_blue_cooling) |
    (1ll<<group_blue_ventilation)
  );
}


bool LocalController::handleLocalControllerMethod(ErrorPtr &aError, VdcApiRequestPtr aRequest,  const string &aMethod, ApiValuePtr aParams)
{
  if (aMethod=="x-p44-queryScenes") {
    // query scenes usable for a zone/group combination
    ApiValuePtr o;
    aError = DsAddressable::checkParam(aParams, "zoneID", o);
    if (Error::isOK(aError)) {
      DsZoneID zoneID = (DsZoneID)o->uint16Value();
      // get zone
      ZoneDescriptorPtr zone = localZones.getZoneById(zoneID, false);
      if (!zone) {
        aError = WebError::webErr(400, "Zone %d not found (never used, no devices)", (int)zoneID);
      }
      else {
        aError = DsAddressable::checkParam(aParams, "group", o);
        if (Error::isOK(aError) || zoneID==zoneId_global) {
          DsGroup group = zoneID==zoneId_global ? group_undefined : (DsGroup)o->uint16Value();
          // optional scene kind flags
          SceneKind required = scene_preset;
          SceneKind forbidden = scene_extended|scene_area;
          o = aParams->get("required"); if (o) { forbidden = 0; required = o->uint32Value(); } // no auto-exclude when explicitly including
          o = aParams->get("forbidden"); if (o) forbidden = o->uint32Value();
          // query possible scenes for this zone/group
          SceneIdsVector scenes = zone->getZoneScenes(group, required, forbidden);
          // create answer object
          ApiValuePtr result = aRequest->newApiValue();
          result->setType(apivalue_object);
          for (size_t i = 0; i<scenes.size(); ++i) {
            ApiValuePtr s = result->newObject();
            s->add("id", s->newString(scenes[i].stringId()));
            s->add("no", s->newUint64(scenes[i].sceneNo));
            s->add("name", s->newString(scenes[i].getName()));
            s->add("action", s->newString(scenes[i].getActionName()));
            s->add("kind", s->newUint64(scenes[i].getKindFlags()));
            result->add(string_format("%zu", i), s);
          }
          aRequest->sendResult(result);
          aError.reset(); // make sure we don't send an extra ErrorOK
        }
      }
    }
    return true;
  }
  else if (aMethod=="x-p44-queryGroups") {
    // query groups that are in use (in a zone or globally)
    DsGroupMask groups = 0;
    ApiValuePtr o = aParams->get("zoneID");
    if (o) {
      // specific zone
      DsZoneID zoneID = (DsZoneID)o->uint16Value();
      ZoneDescriptorPtr zone = localZones.getZoneById(zoneID, false);
      if (!zone) {
        aError = WebError::webErr(400, "Zone %d not found (never used, no devices)", (int)zoneID);
        return true;
      }
      groups = zone->getZoneGroups();
    }
    else {
      // globally
      for (DsDeviceMap::iterator pos = vdcHost.dSDevices.begin(); pos!=vdcHost.dSDevices.end(); ++pos) {
        OutputBehaviourPtr ob = pos->second->getOutput();
        if (ob) groups |= ob->groupMemberships();
      }
    }
    bool allGroups = false;
    o = aParams->get("all"); if (o) allGroups = o->boolValue();
    if (!allGroups) groups = standardRoomGroups(groups);
    // create answer object
    ApiValuePtr result = aRequest->newApiValue();
    result->setType(apivalue_object);
    for (int i = 0; i<64; ++i) {
      if (groups & (1ll<<i)) {
        const GroupDescriptor* gi = groupInfo((DsGroup)i);
        ApiValuePtr g = result->newObject();
        g->add("name", g->newString(gi ? gi->name : "UNKNOWN"));
        g->add("kind", g->newUint64(gi ? gi->kind : 0));
        g->add("color", g->newString(string_format("#%06X", gi ? gi->hexcolor : 0x999999)));
        result->add(string_format("%d", i), g);
      }
    }
    aRequest->sendResult(result);
    aError.reset(); // make sure we don't send an extra ErrorOK
    return true;
  }
  else if (aMethod=="x-p44-checkTriggerCondition" || aMethod=="x-p44-testTriggerAction" || aMethod=="x-p44-stopTriggerAction") {
    // check the trigger condition of a trigger
    ApiValuePtr o;
    aError = DsAddressable::checkParam(aParams, "triggerID", o);
    if (Error::isOK(aError)) {
      int triggerId = o->int32Value();
      TriggerPtr trig = localTriggers.getTrigger(triggerId);
      if (!trig) {
        aError = WebError::webErr(400, "Trigger %d not found", triggerId);
      }
      else {
        if (aMethod=="x-p44-testTriggerAction") {
          trig->handleTestActions(aRequest); // asynchronous!
        }
        else if (aMethod=="x-p44-stopTriggerAction") {
          trig->stopActions();
          aError = Error::ok();
        }
        else {
          aError = trig->handleCheckCondition(aRequest);
        }
      }
    }
    return true;
  }
  else {
    return false; // unknown at the localController level
  }
}



// MARK: - LocalController property access

enum {
  // singledevice level properties
  zones_key,
  scenes_key,
  triggers_key,
  numLocalControllerProperties
};


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
      case triggers_key:
        return TriggerListPtr(&localTriggers);
    }
  }
  // unknown here
  return inherited::getContainer(aPropertyDescriptor, aDomain);
}


#if ENABLE_P44SCRIPT

using namespace P44Script;

// MARK: - Local controller specific functions

// trigger('triggername')    execute a trigger's action script
static const BuiltInArgDesc trigger_args[] = { { text } };
static const size_t trigger_numargs = sizeof(trigger_args)/sizeof(BuiltInArgDesc);
static void trigger_funcExecuted(BuiltinFunctionContextPtr f, ScriptObjPtr aResult)
{
  f->finish(aResult);
}
static void trigger_func(BuiltinFunctionContextPtr f)
{
  TriggerPtr targetTrigger = LocalController::sharedLocalController()->localTriggers.getTriggerByName(f->arg(0)->stringValue());
  if (!targetTrigger) {
    f->finish(new ErrorValue(ScriptError::NotFound, "No trigger named '%s' found", f->arg(0)->stringValue().c_str()));
    return;
  }
  targetTrigger->triggerAction.run(regular|stopall, boost::bind(&trigger_funcExecuted, f, _1), Infinite);
}



// helper for callScene and saveScene
static bool findScene(int &ai, BuiltinFunctionContextPtr f, SceneNo &sceneNo, int &zoneid)
{
  if (f->numArgs()>1 && f->arg(1)->hasType(text)) {
    // second param is a zone
    // - ..so first one must be a scene number or name
    sceneNo = LocalController::sharedLocalController()->localScenes.getSceneIdByKind(f->arg(0)->stringValue());
    if (sceneNo==INVALID_SCENE_NO) {
      f->finish(new ErrorValue(ScriptError::NotFound, "Scene '%s' not found", f->arg(0)->stringValue().c_str()));
      return false;
    }
    // - check zone
    ZoneDescriptorPtr zone = LocalController::sharedLocalController()->localZones.getZoneByName(f->arg(1)->stringValue());
    if (!zone) {
      f->finish(new ErrorValue(ScriptError::NotFound, "Zone '%s' not found", f->arg(1)->stringValue().c_str()));
      return false;
    }
    zoneid = zone->getZoneId();
    ai++;
  }
  else {
    // first param is a named scene that includes the zone
    SceneDescriptorPtr scene = LocalController::sharedLocalController()->localScenes.getSceneByName(f->arg(0)->stringValue());
    if (!scene) {
      f->finish(new ErrorValue(ScriptError::NotFound, "Scene '%s' not found", f->arg(0)->stringValue().c_str()));
      return false;
    }
    zoneid = scene->getZoneID();
    sceneNo = scene->getSceneNo();
  }
  return true;
}


// scene(name)
// scene(name, transition_time)
// scene(id, zone)
// scene(id, zone, transition_time)
// scene(id, zone, transition_time, group)
static const BuiltInArgDesc scene_args[] = { { text|numeric }, { text|numeric|optionalarg }, { numeric|optionalarg }, { text|numeric|optionalarg } };
static const size_t scene_numargs = sizeof(scene_args)/sizeof(BuiltInArgDesc);
static void scene_func(BuiltinFunctionContextPtr f)
{
  int ai = 1;
  int zoneid = -1; // none specified
  SceneNo sceneNo = INVALID_SCENE_NO;
  MLMicroSeconds transitionTime = Infinite; // use scene's standard time
  DsGroup group = group_yellow_light; // default to light
  if (!findScene(ai, f, sceneNo, zoneid)) return;
  if (f->numArgs()>ai) {
    transitionTime = f->arg(ai)->doubleValue()*Second;
    if (transitionTime<0) transitionTime = Infinite; // use default
    ai++;
    if (f->numArgs()>ai) {
      const GroupDescriptor* gdP = LocalController::groupInfoByName(f->arg(ai)->stringValue());
      if (!gdP) {
        f->finish(new ErrorValue(ScriptError::NotFound, "unknown group '%s'", f->arg(ai)->stringValue().c_str()));
        return;
      }
      group = gdP->no;
    }
  }
  // execute the scene
  LocalController::sharedLocalController()->callScene(sceneNo, zoneid, group, transitionTime);
  f->finish();
}


// savescene(name [, group]])
// savescene(id, zone [, group]])
static const BuiltInArgDesc savescene_args[] = { { text|numeric }, { text|numeric|optionalarg }, { text|numeric|optionalarg } };
static const size_t savescene_numargs = sizeof(savescene_args)/sizeof(BuiltInArgDesc);
static void savescene_func(BuiltinFunctionContextPtr f)
{
  int ai = 1;
  int zoneid = -1; // none specified
  SceneNo sceneNo = INVALID_SCENE_NO;
  DsGroup group = group_yellow_light; // default to light
  if (!findScene(ai, f, sceneNo, zoneid)) return;
  if (f->numArgs()>ai) {
    const GroupDescriptor* gdP = LocalController::groupInfoByName(f->arg(ai)->stringValue());
    if (!gdP) {
      f->finish(new ErrorValue(ScriptError::NotFound, "unknown group '%s'", f->arg(ai)->stringValue().c_str()));
      return;
    }
    group = gdP->no;
  }
  // save the scene
  NotificationAudience audience;
  VdcHost::sharedVdcHost()->addToAudienceByZoneAndGroup(audience, zoneid, group);
  JsonApiValuePtr params = JsonApiValuePtr(new JsonApiValue);
  params->setType(apivalue_object);
  // { "notification":"saveScene", "zone_id":0, "group":1, "scene":5 }
  string method = "saveScene";
  params->add("scene", params->newUint64(sceneNo));
  VdcHost::sharedVdcHost()->deliverToAudience(audience, VdcApiConnectionPtr(), method, params);
  f->finish();
}


// set(zone_or_device, value)
// set(zone_or_device, value)
// set(zone_or_device, value, transitiontime)
// set(zone_or_device, value, transitiontime, channelid)
// set(zone, value, transitiontime, channelid, group)
static const BuiltInArgDesc set_args[] = { { text|numeric }, { numeric }, { numeric|optionalarg }, { text|optionalarg }, { text|numeric|optionalarg } };
static const size_t set_numargs = sizeof(set_args)/sizeof(BuiltInArgDesc);
static void set_func(BuiltinFunctionContextPtr f)
{
  double value = f->arg(1)->doubleValue();
  // - optional transitiontime
  MLMicroSeconds transitionTime = Infinite; // use scene's standard time
  if (f->numArgs()>2 && f->arg(2)->defined()) {
    transitionTime = f->arg(2)->doubleValue()*Second;
    if (transitionTime<0) transitionTime = Infinite; // use default
  }
  // - optional channelid
  string channelId = "0"; // default channel
  if (f->numArgs()>3 &&  f->arg(3)->defined()) {
    channelId = f->arg(3)->stringValue();
  }
  // get zone or device
  if (ZoneDescriptorPtr zone = LocalController::sharedLocalController()->localZones.getZoneByName(f->arg(0)->stringValue())) {
    // - might have an optional group argument
    DsGroup group = group_yellow_light; // default to light
    if (f->numArgs()>4) {
      const GroupDescriptor* gdP = LocalController::groupInfoByName(f->arg(4)->stringValue());
      if (!gdP) {
        f->finish(new ErrorValue(ScriptError::NotFound, "unknown group '%s'", f->arg(4)->stringValue().c_str()));
        return;
      }
      group = gdP->no;
    }
    LocalController::sharedLocalController()->setOutputChannelValues(zone->getZoneId(), group, channelId, value, transitionTime);
  }
  else if (DevicePtr device = VdcHost::sharedVdcHost()->getDeviceByNameOrDsUid(f->arg(0)->stringValue())) {
    if (f->numArgs()>4) {
      f->finish(new ErrorValue(ScriptError::Syntax, "group cannot be specified for setting single device's output"));
      return;
    }
    NotificationAudience audience;
    VdcHost::sharedVdcHost()->addTargetToAudience(audience, device);
    LocalController::sharedLocalController()->setOutputChannelValues(audience, channelId, value, transitionTime);
  }
  else {
    f->finish(new ErrorValue(ScriptError::NotFound, "no zone or device named '%s' found", f->arg(0)->stringValue().c_str()));
    return;
  }
  // success
  f->finish();
}


static const BuiltinMemberDescriptor localControllerFuncs[] = {
  { "trigger", executable|any, trigger_numargs, trigger_args, &trigger_func },
  { "scene", executable|any, scene_numargs, scene_args, &scene_func },
  { "savescene", executable|any, savescene_numargs, savescene_args, &savescene_func },
  { "set", executable|any, set_numargs, set_args, &set_func },
  { NULL } // terminator
};

LocalControllerLookup::LocalControllerLookup() :
  inherited(localControllerFuncs)
{
}

static MemberLookupPtr sharedLocalControllerLookup;

MemberLookupPtr LocalControllerLookup::sharedLookup()
{
  if (!sharedLocalControllerLookup) {
    sharedLocalControllerLookup = new LocalControllerLookup;
    sharedLocalControllerLookup->isMemberVariable(); // disable refcounting
  }
  return sharedLocalControllerLookup;
}

#endif // ENABLE_P44SCRIPT

#endif // ENABLE_LOCALCONTROLLER
