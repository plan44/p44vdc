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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 0

#include "channelbehaviour.hpp"
#include "outputbehaviour.hpp"
#include "math.h"

using namespace p44;

// MARK: - channel behaviour

ChannelBehaviour::ChannelBehaviour(OutputBehaviour &aOutput, const string aChannelId) :
  inheritedParams(aOutput.mDevice.getVdcHost().getDsParamStore()),
  mOutput(aOutput),
  mChannelId(aChannelId),
  mChannelUpdatePending(false), // no output update pending
  mNextTransitionTime(0), // none
  mCustomDimPerMS(0), // standard dimming rate
  mChannelLastSync(Never), // we don't known nor have we sent the output state
  mCachedChannelValue(0), // channel output value cache
  mIsVolatileValue(true), // not worth saving yet
  mPreviousChannelValue(0), // previous output value
  mTransitionStarted(Never), // no transition in progress
  mTransitionDirection(0), // shortest way by default
  mProgress(1), // no transition in progress
  mResolution(1), // dummy default resolution (derived classes must provide sensible defaults)
  mEnforceResolution(false) // do NOT enforce resolution by default.
{
}


void ChannelBehaviour::setResolution(double aResolution)
{
  mResolution = aResolution;
}


string ChannelBehaviour::getId()
{
  return getApiId(3); // Note: use API ID 3 string
}


string ChannelBehaviour::getApiId(int aApiVersion)
{
  if (aApiVersion>=3 && !mChannelId.empty()) {
    return mChannelId;
  }
  else {
    // no channel ID set, default to decimal string representation of channel type
    return string_format("%d", getChannelType());
  }
}


bool ChannelBehaviour::isPrimary()
{
  // internal convention: first channel is the default channel
  return mChannelIndex==0;
}


string ChannelBehaviour::description()
{
  return string_format(
    "Channel '%s' (channelType=%d): min: %0.1f, max: %0.1f, resolution: %0.3f",
    getName(), (int)getChannelType(),
    getMin(), getMax(), getResolution()
  );
}


string ChannelBehaviour::logContextPrefix()
{
  return string_format("%s: channel[%d] %s", mOutput.mDevice.logContextPrefix().c_str(), mChannelIndex, getName());
}


string ChannelBehaviour::contextType() const
{
  return mOutput.contextType() + "/" + getName();
}


int ChannelBehaviour::getLogLevelOffset()
{
  return mOutput.getLogLevelOffset();
}


string ChannelBehaviour::getStatusText()
{
  int fracDigits = (int)(-::log(mResolution)/::log(10)+0.99);
  if (fracDigits<0) fracDigits=0;
  return string_format("%0.*f %s", fracDigits, mCachedChannelValue, valueUnitName(getChannelUnit(), true).c_str());
}


void ChannelBehaviour::addEnum(const char *aEnumText, uint32_t aEnumValue)
{
  // in reduced footprint, this is a NOP, we don't have the EnumList
  #if !REDUCED_FOOTPRINT
  if (!mEnumList) mEnumList = EnumListPtr(new EnumList(true));
  mEnumList->addMapping(aEnumText, aEnumValue);
  #endif
}


#if P44SCRIPT_FULL_SUPPORT

// MARK: - value source implementation

string ChannelBehaviour::getSourceId()
{
  return string_format("%s_C%s", mOutput.mDevice.getDsUid().getString().c_str(), getId().c_str());
}


string ChannelBehaviour::getSourceName()
{
  // get device name or dSUID for context
  string n = mOutput.mDevice.getAssignedName();
  if (n.empty()) {
    // use abbreviated dSUID instead
    string d = mOutput.mDevice.getDsUid().getString();
    n = d.substr(0,8) + "..." + d.substr(d.size()-2,2);
  }
  // append behaviour description
  string_format_append(n, ": %s", getId().c_str());
  return n;
}


double ChannelBehaviour::getSourceValue()
{
  return getChannelValueCalculated(false);
}


