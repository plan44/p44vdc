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

#include "outputbehaviour.hpp"
#include "buttonbehaviour.hpp"


#if ENABLE_LOCALCONTROLLER

using namespace p44;

// MARK: ===== ZoneDescriptor

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


static const SceneKindDescriptor roomScenes[] = {
  { ROOM_OFF, scene_room|scene_preset|scene_off , "off"},
  { ROOM_ON, scene_room|scene_preset, "preset 1" },
  { PRESET_2, scene_room|scene_preset, "preset 2" },
  { PRESET_3, scene_room|scene_preset, "preset 3" },
  { PRESET_4, scene_room|scene_preset, "preset 4" },
  { AREA_1_OFF, scene_room|scene_preset|scene_off|scene_area|scene_extended, "area 1 off" },
  { AREA_1_ON, scene_room|scene_preset|scene_area|scene_extended, "area 1 on" },
  { AREA_2_OFF, scene_room|scene_preset|scene_off|scene_area|scene_extended, "area 2 off" },
  { AREA_2_ON, scene_room|scene_preset|scene_area|scene_extended, "area 2 on" },
  { AREA_3_OFF, scene_room|scene_preset|scene_off|scene_area|scene_extended, "area 3 off" },
  { AREA_3_ON, scene_room|scene_preset|scene_area|scene_extended, "area 3 on" },
  { AREA_4_OFF, scene_room|scene_preset|scene_off|scene_area|scene_extended, "area 4 off" },
  { AREA_4_ON, scene_room|scene_preset|scene_area|scene_extended, "area 4 on" },
  { PRESET_OFF_10, scene_room|scene_preset|scene_off|scene_extended, "off 10" },
  { PRESET_11, scene_room|scene_preset|scene_extended, "preset 11" },
  { PRESET_12, scene_room|scene_preset|scene_extended, "preset 12" },
  { PRESET_13, scene_room|scene_preset|scene_extended, "preset 13" },
  { PRESET_14, scene_room|scene_preset|scene_extended, "preset 14" },
  { PRESET_OFF_20, scene_room|scene_preset|scene_off|scene_extended, "off 20" },
  { PRESET_21, scene_room|scene_preset|scene_extended, "preset 21" },
  { PRESET_22, scene_room|scene_preset|scene_extended, "preset 22" },
  { PRESET_23, scene_room|scene_preset|scene_extended, "preset 23" },
  { PRESET_24, scene_room|scene_preset|scene_extended, "preset 24" },
  { PRESET_OFF_30, scene_room|scene_preset|scene_off|scene_extended, "off 30" },
  { PRESET_31, scene_room|scene_preset|scene_extended, "preset 31" },
  { PRESET_32, scene_room|scene_preset|scene_extended, "preset 32" },
  { PRESET_33, scene_room|scene_preset|scene_extended, "preset 33" },
  { PRESET_34, scene_room|scene_preset|scene_extended, "preset 34" },
  { PRESET_OFF_40, scene_room|scene_preset|scene_off|scene_extended, "off 40" },
  { PRESET_41, scene_room|scene_preset|scene_extended, "preset 41" },
  { PRESET_42, scene_room|scene_preset|scene_extended, "preset 42" },
  { PRESET_43, scene_room|scene_preset|scene_extended, "preset 43" },
  { PRESET_44, scene_room|scene_preset|scene_extended, "preset 44" },
  { INVALID_SCENE_NO, 0, NULL } // terminator
};


static const SceneKindDescriptor globalScenes[] = {
  { AUTO_STANDBY, scene_global, "auto-standby" },
  { STANDBY, scene_global|scene_preset, "standby" },
  { DEEP_OFF, scene_global|scene_preset, "deep off" },
  { SLEEPING, scene_global|scene_preset, "sleeping" },
  { WAKE_UP, scene_global|scene_preset, "wakeup" },
  { PRESENT, scene_global|scene_preset, "present" },
  { ABSENT, scene_global|scene_preset, "absent" },
  { ZONE_ACTIVE, scene_global, "zone active" },
  { BELL1, scene_global|scene_preset, "bell 1" },
  { BELL2, scene_global|scene_preset|scene_extended, "bell 2" },
  { BELL3, scene_global|scene_preset|scene_extended, "bell 3" },
  { BELL4, scene_global|scene_preset|scene_extended, "bell 4" },
  { PANIC, scene_global|scene_preset, "panic" },
  { ALARM1, scene_global, "alarm 1" },
  { ALARM2, scene_global|scene_extended, "alarm 2" },
  { ALARM3, scene_global|scene_extended, "alarm 3" },
  { ALARM4, scene_global|scene_extended, "alarm 4" },
  { FIRE, scene_global, "fire" },
  { SMOKE, scene_global, "smoke" },
  { WATER, scene_global, "water" },
  { GAS, scene_global, "gas" },
  { WIND, scene_global, "wind" },
  { NO_WIND, scene_global, "no wind" },
  { RAIN, scene_global, "rain" },
  { NO_RAIN, scene_global, "no rain" },
  { HAIL, scene_global, "hail" },
  { NO_HAIL, scene_global, "no hail" },
  { POLLUTION, scene_global, "pollution" },
  { INVALID_SCENE_NO, 0 } // terminator
};



