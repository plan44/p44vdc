//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "dsscene.hpp"

#include "device.hpp"
#include "outputbehaviour.hpp"
#include "simplescene.hpp"
#include "jsonvdcapi.hpp"
#include "fnv.hpp"

using namespace p44;

static char dsscene_key;

// MARK: - private scene channel access class

static char dsscene_channels_key;
static char scenevalue_key;

// local property container for channels/outputs
class SceneChannels P44_FINAL: public PropertyContainer
{
  typedef PropertyContainer inherited;

  enum {
    value_key,
    dontCare_key,
    numValueProperties
  };

  DsScene &scene;

public:
  SceneChannels(DsScene &aScene) : scene(aScene) {};

protected:

  int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
  {
    if (!aParentDescriptor->hasObjectKey(scenevalue_key)) {
      // channels/outputs container
      return scene.numSceneValues();
    }
    // actual fields of channel/output
    // Note: SceneChannels is final, so no subclass adding properties must be considered
    return numValueProperties;
  }


  PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
  {
    // scene value level properties
    static const PropertyDescription valueproperties[numValueProperties] = {
      { "value", apivalue_null, value_key, OKEY(scenevalue_key) }, // channel value can be of different types
      { "dontCare", apivalue_bool, dontCare_key, OKEY(scenevalue_key) },
    };
    if (aParentDescriptor->hasObjectKey(dsscene_channels_key)) {
      // scene channels by their channel ID
      DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
      descP->propertyName = scene.getDevice().getChannelByIndex(aPropIndex)->getApiId(aParentDescriptor->getApiVersion());
      descP->propertyType = aParentDescriptor->type();
      descP->propertyFieldKey = aPropIndex;
      descP->propertyObjectKey = OKEY(scenevalue_key);
      return descP;
    }
    // Note: SceneChannels is final, so no subclass adding properties must be considered
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&valueproperties[aPropIndex], aParentDescriptor));
  }


  PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
  {
    // the only subcontainer are the fields, handled by myself
    return PropertyContainerPtr(this);
  }


  bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
  {
    if (aPropertyDescriptor->hasObjectKey(scenevalue_key)) {
      // Scene value level
      // - get the output index
      int outputIndex = (int)aPropertyDescriptor->parentDescriptor->fieldKey();
      if (aMode==access_read) {
        // read properties
        switch (aPropertyDescriptor->fieldKey()) {
          case value_key:
            if (scene.getChannelValueType(outputIndex) == apivalue_string) {
              aPropValue->setType(apivalue_string);
              aPropValue->setStringValue(scene.sceneValueString(outputIndex));
            } else {
              aPropValue->setType(apivalue_double);
              aPropValue->setDoubleValue(scene.sceneValue(outputIndex));
            }
            return true;
          case dontCare_key:
            aPropValue->setBoolValue(scene.isSceneValueFlagSet(outputIndex, valueflags_dontCare));
            return true;
        }
      }
      else {
        // write properties
        switch (aPropertyDescriptor->fieldKey()) {
          case value_key:
            if (scene.getChannelValueType(outputIndex) == apivalue_string) {
              scene.setSceneValueString(outputIndex, aPropValue->stringValue());
            } else {
              scene.setSceneValue(outputIndex, aPropValue->doubleValue());
            }
            return true;
          case dontCare_key:
            scene.setSceneValueFlags(outputIndex, valueflags_dontCare, aPropValue->boolValue());
            return true;
        }
      }
    }
    return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
  }
  
};
typedef boost::intrusive_ptr<SceneChannels> SceneChannelsPtr;



// MARK: - scene base class


DsScene::DsScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
  inheritedParams(aSceneDeviceSettings.paramStore),
  mSceneDeviceSettings(aSceneDeviceSettings),
  mSceneNo(aSceneNo),
  mSceneArea(0), // not area scene by default
  mSceneCmd(scene_cmd_invoke), // simple invoke command by default
  mGlobalSceneFlags(0)
{
  sceneChannels = SceneChannelsPtr(new SceneChannels(*this));
}