MLMicroSeconds ChannelBehaviour::getSourceLastUpdate()
{
  return mChannelLastSync;
}


int ChannelBehaviour::getSourceOpLevel()
{
  return mOutput.mDevice.opStateLevel();
}

#endif // P44SCRIPT_FULL_SUPPORT


// MARK: - transition management

// Note: transition management is for rendering software transitions for outputs that don't have native transitions.
//   Hardware implementations might choose not to use it, in favor of a hardware-specific transition mechanism.


bool ChannelBehaviour::updateTimedTransition(MLMicroSeconds aNow, double aMaxProgress)
{
  if (aNow<=0) {
    if (!inTransition() || mChannelUpdatePending) {
      // initialize transition
      if (
        mNextTransitionTime<=0 || // no transition time OR
        aNow<0 || ( // explicit reset requested OR
          mCachedChannelValue==mPreviousChannelValue && // value unchanged AND
          !(wrapsAround() && mTransitionDirection!=0) // no explicitly directional wraparound transition
        )
      ) {
        // no transitiontime or explicitly no transition requested or no value change: set transition to completed
        OLOG(LOG_INFO, "no or immediate transition");
        return setTransitionProgress(1);
      }
      // start transition NOW
      OLOG(LOG_INFO, "initialized for transition in %d mS", (int)(mNextTransitionTime/MilliSecond));
      mTransitionStarted = MainLoop::now();
      mProgress = 0;
      return true;
    }
    // a previous transition is still running, but no channel update is pending
    // This means the transition should just keep running without re-initializing
    OLOG(LOG_INFO, "no channel update pending: keep previous transition running");
  }
  // calculate new progress
  double progress = mNextTransitionTime==0 ? 1 : (double)(aNow-mTransitionStarted)/mNextTransitionTime;
  if (aMaxProgress!=0 && progress>aMaxProgress) progress = aMaxProgress; // keep at provided "almost finished"
  return setTransitionProgress(progress);
}


void ChannelBehaviour::stopTransition()
{
  if (inTransition()) {
    // capture current transitional value as new current value
    mCachedChannelValue = getChannelValue(true);
    setTransitionProgress(1);
  }
}


bool ChannelBehaviour::setTransitionProgress(double aProgress)
{
  if (aProgress<0) aProgress = 0;
  mProgress = aProgress;
  if (mProgress>=1) {
    // transition complete
    mProgress=1;
    mTransitionStarted = Never;
    mPreviousChannelValue = mCachedChannelValue; // end of transition reached, old previous value is no longer needed
    mTransitionDirection = 0; // reset direction to prevent re-running full wraparound transitions again
    return false; // no longer in transition
  }
  return true; // still in transition
}


bool ChannelBehaviour::inTransition()
{
  return mProgress<1;
}


void ChannelBehaviour::startExternallyTimedTransition(MLMicroSeconds aEstimatedTransitionTime)
{
  if (aEstimatedTransitionTime!=Infinite) {
    mNextTransitionTime = aEstimatedTransitionTime;
    // start time-defined transition
    updateTimedTransition();
  }
  else {
    // start
    OLOG(LOG_INFO, "initialized for externally defined transition of unknown time");
    mNextTransitionTime = Infinite; // unknown
    mTransitionStarted = MainLoop::now(); // still remember time started
    setTransitionProgress(0);
  }
}


bool ChannelBehaviour::reportChannelProgress(double aTransitionalChannelValue)
{
  if (mTransitionStarted==Infinite) {
    // no progress simulation started yet, start it now
    startExternallyTimedTransition(Infinite);
  }
  double dist = mCachedChannelValue-mPreviousChannelValue;
  double progress = 1; // assume done
  if (dist!=0 && aTransitionalChannelValue!=mCachedChannelValue) {
    progress = (aTransitionalChannelValue-mPreviousChannelValue) / dist;
  }
  if (progress<mProgress) {
    OLOG(LOG_WARNING, "decreasing progress %.2f from value %.2f NOT APPLIED", mProgress, aTransitionalChannelValue);
    return mProgress>=1; // done when already done before
  }
  bool done = setTransitionProgress(progress);
  OLOG(LOG_INFO, "progress recalculated as %.2f from value %.2f", mProgress, aTransitionalChannelValue);
  return done;
}


