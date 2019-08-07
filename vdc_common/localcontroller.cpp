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


static const SceneKindDescriptor roomScenes[] = {
  { ROOM_OFF, scene_room|scene_preset|scene_off , "off"},
  { AUTO_OFF, scene_room|scene_preset|scene_off|scene_extended , "slow off"},
  { ROOM_ON, scene_room|scene_preset, "preset 1" },
  { PRESET_2, scene_room|scene_preset, "preset 2" },
  { PRESET_3, scene_room|scene_preset, "preset 3" },
  { PRESET_4, scene_room|scene_preset, "preset 4" },
  { STANDBY, scene_room|scene_preset|scene_off|scene_extended, "standby" },
  { DEEP_OFF, scene_room|scene_preset|scene_off|scene_extended, "deep off" },
  { SLEEPING, scene_room|scene_preset|scene_off|scene_extended, "sleeping" },
  { WAKE_UP, scene_room|scene_preset|scene_extended, "wakeup" },
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
  { ROOM_OFF, scene_global|scene_preset|scene_off|scene_extended , "all off"},
  { ROOM_ON, scene_global|scene_preset|scene_extended, "global preset 1" },
  { PRESET_2, scene_global|scene_preset|scene_extended, "global preset 2" },
  { PRESET_3, scene_global|scene_preset|scene_extended, "global preset 3" },
  { PRESET_4, scene_global|scene_preset|scene_extended, "global preset 4" },
  { AUTO_STANDBY, scene_global, "auto-standby" },
  { STANDBY, scene_global|scene_preset|scene_off, "standby" },
  { DEEP_OFF, scene_global|scene_preset|scene_off, "deep off" },
  { SLEEPING, scene_global|scene_preset|scene_off, "sleeping" },
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
  for (ZonesVector::iterator pos = zones.begin(); pos!=zones.end(); ++pos) {
    if ((*pos)->getName()==aZoneName) {
      return *pos;
    }
  }
  return ZoneDescriptorPtr();
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
    if (sc->sceneId.name==aSceneName) {
      scene = sc;
      break;
    }
  }
  return scene;
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
  triggerCondition(*this, &VdcHost::sharedVdcHost()->geolocation),
  triggerActions(*this, &VdcHost::sharedVdcHost()->geolocation),
  conditionMet(undefined)
{
  triggerCondition.isMemberVariable();
  triggerActions.isMemberVariable();
  triggerCondition.setEvaluationResultHandler(boost::bind(&Trigger::triggerEvaluationResultHandler, this, _1));
}


Trigger::~Trigger()
{
}


// MARK: - Trigger condition evaluation


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


ErrorPtr Trigger::triggerEvaluationResultHandler(ExpressionValue aEvaluationResult)
{
  ErrorPtr err = aEvaluationResult.err;
  Tristate newState = undefined;
  if (aEvaluationResult.isOk()) {
    newState = aEvaluationResult.boolValue() ? yes : no;
  }
  if (newState!=conditionMet) {
    LOG(LOG_NOTICE, "Trigger '%s': condition changes to %s", name.c_str(), newState==yes ? "TRUE" : (newState==no ? "FALSE" : "undefined"));
    conditionMet = newState;
    if (conditionMet==yes) {
      // a trigger fire is an activity
      LocalController::sharedLocalController()->signalActivity();
      // trigger when state goes from not met to met.
      err = executeActions();
      if (Error::isOK(err)) {
        LOG(LOG_NOTICE, "Trigger '%s': actions executed successfully: %s", name.c_str(), triggerActions.getCode());
      }
      else {
        LOG(LOG_ERR, "Trigger '%s': actions did not execute successfully: %s", name.c_str(), err->text());
      }
    }
  }
  return err;
}


#define REPARSE_DELAY (30*Second)

void Trigger::parseVarDefs()
{
  bool foundall = valueMapper.parseMappingDefs(
    triggerVarDefs,
    boost::bind(&Trigger::dependentValueNotification, this, _1, _2)
  );
  if (!foundall) {
    // schedule a re-parse later
    varParseTicket.executeOnce(boost::bind(&Trigger::parseVarDefs, this), REPARSE_DELAY);
  }
  else {
    // run an initial check now that all values are defined
    checkAndFire(evalmode_initial);
  }
}


