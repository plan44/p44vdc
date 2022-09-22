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


// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL IFNOTREDUCEDFOOTPRINT(7)

#include "fnv.hpp"

#include "device.hpp"

#if ENABLE_LOCALCONTROLLER
  #include "localcontroller.hpp"
#endif

#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "outputbehaviour.hpp"
#include "sensorbehaviour.hpp"

using namespace p44;


// MARK: - DeviceConfigurationDescriptor


enum {
  dcd_description_key,
  numDcdProperties
};

static char dcd_key;


int DeviceConfigurationDescriptor::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return numDcdProperties;
}


PropertyDescriptorPtr DeviceConfigurationDescriptor::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // device level properties
  static const PropertyDescription properties[numDcdProperties] = {
    { "description", apivalue_string, dcd_description_key, OKEY(dcd_key) },
  };
  if (aParentDescriptor->isRootOfObject()) {
    // root level property of this object hierarchy
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


bool DeviceConfigurationDescriptor::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(dcd_key) && aMode==access_read) {
    switch (aPropertyDescriptor->fieldKey()) {
      case dcd_description_key: aPropValue->setStringValue(mDescription); return true;
    }
  }
  return false;
}


// Well-known device configuration id strings
namespace p44 { namespace DeviceConfigurations {

  const char *buttonSingle = "oneWay";
  const char *buttonTwoWay = "twoWay";
  const char *buttonTwoWayReversed = "twoWayInverse";

} }




// MARK: - Device


Device::Device(Vdc *aVdcP) :
  mProgMode(false),
  mIsDimming(false),
  mCurrentDimMode(dimmode_stop),
  mAreaDimmed(0),
  mAreaDimMode(dimmode_stop),
  mPreparedDim(false),
  mVdcP(aVdcP),
  DsAddressable(&aVdcP->getVdcHost()),
  mColorClass(class_black_joker),
  mApplyInProgress(false),
  mMissedApplyAttempts(0),
  mUpdateInProgress(false)
{
  assert(aVdcP);
}


Device::~Device()
{
  mButtons.clear();
  mInputs.clear();
  mSensors.clear();
  mOutput.reset();
}


Vdc& Device::getVdc()
{
  return *mVdcP;
}


void Device::identificationDone(IdentifyDeviceCB aIdentifyCB, ErrorPtr aError, Device *aActualDevice)
{
  if (Error::isOK(aError) && !aActualDevice) aActualDevice = this;
  if (aIdentifyCB) aIdentifyCB(aError, aActualDevice);
}


void Device::identificationFailed(IdentifyDeviceCB aIdentifyCB, ErrorPtr aError)
{
  if (Error::isOK(aError)) aError = TextError::err("identificationFailed called with no error reason");
  identificationDone(aIdentifyCB, aError, NULL);
}


void Device::identificationOK(IdentifyDeviceCB aIdentifyCB, Device *aActualDevice)
{
  identificationDone(aIdentifyCB, ErrorPtr(), aActualDevice);
}


void Device::addedAndInitialized()
{
  // trigger re-applying channel values if this feature is enabled in the vdchost
  if (mOutput && getVdcHost().doPersistChannels()) {
    if (mOutput->reapplyRestoredChannels()) {
      OLOG(LOG_INFO, "requesting re-applying last known channel values to hardware");
      requestApplyingChannels(boost::bind(&Device::channelValuesRestored, this), false);
    }
  }
}


void Device::channelValuesRestored()
{
  OLOG(LOG_INFO, "re-applied last known channel values to hardware");
}



string Device::modelUID()
{
  // combine basic device type identifier, primary group, behaviours and model features and make UUID based dSUID of it
  DsUid vdcNamespace(DSUID_P44VDC_MODELUID_UUID);
  string s;
  addToModelUIDHash(s);
  // now make UUIDv5 type dSUID out of it
  DsUid modelUID;
  modelUID.setNameInSpace(s, vdcNamespace);
  return modelUID.getString();
}


void Device::addToModelUIDHash(string &aHashedString)
{
  string_format_append(aHashedString, "%s:%d:", deviceTypeIdentifier().c_str(), mColorClass);
  // behaviours
  for (BehaviourVector::iterator pos = mButtons.begin(); pos!=mButtons.end(); ++pos) aHashedString += (*pos)->behaviourTypeIdentifier();
  for (BehaviourVector::iterator pos = mInputs.begin(); pos!=mInputs.end(); ++pos) aHashedString += (*pos)->behaviourTypeIdentifier();
  for (BehaviourVector::iterator pos = mSensors.begin(); pos!=mSensors.end(); ++pos) aHashedString += (*pos)->behaviourTypeIdentifier();
  if (mOutput) aHashedString += mOutput->behaviourTypeIdentifier();
  // model features
  for (int f=0; f<numModelFeatures; f++) {
    aHashedString += hasModelFeature((DsModelFeatures)f)==yes ? 'T' : 'F';
  }
}


DsZoneID Device::getZoneID()
{
  if (mDeviceSettings) {
    return mDeviceSettings->mZoneID;
  }
  return 0; // not assigned to a zone
}


void Device::setZoneID(DsZoneID aZoneId)
{
  if (mDeviceSettings) {
    #if ENABLE_LOCALCONTROLLER
    // must report changes of zone usage to local controller
    DsZoneID previousZone = getZoneID();
    if (mDeviceSettings->setPVar(mDeviceSettings->mZoneID, aZoneId)) {
      LocalControllerPtr lc = getVdcHost().getLocalController();
      if (lc) {
        lc->deviceChangesZone(DevicePtr(this), previousZone, aZoneId);
      }
    }
    #else
    mDeviceSettings->setPVar(mDeviceSettings->mZoneID, aZoneId);
    #endif
  }
}


#if ENABLE_JSONBRIDGEAPI
bool Device::bridgeable()
{
  return mDeviceSettings && mDeviceSettings->mAllowBridging;
}
#endif


string Device::vendorName()
{
  // default to same vendor as class container
  return mVdcP->vendorName();
}


void Device::setName(const string &aName)
{
  if (aName!=getAssignedName()) {
    // has changed
    inherited::setName(aName);
    // make sure it will be saved
    if (mDeviceSettings) {
      mDeviceSettings->markDirty();
    }
    #if ENABLE_JSONBRIDGEAPI
    // inform bridges
    if (isBridged()) {
      VdcApiConnectionPtr api = getVdcHost().getBridgeApi();
      if (api) {
        ApiValuePtr query = api->newApiValue();
        query->setType(apivalue_object);
        query->add("name", query->newNull());
        pushNotification(api, query, ApiValuePtr());
      }
    }
    #endif
  }
}


void Device::setColorClass(DsClass aColorClass)
{
  mColorClass = aColorClass;
}


DsClass Device::colorClassFromGroup(DsGroup aGroup)
{
  switch (aGroup) {
    case group_yellow_light:
      return class_yellow_light;
    case group_grey_shadow:
      return class_grey_shadow;
    case group_blue_heating:
    case group_blue_cooling:
    case group_blue_ventilation:
    case group_blue_windows:
    case group_blue_air_recirculation:
    case group_roomtemperature_control:
    case group_ventilation_control:
      return class_blue_climate;
    case group_cyan_audio:
      return class_cyan_audio;
    case group_magenta_video:
      return class_magenta_video;
    case group_red_security:
      return class_red_security;
    case group_green_access:
      return class_green_access;
    case group_black_variable:
      return class_black_joker;
    default:
      return class_undefined;
  }
}


DsClass Device::getDominantColorClass()
{
  // derive dominant color from colors of device's behaviours
  DsClass colorClass = class_undefined;
  if (mOutput) {
    // output is most important
    colorClass = mOutput->getColorClass();
  }
  // if no or undefined output, check input colors
  if (colorClass==class_undefined) {
    // second priority: color of first button
    ButtonBehaviourPtr btn = getButton(0);
    if (btn) colorClass = btn->getColorClass();
  }
  if (colorClass==class_undefined) {
    // third priority: color of first sensor
    SensorBehaviourPtr sns = getSensor(0);
    if (sns) colorClass = sns->getColorClass();
  }
  if (colorClass==class_undefined) {
    // fourth priority: color of first binary input
    BinaryInputBehaviourPtr bin = getInput(0);
    if (bin) colorClass = bin->getColorClass();
  }
  if (colorClass==class_undefined) {
    // final fallback: use colorClass of the device (housing plastic color)
    colorClass = mColorClass;
  }
  return colorClass;
}



bool Device::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getClassColoredIcon("vdsd", getDominantColorClass(), aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}



void Device::getDeviceConfigurations(DeviceConfigurationsVector &aConfigurations, StatusCB aStatusCB)
{
  aConfigurations.clear();
  if (aStatusCB) aStatusCB(ErrorPtr());
}



string Device::getDeviceConfigurationId()
{
  return ""; // base class: no configuration ID
}



ErrorPtr Device::switchConfiguration(const string aConfigurationId)
{
  // base class: no known configurations
  return WebError::webErr(404, "Unknown configurationId '%s'", aConfigurationId.c_str());
}



void Device::installSettings(DeviceSettingsPtr aDeviceSettings)
{
  if (aDeviceSettings) {
    mDeviceSettings = aDeviceSettings;
  }
  else {
    // use standard settings
    mDeviceSettings = DeviceSettingsPtr(new DeviceSettings(*this));
  }
}


// MARK: - behaviours

void Device::addBehaviour(DsBehaviourPtr aBehaviour)
{
  if (aBehaviour) {
    BehaviourVector *bvP = NULL;
    switch (aBehaviour->getType()) {
      case behaviour_button:
        bvP = &mButtons;
        goto addbehaviour;
      case behaviour_binaryinput:
        bvP = &mInputs;
        goto addbehaviour;
      case behaviour_sensor:
        bvP = &mSensors;
        goto addbehaviour;
      addbehaviour:
      {
        // set automatic id if none set before
        if (aBehaviour->mBehaviourId.empty()) {
          aBehaviour->mBehaviourId = aBehaviour->getAutoId();
        }
        // check for duplicate id
        int instance = 1;
        string id = aBehaviour->mBehaviourId; // start with plain ID
        BehaviourVector::iterator pos = bvP->begin();
        while (pos!=bvP->end()) {
          if ((*pos)->mBehaviourId==id) {
            // duplicate
            instance++;
            id = string_format("%s_%d", aBehaviour->mBehaviourId.c_str(), instance);
            pos = bvP->begin(); // re-check from beginning
          }
          ++pos;
        }
        // now the id is unique for sure
        aBehaviour->mBehaviourId = id; // assign it
        // assign the index
        aBehaviour->mIndex = bvP->size();
        // add it
        bvP->push_back(aBehaviour);
        break;
      }
      case behaviour_output:
      case behaviour_actionOutput:
      {
        aBehaviour->mIndex = 0;
        mOutput = boost::dynamic_pointer_cast<OutputBehaviour>(aBehaviour);
        break;
      }
      default:
        LOG(LOG_ERR, "Device::addBehaviour: unknown behaviour type");
    }
  }
  else {
    LOG(LOG_ERR, "Device::addBehaviour: NULL behaviour passed");
  }
}


DsBehaviourPtr Device::getFromBehaviourVector(BehaviourVector &aBV, int aIndex, const string &aId)
{
  if (aIndex==Device::by_id_or_index) {
    if (isdigit(*aId.c_str())) {
      sscanf(aId.c_str(), "%d", &aIndex);
    }
  }
  if (aIndex>=0) {
    // directly by index
    if (aIndex<aBV.size()) {
      return aBV[aIndex];
    }
  }
  else if (aIndex!=Device::by_index && !aId.empty()) {
    for (BehaviourVector::iterator pos = aBV.begin(); pos != aBV.end(); ++pos) {
      if ((*pos)->getId()==aId) {
        return *pos;
      }
    }
  }
  // not found
  return DsBehaviourPtr();
}



ButtonBehaviourPtr Device::getButton(int aIndex, const string aId)
{
  return boost::dynamic_pointer_cast<ButtonBehaviour>(getFromBehaviourVector(mButtons, aIndex, aId));
}


