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

#include "outputbehaviour.hpp"
#include "simplescene.hpp"

using namespace p44;


OutputBehaviour::OutputBehaviour(Device &aDevice) :
  inherited(aDevice, "output"),
  // hardware derived params
  mOutputFunction(outputFunction_dimmer),
  mOutputUsage(usage_undefined),
  mVariableRamp(true),
  mMaxPower(-1),
  // persistent settings
  mOutputMode(outputmode_default), // use the default
  mDefaultOutputMode(outputmode_disabled), // none by default, hardware should set a default matching the actual HW capabilities
  mPushChangesToDS(false), // do not push changes
  mBridgePushInterval(10*Second), // default to decent progress update for waiting user
  // volatile state
  mLocalPriority(false), // no local priority
  mTransitionTime(0) // immediate transitions by default
{
  // set default group membership (which is group_undefined)
  resetGroupMembership();
  // set default hardware default configuration
  setHardwareOutputConfig(outputFunction_switch, outputmode_binary, usage_undefined, false, -1);
}


Tristate OutputBehaviour::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // now check for light behaviour level features
  switch (aFeatureIndex) {
    case modelFeature_outmodegeneric:
      // At least, outputs can be made inactive or be activated generally
      // Subclasses might suppress this and use another, more specific "outmodeXY" feature
      return yes;
    case modelFeature_outvalue8:
      // Assumption: All normal 8-bit outputs should have this. Exception so far are shade outputs
      return yes;
    case modelFeature_blink:
      // Assumption: devices with an output have this
      return yes;
    default:
      // not available at output level
      return undefined;
  }
}


void OutputBehaviour::setHardwareOutputConfig(VdcOutputFunction aOutputFunction, VdcOutputMode aDefaultOutputMode, VdcUsageHint aUsage, bool aVariableRamp, double aMaxPower)
{
  mOutputFunction = aOutputFunction;
  mOutputUsage = aUsage;
  mVariableRamp = aVariableRamp;
  mMaxPower = aMaxPower;
  mDefaultOutputMode = aDefaultOutputMode;
  // Note: actual outputMode is outputmode_default by default, so without modifying settings, defaultOutputMode applies
}


void OutputBehaviour::addChannel(ChannelBehaviourPtr aChannel)
{
  aChannel->mChannelIndex = (int)mChannels.size();
  mChannels.push_back(aChannel);
}


size_t OutputBehaviour::numChannels()
{
  return mChannels.size();
}



ChannelBehaviourPtr OutputBehaviour::getChannelByIndex(int aChannelIndex, bool aPendingApplyOnly)
{
  if (aChannelIndex<mChannels.size()) {
    ChannelBehaviourPtr ch = mChannels[aChannelIndex];
    if (!aPendingApplyOnly || ch->needsApplying())
      return ch;
    // found but has no apply pending -> return no channel
  }
  return ChannelBehaviourPtr();
}


ChannelBehaviourPtr OutputBehaviour::getChannelByType(DsChannelType aChannelType, bool aPendingApplyOnly)
{
  if (aChannelType==channeltype_default)
    return getChannelByIndex(0, aPendingApplyOnly); // first channel is primary/default channel by internal convention
  // look for channel with matching type
  for (ChannelBehaviourVector::iterator pos = mChannels.begin(); pos!=mChannels.end(); ++pos) {
    if ((*pos)->getChannelType()==aChannelType) {
      if (!aPendingApplyOnly || (*pos)->needsApplying())
        return *pos; // found
      break; // found but has no apply pending -> return no channel
    }
  }
  return ChannelBehaviourPtr();
}


ChannelBehaviourPtr OutputBehaviour::getChannelById(const string aChannelId, bool aPendingApplyOnly)
{
  if (aChannelId=="0") return getChannelByIndex(0); // default channel
  const char *s = aChannelId.c_str();
  if (*s=='#') {
    s++;
    int ci;
    if (sscanf(s, "%d", &ci)==1) {
      return getChannelByIndex(ci);
    }
  }
  for (ChannelBehaviourVector::iterator pos = mChannels.begin(); pos!=mChannels.end(); ++pos) {
    if ((*pos)->mChannelId==aChannelId) {
      if (!aPendingApplyOnly || (*pos)->needsApplying())
        return *pos; // found
      break; // found but has no apply pending -> return no channel
    }
  }
  return ChannelBehaviourPtr();
}


