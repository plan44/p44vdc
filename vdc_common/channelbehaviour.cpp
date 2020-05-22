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

#include "channelbehaviour.hpp"
#include "outputbehaviour.hpp"
#include "math.h"

using namespace p44;

// MARK: - channel behaviour

ChannelBehaviour::ChannelBehaviour(OutputBehaviour &aOutput, const string aChannelId) :
  inheritedParams(aOutput.device.getVdcHost().getDsParamStore()),
  output(aOutput),
  channelId(aChannelId),
  channelUpdatePending(false), // no output update pending
  nextTransitionTime(0), // none
  channelLastSync(Never), // we don't known nor have we sent the output state
  cachedChannelValue(0), // channel output value cache
  volatileValue(true), // not worth saving yet
  previousChannelValue(0), // previous output value
  transitionProgress(1), // no transition in progress
  resolution(1) // dummy default resolution (derived classes must provide sensible defaults)
{
}


void ChannelBehaviour::setResolution(double aResolution)
{
  resolution = aResolution;
}



string ChannelBehaviour::getId()
{
  return getApiId(3); // Note: use API ID 3 string
}


string ChannelBehaviour::getApiId(int aApiVersion)
{
  if (aApiVersion>=3 && !channelId.empty()) {
    return channelId;
  }
  else {
    // no channel ID set, default to decimal string representation of channel type
    return string_format("%d", getChannelType());
  }
}


bool ChannelBehaviour::isPrimary()
{
  // internal convention: first channel is the default channel
  return channelIndex==0;
}


string ChannelBehaviour::description()
{
  return string_format(
    "Channel '%s' (channelType=%d): min: %0.1f, max: %0.1f, resolution: %0.3f",
    getName(), (int)getChannelType(),
    getMin(), getMax(), getResolution()
  );
}



string ChannelBehaviour::getStatusText()
{
  int fracDigits = (int)(-log(resolution)/log(10)+0.99);
  if (fracDigits<0) fracDigits=0;
  return string_format("%0.*f %s", fracDigits, cachedChannelValue, valueUnitName(getChannelUnit(), true).c_str());
}


void ChannelBehaviour::addEnum(const char *aEnumText, uint32_t aEnumValue)
{
  // in reduced footprint, this is a NOP, we don't have the EnumList
  #if !REDUCED_FOOTPRINT
  if (!enumList) enumList = EnumListPtr(new EnumList(true));
  enumList->addMapping(aEnumText, aEnumValue);
  #endif
}





// MARK: - channel value handling


bool ChannelBehaviour::transitionStep(double aStepSize)
{
  if (aStepSize<=0) {
    transitionProgress = 0; // start
    return true; // in transition
  }
  if (inTransition()) {
    setTransitionProgress(transitionProgress+aStepSize);
    return inTransition(); // transition might be complete with this step
  }
  // no longer in transition
  return false;
}


void ChannelBehaviour::setTransitionProgress(double aProgress)
{
  if (aProgress<0) aProgress = 0;
  // set
  transitionProgress = aProgress;
  if (transitionProgress>=1) {
    // transition complete
    transitionProgress=1;
    previousChannelValue = cachedChannelValue; // end of transition reached, old previous value is no longer needed
  }
}



void ChannelBehaviour::setTransitionValue(double aCurrentValue, bool aIsInitial)
{
  if (aIsInitial) {
    // initial value of transition (rather than previously known cached one)
    previousChannelValue = aCurrentValue;
    transitionProgress = 0; // start of transition
  }
  else {
    // intermediate value within transition
    double d = cachedChannelValue-previousChannelValue;
    setTransitionProgress(d==0 ? 1 : aCurrentValue/d-previousChannelValue);
  }
}




bool ChannelBehaviour::inTransition()
{
  return transitionProgress<1;
}


double ChannelBehaviour::getChannelValue()
{
  // current value is cached value
  return cachedChannelValue;
}


bool ChannelBehaviour::getChannelValueBool()
{
  return getChannelValue() >= (getMax()-getMin())/2;
}