SensorBehaviourPtr Device::getSensor(int aIndex, const string aId)
{
  return boost::dynamic_pointer_cast<SensorBehaviour>(getFromBehaviourVector(mSensors, aIndex, aId));
}


BinaryInputBehaviourPtr Device::getInput(int aIndex, const string aId)
{
  return boost::dynamic_pointer_cast<BinaryInputBehaviour>(getFromBehaviourVector(mInputs, aIndex, aId));
}


OutputBehaviourPtr Device::getOutput()
{
  return mOutput;
}


DsBehaviourPtr Device::behaviourById(const string aId)
{
  DsBehaviourPtr b;
  if (mOutput && mOutput->mBehaviourId==aId) b = mOutput;
  if (!b) b = getFromBehaviourVector(mSensors, by_id, aId);
  if (!b) b = getFromBehaviourVector(mInputs, by_id, aId);
  if (!b) b = getFromBehaviourVector(mButtons, by_id, aId);
  return b;
}


void Device::vdSMAnnouncementAcknowledged()
{
  // Push current values of all sensors and inputs
  for (BehaviourVector::iterator pos = mInputs.begin(); pos!=mInputs.end(); ++pos) {
    if ((*pos)->hasDefinedState()) (*pos)->pushBehaviourState(true, false);
  }
  for (BehaviourVector::iterator pos = mSensors.begin(); pos!=mSensors.end(); ++pos) {
    if ((*pos)->hasDefinedState()) (*pos)->pushBehaviourState(true, false);
  }
}


// MARK: - model features

namespace p44 {
  const char *modelFeatureNames[numModelFeatures] = {
    "dontcare",
    "blink",
    "ledauto",
    "leddark",
    "transt",
    "outmode",
    "outmodeswitch",
    "outmodegeneric",
    "outvalue8",
    "pushbutton",
    "pushbdevice",
    "pushbsensor",
    "pushbarea",
    "pushbadvanced",
    "pushbcombined",
    "shadeprops",
    "shadeposition",
    "motiontimefins",
    "optypeconfig",
    "shadebladeang",
    "highlevel",
    "consumption",
    "jokerconfig",
    "akmsensor",
    "akminput",
    "akmdelay",
    "twowayconfig",
    "outputchannels",
    "heatinggroup",
    "heatingoutmode",
    "heatingprops",
    "pwmvalue",
    "valvetype",
    "extradimmer",
    "umvrelay",
    "blinkconfig",
    "umroutmode",
    "fcu",
    "extendedvalvetypes",
    "identification"
  };
}

Tristate Device::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // ask output first, might have more specific info
  if (mOutput) {
    Tristate hasFeature = mOutput->hasModelFeature(aFeatureIndex);
    if (hasFeature!=undefined) return hasFeature; // output has a say about the feature, no need to check at device level
  }
  // now check for device level features
  switch (aFeatureIndex) {
    case modelFeature_identification:
      return canIdentifyToUser() ? yes : no;
    case modelFeature_dontcare:
      // Generic: all devices with scene table have the ability to set scene's don't care flag
      return boost::dynamic_pointer_cast<SceneDeviceSettings>(mDeviceSettings)!=NULL ? yes : no;
    case modelFeature_ledauto:
    case modelFeature_leddark:
      // Virtual devices do not have the standard dS LED at all
      return no;
    case modelFeature_pushbutton:
    case modelFeature_pushbarea:
    case modelFeature_pushbadvanced:
      // Assumption: any device with a buttonInputBehaviour has these props
      return mButtons.size()>0 ? yes : no;
    case modelFeature_pushbsensor:
      return no; // we definitely don't have buttons that can be converted to sensors
    case modelFeature_pushbdevice:
      // Check if any of the buttons has localbutton functionality available
      for (BehaviourVector::iterator pos = mButtons.begin(); pos!=mButtons.end(); ++pos) {
        ButtonBehaviourPtr b = boost::dynamic_pointer_cast<ButtonBehaviour>(*pos);
        if (b && b->mSupportsLocalKeyMode) {
          return yes; // there is a button with local key mode support
        }
      }
      return no; // no button that supports local key mode
    case modelFeature_pushbcombined:
      return no; // this is for SDS200 only, does not make sense with vdcs at all
    case modelFeature_twowayconfig: {
      // devices with one button that has combinables>1 can possibly be combined and thus need this modelfeature to show the UI
      if (mButtons.size()!=1) return no; // none or multiple buttons in this device -> not combinable
      ButtonBehaviourPtr b = getButton(0);
      return b->mCombinables>1 ? yes : no;
    }
    case modelFeature_highlevel:
      // Assumption: only black joker devices can have a high-level (app) functionality
      return mColorClass==class_black_joker ? yes : no;
    case modelFeature_jokerconfig:
      // Assumption: black joker devices need joker config (setting color) only if there are buttons or an output.
      // Pure sensors or binary inputs don't need color config
      return mColorClass==class_black_joker && (mOutput || mButtons.size()>0) ? yes : no;
    case modelFeature_akmsensor:
      // current dSS state is that it can only provide function setting for binary input 0
      if (mInputs.size()>0) {
        BinaryInputBehaviourPtr b = getInput(0);
        if (b->getHardwareInputType()==binInpType_none) {
          return yes; // Input 0 has no predefined function, "akmsensor" can provide setting UI for that
        }
      }
      /* for a better future:
      // Assumption: only devices with binaryinputs that do not have a predefined type need akmsensor
      for (BehaviourVector::iterator pos = binaryInputs.begin(); pos!=binaryInputs.end(); ++pos) {
        BinaryInputBehaviourPtr b = boost::dynamic_pointer_cast<BinaryInputBehaviour>(*pos);
        if (b && b->getHardwareInputType()==binInpType_none) {
          return yes; // input with no predefined functionality, need to be able to configure sensor
        }
      }
      */
      // no inputs or all inputs have predefined functionality
      return no;
    case modelFeature_akminput:
    case modelFeature_akmdelay:
      // TODO: once binaryInputs support the AKM binary input settings (polarity, delays), this should be enabled
      //   for configurable inputs (most likely those that already have modelFeature_akmsensor)
      return no; // %%% for now
    default:
      return undefined; // not known
  }
}


// MARK: - Channels


int Device::numChannels()
{
  if (mOutput)
    return (int)mOutput->numChannels();
  else
    return 0;
}


bool Device::needsToApplyChannels(MLMicroSeconds* aTransitionTimeP)
{
  MLMicroSeconds tt = 0;
  bool needsApply = false;
  for (int i=0; i<numChannels(); i++) {
    ChannelBehaviourPtr ch = getChannelByIndex(i, true);
    if (ch) {
      // at least this channel needs update
      LOG(LOG_DEBUG, "needsToApplyChannels() will return true because of %s", ch->description().c_str());
      if (!aTransitionTimeP) return true; // no need to check more channels
      needsApply = true;
      if (ch->transitionTimeToNewValue()>tt) {
        LOG(LOG_DEBUG, "- channel increases transition time from %lld to %lld mS", tt/MilliSecond, ch->transitionTimeToNewValue());
        tt = ch->transitionTimeToNewValue();
      }
    }
  }
  if (aTransitionTimeP) *aTransitionTimeP = tt;
  return needsApply;
}


void Device::allChannelsApplied(bool aAnyway)
{
  for (int i=0; i<numChannels(); i++) {
    ChannelBehaviourPtr ch = getChannelByIndex(i, true);
    if (ch) {
      ch->channelValueApplied(aAnyway);
    }
  }
}


void Device::invalidateAllChannels()
{
  for (int i=0; i<numChannels(); i++) {
    ChannelBehaviourPtr ch = getChannelByIndex(i, false);
    if (ch) {
      ch->makeApplyPending();
    }
  }
}


ChannelBehaviourPtr Device::getChannelByIndex(int aChannelIndex, bool aPendingApplyOnly)
{
  if (!mOutput) return ChannelBehaviourPtr();
  return mOutput->getChannelByIndex(aChannelIndex, aPendingApplyOnly);
}


ChannelBehaviourPtr Device::getChannelByType(DsChannelType aChannelType, bool aPendingApplyOnly)
{
  if (!mOutput) return ChannelBehaviourPtr();
  return mOutput->getChannelByType(aChannelType, aPendingApplyOnly);
}


ChannelBehaviourPtr Device::getChannelById(const string aChannelId, bool aPendingApplyOnly)
{
  if (!mOutput) return ChannelBehaviourPtr();
  return mOutput->getChannelById(aChannelId, aPendingApplyOnly);
}



// MARK: - Device level vDC API


ErrorPtr Device::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="setConfiguration") {
    ApiValuePtr o = aParams->get("configurationId");
    if (!o) {
      respErr = WebError::webErr(400, "missing configurationId parameter");
    }
    else {
      DevicePtr keepAlive(this); // make sure we live long enough to send result
      respErr = switchConfiguration(o->stringValue());
      if (Error::isOK(respErr)) {
        aRequest->sendResult(ApiValuePtr());
      }
    }
  }
  else if (aMethod=="x-p44-removeDevice") {
    if (isSoftwareDisconnectable()) {
      // confirm first, because device will get deleted in the process
      aRequest->sendResult(ApiValuePtr());
      // Remove this device from the installation, forget the settings
      hasVanished(true);
      // now device does not exist any more, so only thing that may happen is return
    }
    else {
      respErr = WebError::webErr(403, "device cannot be removed with this method");
    }
  }
  else if (aMethod=="x-p44-teachInSignal") {
    uint8_t variant = 0;
    ApiValuePtr o = aParams->get("variant");
    if (o) {
      variant = o->uint8Value();
    }
    if (teachInSignal(variant)) {
      // confirm
      aRequest->sendResult(ApiValuePtr());
    }
    else {
      respErr = WebError::webErr(400, "device cannot send teach in signal of requested variant");
    }
  }
  else if (aMethod=="x-p44-stopSceneActions") {
    // we want everything to stop
    stopTransitions();
    stopSceneActions();
    respErr = Error::ok();
  }
  else if (aMethod=="x-p44-syncChannels") {
    requestUpdatingChannels(boost::bind(&Device::syncedChannels, this, aRequest));
    return ErrorPtr(); // no response now
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}


void Device::syncedChannels(VdcApiRequestPtr aRequest)
{
  // confirm channels synced
  aRequest->sendResult(ApiValuePtr());
}



#define MOC_DIM_STEP_TIMEOUT (5*Second)
#define LEGACY_DIM_STEP_TIMEOUT (500*MilliSecond) // should be 400, but give it extra 100 because of delays in getting next dim call, especially for area scenes
#define EMERGENCY_DIM_STEP_TIMEOUT (300*Second) // just to prevent dimming forever if something goes wrong


ErrorPtr Device::checkChannel(ApiValuePtr aParams, ChannelBehaviourPtr &aChannel)
{
  ChannelBehaviourPtr ch;
  ApiValuePtr o;
  aChannel.reset();
  if ((o = aParams->get("channel"))) {
    aChannel = getChannelByType(o->int32Value());
  }
  else if ((o = aParams->get("channelId"))) {
    aChannel = getChannelById(o->stringValue());
  }
  if (!aChannel) {
    return Error::err<VdcApiError>(400, "Need to specify channel(type) or channelId");
  }
  return ErrorPtr();
}