DsClass OutputBehaviour::getColorClass()
{
  if (mColorClass!=class_undefined) return mColorClass;
  // no specific color set on the behaviour level: try to derive from groups
  DsClass colorclass = class_undefined;
  for (int g = group_undefined; g<64; g++) {
    if (isMember((DsGroup)g)) {
      DsClass c = Device::colorClassFromGroup((DsGroup)g);
      if (c!=class_undefined) {
        colorclass = c;
        break;
      }
    }
  }
  if (colorclass!=class_undefined) return colorclass;
  // no color class obtainable on output level: use colorClass of device
  return mDevice.getColorClass();
}


bool OutputBehaviour::isMember(DsGroup aGroup)
{
  return
    // Output group membership determines function, so primary color is not included by default, only if explicitly set
    (mOutputGroups & (0x1ll<<aGroup))!=0; // explicit extra membership flag set
}


void OutputBehaviour::setGroupMembership(DsGroup aGroup, bool aIsMember)
{
  DsGroupMask newGroups = mOutputGroups;
  if (aIsMember) {
    // make explicitly member of a group
    newGroups |= (0x1ll<<aGroup);
  }
  else {
    // not explicitly member
    newGroups &= ~(0x1ll<<aGroup);
  }
  setPVar(mOutputGroups, newGroups);
}


void OutputBehaviour::resetGroupMembership()
{
  // group_undefined (aka "variable" in old defs) must always be set
  setPVar(mOutputGroups, (DsGroupMask)(1<<group_undefined));
}


VdcOutputMode OutputBehaviour::actualOutputMode()
{
  if (mOutputMode==outputmode_default) {
    return mDefaultOutputMode; // default mode
  }
  else {
    return mOutputMode; // specifically set mode
  }
}


void OutputBehaviour::setOutputMode(VdcOutputMode aOutputMode)
{
  // base class marks all channels needing re-apply and triggers a apply if mode changes
  if (mOutputMode!=aOutputMode) {
    bool actualChanged = actualOutputMode()!=aOutputMode; // check if actual mode also changes (because explicit setting could be same as default)
    // mode setting has changed
    mOutputMode = aOutputMode;
    // if actual mode of output has changed, make sure outputs get chance to apply it
    if (actualChanged) {
      for (ChannelBehaviourVector::iterator pos=mChannels.begin(); pos!=mChannels.end(); ++pos) {
        (*pos)->setNeedsApplying(0); // needs immediate re-apply
      }
      mDevice.requestApplyingChannels(NoOP, false, true); // apply, for mode change
    }
    markDirty();
  }
}


double OutputBehaviour::outputValueAccordingToMode(double aChannelValue, int aChannelIndex)
{
  // non-default channels are just passed directly
  if (aChannelIndex!=0) return aChannelValue;
  // output mode applies to default (=first) channel
  double outval = 0;
  switch (actualOutputMode()) {
    // disabled: zero
    case outputmode_disabled:
      break;
    // binary: 0 or 100
    case outputmode_binary:
      outval = aChannelValue>0 ? 100 : 0;
      break;
    // positive values only
    case outputmode_gradual:
    default:
      outval = aChannelValue;
      break;
  }
  return outval;
}


double OutputBehaviour::channelValueAccordingToMode(double aOutputValue, int aChannelIndex)
{
  // Base class does not do any backwards transformations
  return aOutputValue;
}


bool OutputBehaviour::reportOutputState()
{
  return pushOutputState(mPushChangesToDS, mBridgePushInterval!=Infinite);
}


MLMicroSeconds OutputBehaviour::outputReportInterval()
{
  if (mBridgePushInterval==Infinite || mBridgePushInterval==Never) return Never; // no regular updates
  return mBridgePushInterval; // bridges want regular updates
}