double ChannelBehaviour::getTransitionalValue()
{
  if (inTransition()) {
    double d = cachedChannelValue-previousChannelValue;
    if (wrapsAround()) {
      // wraparound channels - use shorter distance
      double r = getMax()-getMin();
      // - find out shorter transition distance
      double ad = fabs(d);
      if (ad>r/2) {
        // more than half the range -> other way around is shorter
        ad = r-ad; // shorter way
        d = ad * (d>=0 ? -1 : 1); // opposite sign of original
      }
      double res = previousChannelValue+transitionProgress*d;
      // - wraparound
      if (res>=getMax()) res -= r;
      else if (res<getMin()) res += r;
      return res;
    }
    else {
      // simple non-wrapping transition
      return previousChannelValue+transitionProgress*d;
    }
  }
  else {
    // current value is cached value
    return cachedChannelValue;
  }
}


// used at startup and before saving scenes to get the current value FROM the hardware
// NOT to be used to change the hardware channel value!
void ChannelBehaviour::syncChannelValue(double aActualChannelValue, bool aAlwaysSync, bool aVolatile)
{
  if (!channelUpdatePending || aAlwaysSync) {
    if (cachedChannelValue!=aActualChannelValue || LOGENABLED(LOG_DEBUG)) {
      // show only changes except if debugging
      SALOG(output.device,LOG_INFO,
        "Channel '%s': cached value synchronized from %0.2f -> %0.2f%s",
        getName(), cachedChannelValue, aActualChannelValue,
        aVolatile ? " (derived/volatile)" : ""
      );
    }
    // make sure new value is within bounds
    if (aActualChannelValue>getMax())
      aActualChannelValue = getMax();
    else if (aActualChannelValue<getMin())
      aActualChannelValue = getMin();
    // apply
    setPVar(volatileValue, aVolatile); // valatile status is persisted as NULL value, so must mark dirty on change
    if (volatileValue) {
      cachedChannelValue = aActualChannelValue; // when volatile, the actual channel value is not persisted, just updated
    }
    else {
      setPVar(cachedChannelValue, aActualChannelValue);
    }
    // reset transitions and pending updates
    previousChannelValue = cachedChannelValue;
    transitionProgress = 1; // not in transition
    channelUpdatePending = false; // we are in sync
    channelLastSync = MainLoop::now(); // value is current
  }
}


void ChannelBehaviour::syncChannelValueBool(bool aValue, bool aAlwaysSync)
{
  if (aValue!=getChannelValueBool()) {
    syncChannelValue(aValue ? getMax() : getMin(), aAlwaysSync);
  }
}




void ChannelBehaviour::setChannelValue(double aNewValue, MLMicroSeconds aTransitionTimeUp, MLMicroSeconds aTransitionTimeDown, bool aAlwaysApply)
{
  setChannelValue(aNewValue, aNewValue>getTransitionalValue() ? aTransitionTimeUp : aTransitionTimeDown, aAlwaysApply);
}


bool ChannelBehaviour::setChannelValueIfNotDontCare(DsScenePtr aScene, double aNewValue, MLMicroSeconds aTransitionTimeUp, MLMicroSeconds aTransitionTimeDown, bool aAlwaysApply)
{
  if (aScene->isSceneValueFlagSet(getChannelIndex(), valueflags_dontCare))
    return false; // don't care, don't set
  setChannelValue(aNewValue, aNewValue>getTransitionalValue() ? aTransitionTimeUp : aTransitionTimeDown, aAlwaysApply);
  return true; // actually set
}