void Device::handleNotification(const string &aNotification, ApiValuePtr aParams, StatusCB aExaminedCB)
{
  ErrorPtr err;
  if (aNotification=="saveScene") {
    // save scene
    ApiValuePtr o;
    if (Error::isOK(err = checkParam(aParams, "scene", o))) {
      SceneNo sceneNo = (SceneNo)o->int32Value();
      saveScene(sceneNo);
    }
    if (Error::notOK(err)) {
      OLOG(LOG_WARNING, "saveScene error: %s", err->text());
    }
  }
  else if (aNotification=="undoScene") {
    // save scene
    ApiValuePtr o;
    if (Error::isOK(err = checkParam(aParams, "scene", o))) {
      SceneNo sceneNo = (SceneNo)o->int32Value();
      undoScene(sceneNo);
    }
    if (Error::notOK(err)) {
      OLOG(LOG_WARNING, "undoScene error: %s", err->text());
    }
  }
  else if (aNotification=="setLocalPriority") {
    // set local priority
    ApiValuePtr o;
    if (Error::isOK(err = checkParam(aParams, "scene", o))) {
      SceneNo sceneNo = (SceneNo)o->int32Value();
      setLocalPriority(sceneNo);
    }
    if (Error::notOK(err)) {
      OLOG(LOG_WARNING, "setLocalPriority error: %s", err->text());
    }
  }
  else if (aNotification=="setControlValue") {
    // set control value
    ApiValuePtr o;
    if (Error::isOK(err = checkParam(aParams, "name", o))) {
      string controlValueName = o->stringValue();
      if (Error::isOK(err = checkParam(aParams, "value", o))) {
        // get value
        double value = o->doubleValue();
        // now process the value (updates channel values, but does not yet apply them)
        if (processControlValue(controlValueName, value)) {
          // apply the values
          OLOG(LOG_NOTICE, "processControlValue(%s, %f) completed -> requests applying channels now", controlValueName.c_str(), value);
          stopSceneActions();
          requestApplyingChannels(NoOP, false);
        }
      }
    }
    if (Error::notOK(err)) {
      OLOG(LOG_WARNING, "setControlValue error: %s", err->text());
    }
  }
  else if (aNotification=="callSceneMin") {
    // switch device on with minimum output level if not already on (=prepare device for dimming from zero)
    ApiValuePtr o;
    if (Error::isOK(err = checkParam(aParams, "scene", o))) {
      SceneNo sceneNo = (SceneNo)o->int32Value();
      // now call
      callSceneMin(sceneNo);
    }
    if (Error::notOK(err)) {
      OLOG(LOG_WARNING, "callSceneMin error: %s", err->text());
    }
  }
  else if (aNotification=="setOutputChannelValue") {
    // set output channel value
    ApiValuePtr o;
    ChannelBehaviourPtr channel;
    if (Error::isOK(err = checkChannel(aParams, channel))) {
      if (Error::isOK(err = checkParam(aParams, "value", o))) {
        double value = o->doubleValue();
        // check optional relative flag (to increment/decrement a channel, with possible wraparound)
        bool relative = false;
        o = aParams->get("relative");
        if (o) relative = o->boolValue();
        // check optional apply_now flag
        bool apply_now = true; // apply values by default
        o = aParams->get("apply_now");
        if (o) apply_now = o->boolValue();
        // check
        MLMicroSeconds transitionTime = getOutput()->mTransitionTime;
        o = aParams->get("transitionTime");
        if (o) transitionTime = o->doubleValue()*Second;
        if (relative) {
          channel->dimChannelValue(value, transitionTime);
        }
        else {
          channel->setChannelValue(value, transitionTime, true); // always apply precise value
        }
        if (apply_now) {
          mVdcP->cancelNativeActionUpdate(); // still delayed native scene updates must be cancelled before changing channel values
          requestApplyingChannels(NoOP, false);
        }
      }
    }
    if (Error::notOK(err)) {
      OLOG(LOG_WARNING, "setOutputChannelValue error: %s", err->text());
    }
  }
  else {
    inherited::handleNotification(aNotification, aParams, aExaminedCB);
  }
  // successfully examined (possibly also fully handled, but maybe some queued operations remain)
  if (aExaminedCB) aExaminedCB(ErrorPtr());
}


void Device::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // remove from container management
  DevicePtr dev = DevicePtr(this);
  mVdcP->removeDevice(dev, aForgetParams);
  // that's all for the base class
  if (aDisconnectResultHandler)
    aDisconnectResultHandler(true);
}


void Device::hasVanished(bool aForgetParams)
{
  // have device send a vanish message
  reportVanished();
  // then disconnect it in software
  // Note that disconnect() might delete the Device object (so 'this' gets invalid)
  disconnect(aForgetParams, NoOP);
}


void Device::scheduleVanish(bool aForgetParams, MLMicroSeconds aDelay)
{
  mVanishTicket.executeOnce(boost::bind(&Device::hasVanished, this, aForgetParams), aDelay);
}


static SceneNo mainSceneForArea(int aArea)
{
  switch (aArea) {
    case 1: return AREA_1_ON;
    case 2: return AREA_2_ON;
    case 3: return AREA_3_ON;
    case 4: return AREA_4_ON;
  }
  return ROOM_ON; // no area, main scene for room
}


static SceneNo offSceneForArea(int aArea)
{
  switch (aArea) {
    case 1: return AREA_1_OFF;
    case 2: return AREA_2_OFF;
    case 3: return AREA_3_OFF;
    case 4: return AREA_4_OFF;
  }
  return ROOM_OFF; // no area, off scene for room
}


// MARK: - optimized notification delivery


void Device::notificationPrepare(PreparedCB aPreparedCB, NotificationDeliveryStatePtr aDeliveryState)
{
  ErrorPtr err;
  if (aDeliveryState->callType==ntfy_callscene) {
    // call scene
    ApiValuePtr o;
    if (Error::isOK(err = checkParam(aDeliveryState->callParams, "scene", o))) {
      SceneNo sceneNo = (SceneNo)o->int32Value();
      bool force = false;
      // check for custom transition time
      MLMicroSeconds transitionTimeOverride = Infinite; // none
      o = aDeliveryState->callParams->get("transitionTime");
      if (o) {
        transitionTimeOverride = o->doubleValue()*Second;
      }
      // check for force flag
      if (Error::isOK(err = checkParam(aDeliveryState->callParams, "force", o))) {
        force = o->boolValue();
        // set the channel type as actionParam
        aDeliveryState->actionParam = channeltype_brightness; // legacy dimming (i.e. dimming via scene calls) is ALWAYS brightness
        // prepare scene call
        callScenePrepare(aPreparedCB, sceneNo, force, transitionTimeOverride);
        return;
      }
    }
    if (Error::notOK(err)) {
      OLOG(LOG_WARNING, "callScene error: %s", err->text());
    }
  }
  else if (aDeliveryState->callType==ntfy_dimchannel) {
    // start or stop dimming a channel
    ApiValuePtr o;
    ChannelBehaviourPtr channel;
    if (Error::isOK(err = checkChannel(aDeliveryState->callParams, channel))) {
      if (Error::isOK(err = checkParam(aDeliveryState->callParams, "mode", o))) {
        // mode
        int mode = o->int32Value();
        // area
        int area = 0;
        o = aDeliveryState->callParams->get("area");
        if (o) area = o->int32Value();
        // check for custom dimming rate
        double dimPerMSOverride = 0; // none
        o = aDeliveryState->callParams->get("dimPerMS");
        if (o) {
          dimPerMSOverride = o->doubleValue();
        }
        o = aDeliveryState->callParams->get("fullRangeTime");
        if (o) {
          double v = o->doubleValue();
          if (v>0) {
            dimPerMSOverride = (channel->getMax()-channel->getMin())/(v*1000); // full range divided though # of mS
          }
        }
        // autostop of dimming (localcontroller may want to prevent that)
        bool autostop = true;
        o = aDeliveryState->callParams->get("autostop");
        if (o) autostop = o->boolValue();
        // set the channel type as actionParam
        aDeliveryState->actionParam = channel->getChannelType();
        // prepare starting or stopping dimming
        dimChannelForAreaPrepare(
          aPreparedCB,
          channel,
          mode==0 ? dimmode_stop : (mode<0 ? dimmode_down : dimmode_up),
          area,
          autostop ? MOC_DIM_STEP_TIMEOUT : EMERGENCY_DIM_STEP_TIMEOUT,
          dimPerMSOverride
        );
        return;
      }
    }
    if (Error::notOK(err)) {
      OLOG(LOG_WARNING, "dimChannel error: %s", err->text());
    }
  }
  aPreparedCB(ntfy_none);
}


void Device::optimizerRepeatPrepare(NotificationDeliveryStatePtr aDeliveryState)
{
  if (aDeliveryState->optimizedType==ntfy_dimchannel) {
    dimRepeatPrepare(aDeliveryState);
  }
}


void Device::executePreparedOperation(SimpleCB aDoneCB, NotificationType aWhatToApply)
{
  if (mPreparedScene) {
    callSceneExecutePrepared(aDoneCB, aWhatToApply);
    mPreparedDim = false; // calling scene always cancels prepared dimming
    return;
  }
  else if (mPreparedDim) {
    dimChannelExecutePrepared(aDoneCB, aWhatToApply);
    return;
  }
  if (aDoneCB) aDoneCB();
}


void Device::releasePreparedOperation()
{
  mPreparedScene.reset();
  mPreparedTransitionOverride = Infinite; // none
}


bool Device::updateDeliveryState(NotificationDeliveryStatePtr aDeliveryState, bool aForOptimisation)
{
  if (aDeliveryState->optimizedType==ntfy_callscene) {
    if (!mPreparedScene) return false; // no prepared scene -> not part of optimized set
    aDeliveryState->contentId = mPreparedScene->mSceneNo; // to re-identify the scene (the contents might have changed...)
    if (aForOptimisation && prepareForOptimizedSet(aDeliveryState)) {
      // content hash must represent the contents of the called scenes in all affected devices
      Fnv64 sh(mPreparedScene->sceneHash());
      if (sh.getHash()==0) return false; // scene not hashable -> not part of optimized set
      sh.addString(mDSUID.getBinary()); // add device dSUID to make it "mixable" (i.e. combine into one hash via XOR in any order)
      aDeliveryState->contentsHash ^= sh.getHash(); // mix
      return true;
    }
  }
  else if (aDeliveryState->optimizedType==ntfy_dimchannel) {
    if (!mPreparedDim) return false; // no prepared scene -> not part of optimized set
    aDeliveryState->contentId = 0; // no different content ids
    aDeliveryState->actionVariant = mCurrentDimMode; // to actually apply the dim mode to the optimized group later
    aDeliveryState->actionParam = mCurrentDimChannel ? mCurrentDimChannel->getChannelType() : channeltype_default;
    if (aForOptimisation && prepareForOptimizedSet(aDeliveryState)) {
      if (mCurrentDimMode!=dimmode_stop) {
        aDeliveryState->repeatVariant = dimmode_stop; // auto-stop
        aDeliveryState->repeatAfter = mCurrentAutoStopTime; // after this time
      }
      return true;
    }
  }
  return false;
}


bool Device::addToOptimizedSet(NotificationDeliveryStatePtr aDeliveryState)
{
  bool include = updateDeliveryState(aDeliveryState, true);
  if (include) {
    // the device must be added to the device hash
    mDSUID.xorDsUidIntoMix(aDeliveryState->affectedDevicesHash, true); // subdevice-safe mixing
    aDeliveryState->affectedDevices.push_back(DevicePtr(this));
    return true;
  }
  // by default: no optimisation
  return false;
}



// MARK: - high level serialized hardware access

#ifndef SERIALIZER_WATCHDOG
  #define SERIALIZER_WATCHDOG 1
#endif
#define SERIALIZER_WATCHDOG_TIMEOUT (20*Second)