DsGroupMask ZoneDescriptor::getZoneGroups(bool aStandardOnly)
{
  DsGroupMask zoneGroups = 0;
  if (zoneID==zoneId_global) {
    return 0; // groups are not relevant in zone0
  }
  else {
    for (DeviceVector::iterator pos = devices.begin(); pos!=devices.end(); ++pos) {
      OutputBehaviourPtr o = (*pos)->getOutput();
      if (o) {
        zoneGroups |= o->groupMemberships();
      }
    }
    if (aStandardOnly) {
      // only use groups with standard room scenes
      zoneGroups &= (
        (1ll<<group_yellow_light) |
        (1ll<<group_grey_shadow) |
        (1ll<<group_blue_heating) |
        (1ll<<group_cyan_audio) |
        (1ll<<group_blue_cooling) |
        (1ll<<group_blue_ventilation)
      );
    }
    return zoneGroups;
  }
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
    if (
      (sceneKindP->kind & aRequiredKinds)==aRequiredKinds &&
      (sceneKindP->kind & aForbiddenKinds)==0
    ) {
      // create identifier for it
      SceneIdentifier si(*sceneKindP, zoneID, aForGroup);
      // look up in user-defined scenes
      SceneDescriptorPtr userscene = LocalController::sharedLocalController()->localScenes.getScene(si);
      if (userscene) {
        si.name = userscene->getSceneName();
      }
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
    zone->zoneName = aZoneId==0 ? "[global]" : string_format("Zone #%d", aZoneId);
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
    // make sure we have a global (appartment) zone
    getZoneById(0);
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
    descP->propertyName = aPropMatch;
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



// MARK: ===== SceneIdentifier

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


// MARK: ===== SceneDescriptor property access implementation

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


// MARK: ===== SceneList


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
    if (!Error::isOK(err)) LOG(LOG_ERR,"Error saving scene %d: %s", (*pos)->sceneId.sceneNo, err->description().c_str());
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
    descP->propertyType = apivalue_object;
    descP->deletable = true; // new scenes are deletable
    descP->propertyObjectKey = OKEY(scenelist_key);
    size_t si;
    if (getScene(SceneIdentifier(aPropMatch), true, &si)) {
      // valid new scene
      descP->propertyFieldKey = si; // the scene's index
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


LocalControllerPtr LocalController::sharedLocalController()
{
  LocalControllerPtr lc = VdcHost::sharedVdcHost()->getLocalController();
  assert(lc); // must exist at this point
  return lc;
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


// MARK: ===== LocalController specific root (vdchost) level method handling


static const GroupDescriptor groupInfos[] = {
  { group_undefined,               group_global,      "undefined" },
  { group_yellow_light,            group_standard,    "light" },
  { group_grey_shadow,             group_standard,    "shadow" },
  { group_blue_heating,            group_standard,    "heating" },
  { group_cyan_audio,              group_standard,    "audio" },
  { group_magenta_video,           group_standard,    "video" },
  { group_red_security,            group_global,      "security" },
  { group_green_access,            group_global,      "access" },
  { group_black_variable,          group_application, "joker" },
  { group_blue_cooling,            group_standard,    "cooling" },
  { group_blue_ventilation,        group_standard,    "ventilation" },
  { group_blue_windows,            group_standard,    "windows" },
  { group_blue_air_recirculation,  group_controller,  "air recirculation" },
  { group_roomtemperature_control, group_controller,  "room temperature control" },
  { group_ventilation_control,     group_controller,  "ventilation control" },
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
    // query groups that can be used in a zone
    ApiValuePtr o;
    aError = DsAddressable::checkParam(aParams, "zoneID", o);
    if (Error::isOK(aError)) {
      DsZoneID zoneID = (DsZoneID)o->uint16Value();
      ZoneDescriptorPtr zone = localZones.getZoneById(zoneID, false);
      if (!zone) {
        aError = WebError::webErr(400, "Zone %d not found (never used, no devices)", (int)zoneID);
      }
      else {
        bool allGroups = false;
        o = aParams->get("all"); if (o) allGroups = o->boolValue();
        DsGroupMask groups = zone->getZoneGroups(!allGroups);
        // create answer object
        ApiValuePtr result = aRequest->newApiValue();
        result->setType(apivalue_object);
        for (int i = 0; i<64; ++i) {
          if (groups & (1ll<<i)) {
            const GroupDescriptor* gi = groupInfo((DsGroup)i);
            ApiValuePtr g = result->newObject();
            g->add("name", g->newString(gi ? gi->name : "UNKNOWN"));
            g->add("kind", g->newUint64(gi ? gi->kind : 0));
            result->add(string_format("%d", i), g);
          }
        }
        aRequest->sendResult(result);
        aError.reset(); // make sure we don't send an extra ErrorOK
      }
    }
    return true;
  }
  else {
    return false; // unknown at the localController level
  }
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