void Trigger::dependentValueNotification(ValueSource &aValueSource, ValueListenerEvent aEvent)
{
  if (aEvent==valueevent_removed) {
    // a value has been removed, update my map
    parseVarDefs();
  }
  else {
    LOG(LOG_INFO, "Trigger '%s': value source '%s' reports value %f -> re-evaluating trigger condition", name.c_str(), aValueSource.getSourceName().c_str(), aValueSource.getSourceValue());
    checkAndFire(evalmode_externaltrigger);
  }
}


// MARK: - Trigger actions execution


TriggerActionContext::TriggerActionContext(Trigger &aTrigger, const GeoLocation* aGeoLocationP) :
  inherited(aGeoLocationP),
  trigger(aTrigger)
{
}


bool TriggerActionContext::valueLookup(const string &aName, ExpressionValue &aResult)
{
  ExpressionValue res;
  if (trigger.valueMapper.valueLookup(aResult, aName)) return true;
  return inherited::valueLookup(aName, aResult);
}


bool TriggerActionContext::evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, ExpressionValue &aResult)
{
  if (aFunc=="scene" && (aArgs.size()>=1 || aArgs.size()<=2)) {
    // scene(name)
    // scene(name, transition time)
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // return error from argument
    MLMicroSeconds transitionTime = Infinite; // use scene's standard time
    if (aArgs.size()>=2) {
      if (aArgs[1].notOk()) return errorInArg(aArgs[1]); // return error from argument
      transitionTime = aArgs[1].numValue()*Second;
    }
    // execute the scene
    SceneDescriptorPtr scene = LocalController::sharedLocalController()->localScenes.getSceneByName(aArgs[0].stringValue());
    if (!scene)
      abortWithError(ExpressionError::NotFound, "scene '%s' not found", aArgs[0].stringValue().c_str());
    else
      LocalController::sharedLocalController()->callScene(scene->getIdentifier(), transitionTime);
  }
  else if (aFunc=="set" && (aArgs.size()>=2 || aArgs.size()<=5)) {
    // set(zone_or_device, value)
    // set(zone_or_device, value, transitiontime)
    // set(zone_or_device, value, transitiontime, channelid)
    // set(zone, value, transitiontime, channelid, group)
    if (aArgs[0].notOk()) return errorInArg(aArgs[0]); // return error from argument
    if (aArgs[1].notOk()) return errorInArg(aArgs[1]); // return error from argument
    double value = aArgs[1].numValue();
    // - optional transitiontime
    MLMicroSeconds transitionTime = Infinite; // use scene's standard time
    if (aArgs.size()>2) {
      if (!aArgs[2].isNull()) {
        if (aArgs[2].notOk()) return errorInArg(aArgs[2]); // return error from argument
        transitionTime = aArgs[2].numValue()*Second;
      }
    }
    // - optional channelid
    string channelId = "0"; // default channel
    if (aArgs.size()>3) {
      if (!aArgs[3].isNull()) {
        if (aArgs[3].notOk()) return errorInArg(aArgs[3]); // return error from argument
        channelId = aArgs[2].stringValue();
      }
    }
    // get zone or device
    if (ZoneDescriptorPtr zone = LocalController::sharedLocalController()->localZones.getZoneByName(aArgs[0].stringValue())) {
      // - might have an optional group argument
      DsGroup group = group_yellow_light; // default to light
      if (aArgs.size()>4) {
        if (!aArgs[4].valueOk()) return errorInArg(aArgs[4]); // return error from argument
        const GroupDescriptor* gdP = LocalController::groupInfoByName(aArgs[4].stringValue());
        if (!gdP) { abortWithError(ExpressionError::NotFound, "unknown group '%s'", aArgs[4].stringValue().c_str()); return true; }
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
      abortWithError(ExpressionError::NotFound, "no zone or device named '%s' found", aArgs[0].stringValue().c_str());
    }
  }
  else {
    return inherited::evaluateFunction(aFunc, aArgs, aResult);
  }
  return true; // found
}


#if LEGACY_ACTIONS_SUPPORT
bool Trigger::actionExpressionValueLookup(const string aName, ExpressionValue aResult)
{
  if (valueMapper.valueLookup(aResult, aName)) return true;
  return false;
}
#endif // LEGACY_ACTIONS_SUPPORT


ErrorPtr Trigger::executeActions()
{
  ErrorPtr err;
  #if LEGACY_ACTIONS_SUPPORT
  const char *p = triggerActions.getCode();
  while (*p && (*p==' ' || *p=='\t')) p++;
  if (strncasecmp(p, "scene:", 6)==0 || strncasecmp(p, "set:", 4)==0) {
    LOG(LOG_WARNING, "Using legacy action syntax -> please convert to script");
    // Legacy Syntax
    //  actions = <action> [ ; <action> [ ; ...]]
    //  action = <cmd>:<params>
    string action;
    while (nextPart(p, action, ';')) {
      string cmd;
      string params;
      LOG(LOG_INFO, "- starting executing action '%s'", action.c_str());
      if (!keyAndValue(action, cmd, params, ':')) cmd = action; // could be action only
      // substitute params in action
      substituteExpressionPlaceholders(
        params,
        boost::bind(&Trigger::actionExpressionValueLookup, this, _1, _2),
        NULL
      );
      if (cmd=="scene") {
        // scene:<name>[,<transitionTime>]
        const char *p2 = params.c_str();
        string sn;
        if (nextPart(p2, sn, ',')) {
          // scene name
          SceneDescriptorPtr scene = LocalController::sharedLocalController()->localScenes.getSceneByName(sn);
          if (!scene) {
            err = TextError::err("scene '%s' not found", sn.c_str());
          }
          else {
            string ttm;
            MLMicroSeconds transitionTime = Infinite; // use scene's standard time
            if (nextPart(p2, ttm, ',')) {
              double v;
              if (sscanf(ttm.c_str(), "%lf", &v)==1) {
                transitionTime = v*Second;
              }
            }
            // execute the scene
            LocalController::sharedLocalController()->callScene(scene->getIdentifier(), transitionTime);
          }
        }
        else {
          err = TextError::err("scene name missing. Syntax is: scene:<name>[,<transitionTime>]");
          break;
        }
      }
      else if (cmd=="set") {
        // set:<zone>,<value>[,<transitionTime>[,<channelid>[,<group>]]]
        const char *p2 = params.c_str();
        string zn;
        if (nextPart(p2, zn, ',')) {
          // mandatory zone name
          ZoneDescriptorPtr zone = LocalController::sharedLocalController()->localZones.getZoneByName(zn);
          if (!zone) {
            err = TextError::err("zone '%s' not found", zn.c_str());
          }
          else {
            string val;
            if (nextPart(p2, val, ',')) {
              // mandatory output value
              double value;
              if (sscanf(val.c_str(), "%lf", &value)!=1) {
                err = TextError::err("invalid output value");
              }
              else {
                string ttm;
                MLMicroSeconds transitionTime = Infinite; // no override by default, use output's standard transition time
                DsGroup group = group_yellow_light; // default to light
                string channelId = "0"; // default channel
                if (nextPart(p2, ttm, ',')) {
                  // optional transition time
                  double v;
                  if (sscanf(ttm.c_str(), "%lf", &v)==1) {
                    transitionTime = v*Second;
                  }
                  // optional channel id
                  if (nextPart(p2, channelId, ',')) {
                    // optional group
                    string g;
                    if (nextPart(p2, g, ',')) {
                      const GroupDescriptor* gdP = LocalController::groupInfoByName(g);
                      if (gdP) {
                        group = gdP->no;
                      }
                    }
                  }
                }
                // set the ouputs
                LocalController::sharedLocalController()->setOutputChannelValues(zone->getZoneId(), group, channelId, value, transitionTime);
              }
            }
            else {
              err = TextError::err("missing output value");
            }
          }
        }
        else {
          err = TextError::err("zone name missing. Syntax is: set:<zone>,<value>[,<transitionTime>[,<channelid>[,<group>]]]");
          break;
        }
      }
      else {
        err = TextError::err("Action '%s' is unknown", cmd.c_str());
        break;
      }
      LOG(LOG_INFO, "- done executing action '%s'", action.c_str());
    } // while legacy actions
    return err;
  }
  #endif // LEGACY_ACTIONS_SUPPORT
  // run script
  return triggerActions.evaluateSynchronously(evalmode_script).err;
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
  ExpressionValue res;
  res = triggerCondition.evaluateSynchronously(evalmode_initial);
  cond->add("expression", checkResult->newString(triggerCondition.getCode()));
  if (res.isOk()) {
    cond->add("result", res.isString() ? cond->newString(res.stringValue()) : cond->newDouble(res.numValue()));
    LOG(LOG_INFO, "- condition '%s' -> %s", triggerCondition.getCode(), res.stringValue().c_str());
  }
  else {
    cond->add("error", checkResult->newString(res.err->getErrorMessage()));
    if (!res.err->isError(ExpressionError::domain(), ExpressionError::Null)) cond->add("at", cond->newUint64(res.pos));
  }
  checkResult->add("condition", cond);
  // return the result
  aRequest->sendResult(checkResult);
  return ErrorPtr();
}


ErrorPtr Trigger::handleTestActions(VdcApiRequestPtr aRequest)
{
  ApiValuePtr testResult = aRequest->newApiValue();
  testResult->setType(apivalue_object);
  ErrorPtr err = executeActions();
  if (Error::isOK(err)) {
    testResult->add("result", testResult->newString("OK"));
    LOG(LOG_INFO, "- successfully executed '%s'", triggerActions.getCode());
  }
  else {
    testResult->add("error", testResult->newString(err->getErrorMessage()));
  }
  // return the result
  aRequest->sendResult(testResult);
  return ErrorPtr();
}


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
    { "triggerActions", SQLITE_TEXT },
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
  triggerCondition.setCode(nonNullCStr(aRow->get<const char *>(aIndex++)));
  triggerActions.setCode(nonNullCStr(aRow->get<const char *>(aIndex++)));
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
  aStatement.bind(aIndex++, triggerCondition.getCode(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, triggerActions.getCode(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, triggerVarDefs.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
}


// MARK: - Trigger property access implementation

enum {
  triggerName_key,
  triggerCondition_key,
  triggerVarDefs_key,
  triggerActions_key,
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
    { "actions", apivalue_string, triggerActions_key, OKEY(trigger_key) }
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
        case triggerCondition_key: aPropValue->setStringValue(triggerCondition.getCode()); return true;
        case triggerVarDefs_key: aPropValue->setStringValue(triggerVarDefs); return true;
        case triggerActions_key: aPropValue->setStringValue(triggerActions.getCode()); return true;
      }
    }
    else {
      switch (aPropertyDescriptor->fieldKey()) {
        case triggerName_key: setPVar(name, aPropValue->stringValue()); return true;
        case triggerCondition_key:
          if (triggerCondition.setCode(aPropValue->stringValue())) {
            markDirty();
            checkAndFire(evalmode_initial);
          }
          return true;
        case triggerVarDefs_key:
          if (setPVar(triggerVarDefs, aPropValue->stringValue())) {
            parseVarDefs(); // changed variable mappings, re-parse them
          }
          return true;
        case triggerActions_key: if (triggerActions.setCode(aPropValue->stringValue())) markDirty(); return true;
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





// MARK: - LocalController

LocalController::LocalController(VdcHost &aVdcHost) :
  vdcHost(aVdcHost)
{
  localZones.isMemberVariable();
  localScenes.isMemberVariable();
  localTriggers.isMemberVariable();
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
  FOCUSLOG("processGlobalEvent: event = %d", (int)aActivity);
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
  if (group==group_black_variable) {
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
    if (aGroupName==giP->name) {
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
        g->add("color", g->newString(string_format("#%06X", gi->hexcolor)));
        result->add(string_format("%d", i), g);
      }
    }
    aRequest->sendResult(result);
    aError.reset(); // make sure we don't send an extra ErrorOK
    return true;
  }
  else if (aMethod=="x-p44-checkTriggerCondition" || aMethod=="x-p44-testTriggerActions") {
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
        if (aMethod=="x-p44-testTriggerActions") {
          aError = trig->handleTestActions(aRequest);
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




#endif // ENABLE_LOCALCONTROLLER