bool OutputBehaviour::pushOutputState(bool aDS, bool aBridges)
{
  bool requestedPushDone = true;

  if (aDS) {
    // TODO: remove and re-enable dead code below, should dS-vDC-API ever evolve to allow this
    requestedPushDone = false;
    OLOG(LOG_ERR, "pushing to dS is not yet implemented");
    /*
    // push to vDC API
    VdcApiConnectionPtr api = mDevice.getVdcHost().getVdsmSessionConnection();
    if (api) {
     ApiValuePtr query = api->newApiValue();
     query->setType(apivalue_object);
     query->add("channelStates", query->newValue(apivalue_null));
     query->add("outputState", query->newValue(apivalue_null));
     if (!mDevice.pushNotification(api, query, ApiValuePtr())) requestedPushDone = false;
    }
    else {
      requestedPushDone = false;
    }
    */
  }
  #if ENABLE_JSONBRIDGEAPI
  if (aBridges && mDevice.isBridged()) {
    // push to bridges
    VdcApiConnectionPtr api = mDevice.getVdcHost().getBridgeApi();
    if (api) {
      ApiValuePtr query = api->newApiValue();
      query->setType(apivalue_object);
      query->add("channelStates", query->newValue(apivalue_null));
      query->add("outputState", query->newValue(apivalue_null));
      if (!mDevice.pushNotification(api, query, ApiValuePtr())) requestedPushDone = false;
    }
    else {
      requestedPushDone = false;
    }
  }
  #endif
  // true if requested pushes are done or irrelevant (e.g. bridge push requested w/o bridging enabled at all)
  return requestedPushDone;
}




// MARK: - scene handling

// default loader for single-value outputs. Note that this is overridden by more complex behaviours such as light
void OutputBehaviour::loadChannelsFromScene(DsScenePtr aScene)
{
  if (aScene) {
    // load default channel's value from first channel of scene
    ChannelBehaviourPtr ch = getChannelByIndex(0);
    if (ch) {
      ch->setChannelValueIfNotDontCare(aScene, aScene->sceneValue(0), 0, 0, true);
    }
  }
}


void OutputBehaviour::saveChannelsToScene(DsScenePtr aScene)
{
  if (aScene) {
    // save default channel's value to first channel of scene
    ChannelBehaviourPtr ch = getChannelByIndex(0);
    if (ch) {
      double newval = ch->getChannelValue();
      aScene->setSceneValue(0, newval);
    }
    // make sure default channel's dontCare is not set
    aScene->setSceneValueFlags(0, valueflags_dontCare, false);
  }
}


void OutputBehaviour::performSceneActions(DsScenePtr aScene, SimpleCB aDoneCB)
{
  #if ENABLE_SCENE_SCRIPT
  SimpleScenePtr simpleScene = boost::dynamic_pointer_cast<SimpleScene>(aScene);
  if (simpleScene && simpleScene->mEffect==scene_effect_script && simpleScene->mSceneScript.active()) {
    // run scene script
    OLOG(LOG_INFO, "Starting Scene Script: '%s'", singleLine(simpleScene->mSceneScript.getSource().c_str(), true, 80).c_str() );
    simpleScene->mSceneScript.setSharedMainContext(mDevice.getDeviceScriptContext());
    simpleScene->mSceneScript.run(regular|stopall, boost::bind(&OutputBehaviour::sceneScriptDone, this, aDoneCB, _1), ScriptObjPtr(), Infinite);
    return;
  }
  #endif // ENABLE_SCENE_SCRIPT
  if (aDoneCB) aDoneCB(); // NOP
}


#if ENABLE_SCENE_SCRIPT

void OutputBehaviour::sceneScriptDone(SimpleCB aDoneCB, ScriptObjPtr aResult)
{
  OLOG(LOG_INFO, "Scene Script completed, returns: '%s'", aResult->stringValue().c_str());
  if (aDoneCB) aDoneCB();
}

#endif // ENABLE_SCENE_SCRIPT


void OutputBehaviour::stopSceneActions()
{
  #if ENABLE_SCENE_SCRIPT
  mDevice.getDeviceScriptContext()->abort(stopall, new ErrorValue(ScriptError::Aborted, "scene actions stopped"));
  #endif // ENABLE_SCENE_SCRIPT
}


void OutputBehaviour::stopTransitions()
{
  OLOG(LOG_INFO, "stopping channel transitions");
  for (ChannelBehaviourVector::iterator pos = mChannels.begin(); pos!=mChannels.end(); ++pos) {
    (*pos)->stopTransition();
  }
}


void OutputBehaviour::setTransitionTimeOverride(MLMicroSeconds aTransitionTimeOverride)
{
  if (aTransitionTimeOverride!=Infinite) {
    OLOG(LOG_INFO, "Transition times of all changing channels overridden: actual transition time is now %d mS", (int)(aTransitionTimeOverride/MilliSecond));
    // override the transition time in all channels that now need to be applied
    for (ChannelBehaviourVector::iterator pos = mChannels.begin(); pos!=mChannels.end(); ++pos) {
      if ((*pos)->needsApplying()) (*pos)->setTransitionTime(aTransitionTimeOverride);
    }
  }
}