Device &DsScene::getDevice()
{
  return mSceneDeviceSettings.mDevice;
}


OutputBehaviourPtr DsScene::getOutputBehaviour()
{
  return mSceneDeviceSettings.mDevice.getOutput();
}


void DsScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  mSceneNo = aSceneNo; // usually already set, but still make sure
  mSceneCmd = scene_cmd_invoke; // assume invoke type
  mSceneArea = 0; // no area scene by default
  markClean(); // default values are always clean
}


bool DsScene::preciseUndoImportant()
{
  // by default, only alarm scenes are likely to be undone at all, and thus should precisely capture previous output state
  // (for other scenes, capturing the last-known cached output state is sufficient, and much less expensive)
  return
    mSceneNo==PANIC ||
    mSceneNo==ALARM1 ||
    mSceneNo==FIRE ||
    mSceneNo==SMOKE ||
    mSceneNo==WATER ||
    mSceneNo==GAS ||
    mSceneNo==ALARM2 ||
    mSceneNo==ALARM3 ||
    mSceneNo==ALARM4;
}




uint64_t DsScene::sceneHash()
{
  // generic base class implementation: hash over all values and all flags.
  // Subclasses might override or replace this with better methods
  Fnv64 hash;
  for (int i=0; i<numSceneValues(); i++) {
    double v = sceneValue(i);
    hash.addBytes(sizeof(v), (uint8_t *)&v); // is platform dependent, but does not matter - this is for caching only
    uint32_t f = sceneValueFlags(i);
    hash.addBytes(sizeof(f), (uint8_t *)&f); // is platform dependent, but does not matter - this is for caching only
  }
  return hash.getHash();
}





// MARK: - scene persistence

// primary key field definitions

static const size_t numKeys = 1;

size_t DsScene::numKeyDefs()
{
  return inheritedParams::numKeyDefs()+numKeys;
}

const FieldDefinition *DsScene::getKeyDef(size_t aIndex)
{
  static const FieldDefinition keyDefs[numKeys] = {
    { "sceneNo", SQLITE_INTEGER }, // parent's key plus this one identifies the scene among all saved scenes of all devices
  };
  if (aIndex<inheritedParams::numKeyDefs())
    return inheritedParams::getKeyDef(aIndex);
  aIndex -= inheritedParams::numKeyDefs();
  if (aIndex<numKeys)
    return &keyDefs[aIndex];
  return NULL;
}


// data field definitions

#if ENABLE_SCENE_SCRIPT
static const size_t numFields = 2;
#else
static const size_t numFields = 1;
#endif

size_t DsScene::numFieldDefs()
{
  return inheritedParams::numFieldDefs()+numFields;
}


const FieldDefinition *DsScene::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "commonFlags", SQLITE_INTEGER }
    #if ENABLE_SCENE_SCRIPT
    ,{ "sceneScript", SQLITE_TEXT }
    #endif
  };
  if (aIndex<inheritedParams::numFieldDefs())
    return inheritedParams::getFieldDef(aIndex);
  aIndex -= inheritedParams::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


// flags in globalSceneFlags
enum {
  // scene global
  globalflags_sceneDontCare = 0x0001, ///< scene level dontcare
  globalflags_ignoreLocalPriority = 0x0002,
  globalflags_sceneLevelMask = 0x0003,

  // per value dontCare flags, 16 channels max
  globalflags_valueDontCare0 = 0x100,
  globalflags_valueDontCareMask = 0xFFFF00,
};