void ChannelBehaviour::setChannelValue(double aNewValue, MLMicroSeconds aTransitionTime, bool aAlwaysApply)
{
  // round to resolution
  if (enforceResolution() && getResolution()>0) {
    aNewValue = round(aNewValue/getResolution())*getResolution();
  }
  // make sure new value is within bounds
  if (wrapsAround()) {
    // In wrap-around mode, the max value is considered identical to the min value, so already REACHING it must wrap around
    double range = getMax()-getMin();
    int tms = (aNewValue-getMin()) / range;
    if (tms>=1) {
      aNewValue -= range*tms;
    }
    else if (tms<0) {
      aNewValue += range*(tms+1);
    }
  }
  else {
    // setting value between and including max and min is ok, everything above and below will be capped to max and min
    if (aNewValue<getMin()) {
      aNewValue = getMin(); // just stay at min
    }
    else if (aNewValue>getMax()) {
      aNewValue = getMax(); // just stay at max
    }
  }
  // prevent propagating changes smaller than device resolution, but always apply when transition is in progress
  if (aAlwaysApply || inTransition() || fabs(aNewValue-cachedChannelValue)>=getResolution()) {
    SALOG(output.device, LOG_INFO,
      "Channel '%s' is requested to change from %0.2f ->  %0.2f (transition time=%d mS)",
      getName(), cachedChannelValue, aNewValue, (int)(aTransitionTime/MilliSecond)
    );
    // setting new value captures current (possibly transitional) value as previous and completes transition
    previousChannelValue = channelLastSync!=Never ? getTransitionalValue() : aNewValue; // If there is no valid previous value, set current as previous.
    transitionProgress = 1; // consider done
    // save target parameters for next transition
    setPVar(cachedChannelValue, aNewValue); // might need to be persisted
    nextTransitionTime = aTransitionTime;
    channelUpdatePending = true; // pending to be sent to the device
  }
  setPVar(volatileValue, false); // channel actively set, is not volatile
}


double ChannelBehaviour::dimChannelValue(double aIncrement, MLMicroSeconds aTransitionTime)
{
  double newValue = cachedChannelValue+aIncrement;
  if (wrapsAround()) {
    // In wrap-around mode, the max value is considered identical to the min value, so already REACHING it must wrap around
    if (newValue>=getMax()) {
      newValue -= getMax()-getMin(); // wrap from max to min
    }
    else if (newValue<getMin()) {
      newValue += getMax()-getMin(); // wrap from min to max (minDim is not considered in wraparound mode, makes no sense)
    }
  }
  else {
    // normal dimming, will stop at minDim and max
    if (newValue<getMinDim()) {
      newValue = getMinDim(); // just stay at min
    }
    else if (newValue>getMax()) {
      newValue = getMax(); // just stay at max
    }
  }
  // apply (silently), only if value has actually changed (but even if change is below resolution)
  if (newValue!=cachedChannelValue) {
    // setting new value captures current (possibly transitional) value as previous and completes transition
    previousChannelValue = channelLastSync!=Never ? getTransitionalValue() : newValue; // If there is no valid previous value, set current as previous.
    transitionProgress = 1; // consider done
    // save target parameters for next transition
    setPVar(cachedChannelValue, newValue); // might need to be persisted
    nextTransitionTime = aTransitionTime;
    channelUpdatePending = true; // pending to be sent to the device
  }
  setPVar(volatileValue, false); // channel actively dimmed, is not volatile
  return newValue;
}



void ChannelBehaviour::channelValueApplied(bool aAnyWay)
{
  if (channelUpdatePending || aAnyWay) {
    channelUpdatePending = false; // applied (might still be in transition, though)
    channelLastSync = MainLoop::now(); // now we know that we are in sync
    if (!aAnyWay) {
      // only log when actually of importance (to prevent messages for devices that apply mostly immediately)
      SALOG(output.device, LOG_INFO,
        "Channel '%s' has applied new value %0.2f to hardware%s",
        getName(), cachedChannelValue, inTransition() ? " (still in transition)" : " (complete)"
      );
    }
  }
}



// MARK: - channel persistence


const char *ChannelBehaviour::tableName()
{
  return "ChannelStates";
}

// data field definitions

static const size_t numFields = 1;

size_t ChannelBehaviour::numFieldDefs()
{
  return inheritedParams::numFieldDefs()+numFields;
}


const FieldDefinition *ChannelBehaviour::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "channelValue", SQLITE_FLOAT }
  };
  if (aIndex<inheritedParams::numFieldDefs())
    return inheritedParams::getFieldDef(aIndex);
  aIndex -= inheritedParams::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void ChannelBehaviour::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inheritedParams::loadFromRow(aRow, aIndex, NULL); // no common flags
  // get the fields
  if (aRow->getIfNotNull<double>(aIndex++, cachedChannelValue)) {
    // loading a non-NULL persistent channel value always means it must be propagated to the hardware
    channelUpdatePending = true;
    volatileValue = false;
  }
  else {
    volatileValue = true;
  }
}