bool OutputBehaviour::applySceneToChannels(DsScenePtr aScene, MLMicroSeconds aTransitionTimeOverride)
{
  if (aScene) {
    bool ok = performApplySceneToChannels(aScene, aScene->mSceneCmd); // actually apply
    setTransitionTimeOverride(aTransitionTimeOverride);
    return ok;
  }
  return false; // no scene to apply
}



bool OutputBehaviour::performApplySceneToChannels(DsScenePtr aScene, SceneCmd aSceneCmd)
{
  // stop any actions still ongoing from a previous call
  // Note: we do NOT stop transitions here, those channels affected by a new scene value
  //   will stop or retarget anyway, unaffected channels may continue running.
  stopSceneActions();
  // scenes with invoke functionality will apply channel values by default
  if (aSceneCmd==scene_cmd_none) {
    aSceneCmd = aScene->mSceneCmd;
  }
  if (
    aSceneCmd==scene_cmd_invoke ||
    aSceneCmd==scene_cmd_undo ||
    aSceneCmd==scene_cmd_off ||
    aSceneCmd==scene_cmd_slow_off ||
    aSceneCmd==scene_cmd_min ||
    aSceneCmd==scene_cmd_max
  ) {
    // apply stored scene value(s) to channels
    loadChannelsFromScene(aScene);
    LOG(LOG_INFO, "- Scene(%s): new channel value(s) loaded from scene, ready to apply",  VdcHost::sceneText(aScene->mSceneNo).c_str());
    return true;
  }
  else {
    // no channel changes
    LOG(LOG_INFO, "- Scene(%s): no invoke/off/min/max (but cmd=%d) -> no channels loaded", VdcHost::sceneText(aScene->mSceneNo).c_str(), aSceneCmd);
    return false;
  }
}



// capture scene
void OutputBehaviour::captureScene(DsScenePtr aScene, bool aFromDevice, SimpleCB aDoneCB)
{
  if (aFromDevice) {
    // make sure channel values are updated
    mDevice.requestUpdatingChannels(boost::bind(&OutputBehaviour::channelValuesCaptured, this, aScene, aFromDevice, aDoneCB));
  }
  else {
    // just capture the cached channel values
    channelValuesCaptured(aScene, aFromDevice, aDoneCB);
  }
}



void OutputBehaviour::channelValuesCaptured(DsScenePtr aScene, bool aFromDevice, SimpleCB aDoneCB)
{
  // just save the current channel values to the scene
  saveChannelsToScene(aScene);
  // - saving implies clearing scene-level dontcare
  aScene->setDontCare(false);
  // done now
  if (aDoneCB) aDoneCB();
}


MLMicroSeconds OutputBehaviour::transitionTimeFromScene(DsScenePtr aScene, bool aDimUp)
{
  SimpleScenePtr ssc = boost::dynamic_pointer_cast<SimpleScene>(aScene);
  if (ssc) {
    switch (ssc->mEffect) {
      // Note: light scenes have their own timing for these, here we just return the defaults
      // - smooth = 100mS
      // - slow   = 1min (60800mS)
      // - custom = 5sec
      case scene_effect_smooth :
        return 100*MilliSecond;
      case scene_effect_slow :
        return 1*Minute;
      case scene_effect_custom :
        return 5*Second;
      case scene_effect_transition:
        return ssc->mEffectParam*MilliSecond; // transition time is just the effect param (in milliseconds)
      default:
        break;
    }
  }
  return 0; // no known effect -> just return 0 for transition time
}


MLMicroSeconds OutputBehaviour::recommendedTransitionTime(bool aDimUp)
{
  // take preset1 (room on) scene's transition time as default
  SceneDeviceSettingsPtr scenes = getDevice().getScenes();
  DsScenePtr scene;
  if (scenes) {
    scene = scenes->getScene(ROOM_ON);
  }
  // safe to call with null scene
  return transitionTimeFromScene(scene, aDimUp);
}


// MARK: - persistence implementation


// SQLIte3 table name to store these parameters to
const char *OutputBehaviour::tableName()
{
  return "OutputSettings";
}


// data field definitions

static const size_t numFields = 3;

size_t OutputBehaviour::numFieldDefs()
{
  return inherited::numFieldDefs()+numFields;
}