void Device::requestApplyingChannels(SimpleCB aAppliedOrSupersededCB, bool aForDimming, bool aModeChange)
{
  if (!aModeChange && mOutput && !mOutput->isEnabled()) {
    // disabled output and not a mode change -> no operation
    FOCUSOLOG("requestApplyingChannels called with output disabled -> NOP");
    // - just call back immediately
    if (aAppliedOrSupersededCB) aAppliedOrSupersededCB();
    return;
  }
  FOCUSOLOG("requestApplyingChannels entered");
  // Caller wants current channel values applied to hardware
  // Three possible cases:
  // a) hardware is busy applying new values already -> confirm previous request to apply as superseded
  // b) hardware is busy updating values -> wait until this is done
  // c) hardware is not busy -> start apply right now
  if (mApplyInProgress) {
    FOCUSLOG("- requestApplyingChannels called while apply already running");
    // case a) confirm previous request because superseded
    if (mAppliedOrSupersededCB) {
      FOCUSLOG("- confirming previous (superseded) apply request");
      SimpleCB cb = mAppliedOrSupersededCB;
      mAppliedOrSupersededCB = aAppliedOrSupersededCB; // in case current callback should request another change, callback is already installed
      cb(); // call back now, values have been superseded
      FOCUSLOG("- previous (superseded) apply request confirmed");
    }
    else {
      mAppliedOrSupersededCB = aAppliedOrSupersededCB;
    }
    // - when previous request actually terminates, we need another update to make sure finally settled values are correct
    mMissedApplyAttempts++;
    FOCUSLOG("- missed requestApplyingChannels requests now %d", mMissedApplyAttempts);
  }
  else if (mUpdateInProgress) {
    FOCUSLOG("- requestApplyingChannels called while update running -> postpone apply");
    // case b) cannot execute until update finishes
    mMissedApplyAttempts++;
    mAppliedOrSupersededCB = aAppliedOrSupersededCB;
    mApplyInProgress = true;
  }
  else {
    // case c) applying is not currently in progress, can start updating hardware now
    FOCUSOLOG("ready, calling applyChannelValues()");
    #if SERIALIZER_WATCHDOG
    // - start watchdog
    mSerializerWatchdogTicket.executeOnce(boost::bind(&Device::serializerWatchdog, this), 10*Second); // new
    FOCUSLOG("+++++ Serializer watchdog started for apply with ticket #%ld", (MLTicketNo)mSerializerWatchdogTicket);
    #endif
    // - start applying
    mAppliedOrSupersededCB = aAppliedOrSupersededCB;
    mApplyInProgress = true;
    applyChannelValues(boost::bind(&Device::applyingChannelsComplete, this), aForDimming);
  }
}


void Device::waitForApplyComplete(SimpleCB aApplyCompleteCB)
{
  if (!mApplyInProgress) {
    // not applying anything, immediately call back
    FOCUSLOG("- waitForApplyComplete() called while no apply in progress -> immediately call back");
    aApplyCompleteCB();
  }
  else {
    // apply in progress, save callback, will be called once apply is complete
    if (mApplyCompleteCB) {
      // already regeistered, chain it
      FOCUSLOG("- waitForApplyComplete() called while apply in progress and another callback already set -> install callback fork");
      mApplyCompleteCB = boost::bind(&Device::forkDoneCB, this, mApplyCompleteCB, aApplyCompleteCB);
    }
    else {
      FOCUSLOG("- waitForApplyComplete() called while apply in progress and no callback already set -> install callback");
      mApplyCompleteCB = aApplyCompleteCB;
    }
  }
}


void Device::forkDoneCB(SimpleCB aOriginalCB, SimpleCB aNewCallback)
{
  FOCUSLOG("forkDoneCB:");
  FOCUSLOG("- calling original callback");
  aOriginalCB();
  FOCUSLOG("- calling new callback");
  aNewCallback();
}



void Device::serializerWatchdog()
{
  #if SERIALIZER_WATCHDOG
  FOCUSLOG("##### Serializer watchdog ticket #%ld expired", (MLTicketNo)mSerializerWatchdogTicket);
  mSerializerWatchdogTicket = 0;
  if (mApplyInProgress) {
    OLOG(LOG_WARNING, "##### Serializer watchdog force-ends apply with %d missed attempts", mMissedApplyAttempts);
    mMissedApplyAttempts = 0;
    applyingChannelsComplete();
    FOCUSLOG("##### Force-ending apply complete");
  }
  if (mUpdateInProgress) {
    OLOG(LOG_WARNING, "##### Serializer watchdog force-ends update");
    updatingChannelsComplete();
    FOCUSLOG("##### Force-ending complete");
  }
  #endif
}


bool Device::checkForReapply()
{
  OLOG(LOG_DEBUG, "checkForReapply - missed %d apply attempts in between", mMissedApplyAttempts);
  if (mMissedApplyAttempts>0) {
    // request applying again to make sure final values are applied
    // - re-use callback of most recent requestApplyingChannels(), will be called once this attempt has completed (or superseded again)
    FOCUSLOG("- checkForReapply now requesting final channel apply");
    mMissedApplyAttempts = 0; // clear missed
    mApplyInProgress = false; // must be cleared for requestApplyingChannels() to actually do something
    requestApplyingChannels(mAppliedOrSupersededCB, false); // final apply after missing other apply commands may not optimize for dimming
    // - done for now
    return true; // reapply needed and started
  }
  return false; // no repply pending
}



// hardware has completed applying values
void Device::applyingChannelsComplete()
{
  FOCUSOLOG("applyingChannelsComplete entered");
  #if FOCUSLOGGING
  MLTicketNo ticketNo = 0;
  #endif
  #if SERIALIZER_WATCHDOG
  if (mSerializerWatchdogTicket) {
    FOCUSLOG("----- Serializer watchdog ticket #%ld cancelled - apply complete", (MLTicketNo)mSerializerWatchdogTicket);
    #if FOCUSLOGGING
    ticketNo = (MLTicketNo)mSerializerWatchdogTicket;
    #endif
    mSerializerWatchdogTicket.cancel(); // cancel watchdog
  }
  #endif
  mApplyInProgress = false;
  // if more apply request have happened in the meantime, we need to reapply now
  if (!checkForReapply()) {
    // apply complete and no final re-apply pending
    // - confirm because finally applied
    FOCUSLOG("- applyingChannelsComplete - really completed, now checking callbacks (ticket #%ld)", ticketNo);
    SimpleCB cb;
    if (mAppliedOrSupersededCB) {
      FOCUSLOG("- confirming apply (really) finalized (ticket #%ld)", ticketNo);
      cb = mAppliedOrSupersededCB;
      mAppliedOrSupersededCB = NoOP; // ready for possibly taking new callback in case current callback should request another change
      cb(); // call back now, values have been superseded
    }
    // check for independent operation waiting for apply complete
    if (mApplyCompleteCB) {
      FOCUSLOG("- confirming apply (really) finalized to waitForApplyComplete() client (ticket #%ld)", ticketNo);
      cb = mApplyCompleteCB;
      mApplyCompleteCB = NoOP;
      cb();
    }
    FOCUSLOG("- confirmed apply (really) finalized (ticket #%ld)", ticketNo);
    // report channel changes to bridges, but not to dS
    getOutput()->pushChannelStates(false, true);
  }
}



void Device::requestUpdatingChannels(SimpleCB aUpdatedOrCachedCB)
{
  FOCUSOLOG("requestUpdatingChannels entered");
  // Caller wants current values from hardware
  // Three possible cases:
  // a) hardware is busy updating values already -> serve previous callback (with stale values) and install new callback
  // b) hardware is busy applying new values -> consider cache most recent
  // c) hardware is not busy -> start reading values
  if (mUpdateInProgress) {
    // case a) serialize updates: terminate previous callback with stale values and install new one
    if (mUpdatedOrCachedCB) {
      FOCUSLOG("- confirming channels updated for PREVIOUS request with stale values (as asked again)");
      SimpleCB cb = mUpdatedOrCachedCB;
      mUpdatedOrCachedCB = aUpdatedOrCachedCB; // install new
      cb(); // execute old
      FOCUSLOG("- confirmed channels updated for PREVIOUS request with stale values (as asked again)");
    }
    else {
      mUpdatedOrCachedCB = aUpdatedOrCachedCB; // install new
    }
    // done, actual results will serve most recent request for values
  }
  else if (mApplyInProgress) {
    // case b) no update pending, but applying values right now: return current values as hardware values are in
    //   process of being overwritten by those
    if (aUpdatedOrCachedCB) {
      FOCUSLOG("- confirming channels already up-to-date (as HW update is in progress)");
      aUpdatedOrCachedCB(); // execute old
      FOCUSLOG("- confirmed channels already up-to-date (as HW update is in progress)");
    }
  }
  else {
    // case c) hardware is not busy, start reading back current values
    FOCUSOLOG("requestUpdatingChannels: hardware ready, calling syncChannelValues()");
    mUpdatedOrCachedCB = aUpdatedOrCachedCB; // install new callback
    mUpdateInProgress = true;
    #if SERIALIZER_WATCHDOG
    // - start watchdog
    mSerializerWatchdogTicket.executeOnce(boost::bind(&Device::serializerWatchdog, this), SERIALIZER_WATCHDOG_TIMEOUT);
    FOCUSLOG("+++++ Serializer watchdog started for update with ticket #%ld", (MLTicketNo)mSerializerWatchdogTicket);
    #endif
    // - trigger querying hardware
    syncChannelValues(boost::bind(&Device::updatingChannelsComplete, this));
  }
}


void Device::updatingChannelsComplete()
{
  #if SERIALIZER_WATCHDOG
  if (mSerializerWatchdogTicket) {
    FOCUSLOG("----- Serializer watchdog ticket #%ld cancelled - update complete", (MLTicketNo)mSerializerWatchdogTicket);
    mSerializerWatchdogTicket.cancel(); // cancel watchdog
  }
  #endif
  if (mUpdateInProgress) {
    FOCUSOLOG("endUpdatingChannels (while actually waiting for these updates!)");
    mUpdateInProgress = false;
    if (mUpdatedOrCachedCB) {
      FOCUSLOG("- confirming channels updated from hardware (= calling callback now)");
      SimpleCB cb = mUpdatedOrCachedCB;
      mUpdatedOrCachedCB = NoOP; // ready for possibly taking new callback in case current callback should request another change
      cb(); // call back now, cached values are either updated from hardware or superseded by pending updates TO hardware
      FOCUSLOG("- confirmed channels updated from hardware (= callback has possibly launched apply already and returned now)");
    }
  }
  else {
    FOCUSOLOG("UNEXPECTED endUpdatingChannels -> discarded");
  }
  // if we have got apply requests in the meantime, we need to do a reapply now
  checkForReapply();
}


// MARK: - dimming

// dS Dimming rule for Light:
//  Rule 4 All devices which are turned on and not in local priority state take part in the dimming process.


void Device::dimChannelForArea(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode, int aArea, MLMicroSeconds aAutoStopAfter, double aDimPerMSOverride)
{
  // convenience helper
  dimChannelForAreaPrepare(boost::bind(&Device::executePreparedOperation, this, SimpleCB(), _1), aChannel, aDimMode, aArea, aAutoStopAfter, aDimPerMSOverride);
}