MLMicroSeconds ChannelBehaviour::remainingTransitionTime()
{
  if (!inTransition()) {
    return 0; // nothing remaining
  }
  if (mNextTransitionTime>0) {
    // timed transition, we know when it is finished
    return mNextTransitionTime*(1-mProgress);
  }
  // estimate from progress and time already spent
  MLMicroSeconds spent = MainLoop::now()-mTransitionStarted;
  // below 10% progress, just assume 10% done
  return spent / (mProgress>0.1 ? mProgress : 0.1);
}



// MARK: - channel value handling

bool ChannelBehaviour::getChannelValueBool()
{
  return getChannelValue() >= (getMax()-getMin())/2;
}


double ChannelBehaviour::getChannelValue(bool aTransitional)
{
  if (inTransition() && aTransitional) {
    double d = mCachedChannelValue-mPreviousChannelValue;
    if (wrapsAround()) {
      double r = getMax()-getMin(); // full range distance
      if (mTransitionDirection!=0) {
        // explicit direction, if sign is wrong or distance apparently zero -> make distance longer
        if (d==0 || ((d>0) != (mTransitionDirection>0))) {
          d += mTransitionDirection*r; // extend distance by range
        }
      }
      else {
        // automatically use shortest direction
        double ad = fabs(d);
        if (ad>r/2) {
          // more than half the range -> other way around is shorter
          ad = r-ad; // shorter way
          d = ad * (d>=0 ? -1 : 1); // opposite sign of original
        }
      }
      double res = mPreviousChannelValue+mProgress*d;
      // - wraparound
      if (res>=getMax()) res -= r;
      else if (res<getMin()) res += r;
      return res;
    }
    else {
      // simple non-wrapping transition
      return mPreviousChannelValue+mProgress*d;
    }
  }
  else {
    // current value is cached value
    return mCachedChannelValue;
  }
}


// used at startup and before saving scenes to get the current value FROM the hardware
// NOT to be used to change the hardware channel value!
bool ChannelBehaviour::syncChannelValue(double aActualChannelValue, bool aAlwaysSync, bool aVolatile)
{
  bool changed = false;
  if (!mChannelUpdatePending || aAlwaysSync) {
    changed = mCachedChannelValue!=aActualChannelValue;
    if (changed || OLOGENABLED(LOG_DEBUG)) {
      // show only changes except if debugging
      OLOG(LOG_INFO,
        "cached value synchronized from %0.2f -> %0.2f%s",
        mCachedChannelValue, aActualChannelValue,
        aVolatile ? " (derived/volatile)" : ""
      );
    }
    // make sure new value is within bounds
    if (aActualChannelValue>getMax())
      aActualChannelValue = getMax();
    else if (aActualChannelValue<getMin())
      aActualChannelValue = getMin();
    // apply
    setPVar(mIsVolatileValue, aVolatile); // valatile status is persisted as NULL value, so must mark dirty on change
    if (mIsVolatileValue) {
      mCachedChannelValue = aActualChannelValue; // when volatile, the actual channel value is not persisted, just updated
    }
    else {
      setPVar(mCachedChannelValue, aActualChannelValue);
    }
    #if P44SCRIPT_FULL_SUPPORT
    sendValueEvent();
    #endif
    // reset transitions and pending updates
    mPreviousChannelValue = mCachedChannelValue;
    mProgress = 1; // not in transition
    mChannelUpdatePending = false; // we are in sync
    mChannelLastSync = MainLoop::now(); // value is current
  }
  return changed;
}


bool ChannelBehaviour::syncChannelValueBool(bool aValue, bool aAlwaysSync)
{
  if (aValue!=getChannelValueBool()) {
    return syncChannelValue(aValue ? getMax() : getMin(), aAlwaysSync);
  }
  return false;
}