void DsScene::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inheritedParams::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the scene number
  mSceneNo = aRow->get<int>(aIndex++);
  // as the scene is loaded into a object which did not yet have the correct scene number
  // default values must be set again now that the sceneNo is known
  // Note: this is important to make sure those fields which are not stored have the correct scene related value (sceneCmd, sceneArea)
  setDefaultSceneValues(mSceneNo);
  // then proceed with loading other fields
  mGlobalSceneFlags = aRow->get<int>(aIndex++);
  #if ENABLE_SCENE_SCRIPT
  mSceneScript.loadAndActivate(
    string_format("dev_%s.scene_%d", getDevice().getDsUid().getString().c_str(), mSceneNo),
    scriptbody+regular,
    "scenescript",
    string_format("%%C (%%O %s)", VdcHost::sceneText(mSceneNo).c_str()).c_str(), // title
    &mSceneDeviceSettings.mDevice,
    nullptr, // standard scripting domain
    aRow->get<const char *>(aIndex++)
  );
  #endif
}


void DsScene::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, (int)mSceneNo);
  aStatement.bind(aIndex++, (int)mGlobalSceneFlags);
  #if ENABLE_SCENE_SCRIPT
  mSceneScript.storeSource();
  aStatement.bind(aIndex++, mSceneScript.getSourceToStoreLocally().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  #endif
}


// MARK: - scene flags


void DsScene::setGlobalSceneFlag(uint32_t aMask, bool aNewValue)
{
  uint32_t newFlags = (mGlobalSceneFlags & ~aMask) | (aNewValue ? aMask : 0);
  setPVar(mGlobalSceneFlags, newFlags);
}



bool DsScene::isDontCare()
{
  return mGlobalSceneFlags & globalflags_sceneDontCare;
}

void DsScene::setDontCare(bool aDontCare)
{
  setGlobalSceneFlag(globalflags_sceneDontCare, aDontCare);
}



bool DsScene::ignoresLocalPriority()
{
  return mGlobalSceneFlags & globalflags_ignoreLocalPriority;
}

void DsScene::setIgnoreLocalPriority(bool aIgnoreLocalPriority)
{
  uint32_t newFlags = (mGlobalSceneFlags & ~globalflags_ignoreLocalPriority) | (aIgnoreLocalPriority ? globalflags_ignoreLocalPriority : 0);
  setPVar(mGlobalSceneFlags, newFlags);
}


// MARK: - scene values/channels


int DsScene::numSceneValues()
{
  return getDevice().numChannels();
}


uint32_t DsScene::sceneValueFlags(int aChannelIndex)
{
  uint32_t flags = 0;
  // up to 16 channel's dontCare flags are mapped into globalSceneFlags
  if (aChannelIndex<numSceneValues()) {
    if (mGlobalSceneFlags & (globalflags_valueDontCare0<<aChannelIndex)) {
      flags |= valueflags_dontCare; // this value's dontCare is set
    }
  }
  return flags;
}


void DsScene::setSceneValueFlags(int aChannelIndex, uint32_t aFlagMask, bool aSet)
{
  // up to 16 channel's dontCare flags are mapped into globalSceneFlags
  if (aChannelIndex<numSceneValues()) {
    uint32_t flagmask = globalflags_valueDontCare0<<aChannelIndex;
    uint32_t newFlags;
    if (aSet)
      newFlags = mGlobalSceneFlags | ((aFlagMask & valueflags_dontCare) ? flagmask : 0);
    else
      newFlags = mGlobalSceneFlags & ~((aFlagMask & valueflags_dontCare) ? flagmask : 0);
    if (newFlags!=mGlobalSceneFlags) {
      // actually changed
      mGlobalSceneFlags = newFlags;
      markDirty();
    }
  }
}


ApiValueType DsScene::getChannelValueType(int aChannelIndex)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  if (cb) {
    return cb->getChannelValueType();
  }
  return apivalue_null; // should not happen: no such channel
}


// utility function to check scene value flag
bool DsScene::isSceneValueFlagSet(int aChannelIndex, uint32_t aFlagMask)
{
  // only dontCare flag exists per value in base class
  return sceneValueFlags(aChannelIndex) & aFlagMask;
}



// MARK: - scene property access