// bind values to passed statement
void ChannelBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  if (volatileValue) {
    aStatement.bind(aIndex++); // volatile values are not saved
  }
  else {
    aStatement.bind(aIndex++, cachedChannelValue);
  }
}


string ChannelBehaviour::getDbKey()
{
  // Note - we do not key the channel persistence with output behaviour settings' ROWID,
  //   as this often does not exist at all, but use the deviceID+channelID as key, so
  //   channels can be persisted independently of device settings.
  return string_format("%s_%s",output.device.getDsUid().getString().c_str(),getId().c_str());
}


ErrorPtr ChannelBehaviour::load()
{
  ErrorPtr err = loadFromStore(getDbKey().c_str());
  if (Error::notOK(err)) SALOG(output.device, LOG_ERR,"Error loading channel '%s'", getId().c_str());
  return err;
}


ErrorPtr ChannelBehaviour::save()
{
  ErrorPtr err = saveToStore(getDbKey().c_str(), false); // only one record per dbkey (=per device+channelid)
  if (Error::notOK(err)) SALOG(output.device, LOG_ERR,"Error saving channel '%s'", getId().c_str());
  return err;
}


ErrorPtr ChannelBehaviour::forget()
{
  return deleteFromStore();
}




// MARK: - channel property access

// Note: this is a simplified single class property access mechanims.
//   ChannelBehaviour is not meant to have more properties in derived classes

enum {
  name_key,
  channelIndex_key,
  dsIndex_key,
  channelType_key,
  siunit_key,
  unitsymbol_key,
  min_key,
  max_key,
  resolution_key,
  #if !REDUCED_FOOTPRINT
  enumvalues_key,
  #endif
  numChannelDescProperties
};

enum {
  numChannelSettingsProperties
};

enum {
  value_key,
  age_key,
  numChannelStateProperties
};

static char channel_Key;
static char channel_enumvalues_key;


int ChannelBehaviour::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  #if !REDUCED_FOOTPRINT
  if (aParentDescriptor->hasObjectKey(channel_enumvalues_key)) {
    // number of enum values
    return enumList ? enumList->numProps() : 0;
  }
  #endif
  switch (aParentDescriptor->parentDescriptor->fieldKey()) {
    case descriptions_key_offset: return numChannelDescProperties;
    case settings_key_offset: return numChannelSettingsProperties;
    case states_key_offset: return numChannelStateProperties;
    default: return 0;
  }
}

PropertyDescriptorPtr ChannelBehaviour::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription channelDescProperties[numChannelDescProperties] = {
    { "name", apivalue_string, name_key+descriptions_key_offset, OKEY(channel_Key) },
    { "channelIndex", apivalue_uint64, channelIndex_key+descriptions_key_offset, OKEY(channel_Key) },
    { "dsIndex", apivalue_uint64, dsIndex_key+descriptions_key_offset, OKEY(channel_Key) },
    { "channelType", apivalue_uint64, channelType_key+descriptions_key_offset, OKEY(channel_Key) },
    { "siunit", apivalue_string, siunit_key+descriptions_key_offset, OKEY(channel_Key) },
    { "symbol", apivalue_string, unitsymbol_key+descriptions_key_offset, OKEY(channel_Key) },
    { "min", apivalue_double, min_key+descriptions_key_offset, OKEY(channel_Key) },
    { "max", apivalue_double, max_key+descriptions_key_offset, OKEY(channel_Key) },
    { "resolution", apivalue_double, resolution_key+descriptions_key_offset, OKEY(channel_Key) },
    #if !REDUCED_FOOTPRINT
    { "values", apivalue_object+propflag_container, enumvalues_key, OKEY(channel_enumvalues_key) }
    #endif
  };
  //static const PropertyDescription channelSettingsProperties[numChannelSettingsProperties] = {
  //};
  static const PropertyDescription channelStateProperties[numChannelStateProperties] = {
    { "value", apivalue_double, value_key+states_key_offset, OKEY(channel_Key) }, // note: so far, pbuf API requires uint here
    { "age", apivalue_double, age_key+states_key_offset, OKEY(channel_Key) },
  };
  #if !REDUCED_FOOTPRINT
  if (aParentDescriptor->hasObjectKey(channel_enumvalues_key)) {
    return enumList ? enumList->getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor) : NULL;
  }
  #endif
  if (aPropIndex>=numProps(aDomain, aParentDescriptor))
    return NULL;
  switch (aParentDescriptor->parentDescriptor->fieldKey()) {
    case descriptions_key_offset:
      return PropertyDescriptorPtr(new StaticPropertyDescriptor(&channelDescProperties[aPropIndex], aParentDescriptor));
      //case settings_key_offset:
      //  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&channelSettingsProperties[aPropIndex], aParentDescriptor));
    case states_key_offset:
      return PropertyDescriptorPtr(new StaticPropertyDescriptor(&channelStateProperties[aPropIndex], aParentDescriptor));
    default:
      return NULL;
  }
}