// implementation of "dimChannel" vDC API command and legacy dimming
// Note: ensures dimming only continues for at most aAutoStopAfter
void Device::dimChannelForAreaPrepare(PreparedCB aPreparedCB, ChannelBehaviourPtr aChannel, VdcDimMode aDimMode, int aArea, MLMicroSeconds aAutoStopAfter, double aDimPerMSOverride)
{
  if (!aChannel) { aPreparedCB(ntfy_none); return; } // no channel, no dimming
  LOG(LOG_DEBUG, "dimChannelForArea: aChannel=%s, aDimMode=%d, aArea=%d", aChannel->getName(), aDimMode, aArea);
  // check basic dimmability (e.g. avoid dimming brightness for lights that are off)
  if (aDimMode!=dimmode_stop && !(mOutput->canDim(aChannel))) {
    LOG(LOG_DEBUG, "- behaviour does not allow dimming channel '%s' now (e.g. because light is off)", aChannel->getName());
    aPreparedCB(ntfy_none); // cannot dim
    return;
  }
  // always update which area was the last requested to be dimmed for this device (even if device is not in the area)
  // (otherwise, dimming of a previously dimmed area might get restarted by a T1234_CONT for another area)
  mAreaDimmed = aArea;
  mAreaDimMode = dimmode_stop; // but do not assume dimming before we've checked the area dontCare flags
  // check area if any
  if (aArea>0) {
    SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(mDeviceSettings);
    if (scenes) {
      // check area first
      SceneNo areaScene = mainSceneForArea(aArea);
      DsScenePtr scene = scenes->getScene(areaScene);
      if (scene->isDontCare()) {
        LOG(LOG_DEBUG, "- area main scene(%d) is dontCare -> suppress dimChannel for Area %d", areaScene, aArea);
        aPreparedCB(ntfy_none); // not in this area, suppress dimming
        return;
      }
    }
    // dimMode does affect the area, update
    mAreaDimMode = aDimMode;
  }
  else {
    // non-area dimming: suppress if device is in local priority
    // Note: aArea can be set -1 to override local priority checking, for example when using method for identify purposes
    if (aArea==0 && mOutput->hasLocalPriority()) {
      LOG(LOG_DEBUG, "- Non-area dimming, localPriority set -> suppressed");
      aPreparedCB(ntfy_none); // local priority active, suppress dimming
      return;
    }
  }
  // always give device chance to stop, even if no dimming is in progress
  if (aDimMode==dimmode_stop) {
    stopSceneActions();
  }
  finishSceneActionWaiting(); // finish things possibly still waiting for previous call's scene actions to complete
  // requested dimming this device, no area suppress active
  if (aDimMode!=mCurrentDimMode || aChannel!=mCurrentDimChannel) {
    // mode changes
    if (aDimMode!=dimmode_stop) {
      // start or change direction
      if (mCurrentDimMode!=dimmode_stop) {
        // changed dimming direction or channel without having stopped first
        // - stop previous dimming operation here
        dimChannel(mCurrentDimChannel, dimmode_stop, true);
      }
      // apply custom dimming rate if any
      aChannel->setCustomDimPerMS(aDimPerMSOverride);
    }
    else {
      // stopping: reset any custom dimming rate
      aChannel->setCustomDimPerMS();
    }
    // fully prepared now
    // - save parameters for executing dimming now
    mCurrentDimMode = aDimMode;
    mCurrentDimChannel = aChannel;
    mCurrentAutoStopTime = aAutoStopAfter;
    mPreparedDim = true;
    mPreparedScene.reset(); // to make sure there's no leftover
    aPreparedCB(ntfy_dimchannel); // needs to start or stop dimming
    return;
  }
  else {
    // same dim mode, just retrigger if dimming right now
    if (aDimMode!=dimmode_stop) {
      mCurrentAutoStopTime = aAutoStopAfter;
      // if we have a local timer running, reschedule it
      MainLoop::currentMainLoop().rescheduleExecutionTicket(mDimTimeoutTicket, mCurrentAutoStopTime);
      // also indicate to optimizer it must reschedule its repeater
      aPreparedCB(ntfy_retrigger); // retrigger repeater
      return;
    }
    aPreparedCB(ntfy_none); // no change in dimming
    return;
  }
}


void Device::dimRepeatPrepare(NotificationDeliveryStatePtr aDeliveryState)
{
  // we get here ONLY during optimized dimming.
  // This means that this is a request to put device back into non-dimming state
  if (mCurrentDimMode!=dimmode_stop) {
    // prepare to go back to stop state
    // (without applying to hardware, but possibly device-specific dimChannel() methods fetching actual dim state from hardware)
    mCurrentDimMode = dimmode_stop;
    mCurrentAutoStopTime = Never;
    mPreparedDim = true;
  }
}


void Device::dimChannelExecutePrepared(SimpleCB aDoneCB, NotificationType aWhatToApply)
{
  if (mPreparedDim) {
    // call actual dimming method, which will update state in all cases, but start/stop dimming only when not already done (aDoApply)
    dimChannel(mCurrentDimChannel, mCurrentDimMode, aWhatToApply!=ntfy_none);
    if (aWhatToApply!=ntfy_none) {
      if (mCurrentDimMode!=dimmode_stop) {
        // starting
        mDimTimeoutTicket.executeOnce(boost::bind(&Device::dimAutostopHandler, this, mCurrentDimChannel), mCurrentAutoStopTime);
      }
      else {
        // stopping
        mDimTimeoutTicket.cancel();
      }
    }
    mPreparedDim = false;
  }
  if (aDoneCB) aDoneCB();
}





// autostop handler (for both dimChannel and legacy dimming)
void Device::dimAutostopHandler(ChannelBehaviourPtr aChannel)
{
  // timeout: stop dimming immediately
  mDimTimeoutTicket = 0;
  dimChannel(aChannel, dimmode_stop, true);
  mCurrentDimMode = dimmode_stop; // stopped now
}



#define DIM_STEP_INTERVAL_MS 300.0
#define DIM_STEP_INTERVAL (DIM_STEP_INTERVAL_MS*MilliSecond)


// actual dimming implementation, possibly overridden by subclasses to provide more optimized/precise dimming
void Device::dimChannel(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode, bool aDoApply)
{
  if (aChannel) {
    OLOG(LOG_INFO,
      "dimChannel (generic): channel '%s' %s",
      aChannel->getName(), aDimMode==dimmode_stop ? "STOPS dimming" : (aDimMode==dimmode_up ? "starts dimming UP" : "starts dimming DOWN")
    );
    // Simple base class implementation just increments/decrements channel values periodically (and skips steps when applying values is too slow)
    if (aDimMode==dimmode_stop) {
      // stop dimming (including possibly running transitions on the device implementation level)
      stopTransitions();
    }
    else {
      // start dimming
      mIsDimming = true;
      if (aDoApply) {
        // make sure the start point is calculated if needed
        aChannel->getChannelValueCalculated();
        aChannel->setNeedsApplying(0); // force re-applying start point, no transition time
        // calculate increment
        double increment = (aDimMode==dimmode_up ? DIM_STEP_INTERVAL_MS : -DIM_STEP_INTERVAL_MS) * aChannel->getDimPerMS();
        // start dimming
        // ...but setup callback to wait first for all apply operations to really complete before
        SimpleCB dd = boost::bind(&Device::dimDoneHandler, this, aChannel, increment, MainLoop::now()+10*MilliSecond);
        waitForApplyComplete(boost::bind(&Device::requestApplyingChannels, this, dd, false, false));
      }
      else {
        OLOG(LOG_WARNING, "generic dimChannel() without apply -> unlikely (optimized generic dimming?)");
      }
    }
  }
}


void Device::dimHandler(ChannelBehaviourPtr aChannel, double aIncrement, MLMicroSeconds aNow)
{
  // increment channel value
  aChannel->dimChannelValue(aIncrement, DIM_STEP_INTERVAL);
  // apply to hardware
  requestApplyingChannels(boost::bind(&Device::dimDoneHandler, this, aChannel, aIncrement, aNow+DIM_STEP_INTERVAL), true); // apply in dimming mode
}


void Device::dimDoneHandler(ChannelBehaviourPtr aChannel, double aIncrement, MLMicroSeconds aNextDimAt)
{
  // keep up with actual dim time
  MLMicroSeconds now = MainLoop::now();
  while (aNextDimAt<now) {
    // missed this step - simply increment channel and target time, but do not cause re-apply
    LOG(LOG_DEBUG, "dimChannel: applyChannelValues() was too slow while dimming channel=%d -> skipping next dim step", aChannel->getChannelType());
    aChannel->dimChannelValue(aIncrement, DIM_STEP_INTERVAL);
    aNextDimAt += DIM_STEP_INTERVAL;
  }
  if (mIsDimming) {
    // now schedule next inc/update step
    mDimHandlerTicket.executeOnceAt(boost::bind(&Device::dimHandler, this, aChannel, aIncrement, _2), aNextDimAt);
  }
}


// MARK: - scene operations


void Device::callScene(SceneNo aSceneNo, bool aForce, MLMicroSeconds aTransitionTimeOverride)
{
  // convenience method for calling scenes on single devices
  callScenePrepare(boost::bind(&Device::executePreparedOperation, this, SimpleCB(), _1), aSceneNo, aForce, aTransitionTimeOverride);
}



void Device::callScenePrepare(PreparedCB aPreparedCB, SceneNo aSceneNo, bool aForce, MLMicroSeconds aTransitionTimeOverride)
{
  finishSceneActionWaiting(); // finish things possibly still waiting for previous call's scene actions to complete
  mPreparedScene.reset(); // clear possibly previously prepared scene
  mPreparedTransitionOverride = aTransitionTimeOverride; // save for later
  mPreparedDim = false; // no dimming prepared
  SceneDeviceSettingsPtr scenes = getScenes();
  // see if we have a scene table at all
  if (mOutput && scenes) {
    DsScenePtr scene = scenes->getScene(aSceneNo);
    SceneCmd cmd = scene->mSceneCmd;
    SceneArea area = scene->mSceneArea;
    // check special scene commands first
    if (cmd==scene_cmd_area_continue) {
      // area dimming continuation
      if (mAreaDimmed!=0 && mAreaDimMode!=dimmode_stop) {
        // continue or restart area dimming
        dimChannelForAreaPrepare(aPreparedCB, getChannelByIndex(0), mAreaDimMode, mAreaDimmed, LEGACY_DIM_STEP_TIMEOUT, 0);
        return;
      }
      // - otherwise: NOP
      aPreparedCB(ntfy_none);
      return;
    }
    // first check legacy (inc/dec scene) dimming
    if (cmd==scene_cmd_increment) {
      if (!prepareSceneCall(scene)) aPreparedCB(ntfy_none);
      else dimChannelForAreaPrepare(aPreparedCB, getChannelByIndex(0), dimmode_up, area, LEGACY_DIM_STEP_TIMEOUT, 0);
      return;
    }
    else if (cmd==scene_cmd_decrement) {
      if (!prepareSceneCall(scene)) aPreparedCB(ntfy_none);
      else dimChannelForAreaPrepare(aPreparedCB, getChannelByIndex(0), dimmode_down, area, LEGACY_DIM_STEP_TIMEOUT, 0);
      return;
    }
    else if (cmd==scene_cmd_stop) {
      if (!prepareSceneCall(scene)) aPreparedCB(ntfy_none);
      else dimChannelForAreaPrepare(aPreparedCB, getChannelByIndex(0), dimmode_stop, area, 0, 0);
      return;
    }
    // make sure dimming stops for any non-dimming scene call
    if (mCurrentDimMode!=dimmode_stop) {
      // any non-dimming scene call stops dimming
      OLOG(LOG_NOTICE, "CallScene(%d) interrupts dimming in progress", aSceneNo);
      dimChannelForAreaPrepare(boost::bind(&Device::callSceneDimStop, this, aPreparedCB, scene, aForce), mCurrentDimChannel, dimmode_stop, area, 0, 0);
      return;
    }
    else {
      // directly proceed
      callScenePrepare2(aPreparedCB, scene, aForce);
      return;
    }
  }
  aPreparedCB(ntfy_none); // no scenes or no output
}


void Device::callSceneDimStop(PreparedCB aPreparedCB, DsScenePtr aScene, bool aForce)
{
  dimChannelExecutePrepared(NoOP, ntfy_dimchannel);
  callScenePrepare2(aPreparedCB, aScene, aForce);
}