enum {
  channels_key,
  ignoreLocalPriority_key,
  dontCare_key,
  #if !REDUCED_FOOTPRINT
  sceneDesc_key,
  #endif
  #if ENABLE_SCENE_SCRIPT
  sceneScript_key,
  sceneScriptId_key,
  #endif
  numSceneProperties
};




int DsScene::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return inheritedProps::numProps(aDomain, aParentDescriptor)+numSceneProperties;
}



PropertyDescriptorPtr DsScene::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // scene level properties
  static const PropertyDescription sceneproperties[numSceneProperties] = {
    { "channels", apivalue_object+propflag_container, channels_key, OKEY(dsscene_channels_key) },
    { "ignoreLocalPriority", apivalue_bool, ignoreLocalPriority_key, OKEY(dsscene_key) },
    { "dontCare", apivalue_bool, dontCare_key, OKEY(dsscene_key) },
    #if !REDUCED_FOOTPRINT
    { "x-p44-sceneDesc", apivalue_string, sceneDesc_key, OKEY(dsscene_key) },
    #endif
    #if ENABLE_SCENE_SCRIPT
    { "x-p44-sceneScript", apivalue_string, sceneScript_key, OKEY(dsscene_key) },
    { "x-p44-sceneScriptId", apivalue_string, sceneScriptId_key, OKEY(dsscene_key) },
    #endif
  };
  int n = inheritedProps::numProps(aDomain, aParentDescriptor);
  if (aPropIndex<n)
    return inheritedProps::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
  aPropIndex -= n; // rebase to 0 for my own first property
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&sceneproperties[aPropIndex], aParentDescriptor));
}


PropertyContainerPtr DsScene::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  // the only container is sceneChannels
  return sceneChannels;
}



bool DsScene::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(dsscene_key)) {
    // global scene level
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case ignoreLocalPriority_key:
          aPropValue->setBoolValue(ignoresLocalPriority());
          return true;
        case dontCare_key:
          aPropValue->setBoolValue(isDontCare());
          return true;
        #if !REDUCED_FOOTPRINT
        case sceneDesc_key:
          aPropValue->setStringValue(VdcHost::sceneText(mSceneNo));
          return true;
        #endif
        #if ENABLE_SCENE_SCRIPT
        case sceneScript_key:
          aPropValue->setStringValue(mSceneScript.getSource());
          return true;
        case sceneScriptId_key:
          if (!mSceneScript.active()) return false; // no ID yet
          aPropValue->setStringValue(mSceneScript.scriptSourceUid());
          return true;
        #endif
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        case ignoreLocalPriority_key:
          setIgnoreLocalPriority(aPropValue->boolValue());
          return true;
        case dontCare_key:
          setDontCare(aPropValue->boolValue());
          return true;
        #if ENABLE_SCENE_SCRIPT
        case sceneScript_key:
          // lazy activation when setting a non-empty scene script
          if (mSceneScript.setSourceAndActivate(
            aPropValue->stringValue(),
            string_format("dev_%s.scene_%d", getDevice().getDsUid().getString().c_str(),mSceneNo),
            scriptbody+regular,
            "scenescript",
            string_format("%%C (%%O %s)", VdcHost::sceneText(mSceneNo).c_str()).c_str(), // title
            &mSceneDeviceSettings.mDevice,
            nullptr // standard scripting domain
          )) {
            markDirty();
          }
          return true;
        #endif
      }
    }
  }
  return inheritedProps::accessField(aMode, aPropValue, aPropertyDescriptor);
}



// MARK: - scene device settings base class


SceneDeviceSettings::SceneDeviceSettings(Device &aDevice) :
  inherited(aDevice)
{
}


DsScenePtr SceneDeviceSettings::newDefaultScene(SceneNo aSceneNo)
{
  SimpleScenePtr simpleScene = SimpleScenePtr(new SimpleScene(*this, aSceneNo));
  simpleScene->setDefaultSceneValues(aSceneNo);
  // return it
  return simpleScene;
}