#if !REDUCED_FOOTPRINT
PropertyContainerPtr ChannelBehaviour::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->isArrayContainer() && aPropertyDescriptor->hasObjectKey(channel_enumvalues_key)) {
    return enumList ? PropertyContainerPtr(this) : PropertyContainerPtr(); // handle enum values array myself
  }
  // unknown here
  return inheritedProps::getContainer(aPropertyDescriptor, aDomain);
}
#endif


// access to all fields
bool ChannelBehaviour::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  #if !REDUCED_FOOTPRINT
  if (aPropertyDescriptor->hasObjectKey(INSTANCE_OKEY(enumList.get()))) {
    return enumList ? enumList->accessField(aMode, aPropValue, aPropertyDescriptor) : false;
  }
  else
  #endif
  if (aPropertyDescriptor->hasObjectKey(channel_Key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Description properties
        case name_key+descriptions_key_offset:
          aPropValue->setStringValue(getName());
          return true;
        case channelIndex_key+descriptions_key_offset:
          if (aPropertyDescriptor->getApiVersion()>=3) return false; // property does not exist any more in v3 and later
          aPropValue->setUint8Value(channelIndex);
          return true;
        case dsIndex_key+descriptions_key_offset:
          aPropValue->setUint8Value(channelIndex);
          return true;
        case channelType_key+descriptions_key_offset:
          aPropValue->setUint8Value(getChannelType());
          return true;
        case siunit_key+descriptions_key_offset:
          aPropValue->setStringValue(valueUnitName(getChannelUnit(), false));
          return true;
        case unitsymbol_key+descriptions_key_offset:
          aPropValue->setStringValue(valueUnitName(getChannelUnit(), true));
          return true;
        case min_key+descriptions_key_offset:
          aPropValue->setDoubleValue(getMin());
          return true;
        case max_key+descriptions_key_offset:
          aPropValue->setDoubleValue(getMax());
          return true;
        case resolution_key+descriptions_key_offset:
          aPropValue->setDoubleValue(getResolution());
          return true;
        // Settings properties
        // - none for now
        // States properties
        case value_key+states_key_offset:
          // get value of channel, possibly calculating it if needed (color conversions)
          aPropValue->setDoubleValue(getChannelValueCalculated());
          return true;
        case age_key+states_key_offset:
          if (channelLastSync==Never || volatileValue)
            aPropValue->setNull(); // no value known, or volatile
          else
            aPropValue->setDoubleValue((double)(MainLoop::now()-channelLastSync)/Second); // time of last sync (does not necessarily relate to currently visible "value", as this might be a to-be-applied new value already)
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        // - none for now
        // States properties
        case value_key+states_key_offset:
          setChannelValue(aPropValue->doubleValue(), output.transitionTime, true); // always apply, default transition time (normally 0, unless set in outputState)
          return true;
      }
    }
  }
  // single class level properties only, don't call inherited
  return false;
}

// MARK: - string channel behaviour