const FieldDefinition *OutputBehaviour::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "outputMode", SQLITE_INTEGER },
    { "outputFlags", SQLITE_INTEGER },
    { "outputGroups", SQLITE_INTEGER }
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void OutputBehaviour::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, NULL); // common flags are loaded here, not in superclasses
  // get the fields
  aRow->getCastedIfNotNull<VdcOutputMode, int>(aIndex++, mOutputMode);
  uint64_t flags = aRow->getCastedWithDefault<uint64_t, long long int>(aIndex++, 0);
  aRow->getCastedIfNotNull<uint64_t, long long int>(aIndex++, mOutputGroups);
  // decode my own flags
  mPushChangesToDS = flags & outputflag_pushChanges;
  // pass the flags out to subclass which called this superclass to get the flags (and decode themselves)
  if (aCommonFlagsP) *aCommonFlagsP = flags;
}


// bind values to passed statement
void OutputBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // encode the flags
  if (mPushChangesToDS) aCommonFlags |= outputflag_pushChanges;
  // bind the fields
  aStatement.bind(aIndex++, mOutputMode);
  aStatement.bind(aIndex++, (long long int)aCommonFlags);
  aStatement.bind(aIndex++, (long long int)mOutputGroups);
}


ErrorPtr OutputBehaviour::loadChildren()
{
  if (mDevice.getVdcHost().doPersistChannels()) {
    for (ChannelBehaviourVector::iterator pos=mChannels.begin(); pos!=mChannels.end(); ++pos) {
      (*pos)->load();
    }
  }
  return inherited::loadChildren();
}


ErrorPtr OutputBehaviour::saveChildren()
{
  if (mDevice.getVdcHost().doPersistChannels()) {
    for (ChannelBehaviourVector::iterator pos=mChannels.begin(); pos!=mChannels.end(); ++pos) {
      (*pos)->save();
    }
  }
  return inherited::saveChildren();
}

ErrorPtr OutputBehaviour::deleteChildren()
{
  for (ChannelBehaviourVector::iterator pos=mChannels.begin(); pos!=mChannels.end(); ++pos) {
    (*pos)->forget();
  }
  return inherited::deleteChildren();
}


// MARK: - output property access

static char output_key;
static char output_groups_key;


// next level (groups)

int OutputBehaviour::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(output_groups_key)) {
    return 64; // group mask has 64 bits for now
  }
  return inherited::numProps(aDomain, aParentDescriptor);
}


PropertyContainerPtr OutputBehaviour::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->isArrayContainer() && aPropertyDescriptor->hasObjectKey(output_groups_key)) {
    return PropertyContainerPtr(this); // handle groups array myself
  }
  // unknown here
  return inherited::getContainer(aPropertyDescriptor, aDomain);
}


PropertyDescriptorPtr OutputBehaviour::getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(output_groups_key)) {
    // array-like container
    PropertyDescriptorPtr propDesc;
    bool numericName = getNextPropIndex(aPropMatch, aStartIndex);
    int n = numProps(aDomain, aParentDescriptor);
    if (aStartIndex!=PROPINDEX_NONE && aStartIndex<n) {
      // within range, create descriptor
      DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
      descP->propertyName = string_format("%d", aStartIndex);
      descP->propertyType = aParentDescriptor->type();
      descP->propertyFieldKey = aStartIndex;
      descP->propertyObjectKey = aParentDescriptor->objectKey();
      propDesc = PropertyDescriptorPtr(descP);
      // advance index
      aStartIndex++;
    }
    if (aStartIndex>=n || numericName) {
      // no more descriptors OR specific descriptor accessed -> no "next" descriptor
      aStartIndex = PROPINDEX_NONE;
    }
    return propDesc;
  }
  // None of the containers within Device - let base class handle Device-Level properties
  return inherited::getDescriptorByName(aPropMatch, aStartIndex, aDomain, aMode, aParentDescriptor);
}




// description properties

enum {
  outputFunction_key,
  outputUsage_key,
  variableRamp_key,
  maxPower_key,
  recommendedTransitionTime_key,
  numDescProperties
};