void Device::callScenePrepare2(PreparedCB aPreparedCB, DsScenePtr aScene, bool aForce)
{
  SceneArea area = aScene->mSceneArea;
  SceneNo sceneNo = aScene->mSceneNo;
  OLOG(LOG_INFO, "Evaluating CallScene(%s)", VdcHost::sceneText(sceneNo).c_str());
  // filter area scene calls via area main scene's (area x on, Tx_S1) dontCare flag
  if (area) {
    LOG(LOG_INFO, "- callScene(%d): is area #%d scene", sceneNo, area);
    // check if device is in area (criteria used is dontCare flag OF THE AREA ON SCENE (other don't care flags are irrelevant!)
    DsScenePtr areamainscene = getScenes()->getScene(mainSceneForArea(area));
    if (areamainscene->isDontCare()) {
      LOG(LOG_INFO, "- area main scene(%s) is dontCare -> suppress", VdcHost::sceneText(areamainscene->mSceneNo).c_str());
      aPreparedCB(ntfy_none); // not in this area, suppress callScene entirely
      return;
    }
    // call applies, if it is a off scene, it resets localPriority
    if (aScene->mSceneCmd==scene_cmd_off) {
      // area is switched off -> end local priority
      LOG(LOG_INFO, "- is area off scene -> ends localPriority now");
      mOutput->setLocalPriority(false);
    }
  }
  if (!aScene->isDontCare()) {
    // Scene found and dontCare not set, check details
    // - check and update local priority
    if (!area && mOutput->hasLocalPriority()) {
      // non-area scene call, but device is in local priority
      if (!aForce && !aScene->ignoresLocalPriority()) {
        // not forced nor localpriority ignored, localpriority prevents applying non-area scene
        LOG(LOG_DEBUG, "- Non-area scene, localPriority set, scene does not ignore local prio and not forced -> suppressed");
        aPreparedCB(ntfy_none); // suppress scene call entirely
        return;
      }
      else {
        // forced or scene ignores local priority, scene is applied anyway, and also clears localPriority
        mOutput->setLocalPriority(false);
      }
    }
    // we get here only if callScene is actually affecting this device
    OLOG(LOG_NOTICE, "affected by CallScene(%s)", VdcHost::sceneText(sceneNo).c_str());
    // - make sure we have the lastState pseudo-scene for undo
    if (!mPreviousState) {
      mPreviousState = getScenes()->newUndoStateScene();
    }
    // we remember the scene for which these are undo values in sceneNo of the pseudo scene
    // (but without actually re-configuring the scene according to that number!)
    mPreviousState->mSceneNo = sceneNo;
    // - now capture current values and then apply to output
    if (mOutput) {
      // Non-dimming scene: have output save its current state into the previousState pseudo scene
      // Note: we only ask updating from device for scenes that are likely to be undone, and thus important
      //   to capture perfectly. For all others, it is sufficient to just capture the cached output channel
      //   values and not waste time with expensive queries to device hardware.
      // Note: the actual updating is allowed to happen later (when the hardware responds) but
      //   if so, implementations must make sure access to the hardware is serialized such that
      //   the values are captured before values from performApplySceneToChannels() below are applied.
      mOutput->captureScene(mPreviousState, aScene->preciseUndoImportant(), boost::bind(&Device::outputUndoStateSaved, this, aPreparedCB, aScene)); // apply only after capture is complete
    } // if output
  } // not dontCare
  else {
    // Scene is dontCare
    // - do not include in apply
    aPreparedCB(ntfy_none);
    // - but possibly still do other scene actions now, although scene was not applied
    performSceneActions(aScene, boost::bind(&Device::sceneActionsComplete, this, aScene));
  }
}


// scene call preparation continues after current state has been captured for this output
void Device::outputUndoStateSaved(PreparedCB aPreparedCB, DsScenePtr aScene)
{
  // now let device level implementation prepare for scene call and decide if normal apply should follow
  if (prepareSceneCall(aScene)) {
    // this scene should be applied, keep it ready for callSceneExecute()
    mPreparedScene = aScene;
    aPreparedCB(ntfy_callscene);
  }
  else {
    OLOG(LOG_DEBUG, "Device level prepareSceneCall() returns false -> no more actions");
    mPreparedScene.reset();
    aPreparedCB(ntfy_none);
  }
}


MLMicroSeconds Device::transitionTimeForPreparedScene(bool aIncludingOverride)
{
  MLMicroSeconds tt = 0;
  if (mPreparedScene) {
    if (aIncludingOverride && mPreparedTransitionOverride!=Infinite) {
      tt = mPreparedTransitionOverride;
    }
    else if (mOutput) {
      tt = mOutput->transitionTimeFromScene(mPreparedScene, true);
    }
  }
  return tt;
}


void Device::callSceneExecutePrepared(SimpleCB aDoneCB, NotificationType aWhatToApply)
{
  if (mPreparedScene) {
    DsScenePtr scene = mPreparedScene;
    // apply scene logically
    if (mOutput->applySceneToChannels(scene, mPreparedTransitionOverride)) {
      // prepare for apply (but do NOT yet apply!) on device hardware level)
      if (prepareSceneApply(scene)) {
        // now we can apply values to hardware
        if (aWhatToApply!=ntfy_none) {
          // normally apply channel values to hardware
          requestApplyingChannels(boost::bind(&Device::sceneValuesApplied, this, aDoneCB, scene, false), false);
          return;
        }
        else {
          // just consider all channels already applied (e.g. by vdc-level native action)
          // - confirm having applied channels (normally, actual device-level apply would do that)
          allChannelsApplied();
          // - consider scene applied but indirectly
          sceneValuesApplied(aDoneCB, scene, true);
          return;
        }
      }
    }
    else {
      // no apply to channels or hardware needed, directly proceed to actions
      sceneValuesApplied(aDoneCB, scene, false);
      return;
    }
  }
  // callback not passed to another methods -> done -> call it now
  if (aDoneCB) aDoneCB();
}

void Device::confirmSceneActionsComplete()
{
  if (mSceneActionCompleteCB) {
    SimpleCB cb = mSceneActionCompleteCB;
    mSceneActionCompleteCB = NoOP;
    OLOG(LOG_INFO, "- confirming scene actions complete");
    cb();
  }
}


void Device::finishSceneActionWaiting()
{
  if (mSceneActionCompleteCB) {
    OLOG(LOG_WARNING, "Was still waiting for unfinished scene actions from earlier call -> consider done now");
    confirmSceneActionsComplete();
  }
}


void Device::sceneValuesApplied(SimpleCB aDoneCB, DsScenePtr aScene, bool aIndirectly)
{
  // now perform scene special actions such as blinking
  // Note: scene actions might be ongoing, while apply should no go on forever.
  //   Therefore, we store the done callback device-globally to be able to continue before
  //   scene actions are complete
  // confirm previous pending one, if any
  confirmSceneActionsComplete();
  // store new callback
  mSceneActionCompleteCB = aDoneCB;
  // launch new scene actions
  performSceneActions(aScene, boost::bind(&Device::sceneActionsComplete, this, aScene));
}


void Device::sceneActionsComplete(DsScenePtr aScene)
{
  // scene actions are now complete
  OLOG(LOG_INFO, "Scene actions for callScene(%s) complete -> now in final state", VdcHost::sceneText(aScene->mSceneNo).c_str());
  confirmSceneActionsComplete();
}


void Device::performSceneActions(DsScenePtr aScene, SimpleCB aDoneCB)
{
  if (mOutput) {
    mOutput->performSceneActions(aScene, aDoneCB);
  }
  else {
    if (aDoneCB) aDoneCB(); // nothing to do
  }
}


void Device::stopSceneActions()
{
  if (mOutput) {
    mOutput->stopSceneActions();
  }
}


void Device::stopTransitions()
{
  // in the base class, this is just cancelling possibly running default dimming
  mIsDimming = false;
  mDimHandlerTicket.cancel();
  if (mOutput) {
    mOutput->stopTransitions();
  }
}


bool Device::prepareSceneCall(DsScenePtr aScene)
{
  // base class - just let device process the scene normally
  return true;
}


bool Device::prepareSceneApply(DsScenePtr aScene)
{
  // base class - just complete
  return true;
}


void Device::undoScene(SceneNo aSceneNo)
{
  OLOG(LOG_NOTICE, "UndoScene(%s):", VdcHost::sceneText(aSceneNo).c_str());
  if (mPreviousState && mPreviousState->mSceneNo==aSceneNo) {
    // there is an undo pseudo scene we can apply
    // scene found, now apply it to the output (if any)
    if (mOutput) {
      // now apply the pseudo state
      mOutput->applySceneToChannels(mPreviousState, Infinite); // no transition time override
      // apply the values now, not dimming
      if (prepareSceneApply(mPreviousState)) {
        requestApplyingChannels(NoOP, false);
      }
    }
  }
}


void Device::setLocalPriority(SceneNo aSceneNo)
{
  SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(mDeviceSettings);
  if (scenes) {
    OLOG(LOG_NOTICE, "SetLocalPriority(%s):", VdcHost::sceneText(aSceneNo).c_str());
    // we have a device-wide scene table, get the scene object
    DsScenePtr scene = scenes->getScene(aSceneNo);
    if (scene && !scene->isDontCare()) {
      LOG(LOG_DEBUG, "- setLocalPriority(%d): localPriority set", aSceneNo);
      mOutput->setLocalPriority(true);
    }
  }
}


void Device::callSceneMin(SceneNo aSceneNo)
{
  SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(mDeviceSettings);
  if (scenes) {
    OLOG(LOG_NOTICE, "CallSceneMin(%s):", VdcHost::sceneText(aSceneNo).c_str());
    // we have a device-wide scene table, get the scene object
    DsScenePtr scene = scenes->getScene(aSceneNo);
    if (scene && !scene->isDontCare()) {
      if (mOutput) {
        mOutput->onAtMinBrightness(scene);
        // apply the values now, not dimming
        if (prepareSceneApply(scene)) {
          requestApplyingChannels(NoOP, false);
        }
      }
    }
  }
}


void Device::identifyToUser()
{
  if (canIdentifyToUser()) {
    mOutput->identifyToUser(); // pass on to behaviour by default
  }
  else {
    inherited::identifyToUser();
  }
}


bool Device::canIdentifyToUser()
{
  return mOutput && mOutput->canIdentifyToUser();
}


void Device::saveScene(SceneNo aSceneNo)
{
  // see if we have a scene table at all
  OLOG(LOG_NOTICE, "SaveScene(%s)", VdcHost::sceneText(aSceneNo).c_str());
  SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(mDeviceSettings);
  if (scenes) {
    // we have a device-wide scene table, get the scene object
    DsScenePtr scene = scenes->getScene(aSceneNo);
    if (scene) {
      // scene found, now capture to all of our outputs
      if (mOutput) {
        // capture value from this output, reading from device (if possible) to catch e.g. color changes applied via external means (hue remote app etc.)
        mOutput->captureScene(scene, true, boost::bind(&Device::outputSceneValueSaved, this, scene));
      }
    }
  }
}


void Device::outputSceneValueSaved(DsScenePtr aScene)
{
  // Check special area scene case: dontCare need to be updated depending on brightness (if zero, set don't care)
  SceneNo sceneNo = aScene->mSceneNo;
  int area = aScene->mSceneArea;
  if (area) {
    // detail check - set don't care when saving Area On-Scene
    if (sceneNo==mainSceneForArea(area)) {
      // saving Main ON scene - set dontCare flag when main/default channel is zero, otherwise clear dontCare
      ChannelBehaviourPtr ch = mOutput->getChannelByType(channeltype_default);
      if (ch) {
        bool mustBeDontCare = ch->getChannelValue()==0;
        // update this main scene's dontCare
        aScene->setDontCare(mustBeDontCare);
        // also update the off scene's dontCare
        SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(mDeviceSettings);
        DsScenePtr offScene = scenes->getScene(offSceneForArea(area));
        if (offScene) {
          offScene->setDontCare(mustBeDontCare);
          // update scene in scene table and DB if dirty
          updateSceneIfDirty(offScene);
        }
      }
    }
  }
  // update scene in scene table and DB if dirty
  updateSceneIfDirty(aScene);
}


void Device::updateSceneIfDirty(DsScenePtr aScene)
{
  SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(mDeviceSettings);
  if (scenes && aScene->isDirty()) {
    scenes->updateScene(aScene);
  }
}



bool Device::processControlValue(const string &aName, double aValue)
{
  // default base class behaviour is letting know the output behaviour
  if (mOutput) {
    return mOutput->processControlValue(aName, aValue);
  }
  return false; // nothing to process
}



// MARK: - persistent device params


ErrorPtr Device::load()
{
  ErrorPtr err;
  // if we don't have device settings at this point (created by subclass), this is a misconfigured device!
  if (!mDeviceSettings) {
    OLOG(LOG_ERR, "***** no settings at load() time! -> probably misconfigured");
    return WebError::webErr(500,"missing settings");
  }
  // load the device settings
  if (mDeviceSettings) {
    err = mDeviceSettings->loadFromStore(mDSUID.getString().c_str());
    if (Error::notOK(err)) OLOG(LOG_ERR,"Error loading settings: %s", err->text());
  }
  // load the behaviours
  for (BehaviourVector::iterator pos = mButtons.begin(); pos!=mButtons.end(); ++pos) (*pos)->load();
  for (BehaviourVector::iterator pos = mInputs.begin(); pos!=mInputs.end(); ++pos) (*pos)->load();
  for (BehaviourVector::iterator pos = mSensors.begin(); pos!=mSensors.end(); ++pos) (*pos)->load();
  if (mOutput) mOutput->load();
  // load settings from files
  #if ENABLE_SETTINGS_FROM_FILES
  loadSettingsFromFiles();
  #endif
  return ErrorPtr();
}


