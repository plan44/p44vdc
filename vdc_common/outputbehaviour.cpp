//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
  outputFunction(outputFunction_dimmer),
  outputUsage(usage_undefined),
  variableRamp(true),
  maxPower(-1),
  // persistent settings
  outputMode(outputmode_default), // use the default
  defaultOutputMode(outputmode_disabled), // none by default, hardware should set a default matching the actual HW capabilities
  pushChanges(false), // do not push changes
  // volatile state
  localPriority(false), // no local priority
  transitionTime(0) // immediate transitions by default
  #if ENABLE_SCENE_SCRIPT && ENABLE_EXPRESSIONS
  ,sceneScriptContext(*this, &VdcHost::sharedVdcHost()->geolocation)
  #endif
{
  // set default group membership (which is group_undefined)
  resetGroupMembership();
  // set default hardware default configuration
  setHardwareOutputConfig(outputFunction_switch, outputmode_binary, usage_undefined, false, -1);
  #if ENABLE_SCENE_SCRIPT && !ENABLE_P44SCRIPT
  sceneScriptContext.isMemberVariable();
  sceneScriptContext.setContextInfo("scenescript", this);
  #endif
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
  outputFunction = aOutputFunction;
  outputUsage = aUsage;
  variableRamp = aVariableRamp;
  maxPower = aMaxPower;
  defaultOutputMode = aDefaultOutputMode;
  // Note: actual outputMode is outputmode_default by default, so without modifying settings, defaultOutputMode applies
}


void OutputBehaviour::addChannel(ChannelBehaviourPtr aChannel)
{
  aChannel->channelIndex = (int)channels.size();
  channels.push_back(aChannel);
}


size_t OutputBehaviour::numChannels()
{
  return channels.size();
}