void ChannelBehaviour::setChannelValue(double aNewValue, MLMicroSeconds aTransitionTimeUp, MLMicroSeconds aTransitionTimeDown, bool aAlwaysApply)
{
  setChannelValue(aNewValue, aNewValue>getChannelValue(true) ? aTransitionTimeUp : aTransitionTimeDown, aAlwaysApply);
}


bool ChannelBehaviour::setChannelValueIfNotDontCare(DsScenePtr aScene, double aNewValue, MLMicroSeconds aTransitionTimeUp, MLMicroSeconds aTransitionTimeDown, bool aAlwaysApply)
{
  if (aScene->isSceneValueFlagSet(getChannelIndex(), valueflags_dontCare))
    return false; // don't care, don't set
  setChannelValue(aNewValue, aNewValue>getChannelValue(true) ? aTransitionTimeUp : aTransitionTimeDown, aAlwaysApply);
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
  if (aAlwaysApply || inTransition() || fabs(aNewValue-mCachedChannelValue)>=getResolution()) {
    OLOG(LOG_INFO,
      "is requested to change from %0.2f ->  %0.2f (transition time=%dmS)",
      mCachedChannelValue, aNewValue, (int)(aTransitionTime/MilliSecond)
    );
    // setting new value captures current (possibly transitional) value as previous and completes transition
    mPreviousChannelValue = mChannelLastSync!=Never ? getChannelValue(true) : aNewValue; // If there is no valid previous value, set current as previous.
    mProgress = 1; // consider done
    // save target parameters for next transition
    setPVar(mCachedChannelValue, aNewValue); // might need to be persisted
    mNextTransitionTime = aTransitionTime;
    mTransitionDirection = 0; // automatic, shortest way
    mChannelUpdatePending = true; // pending to be sent to the device
  }
  setPVar(mIsVolatileValue, false); // channel actively set, is not volatile
}


void ChannelBehaviour::moveChannelValue(int aDirection, MLMicroSeconds aTimePerUnit)
{
  if (getDimPerMS()==0) return; // non-dimmable channel -> NOP
  if (aDirection==0) {
    // stop
    stopTransition();
  }
  else {
    if (aTimePerUnit==0) {
      // use standard dimming rate
      aTimePerUnit = (1.0/getDimPerMS())*MilliSecond;
    }
    double dist = 0;
    if (wrapsAround()) {
      // do one full round
      dist = getMax()-getMin();
      if (aDirection<0) dist = -dist;
    }
    else {
      // towards end of scale (but not going below minDim)
      dist = (aDirection>0 ? getMax() : getMinDim()) - mCachedChannelValue;
    }
    MLMicroSeconds tt = aTimePerUnit*fabs(dist);
    dimChannelValue(dist, tt);
  }
}



double ChannelBehaviour::dimChannelValue(double aIncrement, MLMicroSeconds aTransitionTime)
{
  if (aIncrement==0) return mCachedChannelValue; // no change
  double newValue = mCachedChannelValue+aIncrement;
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
  // Note: when we get here, an actual increment has been requested.
  // - non-wrapping channels might already be at the end of their range, then nothing needs to be done
  // - dimming a wrapping channel with nonzero transition time *always* means transitioning, possibly one
  //   full round to the same value
  if (newValue!=mCachedChannelValue || (wrapsAround() && aTransitionTime!=0)) {
    FOCUSOLOG(
      "is requested to dim by %f from %0.2f ->  %0.2f (transition time=%dmS)",
      aIncrement, mCachedChannelValue, newValue, (int)(aTransitionTime/MilliSecond)
    );
    // setting new value captures current (possibly transitional) value as previous and completes transition
    mPreviousChannelValue = mChannelLastSync!=Never ? getChannelValue(true) : newValue; // If there is no valid previous value, set current as previous.
    mProgress = 1; // consider done
    // save target parameters for next transition
    setPVar(mCachedChannelValue, newValue); // might need to be persisted
    mNextTransitionTime = aTransitionTime;
    mTransitionDirection = aIncrement>0 ? 1 : -1;
    mChannelUpdatePending = true; // pending to be sent to the device
  }
  setPVar(mIsVolatileValue, false); // channel actively dimmed, is not volatile
  return newValue;
}