ErrorPtr Device::save()
{
  ErrorPtr err;
  // save the device settings
  if (mDeviceSettings) err = mDeviceSettings->saveToStore(mDSUID.getString().c_str(), false); // only one record per device
  if (Error::notOK(err)) OLOG(LOG_ERR,"Error saving settings: %s", err->text());
  // save the behaviours
  for (BehaviourVector::iterator pos = mButtons.begin(); pos!=mButtons.end(); ++pos) (*pos)->save();
  for (BehaviourVector::iterator pos = mInputs.begin(); pos!=mInputs.end(); ++pos) (*pos)->save();
  for (BehaviourVector::iterator pos = mSensors.begin(); pos!=mSensors.end(); ++pos) (*pos)->save();
  if (mOutput) mOutput->save();
  return ErrorPtr();
}


bool Device::isDirty()
{
  // check the device settings
  if (mDeviceSettings && mDeviceSettings->isDirty()) return true;
  for (BehaviourVector::iterator pos = mButtons.begin(); pos!=mButtons.end(); ++pos) if ((*pos)->isDirty()) return true;
  for (BehaviourVector::iterator pos = mInputs.begin(); pos!=mInputs.end(); ++pos) if ((*pos)->isDirty()) return true;
  for (BehaviourVector::iterator pos = mSensors.begin(); pos!=mSensors.end(); ++pos) if ((*pos)->isDirty()) return true;
  if (mOutput && mOutput->isDirty()) return true;
  return false;
}


void Device::markClean()
{
  // check the device settings
  if (mDeviceSettings) mDeviceSettings->markClean();
  for (BehaviourVector::iterator pos = mButtons.begin(); pos!=mButtons.end(); ++pos) (*pos)->markClean();
  for (BehaviourVector::iterator pos = mInputs.begin(); pos!=mInputs.end(); ++pos) (*pos)->markClean();
  for (BehaviourVector::iterator pos = mSensors.begin(); pos!=mSensors.end(); ++pos) (*pos)->markClean();
  if (mOutput) mOutput->markClean();
}


ErrorPtr Device::forget()
{
  // delete the device settings
  if (mDeviceSettings) mDeviceSettings->deleteFromStore();
  // delete the behaviours
  for (BehaviourVector::iterator pos = mButtons.begin(); pos!=mButtons.end(); ++pos) (*pos)->forget();
  for (BehaviourVector::iterator pos = mInputs.begin(); pos!=mInputs.end(); ++pos) (*pos)->forget();
  for (BehaviourVector::iterator pos = mSensors.begin(); pos!=mSensors.end(); ++pos) (*pos)->forget();
  if (mOutput) mOutput->forget();
  return ErrorPtr();
}


#if ENABLE_SETTINGS_FROM_FILES

void Device::loadSettingsFromFiles()
{
  string dir = getVdcHost().getConfigDir();
  const int numLevels = 4;
  string levelids[numLevels];
  // Level strategy: most specialized will be active, unless lower levels specify explicit override
  // - Baselines are hardcoded defaults plus settings (already) loaded from persistent store
  // - Level 0 are settings related to the device instance (dSUID)
  // - Level 1 are settings related to the device class/version (deviceClass()_deviceClassVersion())
  // - Level 2 are settings related to the device type (deviceTypeIdentifier())
  // - Level 3 are settings related to the vDC (vdcClassIdentifier())
  levelids[0] = "vdsd_" + getDsUid().getString();
  levelids[1] = string_format("%s_%d_class", deviceClass().c_str(), deviceClassVersion());
  levelids[2] = string_format("%s_device", deviceTypeIdentifier().c_str());
  levelids[3] = mVdcP->vdcClassIdentifier();
  for(int i=0; i<numLevels; ++i) {
    // try to open config file
    string fn = dir+"devicesettings_"+levelids[i]+".csv";
    // if device has already stored properties, only explicitly marked properties will be applied
    if (loadSettingsFromFile(fn.c_str(), mDeviceSettings->rowid!=0)) markClean();
  }
}

#endif // ENABLE_SETTINGS_FROM_FILES


// MARK: - property access

enum {
  // device level simple parameters
  colorClass_key,
  zoneID_key,
  progMode_key,
  implementationId_key,
  softwareRemovable_key,
  teachinSignals_key,
  // output
  output_description_key, // output is not array!
  output_settings_key, // output is not array!
  output_state_key, // output is not array!
  // the scenes + undo
  scenes_key,
  undoState_key,
  // model features
  modelFeatures_key,
  // device configurations
  configurationDescriptions_key,
  configurationId_key,
  // device class
  deviceClass_key,
  deviceClassVersion_key,
  #if ENABLE_LOCALCONTROLLER
  zoneName_key,
  #endif
  #if ENABLE_JSONBRIDGEAPI
  allowBridging_key,
  #endif
  numDeviceFieldKeys
};


const int numBehaviourArrays = 4; // buttons, inputs, Sensors, Channels
const int numDeviceProperties = numDeviceFieldKeys+3*numBehaviourArrays;



static char device_obj;
static char device_output_key;

static char device_buttons_key;
static char device_inputs_key;
static char device_sensors_key;
static char device_channels_key;

static char device_scenes_key;

static char device_modelFeatures_key;
static char device_configurations_key;



int Device::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->isRootOfObject()) {
    // Accessing properties at the Device (root) level
    return inherited::numProps(aDomain, aParentDescriptor)+numDeviceProperties;
  }
  else if (aParentDescriptor->hasObjectKey(device_modelFeatures_key)) {
    // model features
    return numModelFeatures;
  }
  else if (aParentDescriptor->hasObjectKey(device_buttons_key)) {
    return (int)mButtons.size();
  }
  else if (aParentDescriptor->hasObjectKey(device_inputs_key)) {
    return (int)mInputs.size();
  }
  else if (aParentDescriptor->hasObjectKey(device_sensors_key)) {
    return (int)mSensors.size();
  }
  else if (aParentDescriptor->hasObjectKey(device_channels_key)) {
    return numChannels(); // if no output, this returns 0
  }
  else if (aParentDescriptor->hasObjectKey(device_configurations_key)) {
    return (int)mCachedConfigurations.size();
  }
  else if (aParentDescriptor->hasObjectKey(device_scenes_key)) {
    SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(mDeviceSettings);
    if (scenes)
      return INVALID_SCENE_NO;
    else
      return 0; // device with no scenes
  }
  return 0; // none
}


PropertyDescriptorPtr Device::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // device level properties
  static const PropertyDescription properties[numDeviceProperties] = {
    // common device properties
    { "primaryGroup", apivalue_uint64, colorClass_key, OKEY(device_obj) },
    { "zoneID", apivalue_uint64, zoneID_key, OKEY(device_obj) },
    { "progMode", apivalue_bool, progMode_key, OKEY(device_obj) },
    { "implementationId", apivalue_string, implementationId_key, OKEY(device_obj) },
    { "x-p44-softwareRemovable", apivalue_bool, softwareRemovable_key, OKEY(device_obj) },
    { "x-p44-teachInSignals", apivalue_int64, teachinSignals_key, OKEY(device_obj) },
    // the behaviour arrays
    // Note: the prefixes for xxxDescriptions, xxxSettings and xxxStates must match
    //   getTypeName() of the behaviours.
    { "buttonInputDescriptions", apivalue_object+propflag_container, descriptions_key_offset, OKEY(device_buttons_key) },
    { "buttonInputSettings", apivalue_object+propflag_container, settings_key_offset, OKEY(device_buttons_key) },
    { "buttonInputStates", apivalue_object+propflag_container, states_key_offset, OKEY(device_buttons_key) },
    { "binaryInputDescriptions", apivalue_object+propflag_container, descriptions_key_offset, OKEY(device_inputs_key) },
    { "binaryInputSettings", apivalue_object+propflag_container, settings_key_offset, OKEY(device_inputs_key) },
    { "binaryInputStates", apivalue_object+propflag_container, states_key_offset, OKEY(device_inputs_key) },
    { "sensorDescriptions", apivalue_object+propflag_container, descriptions_key_offset, OKEY(device_sensors_key) },
    { "sensorSettings", apivalue_object+propflag_container, settings_key_offset, OKEY(device_sensors_key) },
    { "sensorStates", apivalue_object+propflag_container, states_key_offset, OKEY(device_sensors_key) },
    { "channelDescriptions", apivalue_object+propflag_container, descriptions_key_offset, OKEY(device_channels_key) },
    { "channelSettings", apivalue_object+propflag_container, settings_key_offset, OKEY(device_channels_key) },
    { "channelStates", apivalue_object+propflag_container, states_key_offset, OKEY(device_channels_key) },
    // the single output
    { "outputDescription", apivalue_object, descriptions_key_offset, OKEY(device_output_key) },
    { "outputSettings", apivalue_object, settings_key_offset, OKEY(device_output_key) },
    { "outputState", apivalue_object, states_key_offset, OKEY(device_output_key) },
    // the scenes array
    { "scenes", apivalue_object+propflag_container, scenes_key, OKEY(device_scenes_key) },
    { "undoState", apivalue_object, undoState_key, OKEY(device_obj) },
    // the modelFeatures (row from former dSS visibility matrix, controlling what is shown in the UI)
    { "modelFeatures", apivalue_object+propflag_container, modelFeatures_key, OKEY(device_modelFeatures_key) },
    // the current and possible configurations for the device (button two-way vs. 2 one-way etc.)
    { "configurationDescriptions", apivalue_object+propflag_container+propflag_needsreadprep, configurationDescriptions_key, OKEY(device_configurations_key) },
    { "configurationId", apivalue_string, configurationId_key, OKEY(device_obj) },
    // device class
    { "deviceClass", apivalue_string, deviceClass_key, OKEY(device_obj) },
    { "deviceClassVersion", apivalue_uint64, deviceClassVersion_key, OKEY(device_obj) },
    #if ENABLE_LOCALCONTROLLER
    { "x-p44-zonename", apivalue_string, zoneName_key, OKEY(device_obj) },
    #endif
    #if ENABLE_JSONBRIDGEAPI
    { "x-p44-allowBridging", apivalue_bool, allowBridging_key, OKEY(device_obj) },
    #endif
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
  else if (aParentDescriptor->hasObjectKey(device_modelFeatures_key)) {
    // model features - distinct set of boolean flags
    if (aPropIndex<numModelFeatures) {
      DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
      descP->propertyName = modelFeatureNames[aPropIndex];
      descP->propertyType = apivalue_bool;
      descP->propertyFieldKey = aPropIndex;
      descP->propertyObjectKey = OKEY(device_modelFeatures_key);
      return descP;
    }
  }
  else if (aParentDescriptor->isArrayContainer()) {
    // accessing one of the other containers: channels, buttons/inputs/sensors, scenes or configurations
    string id;
    if (aParentDescriptor->hasObjectKey(device_buttons_key)) {
      id = mButtons[aPropIndex]->getApiId(aParentDescriptor->getApiVersion());
    }
    else if (aParentDescriptor->hasObjectKey(device_inputs_key)) {
      id = mInputs[aPropIndex]->getApiId(aParentDescriptor->getApiVersion());
    }
    else if (aParentDescriptor->hasObjectKey(device_sensors_key)) {
      id = mSensors[aPropIndex]->getApiId(aParentDescriptor->getApiVersion());
    }
    else if (aParentDescriptor->hasObjectKey(device_channels_key)) {
      id = getChannelByIndex(aPropIndex)->getApiId(aParentDescriptor->getApiVersion());
    }
    else if (aParentDescriptor->hasObjectKey(device_scenes_key)) {
      // scenes are still named by their index
      id = string_format("%d", aPropIndex);
    }
    else if (aParentDescriptor->hasObjectKey(device_configurations_key)) {
      id = mCachedConfigurations[aPropIndex]->getId();
    }
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = id;
    descP->propertyType = apivalue_object;
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = aParentDescriptor->objectKey();
    return descP;
  }
  return PropertyDescriptorPtr();
}



