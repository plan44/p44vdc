//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2017-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "localcontroller.hpp"

#include "jsonvdcapi.hpp"

#include "outputbehaviour.hpp"
#include "buttonbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "simplescene.hpp"

#include "timeutils.hpp"

#if ENABLE_LOCALCONTROLLER

using namespace p44;


// MARK: - ZoneState

ZoneState::ZoneState() :
  mLastGlobalScene(INVALID_SCENE_NO),
  mLastDim(dimmode_stop),
  mLastLightScene(INVALID_SCENE_NO)
{
  for (SceneArea i=0; i<=num_areas; ++i) {
    mLightOn[i] = false;
    mShadesOpen[i] = false;
  }
}


bool ZoneState::stateFor(int aGroup, int aArea)
{
  switch(aGroup) {
    case group_yellow_light : return mLightOn[aArea];
    case group_grey_shadow : return mShadesOpen[aArea];
    default: return false;
  }
}


void ZoneState::setStateFor(int aGroup, int aArea, bool aState)
{
  switch(aGroup) {
    case group_yellow_light : mLightOn[aArea] = aState;
    case group_grey_shadow : mShadesOpen[aArea] = aState;
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
  if (mZoneID==zoneId_global) return; // global zone always contains all devices, no need to maintain a list
  for (DeviceVector::iterator pos = mDevices.begin(); pos!=mDevices.end(); ++pos) {
    if (*pos==aDevice) {
      if (aInUse) return; // already here -> NOP
      // not in use any more, remove it
      mDevices.erase(pos);
      return;
    }
  }
  // not yet in my list
  if (aInUse) {
    mDevices.push_back(aDevice);
  }
}


DsGroupMask ZoneDescriptor::getZoneGroups()
{
  DsGroupMask zoneGroups = 0;
  if (mZoneID==zoneId_global) return 0; // groups are not relevant in zone0
  for (DeviceVector::iterator pos = mDevices.begin(); pos!=mDevices.end(); ++pos) {
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
  if (mZoneID==zoneId_global) {
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
    SceneIdentifier si(*sceneKindP, mZoneID, aForGroup);
    SceneKind k = sceneKindP->kind;
    // look up in user-defined scenes
    SceneDescriptorPtr userscene = LocalController::sharedLocalController()->mLocalScenes.getScene(si);
    SceneKind forbiddenKinds = aForbiddenKinds;
    if (userscene) {
      si.mName = userscene->getSceneName();
      if (!si.mName.empty()) {
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
  if (mZoneID==zoneId_global) {
    return LocalController::sharedLocalController()->totalDevices();
  }
  else {
    return mDevices.size();
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
  mZoneID = aRow->getWithDefault(aIndex++, 0);
  // the name
  mZoneName = nonNullCStr(aRow->get<const char *>(aIndex++));
}


void ZoneDescriptor::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // - my own id
  aStatement.bind(aIndex++, mZoneID);
  // - title
  aStatement.bind(aIndex++, mZoneName.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
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


PropertyContainerPtr ZoneDescriptor::getContainer(const PropertyDescriptorPtr aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->isArrayContainer()) {
    // local container (e.g. all devices of this zone)
    return PropertyContainerPtr(this); // handle myself
  }
  else if (aPropertyDescriptor->hasObjectKey(zonedevice_key)) {
    // - get device
    PropertyContainerPtr container = mDevices[aPropertyDescriptor->fieldKey()];
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



void ZoneDescriptor::prepareAccess(PropertyAccessMode aMode, PropertyPrep& aPrepInfo, StatusCB aPreparedCB)
{
  if (aPrepInfo.mDescriptor->hasObjectKey(zonedevices_container_key) && mZoneID==zoneId_global) {
    // for global zone: create temporary list of all devices
    LocalController::sharedLocalController()->mVdcHost.createDeviceList(mDevices);
  }
  // in any case: let inherited handle the callback
  inherited::prepareAccess(aMode, aPrepInfo, aPreparedCB);
}


void ZoneDescriptor::finishAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(zonedevices_container_key) && mZoneID==zoneId_global) {
    // list is only temporary
    mDevices.clear();
  }
}



bool ZoneDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(zonedescriptor_key)) {
    if (aMode==access_read) {
      switch (aPropertyDescriptor->fieldKey()) {
        case zoneName_key: aPropValue->setStringValue(mZoneName); return true;
        case deviceCount_key: aPropValue->setUint64Value(devicesInZone()); return true;
      }
    }
    else {
      switch (aPropertyDescriptor->fieldKey()) {
        case zoneName_key: setPVar(mZoneName, aPropValue->stringValue()); return true;
      }
    }
  }
  return false;
}


// MARK: - ZoneList


ZoneDescriptorPtr ZoneList::getZoneById(DsZoneID aZoneId, bool aCreateNewIfNotExisting)
{
  ZoneDescriptorPtr zone;
  for (ZonesVector::iterator pos = mZones.begin(); pos!=mZones.end(); ++pos) {
    if ((*pos)->mZoneID==aZoneId) {
      zone = *pos;
      break;
    }
  }
  if (!zone && aCreateNewIfNotExisting) {
    // create new zone descriptor on the fly
    zone = ZoneDescriptorPtr(new ZoneDescriptor);
    zone->mZoneID = aZoneId;
    zone->mZoneName = aZoneId==0 ? "[global]" : string_format("Zone #%d", aZoneId);
    zone->markClean(); // not modified yet, no need to save
    mZones.push_back(zone);
  }
  return zone;
}


ZoneDescriptorPtr ZoneList::getZoneByName(const string aZoneName)
{
  int numName = -1;
  sscanf(aZoneName.c_str(), "%d", &numName);
  for (ZonesVector::iterator pos = mZones.begin(); pos!=mZones.end(); ++pos) {
    if (uequals((*pos)->getName().c_str(),aZoneName.c_str())) {
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
    err = newZone->mParamStore.error();
  }
  else {
    for (sqlite3pp::query::iterator row = queryP->begin(); row!=queryP->end(); ++row) {
      // got record
      // - load record fields into object
      int index = 0;
      newZone->loadFromRow(row, index, NULL);
      // - put custom action into container
      mZones.push_back(newZone);
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
  for (ZonesVector::iterator pos = mZones.begin(); pos!=mZones.end(); ++pos) {
    err = (*pos)->saveToStore(NULL, true); // multiple instances allowed, it's a *list*!
    if (Error::notOK(err)) LOG(LOG_ERR,"Error saving zone %d: %s", (*pos)->mZoneID, err->text());
  }
  return err;
}


// MARK: - ZoneList property access implementation

int ZoneList::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)mZones.size();
}


static char zonelist_key;

PropertyDescriptorPtr ZoneList::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<mZones.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->mPropertyName = string_format("%hu", mZones[aPropIndex]->mZoneID);
    descP->mPropertyType = apivalue_object;
    descP->mDeletable = mZones[aPropIndex]->mDevices.size()==0; // zone is deletable when no device uses it
    descP->mPropertyFieldKey = aPropIndex;
    descP->mPropertyObjectKey = OKEY(zonelist_key);
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
    descP->mPropertyType = apivalue_object;
    descP->mDeletable = true; // new zones are deletable
    descP->mPropertyFieldKey = mZones.size(); // new zone will be appended, so index is current size
    descP->mPropertyObjectKey = OKEY(zonelist_key);
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
    descP->mPropertyName = string_format("%hu", newId);
    descP->mCreatedNew = true;
    p = descP;
  }
  return p;
}


bool ZoneList::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(zonelist_key) && aMode==access_delete) {
    // only field-level access is deleting a zone
    ZoneDescriptorPtr dz = mZones[aPropertyDescriptor->fieldKey()];
    dz->deleteFromStore(); // remove from store
    mZones.erase(mZones.begin()+aPropertyDescriptor->fieldKey()); // remove from container
    return true;
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


PropertyContainerPtr ZoneList::getContainer(const PropertyDescriptorPtr aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(zonelist_key)) {
    return mZones[aPropertyDescriptor->fieldKey()];
  }
  return NULL;
}



// MARK: - SceneIdentifier

SceneIdentifier::SceneIdentifier()
{
  mSceneKindP = NULL;
  mSceneNo = INVALID_SCENE_NO;
  mZoneID = scene_global;
  mGroup = group_undefined;
}


SceneIdentifier::SceneIdentifier(const SceneKindDescriptor &aSceneKind, DsZoneID aZone, DsGroup aGroup)
{
  mSceneKindP = &aSceneKind;
  mSceneNo = mSceneKindP->no;
  mZoneID = aZone;
  mGroup = aGroup;
}


SceneIdentifier::SceneIdentifier(SceneNo aNo, DsZoneID aZone, DsGroup aGroup)
{
  mSceneNo = aNo;
  mZoneID = aZone;
  mGroup = aGroup;
  deriveSceneKind();
}


SceneIdentifier::SceneIdentifier(const string aStringId)
{
  uint16_t tmpSceneNo = INVALID_SCENE_NO;
  uint16_t tmpZoneID = scene_global;
  uint16_t tmpGroup = group_undefined;
  sscanf(aStringId.c_str(), "%hu_%hu_%hu", &tmpSceneNo, &tmpZoneID, &tmpGroup);
  mSceneNo = tmpSceneNo;
  mZoneID = tmpZoneID;
  mGroup = (DsGroup)tmpGroup;
  deriveSceneKind();
}


string SceneIdentifier::stringId() const
{
  return string_format("%hu_%hu_%hu", (uint16_t)mSceneNo, (uint16_t)mZoneID, (uint16_t)mGroup);
}



string SceneIdentifier::getActionName() const
{
  return mSceneKindP ? mSceneKindP->actionName : string_format("scene %d", mSceneNo);
}


string SceneIdentifier::getName() const
{
  return mName;
}




bool SceneIdentifier::deriveSceneKind()
{
  const SceneKindDescriptor *sk = mSceneNo>=START_APARTMENT_SCENES ? globalScenes : roomScenes;
  while (sk->no<NUM_VALID_SCENES) {
    if (sk->no==mSceneNo) {
      mSceneKindP = sk;
      return true;
    }
    sk++;
  }
  mSceneKindP = NULL; // unknown
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
  mSceneIdentifier.mSceneNo = aRow->getCastedWithDefault<SceneNo, int>(aIndex++, 0);
  mSceneIdentifier.mZoneID = aRow->getCastedWithDefault<DsZoneID, int>(aIndex++, 0);
  mSceneIdentifier.mGroup = aRow->getCastedWithDefault<DsGroup, int>(aIndex++, group_undefined);
  mSceneIdentifier.deriveSceneKind();
  // the name
  mSceneIdentifier.mName = nonNullCStr(aRow->get<const char *>(aIndex++));
}


void SceneDescriptor::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // - my own id
  aStatement.bind(aIndex++, mSceneIdentifier.mSceneNo);
  aStatement.bind(aIndex++, mSceneIdentifier.mZoneID);
  aStatement.bind(aIndex++, mSceneIdentifier.mGroup);
  // - title
  aStatement.bind(aIndex++, mSceneIdentifier.mName.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
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
        case sceneZoneID_key: aPropValue->setUint16Value(mSceneIdentifier.mZoneID); return true;
        case sceneGroup_key: aPropValue->setUint8Value(mSceneIdentifier.mGroup); return true;
      }
    }
    else {
      switch (aPropertyDescriptor->fieldKey()) {
        case sceneName_key: setPVar(mSceneIdentifier.mName, aPropValue->stringValue()); return true;
      }
    }
  }
  return false;
}