void ChannelBehaviour::channelValueApplied(bool aAnyWay)
{
  if (mChannelUpdatePending || aAnyWay) {
    mChannelUpdatePending = false; // applied (might still be in transition, though)
    mChannelLastSync = MainLoop::now(); // now we know that we are in sync
    #if P44SCRIPT_FULL_SUPPORT
    sendValueEvent();
    #endif
    if (!aAnyWay) {
      // only log when actually of importance (to prevent messages for devices that apply mostly immediately)
      OLOG(LOG_INFO,
        "applied new value %0.2f to hardware%s",
        mCachedChannelValue, inTransition() ? " (still in transition)" : " (complete)"
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
  if (aRow->getIfNotNull<double>(aIndex++, mCachedChannelValue)) {
    // loading a non-NULL persistent channel value always means it must be propagated to the hardware
    mChannelUpdatePending = true;
    mIsVolatileValue = false;
  }
  else {
    mIsVolatileValue = true;
  }
}


// bind values to passed statement
void ChannelBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  if (mIsVolatileValue) {
    aStatement.bind(aIndex++); // volatile values are not saved
  }
  else {
    aStatement.bind(aIndex++, mCachedChannelValue);
  }
}


string ChannelBehaviour::getDbKey()
{
  // Note - we do not key the channel persistence with output behaviour settings' ROWID,
  //   as this often does not exist at all, but use the deviceID+channelID as key, so
  //   channels can be persisted independently of device settings.
  return string_format("%s_%s",mOutput.mDevice.getDsUid().getString().c_str(),getId().c_str());
}


ErrorPtr ChannelBehaviour::load()
{
  ErrorPtr err = loadFromStore(getDbKey().c_str());
  if (Error::notOK(err)) OLOG(LOG_ERR,"cannot load: %s", err->text());
  return err;
}


ErrorPtr ChannelBehaviour::save()
{
  ErrorPtr err = saveToStore(getDbKey().c_str(), false); // only one record per dbkey (=per device+channelid)
  if (Error::notOK(err)) OLOG(LOG_ERR,"cannot save: %s", err->text());
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
  transitionalvalue_key,
  transitiontimeleft_key,
  progress_key,
  numChannelStateProperties
};

static char channel_Key;
static char channel_enumvalues_key;


int ChannelBehaviour::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  #if !REDUCED_FOOTPRINT
  if (aParentDescriptor->hasObjectKey(channel_enumvalues_key)) {
    // number of enum values
    return mEnumList ? mEnumList->numProps() : 0;
  }
  #endif
  switch (aParentDescriptor->mParentDescriptor->fieldKey()) {
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
    { "value", apivalue_null, value_key+states_key_offset, OKEY(channel_Key) },
    { "age", apivalue_double, age_key+states_key_offset, OKEY(channel_Key) },
    { "x-p44-transitional", apivalue_double, transitionalvalue_key+states_key_offset, OKEY(channel_Key) },
    { "x-p44-transitiontimeleft", apivalue_double, transitiontimeleft_key+states_key_offset, OKEY(channel_Key) },
    { "x-p44-progress", apivalue_double, progress_key+states_key_offset, OKEY(channel_Key) },
  };
  #if !REDUCED_FOOTPRINT
  if (aParentDescriptor->hasObjectKey(channel_enumvalues_key)) {
    return mEnumList ? mEnumList->getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor) : NULL;
  }
  #endif
  if (aPropIndex>=numProps(aDomain, aParentDescriptor))
    return NULL;
  switch (aParentDescriptor->mParentDescriptor->fieldKey()) {
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
PropertyContainerPtr ChannelBehaviour::getContainer(const PropertyDescriptorPtr aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->isArrayContainer() && aPropertyDescriptor->hasObjectKey(channel_enumvalues_key)) {
    return mEnumList ? PropertyContainerPtr(this) : PropertyContainerPtr(); // handle enum values array myself
  }
  // unknown here
  return inheritedProps::getContainer(aPropertyDescriptor, aDomain);
}
#endif


// access to all fields
bool ChannelBehaviour::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  #if !REDUCED_FOOTPRINT
  if (aPropertyDescriptor->hasObjectKey(INSTANCE_OKEY(mEnumList.get()))) {
    return mEnumList ? mEnumList->accessField(aMode, aPropValue, aPropertyDescriptor) : false;
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
          aPropValue->setUint8Value(mChannelIndex);
          return true;
        case dsIndex_key+descriptions_key_offset:
          aPropValue->setUint8Value(mChannelIndex);
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
          aPropValue->setType(apivalue_double);
          aPropValue->setDoubleValue(getChannelValueCalculated(false));
          return true;
        case age_key+states_key_offset:
          if (mChannelLastSync==Never || mIsVolatileValue)
            aPropValue->setNull(); // no value known, or volatile
          else
            aPropValue->setDoubleValue((double)(MainLoop::now()-mChannelLastSync)/Second); // time of last sync (does not necessarily relate to currently visible "value", as this might be a to-be-applied new value already)
          return true;
        case transitionalvalue_key+states_key_offset:
          if (!inTransition()) return false; // show only when transition is in progress
          // get transitional value of channel
          aPropValue->setDoubleValue(getChannelValueCalculated(true));
          return true;
        case transitiontimeleft_key+states_key_offset:
          if (!inTransition()) return false; // show only when transition is in progress
          aPropValue->setDoubleValue((double)remainingTransitionTime()/Second);
          return true;
        case progress_key+states_key_offset:
          if (!inTransition()) return false; // show only when transition is in progress
          aPropValue->setDoubleValue(transitionProgress());
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
          setChannelValue(aPropValue->doubleValue(), mOutput.mTransitionTime, true); // always apply, default transition time (normally 0, unless set in outputState)
          return true;
      }
    }
  }
  // single class level properties only, don't call inherited
  return false;
}