PropertyDescriptorPtr Device::getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor)
{
  // efficient by-index access for scenes, as these are always accessed by index (they do not have a id)
  if (aParentDescriptor->hasObjectKey(device_scenes_key)) {
    // array-like container: channels, buttons/inputs/sensors or scenes
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
  else if (
    aParentDescriptor->hasObjectKey(device_channels_key) &&
    aStartIndex==0 &&
    aPropMatch=="0" &&
    mOutput &&
    mOutput->numChannels()>0
  ) {
    // special case for backwards compatibility: channel with id "0" is the default (first) channel
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = "0";
    descP->propertyType = aParentDescriptor->type();
    descP->propertyFieldKey = aStartIndex;
    descP->propertyObjectKey = aParentDescriptor->objectKey();
    // advance index
    aStartIndex++;
    return descP;
  }
  // None of the containers within Device - let base class handle Device-Level properties
  return inherited::getDescriptorByName(aPropMatch, aStartIndex, aDomain, aMode, aParentDescriptor);
}



PropertyContainerPtr Device::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  // might be virtual container
  if (aPropertyDescriptor->isArrayContainer()) {
    // one of the local containers
    return PropertyContainerPtr(this); // handle myself
  }
  // containers are elements from the behaviour arrays
  else if (aPropertyDescriptor->hasObjectKey(device_buttons_key)) {
    return mButtons[aPropertyDescriptor->fieldKey()];
  }
  else if (aPropertyDescriptor->hasObjectKey(device_inputs_key)) {
    return mInputs[aPropertyDescriptor->fieldKey()];
  }
  else if (aPropertyDescriptor->hasObjectKey(device_sensors_key)) {
    return mSensors[aPropertyDescriptor->fieldKey()];
  }
  else if (aPropertyDescriptor->hasObjectKey(device_channels_key)) {
    if (!mOutput) return PropertyContainerPtr(); // none
    return mOutput->getChannelByIndex((int)aPropertyDescriptor->fieldKey());
  }
  else if (aPropertyDescriptor->hasObjectKey(device_scenes_key)) {
    SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(mDeviceSettings);
    if (scenes) {
      return scenes->getScene(aPropertyDescriptor->fieldKey());
    }
  }
  else if (aPropertyDescriptor->hasObjectKey(device_output_key)) {
    if (mOutput && mOutput->numDescProps()>0) {
      return mOutput;
    }
    return NULL; // no output or special output with no standard properties (e.g. actionOutput)
  }
  else if (aPropertyDescriptor->hasObjectKey(device_configurations_key)) {
    return mCachedConfigurations[aPropertyDescriptor->fieldKey()];
  }
  else if (aPropertyDescriptor->hasObjectKey(device_obj)) {
    // device level object properties
    if (aPropertyDescriptor->fieldKey()==undoState_key) {
      return mPreviousState;
    }
  }
  // unknown here
  return NULL;
}



void Device::prepareAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor, StatusCB aPreparedCB)
{
  if (aPropertyDescriptor->hasObjectKey(device_configurations_key)) {
    // have device create these
    getDeviceConfigurations(mCachedConfigurations, aPreparedCB);
    return;
  }
  // nothing to do here, let inherited handle it
  inherited::prepareAccess(aMode, aPropertyDescriptor, aPreparedCB);
}


void Device::finishAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(device_configurations_key)) {
    // we dont need these any more
    mCachedConfigurations.clear();
  }
}


bool Device::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(device_obj)) {
    // Device level field properties
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case colorClass_key:
          aPropValue->setUint16Value(getColorClass()); return true;
        case zoneID_key:
          aPropValue->setUint16Value(getZoneID()); return true;
        case progMode_key:
          aPropValue->setBoolValue(mProgMode); return true;
        case implementationId_key:
          aPropValue->setStringValue(deviceTypeIdentifier()); return true;
        case deviceClass_key:
          if (deviceClass().size()>0) { aPropValue->setStringValue(deviceClass()); return true; } else return false;
        case deviceClassVersion_key:
          if (deviceClassVersion()>0) { aPropValue->setUint32Value(deviceClassVersion()); return true; } else return false;
        case softwareRemovable_key:
          aPropValue->setBoolValue(isSoftwareDisconnectable()); return true;
        case teachinSignals_key:
          aPropValue->setInt8Value(teachInSignal(-1)); return true; // query number of teach-in signals
        case configurationId_key:
          if (getDeviceConfigurationId().empty()) return false; // device does not have multiple configurations
          aPropValue->setStringValue(getDeviceConfigurationId()); return true; // current device configuration
        #if ENABLE_LOCALCONTROLLER
        case zoneName_key: if (mDeviceSettings) {
          LocalControllerPtr lc = getVdcHost().getLocalController();
          if (!lc) return false; // only available with localcontroller
          ZoneDescriptorPtr z = LocalController::sharedLocalController()->mLocalZones.getZoneById(mDeviceSettings->mZoneID, false);
          string zn;
          if (mDeviceSettings->mZoneID!=0) {
            // no name for devices without zone assignment (even if localcontroller does have a name for those)
            if (z) zn = z->getName();
            else zn = string_format("Zone_#%d", mDeviceSettings->mZoneID);
          }
          aPropValue->setStringValue(zn);
          return true;
        } else return false;
        #endif // ENABLE_LOCALCONTROLLER
        #if ENABLE_JSONBRIDGEAPI
        case allowBridging_key:
          aPropValue->setBoolValue(mDeviceSettings && mDeviceSettings->mAllowBridging); return true;
        #endif // ENABLE_JSONBRIDGEAPI
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        case zoneID_key:
          setZoneID(aPropValue->int32Value());
          return true;
        case progMode_key:
          mProgMode = aPropValue->boolValue();
          return true;
        #if ENABLE_JSONBRIDGEAPI
        case allowBridging_key:
          if (mDeviceSettings->setPVar(mDeviceSettings->mAllowBridging, aPropValue->boolValue())) {
            // bridgeability changed, push to bridge
            pushBridgeable();
          }
          return true;
        #endif
      }
    }
  }
  else if (aPropertyDescriptor->hasObjectKey(device_modelFeatures_key)) {
    // model features
    if (aMode==access_read) {
      if (hasModelFeature((DsModelFeatures)aPropertyDescriptor->fieldKey())==yes) {
        aPropValue->setBoolValue(true);
        return true;
      }
      else {
        return false;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


ErrorPtr Device::writtenProperty(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor, int aDomain, PropertyContainerPtr aContainer)
{
  if (aPropertyDescriptor->hasObjectKey(device_scenes_key)) {
    // a scene was written, update needed if dirty
    DsScenePtr scene = boost::dynamic_pointer_cast<DsScene>(aContainer);
    SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(mDeviceSettings);
    if (scenes && scene && scene->isDirty()) {
      scenes->updateScene(scene);
      return ErrorPtr();
    }
  }
  else if (
    aPropertyDescriptor->hasObjectKey(device_channels_key) && // one or multiple channel's...
    aPropertyDescriptor->fieldKey()==states_key_offset && // ...state(s)...
    aMode==access_write // ...got a non-preload write
  ) {
    // apply new channel values to hardware, not dimming
    mVdcP->cancelNativeActionUpdate(); // still delayed native scene updates must be cancelled before changing channel values
    requestApplyingChannels(NoOP, false);
  }
  return inherited::writtenProperty(aMode, aPropertyDescriptor, aDomain, aContainer);
}


// MARK: - Device description/shortDesc/status


string Device::description()
{
  string s = inherited::description(); // DsAdressable
  if (mButtons.size()>0) string_format_append(s, "\n- Buttons: %lu", mButtons.size());
  if (mInputs.size()>0) string_format_append(s, "\n- Binary Inputs: %lu", mInputs.size());
  if (mSensors.size()>0) string_format_append(s, "\n- Sensors: %lu", mSensors.size());
  if (numChannels()>0) string_format_append(s, "\n- Output Channels: %d", numChannels());
  return s;
}


string Device::getStatusText()
{
  string s;
  if (mOutput) {
    s = mOutput->getStatusText();
  }
  if (s.empty() && mSensors.size()>0) {
    s = mSensors[0]->getStatusText();
  }
  if (s.empty() && mInputs.size()>0) {
    s = mInputs[0]->getStatusText();
  }
  return s;
}


// MARK: - Device scripting object

#if P44SCRIPT_FULL_SUPPORT

using namespace P44Script;

ScriptObjPtr Device::newDeviceObj()
{
  return new DeviceObj(this);
}

ScriptMainContextPtr Device::getDeviceScriptContext()
{
  if (!mDeviceScriptContext) {
    // create on demand, such that only devices actually using scripts get a context
    mDeviceScriptContext = StandardScriptingDomain::sharedDomain().newContext(newDeviceObj());
  }
  return mDeviceScriptContext;
}



static ScriptObjPtr output_accessor(BuiltInMemberLookup& aMemberLookup, ScriptObjPtr aParentObj, ScriptObjPtr aObjToWrite)
{
  DeviceObj* d = dynamic_cast<DeviceObj*>(aParentObj.get());
  assert(d);
  OutputBehaviourPtr o = d->device()->getOutput();
  if (o) {
    return new OutputObj(o);
  }
  return new AnnotatedNullValue("device has no output");
}

static ScriptObjPtr name_accessor(BuiltInMemberLookup& aMemberLookup, ScriptObjPtr aParentObj, ScriptObjPtr aObjToWrite)
{
  DeviceObj* d = dynamic_cast<DeviceObj*>(aParentObj.get());
  assert(d);
  return new StringValue(d->device()->getName());
}


// button(id_or_index)
// sensor(id_or_index)
// input(id_or_index)
static const BuiltInArgDesc behaviour_args[] = { { text } };
static const size_t behaviour_numargs = sizeof(behaviour_args)/sizeof(BuiltInArgDesc);
static void inputValueSource_func(BuiltinFunctionContextPtr f)
{
  string fn = f->funcObj()->getIdentifier(); // function name determines behaviour type to get
  DeviceObj* dev = dynamic_cast<DeviceObj *>(f->thisObj().get());
  DevicePtr device = dev->device();
  DsBehaviourPtr b;
  if (uequals(fn, "button")) b = device->getButton(Device::by_id_or_index, f->arg(0)->stringValue());
  else if (uequals(fn, "sensor")) b = device->getSensor(Device::by_id_or_index, f->arg(0)->stringValue());
  else if (uequals(fn, "input")) b = device->getInput(Device::by_id_or_index, f->arg(0)->stringValue());
  ValueSource* vs = dynamic_cast<ValueSource *>(b.get());
  if (!vs) {
    f->finish(new ErrorValue(ScriptError::NotFound, "%s '%s' not found in device '%s'", fn.c_str(), f->arg(0)->stringValue().c_str(), device->getName().c_str()));
    return;
  }
  // return the value source corresponding with this input behaviour
  f->finish(new ValueSourceObj(vs));
}

static const BuiltinMemberDescriptor deviceMembers[] = {
  { "output", builtinmember, 0, NULL, (BuiltinFunctionImplementation)&output_accessor }, // Note: correct '.accessor=&lrg_accessor' form does not work with OpenWrt g++, so need ugly cast here
  { "name", builtinmember, 0, NULL, (BuiltinFunctionImplementation)&name_accessor }, // Note: correct '.accessor=&lrg_accessor' form does not work with OpenWrt g++, so need ugly cast here
  { "button", executable|any, behaviour_numargs, behaviour_args, &inputValueSource_func },
  { "sensor", executable|any, behaviour_numargs, behaviour_args, &inputValueSource_func },
  { "input", executable|any, behaviour_numargs, behaviour_args, &inputValueSource_func },
  { NULL } // terminator
};


static BuiltInMemberLookup* sharedDeviceMemberLookupP = NULL;

DeviceObj::DeviceObj(DevicePtr aDevice) :
  mDevice(aDevice)
{
  if (sharedDeviceMemberLookupP==NULL) {
    sharedDeviceMemberLookupP = new BuiltInMemberLookup(deviceMembers);
    sharedDeviceMemberLookupP->isMemberVariable(); // disable refcounting
  }
  registerMemberLookup(sharedDeviceMemberLookupP);
}


#endif // P44SCRIPT_FULL_SUPPORT