int OutputBehaviour::numDescProps() { return numDescProperties; }
const PropertyDescriptorPtr OutputBehaviour::getDescDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numDescProperties] = {
    { "function", apivalue_uint64, outputFunction_key+descriptions_key_offset, OKEY(output_key) },
    { "outputUsage", apivalue_uint64, outputUsage_key+descriptions_key_offset, OKEY(output_key) },
    { "variableRamp", apivalue_bool, variableRamp_key+descriptions_key_offset, OKEY(output_key) },
    { "maxPower", apivalue_double, maxPower_key+descriptions_key_offset, OKEY(output_key) },
    { "x-p44-recommendedTransitionTime", apivalue_double, recommendedTransitionTime_key+descriptions_key_offset, OKEY(output_key) },
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// settings properties

enum {
  mode_key,
  pushChangesToDs_key,
  bridgePushInterval_key,
  groups_key,
  numSettingsProperties
};


int OutputBehaviour::numSettingsProps() { return numSettingsProperties; }
const PropertyDescriptorPtr OutputBehaviour::getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numSettingsProperties] = {
    { "mode", apivalue_uint64, mode_key+settings_key_offset, OKEY(output_key) },
    { "pushChanges", apivalue_bool, pushChangesToDs_key+settings_key_offset, OKEY(output_key) },
    { "x-p44-bridgePushInterval", apivalue_double, bridgePushInterval_key+settings_key_offset, OKEY(output_key) },
    { "groups", apivalue_bool+propflag_container, groups_key+settings_key_offset, OKEY(output_groups_key) }
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// state properties

enum {
  localPriority_key,
  transitiontime_key,
  numStateProperties
};


int OutputBehaviour::numStateProps() { return numStateProperties; }
const PropertyDescriptorPtr OutputBehaviour::getStateDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numStateProperties] = {
    { "localPriority", apivalue_bool, localPriority_key+states_key_offset, OKEY(output_key) },
    { "transitionTime", apivalue_double, transitiontime_key+states_key_offset, OKEY(output_key) }
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}



// access to all fields

bool OutputBehaviour::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(output_groups_key)) {
    if (aMode==access_read) {
      // read group membership
      if (isMember((DsGroup)aPropertyDescriptor->fieldKey())) {
        aPropValue->setBoolValue(true);
        return true;
      }
      return false;
    }
    else {
      // write group
      setGroupMembership((DsGroup)aPropertyDescriptor->fieldKey(), aPropValue->boolValue());
      return true;
    }
  }
  else if (aPropertyDescriptor->hasObjectKey(output_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Description properties
        case outputFunction_key+descriptions_key_offset:
          aPropValue->setUint8Value(mOutputFunction);
          return true;
        case outputUsage_key+descriptions_key_offset:
          aPropValue->setUint16Value(mOutputUsage);
          return true;
        case variableRamp_key+descriptions_key_offset:
          aPropValue->setBoolValue(mVariableRamp);
          return true;
        case maxPower_key+descriptions_key_offset:
          aPropValue->setDoubleValue(mMaxPower);
          return true;
        case recommendedTransitionTime_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)recommendedTransitionTime(true)/Second); // standard transition time for dimming up
          return true;
        // Settings properties
        case mode_key+settings_key_offset:
          aPropValue->setUint8Value(actualOutputMode()); // return actual mode, never outputmode_default
          return true;
        case pushChangesToDs_key+settings_key_offset:
          aPropValue->setBoolValue(mPushChangesToDS);
          return true;
        // Operational, non-persistent settings
        case bridgePushInterval_key+settings_key_offset:
          if (mBridgePushInterval==Infinite) aPropValue->setNull();
          aPropValue->setDoubleValue((double)mBridgePushInterval/Second);
          return true;
        // State properties
        case localPriority_key+states_key_offset:
          aPropValue->setBoolValue(mLocalPriority);
          return true;
        case transitiontime_key+states_key_offset:
          aPropValue->setDoubleValue((double)mTransitionTime/Second);
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case mode_key+settings_key_offset:
          setOutputMode((VdcOutputMode)aPropValue->int32Value());
          return true;
        case pushChangesToDs_key+settings_key_offset:
          setPVar(mPushChangesToDS, aPropValue->boolValue());
          return true;
        // Operational, non-persistent settings
        case bridgePushInterval_key+settings_key_offset:
          mBridgePushInterval = aPropValue->isNull() ? Infinite : aPropValue->doubleValue()*Second;
          return true;
        // State properties
        case localPriority_key+states_key_offset:
          mLocalPriority = aPropValue->boolValue();
          return true;
        case transitiontime_key+states_key_offset:
          mTransitionTime = aPropValue->doubleValue()*Second;
          return true;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}



// MARK: - description/shortDesc

string OutputBehaviour::logContextPrefix()
{
  return string_format("%s: %s", mDevice.logContextPrefix().c_str(), getTypeName());
}


string OutputBehaviour::contextId() const
{
  return ""; // only one output per device
}