DsScenePtr SceneDeviceSettings::newUndoStateScene()
{
  DsScenePtr undoStateScene = newDefaultScene(ROOM_ON); // use main on as template
  // to make sure: the "previous" pseudo-screne must always be "invoke" type (restoring output values)
  undoStateScene->mSceneCmd = scene_cmd_undo;
  undoStateScene->mSceneArea = 0; // no area
  return undoStateScene;
}




DsScenePtr SceneDeviceSettings::getScene(SceneNo aSceneNo)
{
  // see if we have a stored version different from the default
  DsSceneMap::iterator pos = mScenes.find(aSceneNo);
  if (pos!=mScenes.end()) {
    // found scene params in map
    return pos->second;
  }
  else {
    // just return default values for this scene
    return newDefaultScene(aSceneNo);
  }
}



void SceneDeviceSettings::updateScene(DsScenePtr aScene)
{
  if (aScene->rowid==0) {
    // unstored so far, add to map of non-default scenes
    mScenes[aScene->mSceneNo] = aScene;
  }
  // anyway, mark scene dirty
  aScene->markDirty();
  // as we need the ROWID of the settings as parentID, make sure we get saved if we don't have one
  if (rowid==0) markDirty();
}


// MARK: - scene table persistence


// Note: we explicitly define the table name here, altough at this time it is the same as for
//   the base class.
//   Subclasses that define a new tableName must ALSO define parentIdForScenes();
const char *SceneDeviceSettings::tableName()
{
  return inherited::tableName();
}


string SceneDeviceSettings::parentIdForScenes()
{
  // base class just uses ROWID of the record in DeviceSettings (base table)
  // derived classes which define a tableName() will prefix the rowid with an unique identifier
  return string_format("%llu",rowid);
}



ErrorPtr SceneDeviceSettings::loadChildren()
{
  ErrorPtr err;
  // get the parent key for the children (which might be the ROWID alone, but derived deviceSettings might need extra prefix)
  string parentID = parentIdForScenes();
  // create a template
  DsScenePtr scene = newDefaultScene(0);
  // get the query
  sqlite3pp::query *queryP = scene->newLoadAllQuery(parentID.c_str());
  if (queryP==NULL) {
    // real error preparing query
    err = paramStore.error();
  }
  else {
    for (sqlite3pp::query::iterator row = queryP->begin(); row!=queryP->end(); ++row) {
      // got record
      // - load record fields into scene object
      int index = 0;
      uint64_t flags;
      scene->loadFromRow(row, index, &flags);
      // - put scene into map of non-default scenes
      mScenes[scene->mSceneNo] = scene;
      // - fresh object for next row
      scene = newDefaultScene(0);
    }
    delete queryP; queryP = NULL;
    #if ENABLE_SETTINGS_FROM_FILES
    // Now check for default settings from files
    loadScenesFromFiles();
    #endif
  }
  return err;
}


ErrorPtr SceneDeviceSettings::saveChildren()
{
  ErrorPtr err;
  // Cannot save children before I have my own rowID
  if (rowid!=0) {
    // my own ROWID is the parent key for the children
    string parentID = parentIdForScenes();
    // save all elements of the map (only dirty ones will be actually stored to DB
    for (DsSceneMap::iterator pos = mScenes.begin(); pos!=mScenes.end(); ++pos) {
      err = pos->second->saveToStore(parentID.c_str(), true); // multiple children of same parent allowed
      if (Error::notOK(err)) SOLOG(mDevice, LOG_ERR,"Error saving scene %d: %s", pos->second->mSceneNo, err->text());
    }
  }
  return err;
}


ErrorPtr SceneDeviceSettings::deleteChildren()
{
  ErrorPtr err;
  for (DsSceneMap::iterator pos = mScenes.begin(); pos!=mScenes.end(); ++pos) {
    err = pos->second->deleteFromStore();
  }
  return err;
}



// MARK: - additional scene defaults from files

#if ENABLE_SETTINGS_FROM_FILES