#if !REDUCED_FOOTPRINT
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
  if (!mChannelUpdatePending || aAlwaysSync) {
    if (mStringValue!=aActualChannelValue || OLOGENABLED(LOG_DEBUG)) {
      // show only changes except if debugging
      OLOG(LOG_INFO, "cached value synchronized from %s -> %s",
        mStringValue.c_str(), aActualChannelValue.c_str()
      );
    }
    setPVar(mStringValue, aActualChannelValue);
    // reset pending updates
    mChannelUpdatePending = false; // we are in sync
    mChannelLastSync = MainLoop::now(); // value is current
  }
}

void StringChannel::setChannelValueString(const string& aNewValue, bool aAlwaysApply)
{
  if (aAlwaysApply || (aNewValue!=mStringValue)) {
    OLOG(LOG_INFO, "is requested to change from %s ->  %s", mStringValue.c_str(), aNewValue.c_str());
    setPVar(mStringValue, aNewValue); // might need to be persisted
    mChannelUpdatePending = true; // pending to be sent to the device
  }
}

string StringChannel::getChannelValueString()
{
  return mStringValue;
}

void StringChannel::setValueOptions(const vector<const char*>& aValues)
{
  mEnumList = EnumListPtr(new EnumList(false));
  mEnumList->addEnumTexts(aValues);
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
  if (newValue != mStringValue) {
    mChannelUpdatePending = true;
  }
}


// bind values to passed statement
void StringChannel::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, mStringValue.c_str(), false);
}


// access to string value property
bool StringChannel::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(channel_Key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Description properties
        // States properties
        case value_key+states_key_offset:
          aPropValue->setType(apivalue_string);
          aPropValue->setStringValue(mStringValue);
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // States properties
        case value_key+states_key_offset:
          setChannelValueString(aPropValue->stringValue());
          return true;
      }
    }
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}

#endif // !REDUCED_FOOTPRINT