string OutputBehaviour::description()
{
  string s = string_format("%s behaviour", shortDesc().c_str());
  string_format_append(s, "\n- hardware output function: %d, default output mode: %d", mOutputFunction, mDefaultOutputMode);
  s.append(inherited::description());
  return s;
}


string OutputBehaviour::getStatusText()
{
  // show first channel's value
  ChannelBehaviourPtr ch = getChannelByType(channeltype_default); // get default channel
  if (ch) {
    return ch->getStatusText();
  }
  return "";
}


// MARK: - Output scripting object

#if ENABLE_SCENE_SCRIPT

using namespace P44Script;

static void outputOpComplete(BuiltinFunctionContextPtr f, OutputBehaviourPtr aOutput)
{
  POLOG(aOutput, LOG_INFO, "scripted operation complete");
  f->finish();
}

// helper for loadscene, runactions
static DsScenePtr findScene(OutputObj* o, const string aSceneId)
{
  DsScenePtr scene;
  SceneDeviceSettingsPtr scenes;
  scenes = o->output()->getDevice().getScenes();
  if (scenes) {
    SceneNo sceneNo = VdcHost::sharedVdcHost()->getSceneIdByKind(aSceneId);
    if (sceneNo!=INVALID_SCENE_NO) {
      scene = scenes->getScene(sceneNo);
    }
  }
  return scene;
}

// loadscene(sceneNoOrName [, transitionTimeOverride])
FUNC_ARG_DEFS(loadscene, { numeric|text }, { numeric|optionalarg} );
static void loadscene_func(BuiltinFunctionContextPtr f)
{
  OutputObj* o = dynamic_cast<OutputObj*>(f->thisObj().get());
  assert(o);
  DsScenePtr scene = findScene(o, f->arg(0)->stringValue());
  if (scene) {
    MLMicroSeconds transition = Infinite; // no override
    if (f->numArgs()>=2) transition = f->arg(1)->doubleValue()*Second;
    POLOG(o->output(), LOG_INFO, "loadscene(%s) loads channel values", VdcHost::sceneText(scene->mSceneNo).c_str());
    o->output()->applySceneToChannels(scene, transition);
  }
  f->finish();
}


// runactions(sceneNoOrName)
FUNC_ARG_DEFS(runactions, { numeric|optionalarg } );
static void runactions_func(BuiltinFunctionContextPtr f)
{
  OutputObj* o = dynamic_cast<OutputObj*>(f->thisObj().get());
  assert(o);
  DsScenePtr scene = findScene(o, f->arg(0)->stringValue());
  if (scene) {
    POLOG(o->output(), LOG_INFO, "runactions(%s) starts scene actions", VdcHost::sceneText(scene->mSceneNo).c_str());
    o->output()->performSceneActions(scene, boost::bind(&outputOpComplete, f, o->output()));
    return;
  }
  f->finish();
}


// stopactions()
static void stopactions_func(BuiltinFunctionContextPtr f)
{
  OutputObj* o = dynamic_cast<OutputObj*>(f->thisObj().get());
  assert(o);
  POLOG(o->output(), LOG_INFO, "stopping all scene actions");
  // Note: call this on device level, so device implementations
  //   have the chance to stop device-specific ongoing actions and transition
  o->output()->getDevice().stopTransitions();
  o->output()->getDevice().stopSceneActions();
  f->finish();
}


// applychannels()
// applychannels(forced [, transitionTimeOverride])
FUNC_ARG_DEFS(applychannels, { numeric|optionalarg }, { numeric|optionalarg } );
static void applychannels_func(BuiltinFunctionContextPtr f)
{
  OutputObj* o = dynamic_cast<OutputObj*>(f->thisObj().get());
  assert(o);
  if (f->arg(0)->boolValue()) {
    // force apply, invalidate all channels first
    o->output()->getDevice().invalidateAllChannels();
  }
  if (f->numArgs()>=2) {
    o->output()->setTransitionTimeOverride(f->arg(1)->doubleValue()*Second);
  }
  POLOG(o->output(), LOG_INFO, "applychannels() requests applying channels now");
  o->output()->getDevice().getVdc().cancelNativeActionUpdate(); // still delayed native scene updates must be cancelled before changing channel values
  o->output()->getDevice().requestApplyingChannels(boost::bind(&outputOpComplete, f, o->output()), false);
}