void SceneDeviceSettings::loadScenesFromFiles()
{
  string dir = mDevice.getVdcHost().getConfigDir();
  const int numLevels = 5;
  string levelids[numLevels];
  // Level strategy: most specialized will be active, unless lower levels specify explicit override
  // - Baselines are hardcoded defaults plus settings (already) loaded from persistent store
  // - Level 0 are scenes related to the device instance (dSUID)
  // - Level 1 are scenes related to the device type (deviceTypeIdentifier())
  // - Level 2 are scenes related to the device class/version (deviceClass()_deviceClassVersion())
  // - Level 3 are scenes related to the behaviour (behaviourTypeIdentifier())
  // - Level 4 are scenes related to the vDC (vdcClassIdentifier())
  levelids[0] = "vdsd_" + mDevice.getDsUid().getString();
  levelids[1] = string(mDevice.deviceTypeIdentifier()) + "_device";
  levelids[2] = string_format("%s_%d_class", mDevice.deviceClass().c_str(), mDevice.deviceClassVersion());
  levelids[3] = string(mDevice.getOutput()->behaviourTypeIdentifier()) + "_behaviour";
  levelids[4] = mDevice.mVdcP->vdcClassIdentifier();
  for(int i=0; i<numLevels; ++i) {
    // try to open config file
    string fn = dir+"scenes_"+levelids[i]+".csv";
    string line;
    int lineNo = 0;
    FILE *file = fopen(fn.c_str(), "r");
    if (!file) {
      int syserr = errno;
      if (syserr!=ENOENT) {
        // file not existing is ok, all other errors must be reported
        SOLOG(mDevice, LOG_ERR, "failed opening file '%s' - %s", fn.c_str(), strerror(syserr));
      }
      // don't process, try next
      SOLOG(mDevice, LOG_DEBUG, "loadScenesFromFiles: tried '%s' - not found", fn.c_str());
    }
    else {
      // file opened
      SOLOG(mDevice, LOG_DEBUG, "loadScenesFromFiles: found '%s' - processing", fn.c_str());
      while (string_fgetline(file, line)) {
        lineNo++;
        // skip empty lines and those starting with #, allowing to format and comment CSV
        if (line.empty() || line[0]=='#') {
          // skip this line
          continue;
        }
        string f;
        const char *p = line.c_str();
        // first field is scene number
        bool overridden = false;
        if (nextCSVField(p, f)) {
          const char *fp = f.c_str();
          if (!*fp) continue; // empty scene number field -> invalid line
          // check override prefix
          if (*fp=='!') {
            ++fp;
            overridden = true;
          }
          // read scene number
          int sceneNo;
          if (sscanf(fp, "%d", &sceneNo)!=1) {
            SOLOG(mDevice, LOG_ERR, "%s:%d - no or invalid scene number", fn.c_str(), lineNo);
            continue; // no valid scene number -> invalid line
          }
          // check if this scene is already in the list (i.e. already has non-hardwired settings)
          DsSceneMap::iterator pos = mScenes.find(sceneNo);
          DsScenePtr scene;
          if (pos!=mScenes.end()) {
            // this scene already has settings, only apply if this is an overridden
            if (!overridden) continue; // scene already configured by more specialized level -> dont apply
            scene = pos->second;
          }
          else {
            // no settings yet, create the scene object
            scene = newDefaultScene(sceneNo);
          }
          // process rest of CSV line as property name/value pairs
          scene->readPropsFromCSV(VDC_API_DOMAIN, false, p, fn.c_str(), lineNo);
          // these changes are NOT to be made persistent in DB!
          scene->markClean();
          // put scene into table
          mScenes[sceneNo] = scene;
          SOLOG(mDevice, LOG_INFO, "Customized scene %d %sfrom config file %s", sceneNo, overridden ? "(with override) " : "", fn.c_str());
        }
      }
      fclose(file);
    }
  }
}

#endif // ENABLE_SETTINGS_FROM_FILES