ChannelBehaviourPtr OutputBehaviour::getChannelByIndex(int aChannelIndex, bool aPendingApplyOnly)
{
  if (aChannelIndex<channels.size()) {
    ChannelBehaviourPtr ch = channels[aChannelIndex];
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
  for (ChannelBehaviourVector::iterator pos = channels.begin(); pos!=channels.end(); ++pos) {
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
  for (ChannelBehaviourVector::iterator pos = channels.begin(); pos!=channels.end(); ++pos) {
    if ((*pos)->channelId==aChannelId) {
      if (!aPendingApplyOnly || (*pos)->needsApplying())
        return *pos; // found
      break; // found but has no apply pending -> return no channel
    }
  }
  return ChannelBehaviourPtr();
}



bool OutputBehaviour::isMember(DsGroup aGroup)
{
  return
    // Output group membership determines function, so primary color is not included by default, only if explicitly set
    (outputGroups & (0x1ll<<aGroup))!=0; // explicit extra membership flag set
}


void OutputBehaviour::setGroupMembership(DsGroup aGroup, bool aIsMember)
{
  DsGroupMask newGroups = outputGroups;
  if (aIsMember) {
    // make explicitly member of a group
    newGroups |= (0x1ll<<aGroup);
  }
  else {
    // not explicitly member
    newGroups &= ~(0x1ll<<aGroup);
  }
  setPVar(outputGroups, newGroups);
}


void OutputBehaviour::resetGroupMembership()
{
  // group_undefined (aka "variable" in old defs) must always be set
  setPVar(outputGroups, (DsGroupMask)(1<<group_undefined));
}


VdcOutputMode OutputBehaviour::actualOutputMode()
{
  if (outputMode==outputmode_default) {
    return defaultOutputMode; // default mode
  }
  else {
    return outputMode; // specifically set mode
  }
}


void OutputBehaviour::setOutputMode(VdcOutputMode aOutputMode)
{
  // base class marks all channels needing re-apply and triggers a apply if mode changes
  if (outputMode!=aOutputMode) {
    bool actualChanged = actualOutputMode()!=aOutputMode; // check if actual mode also changes (because explicit setting could be same as default)
    // mode setting has changed
    outputMode = aOutputMode;
    // if actual mode of output has changed, make sure outputs get chance to apply it
    if (actualChanged) {
      for (ChannelBehaviourVector::iterator pos=channels.begin(); pos!=channels.end(); ++pos) {
        (*pos)->setNeedsApplying(0); // needs immediate re-apply
      }
      device.requestApplyingChannels(NULL, false, true); // apply, for mode change
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
  if (simpleScene && simpleScene->effect==scene_effect_script) {
    // run scene script
    #if ENABLE_P44SCRIPT
    OLOG(LOG_INFO, "Starting Scene Script: '%s'", singleLine(simpleScene->sceneScript.getSource().c_str(), true, 80).c_str() );
    simpleScene->sceneScript.setSharedMainContext(device.getDeviceScriptContext());
    simpleScene->sceneScript.run(regular|stopall, boost::bind(&OutputBehaviour::sceneScriptDone, this, aDoneCB, _1), Infinite);
    #else
    sceneScriptContext.abort(false); // abort previous, no callback
    OLOG(LOG_INFO, "Starting Scene Script: '%.40s...'", simpleScene->sceneScript.c_str());
    sceneScriptContext.releaseState();
    sceneScriptContext.setCode(simpleScene->sceneScript);
    sceneScriptContext.execute(true, boost::bind(&OutputBehaviour::sceneScriptDone, this, aDoneCB, _1));
    return;
    #endif
  }
  #endif
  if (aDoneCB) aDoneCB(); // NOP
}


#if ENABLE_SCENE_SCRIPT

#if ENABLE_P44SCRIPT
void OutputBehaviour::sceneScriptDone(SimpleCB aDoneCB, ScriptObjPtr aResult)
{
  OLOG(LOG_INFO, "Scene Script completed, returns: '%s'", aResult->stringValue().c_str());
  if (aDoneCB) aDoneCB();
}
#else
void OutputBehaviour::sceneScriptDone(SimpleCB aDoneCB, ExpressionValue aEvaluationResult)
{
  OLOG(LOG_INFO, "Scene Script completed, returns: '%s'", aEvaluationResult.stringValue().c_str());
  if (aDoneCB) aDoneCB();
}
#endif

#endif // ENABLE_SCENE_SCRIPT


void OutputBehaviour::stopSceneActions()
{
  #if ENABLE_SCENE_SCRIPT
  #if ENABLE_P44SCRIPT
  device.getDeviceScriptContext()->abort(stopall, new ErrorValue(ScriptError::Aborted, "scene actions stopped"));
  #else
  sceneScriptContext.abort(false); // do not call back
  #endif
  #endif
}



bool OutputBehaviour::applySceneToChannels(DsScenePtr aScene, MLMicroSeconds aTransitionTimeOverride)
{
  if (aScene) {
    bool ok = performApplySceneToChannels(aScene, aScene->sceneCmd); // actually apply
    if (aTransitionTimeOverride!=Infinite) {
      OLOG(LOG_INFO, "Transition times of all changing channels overridden: actual transition time is now %d mS", (int)(aTransitionTimeOverride/MilliSecond));
      // override the transition time in all channels that now need to be applied
      for (ChannelBehaviourVector::iterator pos = channels.begin(); pos!=channels.end(); ++pos) {
        if ((*pos)->needsApplying()) (*pos)->setTransitionTime(aTransitionTimeOverride);
      }
    }
    return ok;
  }
  return false; // no scene to apply
}



bool OutputBehaviour::performApplySceneToChannels(DsScenePtr aScene, SceneCmd aSceneCmd)
{
  // stop any actions still ongoing from a previous call
  stopSceneActions();
  // scenes with invoke functionality will apply channel values by default
  if (aSceneCmd==scene_cmd_none) {
    aSceneCmd = aScene->sceneCmd;
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
    LOG(LOG_INFO, "- Scene(%d): new channel value(s) loaded from scene, ready to apply", aScene->sceneNo);
    return true;
  }
  else {
    // no channel changes
    LOG(LOG_INFO, "- Scene(%d): no invoke/off/min/max (but cmd=%d) -> no channels loaded", aScene->sceneNo, aSceneCmd);
    return false;
  }
}



// capture scene
void OutputBehaviour::captureScene(DsScenePtr aScene, bool aFromDevice, SimpleCB aDoneCB)
{
  if (aFromDevice) {
    // make sure channel values are updated
    device.requestUpdatingChannels(boost::bind(&OutputBehaviour::channelValuesCaptured, this, aScene, aFromDevice, aDoneCB));
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
    switch (ssc->effect) {
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
        return ssc->effectParam*MilliSecond; // transition time is just the effect param (in milliseconds)
      default:
        break;
    }
  }
  return 0; // no known effect -> just return 0 for transition time
}



// MARK: - scene script context

#if ENABLE_SCENE_SCRIPT && ENABLE_EXPRESSIONS


SceneScriptContext::SceneScriptContext(OutputBehaviour &aOutput, const GeoLocation* aGeoLocationP) :
  inherited(aGeoLocationP),
  output(aOutput)
{
}


bool SceneScriptContext::evaluateAsyncFunction(const string &aFunc, const FunctionArguments &aArgs, bool &aNotYielded)
{
  if (aFunc=="applychannels" && aArgs.size()==0) {
    SOLOG(output, LOG_INFO, "scene script: applychannels() requests applying channels now");
    output.device.requestApplyingChannels(boost::bind(&SceneScriptContext::channelOpComplete, this), false);
    aNotYielded = false; // yielded execution
  }
  else if (aFunc=="syncchannels" && aArgs.size()==0) {
    SOLOG(output, LOG_INFO, "scene script: syncchannels() requests updating channels from device");
    output.device.requestUpdatingChannels(boost::bind(&SceneScriptContext::channelOpComplete, this));
    aNotYielded = false; // yielded execution
  }
  else {
    return inherited::evaluateAsyncFunction(aFunc, aArgs, aNotYielded);
  }
  return true; // found
}


void SceneScriptContext::channelOpComplete()
{
  SOLOG(output, LOG_INFO, "scene script: channel operation complete");
  ExpressionValue res;
  continueWithAsyncFunctionResult(res);
}



bool SceneScriptContext::evaluateFunction(const string &aFunc, const FunctionArguments &aArgs, ExpressionValue &aResult)
{
  bool isDimchannel = aFunc=="dimchannel";
  if ((isDimchannel || aFunc=="channel") && (aArgs.size()>=isDimchannel ? 2 : 1) && aArgs.size()<=3) {
    // channel(channelid)               - return the value of the specified channel
    // [dim]channel(channelid, value)   - set the channel value to the specified value or dim it relatively
    // [dim]channel(channelid, value, transitiontime)
    if (aArgs[0].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
    ChannelBehaviourPtr channel = output.getChannelById(aArgs[0].stringValue());
    if (!channel) {
      aResult.setNull("unknown channel");
    }
    else {
      // channel found
      if (aArgs.size()==1) {
        // return channel value
        aResult.setNumber(channel->getChannelValueCalculated());
      }
      else {
        // set value
        if (aArgs[1].notValue()) return errorInArg(aArgs[0], aResult); // return error/null from argument
        MLMicroSeconds transitionTime = 0; // default to immediate
        if (aArgs.size()>2) {
          if (!aArgs[2].isNull()) {
            if (aArgs[2].notValue()) return errorInArg(aArgs[2], aResult); // return error/null from argument
            transitionTime = aArgs[2].numValue()*Second;
          }
        }
        if (isDimchannel)
          channel->dimChannelValue(aArgs[1].numValue(), transitionTime);
        else
          channel->setChannelValue(aArgs[1].numValue(), transitionTime, true); // always apply
        aResult.setBool(true);
      }
    }
  }
  else {
    return inherited::evaluateFunction(aFunc, aArgs, aResult);
  }
  return true; // found
}


#endif // ENABLE_SCENE_SCRIPT && ENABLE_EXPRESSIONS




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
  aRow->getCastedIfNotNull<VdcOutputMode, int>(aIndex++, outputMode);
  uint64_t flags = aRow->getCastedWithDefault<uint64_t, long long int>(aIndex++, 0);
  aRow->getCastedIfNotNull<uint64_t, long long int>(aIndex++, outputGroups);
  // decode my own flags
  pushChanges = flags & outputflag_pushChanges;
  // pass the flags out to subclass which called this superclass to get the flags (and decode themselves)
  if (aCommonFlagsP) *aCommonFlagsP = flags;
}


// bind values to passed statement
void OutputBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // encode the flags
  if (pushChanges) aCommonFlags |= outputflag_pushChanges;
  // bind the fields
  aStatement.bind(aIndex++, outputMode);
  aStatement.bind(aIndex++, (long long int)aCommonFlags);
  aStatement.bind(aIndex++, (long long int)outputGroups);
}


ErrorPtr OutputBehaviour::loadChildren()
{
  if (device.getVdcHost().doPersistChannels()) {
    for (ChannelBehaviourVector::iterator pos=channels.begin(); pos!=channels.end(); ++pos) {
      (*pos)->load();
    }
  }
  return inherited::loadChildren();
}


ErrorPtr OutputBehaviour::saveChildren()
{
  if (device.getVdcHost().doPersistChannels()) {
    for (ChannelBehaviourVector::iterator pos=channels.begin(); pos!=channels.end(); ++pos) {
      (*pos)->save();
    }
  }
  return inherited::saveChildren();
}

ErrorPtr OutputBehaviour::deleteChildren()
{
  for (ChannelBehaviourVector::iterator pos=channels.begin(); pos!=channels.end(); ++pos) {
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
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// settings properties

enum {
  mode_key,
  pushChanges_key,
  groups_key,
  numSettingsProperties
};


int OutputBehaviour::numSettingsProps() { return numSettingsProperties; }
const PropertyDescriptorPtr OutputBehaviour::getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numSettingsProperties] = {
    { "mode", apivalue_uint64, mode_key+settings_key_offset, OKEY(output_key) },
    { "pushChanges", apivalue_bool, pushChanges_key+settings_key_offset, OKEY(output_key) },
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
          aPropValue->setUint8Value(outputFunction);
          return true;
        case outputUsage_key+descriptions_key_offset:
          aPropValue->setUint16Value(outputUsage);
          return true;
        case variableRamp_key+descriptions_key_offset:
          aPropValue->setBoolValue(variableRamp);
          return true;
        case maxPower_key+descriptions_key_offset:
          aPropValue->setDoubleValue(maxPower);
          return true;
        // Settings properties
        case mode_key+settings_key_offset:
          aPropValue->setUint8Value(actualOutputMode()); // return actual mode, never outputmode_default
          return true;
        case pushChanges_key+settings_key_offset:
          aPropValue->setBoolValue(pushChanges);
          return true;
        // State properties
        case localPriority_key+states_key_offset:
          aPropValue->setBoolValue(localPriority);
          return true;
        case transitiontime_key+states_key_offset:
          aPropValue->setDoubleValue((double)transitionTime/Second);
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
        case pushChanges_key+settings_key_offset:
          setPVar(pushChanges, aPropValue->boolValue());
          return true;
        // State properties
        case localPriority_key+states_key_offset:
          localPriority = aPropValue->boolValue();
          return true;
        case transitiontime_key+states_key_offset:
          transitionTime = aPropValue->doubleValue()*Second;
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
  return string_format("%s: %s", device.logContextPrefix().c_str(), getTypeName());
}



string OutputBehaviour::description()
{
  string s = string_format("%s behaviour", shortDesc().c_str());
  string_format_append(s, "\n- hardware output function: %d, default output mode: %d", outputFunction, defaultOutputMode);
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

#if ENABLE_SCENE_SCRIPT && ENABLE_P44SCRIPT

using namespace P44Script;

static void channelOpComplete(BuiltinFunctionContextPtr f, OutputBehaviourPtr aOutput)
{
  SPLOG(aOutput, LOG_INFO, "scene script: channel operation complete");
  f->finish();
}

// applychannels()
// applychannels(forced)
static const BuiltInArgDesc applychannels_args[] = { { numeric|optionalarg } };
static const size_t applychannels_numargs = sizeof(applychannels_args)/sizeof(BuiltInArgDesc);
static void applychannels_func(BuiltinFunctionContextPtr f)
{
  OutputObj* o = dynamic_cast<OutputObj*>(f->thisObj().get());
  assert(o);
  if (f->arg(0)->boolValue()) {
    // force apply, invalidate all channels first
    o->output()->getDevice().invalidateAllChannels();
  }
  SPLOG(o->output(), LOG_INFO, "scene script: applychannels() requests applying channels now");
  o->output()->getDevice().requestApplyingChannels(boost::bind(&channelOpComplete, f, o->output()), false);
}

// syncchannels()
static void syncchannels_func(BuiltinFunctionContextPtr f)
{
  OutputObj* o = dynamic_cast<OutputObj*>(f->thisObj().get());
  assert(o);
  SPLOG(o->output(), LOG_INFO, "scene script: syncchannels() requests applying channels now");
  o->output()->getDevice().requestUpdatingChannels(boost::bind(&channelOpComplete, f, o->output()));
}


// channel(channelid)               - return the value of the specified channel
// [dim]channel(channelid, value)   - set the channel value to the specified value or dim it relatively
// [dim]channel(channelid, value, transitiontime)
static const BuiltInArgDesc channel_args[] = { { text }, { numeric|optionalarg }, { numeric|optionalarg } };
static const size_t channel_numargs = sizeof(channel_args)/sizeof(BuiltInArgDesc);
static void channel_funcImpl(bool aDim, BuiltinFunctionContextPtr f)
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
        f->finish(new NumericValue(channel->getChannelValueCalculated()));
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
  channel_funcImpl(false, f);
}
static void dimchannel_func(BuiltinFunctionContextPtr f)
{
  channel_funcImpl(true, f);
}

static const BuiltinMemberDescriptor outputMembers[] = {
  { "applychannels", executable|null, applychannels_numargs, applychannels_args, &applychannels_func },
  { "syncchannels", executable|null, 0, NULL, &syncchannels_func },
  { "channel", executable|numeric, channel_numargs, channel_args, &channel_func },
  { "dimchannel", executable|numeric, channel_numargs, channel_args, &dimchannel_func },
  { NULL } // terminator
};


static BuiltInMemberLookup* sharedOutputMemberLookupP = NULL;

OutputObj::OutputObj(OutputBehaviourPtr aOutput) :
  mOutput(aOutput)
{
  if (sharedOutputMemberLookupP==NULL) {
    sharedOutputMemberLookupP = new BuiltInMemberLookup(outputMembers);
    sharedOutputMemberLookupP->isMemberVariable(); // disable refcounting
  }
  registerMemberLookup(sharedOutputMemberLookupP);
}


#endif // ENABLE_SCENE_SCRIPT && ENABLE_P44SCRIPT