// syncchannels()
static void syncchannels_func(BuiltinFunctionContextPtr f)
{
  OutputObj* o = dynamic_cast<OutputObj*>(f->thisObj().get());
  assert(o);
  POLOG(o->output(), LOG_INFO, "syncchannels() requests reading channels now");
  o->output()->getDevice().requestUpdatingChannels(boost::bind(&outputOpComplete, f, o->output()));
}


// channel(channelid)               - return the value of the specified channel
// channel_t(channelid)             - return the transitional value of the specified channel
// [dim]channel(channelid, value)   - set the channel value to the specified value or dim it relatively
// [dim]channel(channelid, value, transitiontime)
FUNC_ARG_DEFS(channel, { text }, { numeric|optionalarg }, { numeric|optionalarg } );
static void channel_funcImpl(bool aDim, bool aTransitional, BuiltinFunctionContextPtr f)
{
  OutputObj* o = dynamic_cast<OutputObj*>(f->thisObj().get());
  assert(o);
  ChannelBehaviourPtr channel = o->output()->getChannelById(f->arg(0)->stringValue());
  if (!channel) {
    f->finish(new AnnotatedNullValue("unknown channel"));
    return;
  }
  else {
    // channel found
    if (f->numArgs()==1) {
      // return channel value
      #if P44SCRIPT_FULL_SUPPORT
      // value source representing the channel
      ValueSource* vs = dynamic_cast<ValueSource *>(channel.get());
      if (vs) {
        f->finish(new ValueSourceObj(vs));
      }
      else
      #endif
      {
        // is not a value source, return numeric value only
        f->finish(new NumericValue(channel->getChannelValueCalculated(aTransitional)));
      }
      return;
    }
    else {
      // set value
      MLMicroSeconds transitionTime = 0; // default to immediate
      if (f->numArgs()>2) {
        transitionTime = f->arg(2)->doubleValue()*Second;
      }
      if (aDim)
        channel->dimChannelValue(f->arg(1)->doubleValue(), transitionTime);
      else
        channel->setChannelValue(f->arg(1)->doubleValue(), transitionTime, true); // always apply
    }
  }
  f->finish();
}

static void channel_func(BuiltinFunctionContextPtr f)
{
  channel_funcImpl(false, false, f);
}
static void channel_t_func(BuiltinFunctionContextPtr f)
{
  channel_funcImpl(false, true, f);
}
static void dimchannel_func(BuiltinFunctionContextPtr f)
{
  channel_funcImpl(true, false, f);
}

// movechannel(channelid, direction)   - start or stop moving the channel value in the specified direction
// movechannel(channelid, direction, timePerUnit)
FUNC_ARG_DEFS(movechannel, { text }, { numeric }, { numeric|optionalarg } );
static void movechannel_func(BuiltinFunctionContextPtr f)
{
  OutputObj* o = dynamic_cast<OutputObj*>(f->thisObj().get());
  assert(o);
  ChannelBehaviourPtr channel = o->output()->getChannelById(f->arg(0)->stringValue());
  if (!channel) {
    f->finish(new AnnotatedNullValue("unknown channel"));
    return;
  }
  else {
    MLMicroSeconds timePerUnit = 0; // default to standard dimming rate of the channel
    if (f->numArgs()>2) {
      timePerUnit = f->arg(2)->doubleValue()*Second;
    }
    channel->moveChannelValue(f->arg(1)->intValue(), timePerUnit);
  }
  f->finish();
}


static const BuiltinMemberDescriptor outputMembers[] = {
  FUNC_DEF_W_ARG(loadscene, executable|null),
  FUNC_DEF_W_ARG(runactions, executable|async|null),
  FUNC_DEF_NOARG(stopactions, executable|null),
  FUNC_DEF_W_ARG(applychannels, executable|async|null),
  FUNC_DEF_NOARG(syncchannels, executable|async|null),
  FUNC_DEF_W_ARG(channel, executable|numeric),
  FUNC_DEF_C_ARG(channel_t, executable|numeric, channel),
  FUNC_DEF_C_ARG(dimchannel, executable|numeric, channel),
  FUNC_DEF_W_ARG(movechannel, executable|numeric),
  { NULL } // terminator
};


static BuiltInMemberLookup* sharedOutputMemberLookupP = NULL;

OutputObj::OutputObj(OutputBehaviourPtr aOutput) :
  mOutput(aOutput)
{
  registerSharedLookup(sharedOutputMemberLookupP, outputMembers);
}


#endif // ENABLE_SCENE_SCRIPT