bool StringChannel::setChannelValueIfNotDontCare(DsScenePtr aScene, const string& aNewValue, bool aAlwaysApply)
{
  if (aScene->isSceneValueFlagSet(getChannelIndex(), valueflags_dontCare))
    return false; // don't care, don't set
  setChannelValueString(aNewValue, aAlwaysApply);
  return true; // actually set
}

void StringChannel::syncChannelValueString(const string& aActualChannelValue, bool aAlwaysSync)
{
  if (!channelUpdatePending || aAlwaysSync) {
    if (stringValue!=aActualChannelValue || LOGENABLED(LOG_DEBUG)) {
      // show only changes except if debugging
      LOG(LOG_INFO, "Channel '%s': cached value synchronized from %s -> %s",
        getName(), stringValue.c_str(), aActualChannelValue.c_str()
      );
    }
    setPVar(stringValue, aActualChannelValue);
    // reset pending updates
    channelUpdatePending = false; // we are in sync
    channelLastSync = MainLoop::now(); // value is current
  }
}

void StringChannel::setChannelValueString(const string& aNewValue, bool aAlwaysApply)
{
  if (aAlwaysApply || (aNewValue!=stringValue)) {
    LOG(LOG_INFO, "Channel '%s' is requested to change from %s ->  %s", getName(), stringValue.c_str(), aNewValue.c_str());
    setPVar(stringValue, aNewValue); // might need to be persisted
    channelUpdatePending = true; // pending to be sent to the device
  }
}

string StringChannel::getChannelValueString()
{
  return stringValue;
}

static const size_t numStringChannelFields = 1;

size_t StringChannel::numFieldDefs()
{
  return inheritedParams::numFieldDefs()+numStringChannelFields;
}


const FieldDefinition *StringChannel::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numStringChannelFields] = {
    { "stringChannelValue", SQLITE_TEXT }
  };
  if (aIndex<inheritedParams::numFieldDefs())
    return inheritedParams::getFieldDef(aIndex);
  aIndex -= inheritedParams::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void StringChannel::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inheritedParams::loadFromRow(aRow, aIndex, NULL); // no common flags
  string newValue = nonNullCStr(aRow->get<const char *>(aIndex++));
  if (newValue != stringValue) {
    channelUpdatePending = true;
  }
}


// bind values to passed statement
void StringChannel::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, stringValue.c_str(), false);
}


static char stringchannel_Key;


int StringChannel::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->isRootOfObject()) {
    switch (aParentDescriptor->parentDescriptor->fieldKey()) {
      case states_key_offset: return numChannelStateProperties;
      default: return inherited::numProps(aDomain, aParentDescriptor);;
    }
  }
  return inherited::numProps(aDomain, aParentDescriptor);
}

PropertyDescriptorPtr StringChannel::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription channelStateProperties[numChannelStateProperties] = {
    { "value", apivalue_string, value_key+states_key_offset, OKEY(stringchannel_Key) },
    { "age", apivalue_double, age_key+states_key_offset, OKEY(stringchannel_Key) }
  };
  if (aParentDescriptor->parentDescriptor->isRootOfObject()) {
    if (aPropIndex>=numProps(aDomain, aParentDescriptor))
      return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor);;
    switch (aParentDescriptor->parentDescriptor->fieldKey()) {
      case states_key_offset:
        return PropertyDescriptorPtr(new StaticPropertyDescriptor(&channelStateProperties[aPropIndex], aParentDescriptor));
      default:
        return NULL;
    }
  }
  return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor);
}

// access to all fields
bool StringChannel::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aMode==access_read) {
    // read properties
    switch (aPropertyDescriptor->fieldKey()) {
      // Description properties
      // States properties
      case value_key+states_key_offset:
        aPropValue->setType(apivalue_string);
        aPropValue->setStringValue(stringValue);
        return true;
      case age_key+states_key_offset:
        aPropValue->setDoubleValue((double)(MainLoop::now()-channelLastSync)/Second); // time of last sync (does not necessarily relate to currently visible "value", as this might be a to-be-applied new value already)
        return true;
    }
  }
  else {
    // write properties
    switch (aPropertyDescriptor->fieldKey()) {
      // States properties
      case value_key+states_key_offset:
        stringValue = aPropValue->stringValue();
        return true;
    }
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}