// MARK: - SceneList


SceneDescriptorPtr SceneList::getSceneByName(const string aSceneName)
{
  SceneDescriptorPtr scene;
  for (int i = 0; i<mScenes.size(); ++i) {
    SceneDescriptorPtr sc = mScenes[i];
    if (uequals(sc->mSceneIdentifier.mName.c_str(), aSceneName.c_str())) {
      scene = sc;
      break;
    }
  }
  return scene;
}


SceneDescriptorPtr SceneList::getScene(const SceneIdentifier &aSceneId, bool aCreateNewIfNotExisting, size_t *aSceneIndexP)
{
  SceneDescriptorPtr scene;
  for (int i = 0; i<mScenes.size(); ++i) {
    SceneDescriptorPtr sc = mScenes[i];
    if (sc->mSceneIdentifier.mSceneNo==aSceneId.mSceneNo && sc->mSceneIdentifier.mZoneID==aSceneId.mZoneID && sc->mSceneIdentifier.mGroup==aSceneId.mGroup) {
      scene = sc;
      if (aSceneIndexP) *aSceneIndexP = i;
      break;
    }
  }
  if (!scene && aCreateNewIfNotExisting && aSceneId.mSceneNo<NUM_VALID_SCENES) {
    // create new scene descriptor
    scene = SceneDescriptorPtr(new SceneDescriptor);
    scene->mSceneIdentifier = aSceneId;
    if (scene->mSceneIdentifier.deriveSceneKind()) {
      scene->markClean(); // not modified yet, no need to save
      if (aSceneIndexP) *aSceneIndexP = mScenes.size();
      mScenes.push_back(scene);
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
    err = newScene->mParamStore.error();
  }
  else {
    for (sqlite3pp::query::iterator row = queryP->begin(); row!=queryP->end(); ++row) {
      // got record
      // - load record fields into object
      int index = 0;
      newScene->loadFromRow(row, index, NULL);
      // - put custom action into container
      mScenes.push_back(newScene);
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
  for (ScenesVector::iterator pos = mScenes.begin(); pos!=mScenes.end(); ++pos) {
    err = (*pos)->saveToStore(NULL, true); // multiple instances allowed, it's a *list*!
    if (Error::notOK(err)) LOG(LOG_ERR,"Error saving scene %d: %s", (*pos)->mSceneIdentifier.mSceneNo, err->text());
  }
  return err;
}


// MARK: - SceneList property access implementation

int SceneList::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)mScenes.size();
}


static char scenelist_key;

PropertyDescriptorPtr SceneList::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<mScenes.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->mPropertyName = mScenes[aPropIndex]->getStringID();
    descP->mPropertyType = apivalue_object;
    descP->mDeletable = true; // scene is deletable
    descP->mPropertyFieldKey = aPropIndex;
    descP->mPropertyObjectKey = OKEY(scenelist_key);
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
    descP->mPropertyName = aPropMatch;
    descP->mCreatedNew = true;
    descP->mPropertyType = apivalue_object;
    descP->mDeletable = true; // new scenes are deletable
    descP->mPropertyObjectKey = OKEY(scenelist_key);
    size_t si;
    if (getScene(SceneIdentifier(aPropMatch), true, &si)) {
      // valid new scene
      descP->mPropertyFieldKey = si; // the scene's index
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
    SceneDescriptorPtr ds = mScenes[aPropertyDescriptor->fieldKey()];
    ds->deleteFromStore(); // remove from store
    mScenes.erase(mScenes.begin()+aPropertyDescriptor->fieldKey()); // remove from container
    return true;
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


PropertyContainerPtr SceneList::getContainer(const PropertyDescriptorPtr aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(scenelist_key)) {
    return mScenes[aPropertyDescriptor->fieldKey()];
  }
  return NULL;
}



// MARK: - Trigger

Trigger::Trigger() :
  inheritedParams(VdcHost::sharedVdcHost()->getDsParamStore()),
  mTriggerId(0),
  mTriggerCondition("condition", nullptr, this, boost::bind(&Trigger::handleTrigger, this, _1), onGettingTrue, Never, expression+keepvars+synchronously+concurrently), // concurrently+keepvars: because action might still be running in this context
  mTriggerAction(sourcecode|regular, "action", "%C (trigger action)", this),
  mConditionMet(undefined)
{
  mValueMapper.isMemberVariable();
  mTriggerContext = mTriggerCondition.domain()->newContext(); // common context for condition and action
  mTriggerContext->registerMemberLookup(&mValueMapper); // allow context to access the mapped values
  mTriggerCondition.setSharedMainContext(mTriggerContext);
  mTriggerAction.setSharedMainContext(mTriggerContext);
  // Note: sourceCodeUid will be set when mTriggerId gets defined
}


Trigger::~Trigger()
{
}


// MARK: - Trigger condition evaluation

#define REPARSE_DELAY (30*Second)

void Trigger::parseVarDefs()
{
  mVarParseTicket.cancel();
  bool foundall = mValueMapper.parseMappingDefs(mTriggerVarDefs, NULL); // use EventSource/EventSink notification
  if (!foundall) {
    // schedule a re-parse later
    mVarParseTicket.executeOnce(boost::bind(&Trigger::parseVarDefs, this), REPARSE_DELAY);
  }
  else if (LocalController::sharedLocalController()->mDevicesReady) {
    // do not run checks (and fire triggers too early) before devices are reported initialized
    mTriggerCondition.compileAndInit();
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
    if (!mVarParseTicket) {
      // Note: if variable re-parsing is already scheduled, this will re-evaluate anyway
      //   Otherwise: have condition re-evaluated (because it possibly contains references to local time)
      mTriggerCondition.nextEvaluationNotLaterThan(MainLoop::now()+REPARSE_DELAY);
    }
  }
}


// MARK: - Trigger actions execution

void Trigger::handleTrigger(ScriptObjPtr aResult)
{
  // note: is a onGettingTrue trigger, no further result evaluation needed
  OLOG(LOG_NOTICE, "triggers based on values (and maybe timing): %s",
    mValueMapper.shortDesc().c_str()
  );
  // launch action (but let trigger evaluation IN SAME CONTEXT actually finish first)
  MainLoop::currentMainLoop().executeNow(boost::bind(&Trigger::executeTriggerAction, this));
}


void Trigger::executeTriggerAction()
{
  // a trigger fire is an activity
  LocalController::sharedLocalController()->signalActivity();
  mTriggerAction.runCommand(restart, boost::bind(&Trigger::triggerActionExecuted, this, _1));
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
  if (mValueMapper.getMappedSourcesInfo(mappingInfo)) {
    checkResult->add("varDefs", mappingInfo);
  }
  // Condition
  ApiValuePtr cond = checkResult->newObject();
  ScriptObjPtr res = mTriggerCondition.run(initial|synchronously, NoOP, ScriptObjPtr(), 2*Second);
  cond->add("expression", checkResult->newString(mTriggerCondition.getSource().c_str()));
  if (!res->isErr()) {
    cond->add("result", cond->newScriptValue(res));
    cond->add("text", cond->newString(res->defined() ? res->stringValue() : res->getAnnotation()));
    OLOG(LOG_INFO, "condition '%s' -> %s", mTriggerCondition.getSource().c_str(), res->stringValue().c_str());
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


ErrorPtr Trigger::handleTestActions(VdcApiRequestPtr aRequest, ScriptObjPtr aTriggerParam)
{
  ScriptObjPtr threadLocals;
  if (aTriggerParam) {
    threadLocals = new SimpleVarContainer();
    threadLocals->setMemberByName("triggerparam", aTriggerParam);
  }
  mTriggerAction.runCommand(restart, boost::bind(&Trigger::testTriggerActionExecuted, this, aRequest, _1), threadLocals);
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
  mTriggerContext->abort(stopall, new ErrorValue(ScriptError::Aborted, "trigger action stopped"));
}


void Trigger::setTriggerId(int aTriggerId)
{
  if (mTriggerId==0) {
    // unset so far
    mTriggerId = aTriggerId;
    // update dependent trigger script IDs
    string uidbase = string_format("trigger_%d.", mTriggerId);
    mTriggerCondition.setScriptHostUid(uidbase+"condition");
    mTriggerAction.setScriptHostUid(uidbase+"action");
  }
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

static const size_t numTriggerFields = 7;

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
    { "triggerVarDefs", SQLITE_TEXT },
    { "triggerMode", SQLITE_INTEGER },
    { "triggerHoldoffTime", SQLITE_INTEGER },
    { "uiparams", SQLITE_TEXT }, // usually JSON info used to render this trigger in WebUI
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
  setTriggerId(aRow->getWithDefault<int>(aIndex++, 0));
  // the fields
  mName = nonNullCStr(aRow->get<const char *>(aIndex++));
  mTriggerCondition.setTriggerSource(nonNullCStr(aRow->get<const char *>(aIndex++)));
  mTriggerAction.loadSource(nonNullCStr(aRow->get<const char *>(aIndex++)));
  mTriggerVarDefs = nonNullCStr(aRow->get<const char *>(aIndex++));
  mTriggerCondition.setTriggerMode(aRow->getCastedWithDefault<TriggerMode, int>(aIndex++, onGettingTrue), false); // do not initialize at load yet
  mTriggerCondition.setTriggerHoldoff(aRow->getCastedWithDefault<MLMicroSeconds, long long int>(aIndex++, 0), false); // do not initialize at load yet
  mUiParams = nonNullCStr(aRow->get<const char *>(aIndex++));
  // initiate evaluation, first vardefs and eventually trigger expression to get timers started
  parseVarDefs();
}


void Trigger::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // - my own id
  aStatement.bind(aIndex++, mTriggerId);
  // the fields
  aStatement.bind(aIndex++, mName.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, mTriggerCondition.getSource().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, mTriggerAction.getSourceToStoreLocally().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, mTriggerVarDefs.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, (int)mTriggerCondition.getTriggerMode());
  aStatement.bind(aIndex++, (long long int)mTriggerCondition.getTriggerHoldoff());
  aStatement.bind(aIndex++, mUiParams.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
}


// MARK: - Trigger property access implementation

enum {
  triggerName_key,
  uiParams_key,
  triggerCondition_key,
  triggerMode_key,
  triggerHoldOff_key,
  triggerVarDefs_key,
  triggerAction_key,
  triggerActionId_key,
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
    { "uiparams", apivalue_string, uiParams_key, OKEY(trigger_key) },
    { "condition", apivalue_string, triggerCondition_key, OKEY(trigger_key) },
    { "mode", apivalue_int64, triggerMode_key, OKEY(trigger_key) },
    { "holdofftime", apivalue_double, triggerHoldOff_key, OKEY(trigger_key) },
    { "varDefs", apivalue_string, triggerVarDefs_key, OKEY(trigger_key) },
    { "action", apivalue_string, triggerAction_key, OKEY(trigger_key) },
    { "actionId", apivalue_string, triggerActionId_key, OKEY(trigger_key) },
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
        case triggerName_key: aPropValue->setStringValue(mName); return true;
        case uiParams_key: aPropValue->setStringValue(mUiParams); return true;
        case triggerVarDefs_key: aPropValue->setStringValue(mTriggerVarDefs); return true;
        case triggerCondition_key: aPropValue->setStringValue(mTriggerCondition.getSource().c_str()); return true;
        case triggerMode_key: aPropValue->setInt32Value(mTriggerCondition.getTriggerMode()); return true;
        case triggerHoldOff_key: aPropValue->setDoubleValue((double)mTriggerCondition.getTriggerHoldoff()/Second); return true;
        case triggerAction_key: aPropValue->setStringValue(mTriggerAction.getSource()); return true;
        case triggerActionId_key: aPropValue->setStringValue(mTriggerAction.getSourceUid()); return true;
        case logLevelOffset_key: aPropValue->setInt32Value(getLocalLogLevelOffset()); return true;
      }
    }
    else {
      switch (aPropertyDescriptor->fieldKey()) {
        case triggerName_key:
          setPVar(mName, aPropValue->stringValue());
          return true;
        case uiParams_key:
          setPVar(mUiParams, aPropValue->stringValue());
          return true;
        case triggerVarDefs_key:
          if (setPVar(mTriggerVarDefs, aPropValue->stringValue())) {
            parseVarDefs(); // changed variable mappings, re-parse them
          }
          return true;
        case triggerCondition_key:
          if (mTriggerCondition.setTriggerSource(aPropValue->stringValue(), true)) {
            markDirty();
          }
          return true;
        case triggerMode_key:
          if (mTriggerCondition.setTriggerMode(TriggerMode(aPropValue->int32Value()), true)) {
            markDirty();
          }
          return true;
        case triggerHoldOff_key:
          if (mTriggerCondition.setTriggerHoldoff((MLMicroSeconds)(aPropValue->doubleValue()*Second), true)) {
            markDirty();
          }
          return true;
        case triggerAction_key:
          if (mTriggerAction.setAndStoreSource(aPropValue->stringValue())) {
            markDirty();
          }
          return true;
        case logLevelOffset_key:
          setLogLevelOffset(aPropValue->int32Value());
          return true;
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
  for (tidx=0; tidx<mTriggers.size(); ++tidx) {
    int tid = mTriggers[tidx]->mTriggerId;
    if (aTriggerId!=0 && tid==aTriggerId) {
      // found
      break;
    }
    if (tid>=highestId) highestId = tid;
  }
  if (tidx>=mTriggers.size() && aCreateNewIfNotExisting) {
    TriggerPtr newTrigger = TriggerPtr(new Trigger);
    newTrigger->setTriggerId(highestId+1);
    mTriggers.push_back(newTrigger);
  }
  if (tidx<mTriggers.size()) {
    trig = mTriggers[tidx];
    if (aTriggerIndexP) *aTriggerIndexP = tidx;
  }
  return trig;
}


TriggerPtr TriggerList::getTriggerByName(const string aTriggerName)
{
  for (TriggersVector::iterator pos = mTriggers.begin(); pos!=mTriggers.end(); ++pos) {
    if (uequals((*pos)->mName.c_str(),aTriggerName.c_str())) {
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
    err = newTrigger->mParamStore.error();
  }
  else {
    for (sqlite3pp::query::iterator row = queryP->begin(); row!=queryP->end(); ++row) {
      // got record
      // - load record fields into object
      int index = 0;
      newTrigger->loadFromRow(row, index, NULL);
      // - put custom action into container
      mTriggers.push_back(newTrigger);
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
  for (TriggersVector::iterator pos = mTriggers.begin(); pos!=mTriggers.end(); ++pos) {
    err = (*pos)->saveToStore(NULL, true); // multiple instances allowed, it's a *list*!
    if (Error::notOK(err)) LOG(LOG_ERR,"Error saving trigger %d: %s", (*pos)->mTriggerId, err->text());
  }
  return err;
}


// MARK: - TriggerList property access implementation

int TriggerList::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return (int)mTriggers.size();
}


static char triggerlist_key;

PropertyDescriptorPtr TriggerList::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aPropIndex<mTriggers.size()) {
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->mPropertyName = string_format("%d",mTriggers[aPropIndex]->mTriggerId);
    descP->mPropertyType = apivalue_object;
    descP->mDeletable = true; // trigger is deletable
    descP->mPropertyFieldKey = aPropIndex;
    descP->mPropertyObjectKey = OKEY(triggerlist_key);
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
    descP->mPropertyType = apivalue_object;
    descP->mDeletable = true; // new scenes are deletable
    descP->mPropertyObjectKey = OKEY(triggerlist_key);
    size_t ti;
    int newId = 0;
    sscanf(aPropMatch.c_str(), "%d", &newId); // use specified new id, otherwise use 0
    TriggerPtr trg = getTrigger(newId, true, &ti);
    if (trg) {
      // valid new trigger
      descP->mPropertyFieldKey = ti; // the scene's index
      descP->mPropertyName = string_format("%d", trg->mTriggerId);
      descP->mCreatedNew = true;
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
    TriggerPtr ds = mTriggers[aPropertyDescriptor->fieldKey()];
    ds->deleteFromStore(); // remove from store
    mTriggers.erase(mTriggers.begin()+aPropertyDescriptor->fieldKey()); // remove from container
    return true;
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


PropertyContainerPtr TriggerList::getContainer(const PropertyDescriptorPtr aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->hasObjectKey(triggerlist_key)) {
    return mTriggers[aPropertyDescriptor->fieldKey()];
  }
  return NULL;
}


void TriggerList::processGlobalEvent(VdchostEvent aActivity)
{
  for (TriggersVector::iterator pos = mTriggers.begin(); pos!=mTriggers.end(); ++pos) {
    (*pos)->processGlobalEvent(aActivity);
  }
}



// MARK: - LocalController

LocalController::LocalController(VdcHost &aVdcHost) :
  mVdcHost(aVdcHost),
  mDevicesReady(false)
{
  mLocalZones.isMemberVariable();
  mLocalScenes.isMemberVariable();
  mLocalTriggers.isMemberVariable();
  StandardScriptingDomain::sharedDomain().registerMemberLookup(LocalControllerLookup::sharedLookup());
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
  mVdcHost.signalActivity();
}


void LocalController::processGlobalEvent(VdchostEvent aActivity)
{
  if (aActivity==vdchost_devices_initialized) {
    // from now on, triggers can/should fire
    mDevicesReady = true;
  }
  if (aActivity>=vdchost_redistributed_events) {
    // only process events that should be redistributed to all objects
    LOG(LOG_INFO, ">>> localcontroller starts processing global event %d", (int)aActivity);
    mLocalTriggers.processGlobalEvent(aActivity);
    LOG(LOG_INFO, ">>> localcontroller done processing event %d", (int)aActivity);
  }
}


bool LocalController::processSensorChange(SensorBehaviour &aSensorBehaviour, double aCurrentValue)
{
  DsZoneID zoneID = aSensorBehaviour.mDevice.getZoneID();
  int area = 0;
  switch (aSensorBehaviour.mSensorFunc) {
    default:
    case sensorFunc_standard:
    case sensorFunc_app:
      return false;
    case sensorFunc_dimmer_area1: area = 1; break;
    case sensorFunc_dimmer_area2: area = 2; break;
    case sensorFunc_dimmer_area3: area = 3; break;
    case sensorFunc_dimmer_area4: area = 4; break;
    case sensorFunc_dimmer_global: zoneID = zoneId_global;
    case sensorFunc_dimmer_room: break;
  }
  DsGroup group = aSensorBehaviour.mSensorGroup;
  DsChannelType channelType = aSensorBehaviour.mSensorChannel;
  // deliver the channel change
  NotificationAudience audience;
  mVdcHost.addToAudienceByZoneAndGroup(audience, zoneID, group);
  JsonApiValuePtr params = JsonApiValuePtr(new JsonApiValue);
  params->setType(apivalue_object);
  // - define audience
  params->add("zone_id", params->newUint64(zoneID));
  params->add("group", params->newUint64(group));
  string method = "setOutputChannelValue";
  if (aSensorBehaviour.mSensorType==sensorType_percent_speed) {
    // set dimming speed
    int dir = 0;
    if (aCurrentValue>0) dir = 1;
    else if (aCurrentValue<0) dir = -1;
    params->add("move", params->newInt64(dir));
    params->add("rate", params->newDouble(fabs(aCurrentValue)));
  }
  else {
    // limit value range to percentage
    if (aCurrentValue>100) aCurrentValue = 100;
    else if (aCurrentValue<0) aCurrentValue = 0;
    if (channelType==channeltype_hue || channelType==channeltype_p44_rotation) {
      aCurrentValue *= 3.6; // expande it to 0..360
    }
    params->add("value", params->newDouble(aCurrentValue));
    double tt = (double)aSensorBehaviour.mMinPushInterval/Second;
    if (tt>0.5) tt = 0.5; // not too slow
    params->add("transitionTime", params->newDouble(tt));
  }
  params->add("channel", params->newUint64(channelType));
  params->add("area", params->newUint64(area));
  // - deliver
  mVdcHost.deliverToAudience(audience, VdcApiConnectionPtr(), method, params);
  LocalController::sharedLocalController()->signalActivity(); // local dimming is activity
  return true; // acted upon sensor change
}



bool LocalController::processButtonClick(ButtonBehaviour &aButtonBehaviour)
{
  LocalController::sharedLocalController()->signalActivity(); // button clicks are activity
  FOCUSLOG("processButtonClick: button = %s", aButtonBehaviour.shortDesc().c_str());
  // defaults
  DsClickType clickType = aButtonBehaviour.mClickType;
  DsGroup group = aButtonBehaviour.mButtonGroup;
  DsChannelType channelType = channeltype_default;
  DsZoneID zoneID = zoneId_global;
  bool global = group==group_black_variable;
  ButtonScenesMap map(aButtonBehaviour.mButtonFunc, global);
  // possible actions
  bool doDim = false;
  bool force = false;
  bool undo = false;
  SceneNo sceneToCall = INVALID_SCENE_NO;
  VdcDimMode direction = dimmode_stop; // none known
  // direct action?
  if (aButtonBehaviour.mActionMode!=buttonActionMode_none) {
    // direct action
    FOCUSLOG("processButtonClick: direct action");
    sceneToCall = aButtonBehaviour.mActionId;
    zoneID = global ? zoneId_global : aButtonBehaviour.mDevice.getZoneID();
    if (aButtonBehaviour.mActionMode==buttonActionMode_force) force = true;
    else if (aButtonBehaviour.mActionMode==buttonActionMode_undo) undo = true;
  }
  else {
    // actual click: determine what to do
    FOCUSLOG("processButtonClick: actual click: %d", clickType);
    switch (aButtonBehaviour.mButtonMode) {
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
    if (aButtonBehaviour.mButtonFunc==buttonFunc_app) {
      FOCUSLOG("processButtonClick: no default action for app button -> must be handled programmatically");
      return false; // we do not handle app buttons
    }
    if (global) {
      // global scene
      zoneID = zoneId_global;
      group = group_undefined;
      direction = dimmode_up; // always "on"
      switch (clickType) {
        case ct_tip_1x:
        case ct_click_1x:
          sceneToCall = map.mSceneClick[1];
          break;
        default:
          return true; // unknown click -> ignore, but handled
      }
    }
    else {
      // room scene
      zoneID = aButtonBehaviour.mDevice.getZoneID();
      channelType = aButtonBehaviour.mButtonChannel;
      ZoneDescriptorPtr zone = mLocalZones.getZoneById(zoneID, false);
      if (!zone) return false; // button in a non-local zone, cannot handle
      if (group!=group_yellow_light && group!=group_grey_shadow) return true; // NOP because we don't support anything except light and shadow for now, but handled
      // evaluate click
      if (clickType==ct_hold_start) {
        // start dimming if not off (or if it is specifically the up-key of a rocker)
        if (!zone->mZoneState.stateFor(group, map.mArea)) {
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
          direction = zone->mZoneState.mLastDim==dimmode_up ? dimmode_down : dimmode_up;
        }
      }
      else if (clickType==ct_hold_end) {
        // stop dimming
        direction = dimmode_stop;
        doDim = true;
      }
      else {
        // - not hold or release
        SceneNo sceneOnClick = INVALID_SCENE_NO;
        switch (clickType) {
          case ct_tip_1x:
          case ct_click_1x:
            sceneOnClick = map.mSceneClick[1];
            break;
          case ct_tip_2x:
          case ct_click_2x:
            sceneOnClick = map.mSceneClick[2];
            direction = dimmode_up;
            break;
          case ct_tip_3x:
          case ct_click_3x:
            sceneOnClick = map.mSceneClick[3];
            direction = dimmode_up;
            break;
          case ct_tip_4x:
            sceneOnClick = map.mSceneClick[4];
            direction = dimmode_up;
            break;
          default:
            return true; // unknown click -> ignore, but handled
        }
        if (direction==dimmode_stop) {
          // single button, no explicit direction
          direction = zone->mZoneState.stateFor(group,map.mArea) ? dimmode_down : dimmode_up;
        }
        // local
        if (direction==dimmode_up) {
          // calling a preset
          sceneToCall = sceneOnClick;
        }
        else {
          // calling an off scene
          sceneToCall = map.mSceneClick[0];
        }
      }
    }
  } // click
  // now perform actions
  if (sceneToCall!=INVALID_SCENE_NO && !undo) {
    // plain scene call
    callScene(sceneToCall, zoneID, group, force);
    return true; // handled
  }
  else if (doDim || undo) {
    // deliver other notification types
    NotificationAudience audience;
    mVdcHost.addToAudienceByZoneAndGroup(audience, zoneID, group);
    JsonApiValuePtr params = JsonApiValuePtr(new JsonApiValue);
    params->setType(apivalue_object);
    string method;
    // - define audience
    params->add("zone_id", params->newUint64(zoneID));
    params->add("group", params->newUint64(group));
    if (undo) {
      method = "undoScene";
      params->add("scene", params->newInt64(sceneToCall));
    }
    else {
      method = "dimChannel";
      params->add("mode", params->newInt64(direction));
      params->add("autoStop", params->newBool(false)); // prevent stop dimming event w/o repeating command
      params->add("stopActions", params->newBool(false)); // prevent stopping runnig scene actions
      params->add("channel", params->newUint64(channelType));
      params->add("area", params->newUint64(map.mArea));
    }
    // - deliver, NOT from an API connection
    mVdcHost.deliverToAudience(audience, VdcApiConnectionPtr(), method, params);
    return true; // handled
  }
  else {
    return true; // NOP, but handled
  }
  return false; // not handled so far
}


void LocalController::callScene(SceneIdentifier aScene, MLMicroSeconds aTransitionTimeOverride, bool aForce)
{
  callScene(aScene.mSceneNo, aScene.mZoneID, aScene.mGroup, aTransitionTimeOverride, aForce);
}


void LocalController::callScene(SceneNo aSceneNo, DsZoneID aZone, DsGroup aGroup, MLMicroSeconds aTransitionTimeOverride, bool aForce)
{
  NotificationAudience audience;
  mVdcHost.addToAudienceByZoneAndGroup(audience, aZone, aGroup);
  callScene(aSceneNo, audience, aTransitionTimeOverride, aForce);
}


void LocalController::callScene(SceneNo aSceneNo, NotificationAudience &aAudience, MLMicroSeconds aTransitionTimeOverride, bool aForce)
{
  JsonApiValuePtr params = JsonApiValuePtr(new JsonApiValue);
  params->setType(apivalue_object);
  // { "notification":"callScene", "zone_id":0, "group":1, "scene":5, "force":false }
  // Note: we don't need the zone/group params, these are defined by the audience already
  string method = "callScene";
  params->add("scene", params->newUint64(aSceneNo));
  params->add("force", params->newBool(aForce));
  if (aTransitionTimeOverride!=Infinite) {
    params->add("transitionTime", params->newDouble((double)aTransitionTimeOverride/Second));
  }
  // - deliver
  mVdcHost.deliverToAudience(aAudience, VdcApiConnectionPtr(), method, params);
}


void LocalController::setOutputChannelValues(DsZoneID aZone, DsGroup aGroup, string aChannelId, double aValue, MLMicroSeconds aTransitionTimeOverride)
{
  NotificationAudience audience;
  mVdcHost.addToAudienceByZoneAndGroup(audience, aZone, aGroup);
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
  mVdcHost.deliverToAudience(aAudience, VdcApiConnectionPtr(), method, params);
}



void LocalController::deviceAdded(DevicePtr aDevice)
{
  FOCUSLOG("deviceAdded: device = %s", aDevice->shortDesc().c_str());
  // make sure this device's zone exists in the global list
  ZoneDescriptorPtr deviceZone = mLocalZones.getZoneById(aDevice->getZoneID(), true);
  deviceZone->usedByDevice(aDevice, true);
}


void LocalController::deviceRemoved(DevicePtr aDevice)
{
  FOCUSLOG("deviceRemoved: device = %s", aDevice->shortDesc().c_str());
  ZoneDescriptorPtr deviceZone = mLocalZones.getZoneById(aDevice->getZoneID(), false);
  if (deviceZone) deviceZone->usedByDevice(aDevice, false);
}


void LocalController::deviceChangesZone(DevicePtr aDevice, DsZoneID aFromZone, DsZoneID aToZone)
{
  FOCUSLOG("deviceChangesZone: device = %s, zone %d -> %d", aDevice->shortDesc().c_str(), aFromZone, aToZone);
  if (aFromZone!=aToZone) {
    // - remove from old
    ZoneDescriptorPtr deviceZone = mLocalZones.getZoneById(aFromZone, false);
    if (deviceZone) deviceZone->usedByDevice(aDevice, false);
    // - add to new (and create it in case it is new)
    deviceZone = mLocalZones.getZoneById(aToZone, true);
    deviceZone->usedByDevice(aDevice, true);
  }
}


void LocalController::deviceWillApplyNotification(DevicePtr aDevice, NotificationDeliveryState &aDeliveryState)
{
  ZoneDescriptorPtr zone = mLocalZones.getZoneById(aDevice->getZoneID(), false);
  if (zone && aDevice->getOutput()) {
    DsGroupMask affectedGroups = aDevice->getOutput()->groupMemberships();
    if (aDeliveryState.mOptimizedType==ntfy_callscene) {
      // scene call
      for (int g = group_undefined; affectedGroups; affectedGroups>>=1, ++g) {
        if (affectedGroups & 1) {
          SceneIdentifier calledScene(aDeliveryState.mContentId, zone->getZoneId(), (DsGroup)g);
          // general
          int area = SimpleScene::areaForScene(calledScene.mSceneNo);
          if (calledScene.getKindFlags()&scene_off) {
            // is a off scene (area or not), cancels the local priority
            aDevice->getOutput()->setLocalPriority(false);
          }
          else if (area!=0) {
            // is area on scene, set local priority in the device
            aDevice->setLocalPriority(calledScene.mSceneNo);
          }
          if (calledScene.getKindFlags()&scene_global) {
            zone->mZoneState.mLastGlobalScene = calledScene.mSceneNo;
          }
          // group specific
          bool isOffScene = calledScene.getKindFlags()&scene_off;
          if (g==group_yellow_light) {
            zone->mZoneState.mLastLightScene = calledScene.mSceneNo;
            // calling on scenes is remembered as dimming default channel up (so next dim will go down)
            zone->mZoneState.mLastDimChannel = channeltype_default;
            zone->mZoneState.mLastDim = isOffScene ? dimmode_down : dimmode_up;
          }
          zone->mZoneState.setStateFor(g, area, !isOffScene);
          if (calledScene.mSceneNo==DEEP_OFF) {
            // force areas off as well
            zone->mZoneState.setStateFor(g, 1, false);
            zone->mZoneState.setStateFor(g, 2, false);
            zone->mZoneState.setStateFor(g, 3, false);
            zone->mZoneState.setStateFor(g, 4, false);
          }
        }
      }
    }
    else if (aDeliveryState.mOptimizedType==ntfy_dimchannel) {
      // dimming
      if (aDeliveryState.mActionVariant!=dimmode_stop) {
        zone->mZoneState.mLastDimChannel = (DsChannelType)aDeliveryState.mActionParam;
        zone->mZoneState.mLastDim = (VdcDimMode)aDeliveryState.mActionVariant;
      }
    }
    LOG(LOG_INFO,
      "Zone '%s' (%d) state updated: lastLightScene:%d, lastDim=%d, lastGlobalScene:%d, lightOn=%d/areas1234=%d%d%d%d, shadesOpen=%d/%d%d%d%d",
      zone->getName().c_str(), zone->getZoneId(),
      zone->mZoneState.mLastLightScene,
      (int)zone->mZoneState.mLastDim,
      zone->mZoneState.mLastGlobalScene,
      zone->mZoneState.mLightOn[0],
      zone->mZoneState.mLightOn[1], zone->mZoneState.mLightOn[2], zone->mZoneState.mLightOn[3], zone->mZoneState.mLightOn[4],
      zone->mZoneState.mShadesOpen[0],
      zone->mZoneState.mShadesOpen[1], zone->mZoneState.mShadesOpen[2], zone->mZoneState.mShadesOpen[3], zone->mZoneState.mShadesOpen[4]
    );
  }
}




size_t LocalController::totalDevices() const
{
  return mVdcHost.mDSDevices.size();
}


void LocalController::startRunning()
{
  FOCUSLOG("startRunning");
}



ErrorPtr LocalController::load()
{
  ErrorPtr err;
  err = mLocalZones.load();
  if (Error::notOK(err)) LOG(LOG_ERR, "could not load localZones: %s", err->text());
  err = mLocalScenes.load();
  if (Error::notOK(err)) LOG(LOG_ERR, "could not load localScenes: %s", err->text());
  err = mLocalTriggers.load();
  if (Error::notOK(err)) LOG(LOG_ERR, "could not load localTriggers: %s", err->text());
  return err;
}


ErrorPtr LocalController::save()
{
  ErrorPtr err;
  err = mLocalZones.save();
  if (Error::notOK(err)) LOG(LOG_ERR, "could not save localZones: %s", err->text());
  err = mLocalScenes.save();
  if (Error::notOK(err)) LOG(LOG_ERR, "could not save localScenes: %s", err->text());
  err = mLocalTriggers.save();
  if (Error::notOK(err)) LOG(LOG_ERR, "could not save localTriggers: %s", err->text());
  return err;
}


// MARK: - LocalController specific root (vdchost) level method handling


static const GroupDescriptor groupInfos[] = {
  { group_undefined,               group_global,      "undefined",                "", 0x000000 },
  { group_yellow_light,            group_standard,    "light",                    "🟡", 0xFFFF00 },
  { group_grey_shadow,             group_standard,    "shadow",                   "⚪️", 0x999999 },
  { group_blue_heating,            group_standard,    "heating",                  "🔵", 0x0000FF },
  { group_cyan_audio,              group_standard,    "audio",                    "🟣", 0x00FFFF },
  { group_magenta_video,           group_standard,    "video",                    "🟣", 0xFF00FF },
  { group_red_security,            group_global,      "security",                 "🔴", 0xFF0000 },
  { group_green_access,            group_global,      "access",                   "🟢", 0x00FF00 },
  { group_black_variable,          group_application, "joker",                    "⚫️", 0x000000 },
  { group_blue_cooling,            group_standard,    "cooling",                  "🔵", 0x0000FF },
  { group_blue_ventilation,        group_standard,    "ventilation",              "🔵", 0x0000FF },
  { group_blue_windows,            group_standard,    "windows",                  "🔵", 0x0000FF },
  { group_blue_air_recirculation,  group_controller,  "air recirculation",        "🔵", 0x0000FF },
  { group_roomtemperature_control, group_controller,  "room temperature control", "🔵", 0x0000FF },
  { group_ventilation_control,     group_controller,  "ventilation control",      "🔵", 0x0000FF },
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
    if (uequals(aGroupName.c_str(), giP->name)) {
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
      ZoneDescriptorPtr zone = mLocalZones.getZoneById(zoneID, false);
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
            s->add("no", s->newUint64(scenes[i].mSceneNo));
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
      ZoneDescriptorPtr zone = mLocalZones.getZoneById(zoneID, false);
      if (!zone) {
        aError = WebError::webErr(400, "Zone %d not found (never used, no devices)", (int)zoneID);
        return true;
      }
      groups = zone->getZoneGroups();
    }
    else {
      // globally
      for (DsDeviceMap::iterator pos = mVdcHost.mDSDevices.begin(); pos!=mVdcHost.mDSDevices.end(); ++pos) {
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
        g->add("symbol", g->newString(gi->symbol));
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
      TriggerPtr trig = mLocalTriggers.getTrigger(triggerId);
      if (!trig) {
        aError = WebError::webErr(400, "Trigger %d not found", triggerId);
      }
      else {
        if (aMethod=="x-p44-testTriggerAction") {
          ScriptObjPtr triggerParam;
          ApiValuePtr o = aParams->get("triggerParam");
          if (o) {
            // has a trigger parameter
            triggerParam = new StringValue(o->stringValue());
          }
          trig->handleTestActions(aRequest, triggerParam); // asynchronous!
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
  if (aParentDescriptor->isRootOfObject()) {
    // Accessing properties at the localcontroller (root) level
    return inherited::numProps(aDomain, aParentDescriptor)+numLocalControllerProperties;
  }
  return inherited::numProps(aDomain, aParentDescriptor); // only the inherited ones
}


PropertyDescriptorPtr LocalController::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // localcontroller level properties
  static const PropertyDescription properties[numLocalControllerProperties] = {
    // common device properties
    { "zones", apivalue_object, zones_key, OKEY(zonelist_key) },
    { "scenes", apivalue_object, scenes_key, OKEY(scenelist_key) },
    { "triggers", apivalue_object, triggers_key, OKEY(triggerlist_key) },
  };
  // C++ object manages different levels, check aParentDescriptor
  if (aParentDescriptor->isRootOfObject()) {
    // root level - accessing properties on the localcontroller level
    int n = inherited::numProps(aDomain, aParentDescriptor);
    if (aPropIndex<n)
      return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
    aPropIndex -= n; // rebase to 0 for my own first property
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor);
}


PropertyContainerPtr LocalController::getContainer(const PropertyDescriptorPtr aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->mParentDescriptor->isRootOfObject()) {
    if (aPropertyDescriptor->hasObjectKey(zonelist_key)) {
      return ZoneListPtr(&mLocalZones);
    }
    else if (aPropertyDescriptor->hasObjectKey(scenelist_key)) {
      return SceneListPtr(&mLocalScenes);
    }
    else if (aPropertyDescriptor->hasObjectKey(triggerlist_key)) {
      return TriggerListPtr(&mLocalTriggers);
    }
  }
  // unknown here
  return inherited::getContainer(aPropertyDescriptor, aDomain);
}


using namespace P44Script;

// MARK: - Local controller specific functions

// trigger('triggername')    execute a trigger's action script
FUNC_ARG_DEFS(trigger, { text } );
static void trigger_funcExecuted(BuiltinFunctionContextPtr f, ScriptObjPtr aResult)
{
  f->finish(aResult);
}
static void trigger_func(BuiltinFunctionContextPtr f)
{
  TriggerPtr targetTrigger = LocalController::sharedLocalController()->mLocalTriggers.getTriggerByName(f->arg(0)->stringValue());
  if (!targetTrigger) {
    f->finish(new ErrorValue(ScriptError::NotFound, "No trigger named '%s' found", f->arg(0)->stringValue().c_str()));
    return;
  }
  targetTrigger->mTriggerAction.runCommand(restart, boost::bind(&trigger_funcExecuted, f, _1), ScriptObjPtr());
}



// helper for callScene and saveScene
static bool findSceneAndTarget(int &ai, BuiltinFunctionContextPtr f, SceneNo &aSceneNo, int &aZoneid, DsGroup &aGroup, DevicePtr &aDevice)
{
  if (f->numArgs()>1 && f->arg(1)->hasType(text)) {
    // second param is text, cannot be transition time, must be a zone or device specification
    // - ..so first one must be a scene number or name
    aSceneNo = VdcHost::sharedVdcHost()->getSceneIdByKind(f->arg(0)->stringValue());
    if (aSceneNo==INVALID_SCENE_NO) {
      f->finish(new ErrorValue(ScriptError::NotFound, "Scene '%s' not found", f->arg(0)->stringValue().c_str()));
      return false;
    }
    // get zone or device
    aDevice.reset();
    ZoneDescriptorPtr zone = LocalController::sharedLocalController()->mLocalZones.getZoneByName(f->arg(1)->stringValue());
    if (zone) {
      // is a zone
      aZoneid = zone->getZoneId();
    }
    else {
      // might be a device
      DevicePtr device = VdcHost::sharedVdcHost()->getDeviceByNameOrDsUid(f->arg(1)->stringValue());
      if (!device) {
        f->finish(new ErrorValue(ScriptError::NotFound, "Zone or Device '%s' not found", f->arg(1)->stringValue().c_str()));
        return false;
      }
      aDevice = device;
    }
    ai++;
  }
  else {
    // first param is a named scene that implies the zone and the group
    SceneDescriptorPtr scene = LocalController::sharedLocalController()->mLocalScenes.getSceneByName(f->arg(0)->stringValue());
    if (!scene) {
      f->finish(new ErrorValue(ScriptError::NotFound, "Scene '%s' not found", f->arg(0)->stringValue().c_str()));
      return false;
    }
    aZoneid = scene->getZoneID();
    aSceneNo = scene->getSceneNo();
    aGroup = scene->getGroup(); // override
  }
  return true;
}


// sceneid(name)
// sceneno(name_or_id)
FUNC_ARG_DEFS(sceneid_no, { text } );
static void sceneid_func(BuiltinFunctionContextPtr f)
{
  SceneDescriptorPtr scene = LocalController::sharedLocalController()->mLocalScenes.getSceneByName(f->arg(0)->stringValue());
  if (scene) {
    f->finish(new StringValue(scene->getActionName()));
    return;
  }
  f->finish(new AnnotatedNullValue("no such scene"));
}
static void sceneno_func(BuiltinFunctionContextPtr f)
{
  SceneDescriptorPtr scene = LocalController::sharedLocalController()->mLocalScenes.getSceneByName(f->arg(0)->stringValue());
  SceneNo sceneNo = INVALID_SCENE_NO;
  if (scene) {
    sceneNo = scene->getSceneNo();
  }
  else {
    sceneNo = VdcHost::getSceneIdByKind(f->arg(0)->stringValue());
  }
  if (sceneNo!=INVALID_SCENE_NO) {
    f->finish(new IntegerValue(sceneNo));
  }
  else {
    f->finish(new AnnotatedNullValue("no such scene"));
  }
}


// scene(name)
// scene(name, transition_time)
// scene(id, zone_or_device)
// scene(id, zone_or_device, groupname)
// scene(id, zone_or_device, transition_time)
// scene(id, zone_or_device, transition_time, groupname)
FUNC_ARG_DEFS(scene, { text|numeric }, { text|numeric|optionalarg }, { text|numeric|optionalarg }, { text|numeric|optionalarg } );
static void scene_func(BuiltinFunctionContextPtr f)
{
  int ai = 1;
  int zoneid = -1; // none specified
  SceneNo sceneNo = INVALID_SCENE_NO;
  MLMicroSeconds transitionTime = Infinite; // use scene's standard time
  DsGroup group = group_yellow_light; // default to light
  DevicePtr dev;
  if (!findSceneAndTarget(ai, f, sceneNo, zoneid, group, dev)) return; // finish already done by findScene helper
  if (f->numArgs()>ai && f->arg(ai)->hasType(numeric)) {
    // custom transition time
    transitionTime = f->arg(ai)->doubleValue()*Second;
    if (transitionTime<0) transitionTime = Infinite; // use default
    ai++;
  }
  if (dev) {
    // targeting single device
    dev->callScene(sceneNo, true, transitionTime); // force call when targeting single device
    f->finish();
    return;
  }
  // targeting zone, there might be an extra group arg
  if (f->numArgs()>ai) {
    const GroupDescriptor* gdP = LocalController::groupInfoByName(f->arg(ai)->stringValue());
    if (!gdP) {
      f->finish(new ErrorValue(ScriptError::NotFound, "unknown group '%s'", f->arg(ai)->stringValue().c_str()));
      return;
    }
    group = gdP->no;
  }
  // execute the scene
  LocalController::sharedLocalController()->callScene(sceneNo, zoneid, group, transitionTime);
  f->finish();
}


// savescene(name [, group]])
// savescene(id, zone_or_device [, group]])
FUNC_ARG_DEFS(savescene, { text|numeric }, { text|numeric|optionalarg }, { text|numeric|optionalarg } );
static void savescene_func(BuiltinFunctionContextPtr f)
{
  int ai = 1;
  int zoneid = -1; // none specified
  SceneNo sceneNo = INVALID_SCENE_NO;
  DsGroup group = group_yellow_light; // default to light
  DevicePtr dev;
  if (!findSceneAndTarget(ai, f, sceneNo, zoneid, group, dev)) return;  // finish already done by findScene helper
  if (dev) {
    // targeting single device
    dev->saveScene(sceneNo);
    f->finish();
    return;
  }
  // targeting zone, there might be an extra group arg
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
FUNC_ARG_DEFS(set, { text|numeric }, { numeric }, { numeric|optionalarg }, { text|optionalarg }, { text|numeric|optionalarg } );
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
  if (ZoneDescriptorPtr zone = LocalController::sharedLocalController()->mLocalZones.getZoneByName(f->arg(0)->stringValue())) {
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
  FUNC_DEF_W_ARG(trigger, executable|anyvalid),
  FUNC_DEF_W_ARG(scene, executable|anyvalid),
  { "sceneid", executable|text, sceneid_no_numargs, sceneid_no_args, &sceneid_func },
  FUNC_DEF_C_ARG(sceneno, executable|numeric, sceneid_no),
  FUNC_DEF_W_ARG(savescene, executable|anyvalid),
  FUNC_DEF_W_ARG(set, executable|anyvalid),
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

#endif // ENABLE_LOCALCONTROLLER
