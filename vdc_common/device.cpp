//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


#include "device.hpp"

#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "outputbehaviour.hpp"
#include "sensorbehaviour.hpp"

using namespace p44;


// MARK: ===== Device


Device::Device(Vdc *aVdcP) :
  progMode(false),
  isDimming(false),
  dimHandlerTicket(0),
  dimTimeoutTicket(0),
  currentDimMode(dimmode_stop),
  areaDimmed(0),
  areaDimMode(dimmode_stop),
  vdcP(aVdcP),
  DsAddressable(&aVdcP->getVdcHost()),
  colorClass(class_black_joker),
  applyInProgress(false),
  missedApplyAttempts(0),
  updateInProgress(false),
  serializerWatchdogTicket(0)
{
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
  string_format_append(aHashedString, "%s:%d:", deviceTypeIdentifier().c_str(), colorClass);
  // behaviours
  for (BehaviourVector::iterator pos = buttons.begin(); pos!=buttons.end(); ++pos) aHashedString += (*pos)->behaviourTypeIdentifier();
  for (BehaviourVector::iterator pos = binaryInputs.begin(); pos!=binaryInputs.end(); ++pos) aHashedString += (*pos)->behaviourTypeIdentifier();
  for (BehaviourVector::iterator pos = sensors.begin(); pos!=sensors.end(); ++pos) aHashedString += (*pos)->behaviourTypeIdentifier();
  if (output) aHashedString += output->behaviourTypeIdentifier();
  // model features
  for (int f=0; f<numModelFeatures; f++) {
    aHashedString += hasModelFeature((DsModelFeatures)f)==yes ? 'T' : 'F';
  }
}


Device::~Device()
{
  buttons.clear();
  binaryInputs.clear();
  sensors.clear();
  output.reset();
}


string Device::vendorName()
{
  // default to same vendor as class container
  return vdcP->vendorName();
}


void Device::setName(const string &aName)
{
  if (aName!=getAssignedName()) {
    // has changed
    inherited::setName(aName);
    // make sure it will be saved
    if (deviceSettings) {
      deviceSettings->markDirty();
    }
  }
}


void Device::setColorClass(DsClass aColorGroup)
{
  colorClass = aColorGroup;
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
  // check if group determines apparent (icon color) class
  DsGroup group = group_undefined;
  if (output) {
    // lowest group of output determines dominant color
    for (int i = group_yellow_light; i<numColorClasses; i++) {
      if (output->isMember((DsGroup)i)) {
        group = (DsGroup)i;
        break;
      }
    }
  }
  // if no or undefined output, check input colors
  if (group==group_undefined && buttons.size()>0) {
    // second priority: color of first button
    ButtonBehaviourPtr btn = boost::dynamic_pointer_cast<ButtonBehaviour>(buttons[0]);
    group =  btn->buttonGroup;
  }
  if (group==group_undefined && sensors.size()>0) {
    // third priority: color of first sensor
    SensorBehaviourPtr sns = boost::dynamic_pointer_cast<SensorBehaviour>(sensors[0]);
    group =  sns->sensorGroup;
  }
  if (group==group_undefined && binaryInputs.size()>0) {
    // fourth priority: color of first binary input
    BinaryInputBehaviourPtr bin = boost::dynamic_pointer_cast<BinaryInputBehaviour>(binaryInputs[0]);
    group =  bin->binInputGroup;
  }
  // Return color class the dominant group belongs to
  DsClass cl = colorClassFromGroup(group);
  return cl!=class_undefined ? cl : colorClass;
}


bool Device::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getClassColoredIcon("vdsd", getDominantColorClass(), aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


void Device::installSettings(DeviceSettingsPtr aDeviceSettings)
{
  if (aDeviceSettings) {
    deviceSettings = aDeviceSettings;
  }
  else {
    // use standard settings
    deviceSettings = DeviceSettingsPtr(new DeviceSettings(*this));
  }
}




void Device::addBehaviour(DsBehaviourPtr aBehaviour)
{
  if (aBehaviour) {
    switch (aBehaviour->getType()) {
      case behaviour_button:
        aBehaviour->index = buttons.size();
        buttons.push_back(aBehaviour);
        break;
      case behaviour_binaryinput:
        aBehaviour->index = binaryInputs.size();
        binaryInputs.push_back(aBehaviour);
        break;
      case behaviour_sensor:
        aBehaviour->index = sensors.size();
        sensors.push_back(aBehaviour);
        break;
      case behaviour_output:
      case behaviour_actionOutput:
      {
        aBehaviour->index = 0;
        output = boost::dynamic_pointer_cast<OutputBehaviour>(aBehaviour);
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

// MARK: ===== model features

static const char *modelFeatureNames[numModelFeatures] = {
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
  "fcu"
};


Tristate Device::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // ask output first, might have more specific info
  if (output) {
    Tristate hasFeature = output->hasModelFeature(aFeatureIndex);
    if (hasFeature!=undefined) return hasFeature; // output has a say about the feature, no need to check at device level
  }
  // now check for device level features
  switch (aFeatureIndex) {
    case modelFeature_dontcare:
      // Generic: all devices with scene table have the ability to set scene's don't care flag
      return boost::dynamic_pointer_cast<SceneDeviceSettings>(deviceSettings)!=NULL ? yes : no;
    case modelFeature_ledauto:
    case modelFeature_leddark:
      // Virtual devices do not have the standard dS LED at all
      return no;
    case modelFeature_pushbutton:
    case modelFeature_pushbarea:
    case modelFeature_pushbadvanced:
      // Assumption: any device with a buttonInputBehaviour has these props
      return buttons.size()>0 ? yes : no;
    case modelFeature_pushbsensor:
      return no; // we definitely don't have buttons that can be converted to sensors
    case modelFeature_pushbdevice:
      // Check if any of the buttons has localbutton functionality available
      for (BehaviourVector::iterator pos = buttons.begin(); pos!=buttons.end(); ++pos) {
        ButtonBehaviourPtr b = boost::dynamic_pointer_cast<ButtonBehaviour>(*pos);
        if (b && b->supportsLocalKeyMode) {
          return yes; // there is a button with local key mode support
        }
      }
      return no; // no button that supports local key mode
    case modelFeature_pushbcombined:
    case modelFeature_twowayconfig:
      // Assumption: devices with more than single button input are combined up/down (or even 4-way and more) buttons, and need two-way config
      return buttons.size()>1 ? yes : no;
    case modelFeature_highlevel:
      // Assumption: only black joker devices can have a high-level (app) functionality
      return colorClass==class_black_joker ? yes : no;
    case modelFeature_jokerconfig:
      // Assumption: black joker devices need joker config (setting color) only if there are buttons or an output.
      // Pure sensors or binary inputs don't need color config
      return colorClass==class_black_joker && (output || buttons.size()>0) ? yes : no;
    case modelFeature_akmsensor:
      // Assumption: only devices with binaryinputs that do not have a predefined type need akmsensor
      for (BehaviourVector::iterator pos = binaryInputs.begin(); pos!=binaryInputs.end(); ++pos) {
        BinaryInputBehaviourPtr b = boost::dynamic_pointer_cast<BinaryInputBehaviour>(*pos);
        if (b && b->getHardwareInputType()==binInpType_none) {
          return yes; // input with no predefined functionality, need to be able to configure sensor
        }
      }
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


// MARK: ===== Channels


int Device::numChannels()
{
  if (output)
    return (int)output->numChannels();
  else
    return 0;
}


bool Device::needsToApplyChannels()
{
  for (int i=0; i<numChannels(); i++) {
    ChannelBehaviourPtr ch = getChannelByIndex(i, true);
    if (ch) {
      // at least this channel needs update
      LOG(LOG_DEBUG, "needsToApplyChannels() returns true because of %s", ch->description().c_str());
      return true;
    }
  }
  // no channel needs apply
  return false;
}


ChannelBehaviourPtr Device::getChannelByIndex(int aChannelIndex, bool aPendingApplyOnly)
{
  if (!output) return ChannelBehaviourPtr();
  return output->getChannelByIndex(aChannelIndex, aPendingApplyOnly);
}


ChannelBehaviourPtr Device::getChannelByType(DsChannelType aChannelType, bool aPendingApplyOnly)
{
  if (!output) return ChannelBehaviourPtr();
  return output->getChannelByType(aChannelType, aPendingApplyOnly);
}


ChannelBehaviourPtr Device::getChannelById(const string aChannelId, bool aPendingApplyOnly)
{
  if (!output) return ChannelBehaviourPtr();
  return output->getChannelById(aChannelId, aPendingApplyOnly);
}



// MARK: ===== Device level vDC API


ErrorPtr Device::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="x-p44-removeDevice") {
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
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}



#define MOC_DIM_STEP_TIMEOUT (5*Second)
#define LEGACY_DIM_STEP_TIMEOUT (500*MilliSecond) // should be 400, but give it extra 100 because of delays in getting next dim call, especially for area scenes


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




void Device::handleNotification(VdcApiConnectionPtr aApiConnection, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr err;
  if (aMethod=="callScene") {
    // call scene
    ApiValuePtr o;
    if (Error::isOK(err = checkParam(aParams, "scene", o))) {
      SceneNo sceneNo = (SceneNo)o->int32Value();
      bool force = false;
      // check for force flag
      if (Error::isOK(err = checkParam(aParams, "force", o))) {
        force = o->boolValue();
        // now call
        callScene(sceneNo, force);
      }
    }
    if (!Error::isOK(err)) {
      ALOG(LOG_WARNING, "callScene error: %s", err->description().c_str());
    }
  }
  else if (aMethod=="saveScene") {
    // save scene
    ApiValuePtr o;
    if (Error::isOK(err = checkParam(aParams, "scene", o))) {
      SceneNo sceneNo = (SceneNo)o->int32Value();
      // now save
      saveScene(sceneNo);
    }
    if (!Error::isOK(err)) {
      ALOG(LOG_WARNING, "saveScene error: %s", err->description().c_str());
    }
  }
  else if (aMethod=="undoScene") {
    // save scene
    ApiValuePtr o;
    if (Error::isOK(err = checkParam(aParams, "scene", o))) {
      SceneNo sceneNo = (SceneNo)o->int32Value();
      // now save
      undoScene(sceneNo);
    }
    if (!Error::isOK(err)) {
      ALOG(LOG_WARNING, "undoScene error: %s", err->description().c_str());
    }
  }
  else if (aMethod=="setLocalPriority") {
    // set local priority
    ApiValuePtr o;
    if (Error::isOK(err = checkParam(aParams, "scene", o))) {
      SceneNo sceneNo = (SceneNo)o->int32Value();
      // now save
      setLocalPriority(sceneNo);
    }
    if (!Error::isOK(err)) {
      ALOG(LOG_WARNING, "setLocalPriority error: %s", err->description().c_str());
    }
  }
  else if (aMethod=="setControlValue") {
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
          ALOG(LOG_NOTICE, "processControlValue(%s, %f) completed -> requests applying channels now", controlValueName.c_str(), value);
          stopSceneActions();
          requestApplyingChannels(NULL, false);
        }
      }
    }
    if (!Error::isOK(err)) {
      ALOG(LOG_WARNING, "setControlValue error: %s", err->description().c_str());
    }
  }
  else if (aMethod=="callSceneMin") {
    // switch device on with minimum output level if not already on (=prepare device for dimming from zero)
    ApiValuePtr o;
    if (Error::isOK(err = checkParam(aParams, "scene", o))) {
      SceneNo sceneNo = (SceneNo)o->int32Value();
      // now call
      callSceneMin(sceneNo);
    }
    if (!Error::isOK(err)) {
      ALOG(LOG_WARNING, "callSceneMin error: %s", err->description().c_str());
    }
  }
  else if (aMethod=="dimChannel") {
    // start or stop dimming a channel
    ApiValuePtr o;
    ChannelBehaviourPtr channel;
    if (Error::isOK(err = checkChannel(aParams, channel))) {
      if (Error::isOK(err = checkParam(aParams, "mode", o))) {
        // mode
        int mode = o->int32Value();
        int area = 0;
        o = aParams->get("area");
        if (o) {
          area = o->int32Value();
        }
        // start/stop dimming
        dimChannelForArea(channel, mode==0 ? dimmode_stop : (mode<0 ? dimmode_down : dimmode_up), area, MOC_DIM_STEP_TIMEOUT);
      }
      else {
        err = Error::err<VdcApiError>(400, "Need to specify channel(type) or channelId");
      }
    }
    if (!Error::isOK(err)) {
      ALOG(LOG_WARNING, "dimChannel error: %s", err->description().c_str());
    }
  }
  else if (aMethod=="setOutputChannelValue") {
    // set output channel value (alias for setProperty outputStates)
    ApiValuePtr o;
    ChannelBehaviourPtr channel;
    if (Error::isOK(err = checkChannel(aParams, channel))) {
      if (Error::isOK(err = checkParam(aParams, "value", o))) {
        double value = o->doubleValue();
        // check optional apply_now flag
        bool apply_now = true; // non-buffered write by default
        o = aParams->get("apply_now");
        if (o) {
          apply_now = o->boolValue();
        }
        // reverse build the correctly structured property value: { channelStates: { <channel>: { value:<value> } } }
        // - value
        o = aParams->newObject();
        o->add("value", o->newDouble(value));
        // - channel id
        ApiValuePtr ch = o->newObject();
        ch->add(channel->getApiId(3), o);
        // - channelStates
        ApiValuePtr propValue = ch->newObject();
        propValue->add("channelStates", ch);
        // now access the property for write
        accessProperty(apply_now ? access_write : access_write_preload, propValue, VDC_API_DOMAIN, 3, NULL); // no callback
      }
    }
    if (!Error::isOK(err)) {
      ALOG(LOG_WARNING, "setOutputChannelValue error: %s", err->description().c_str());
    }
  }
  else if (aMethod=="identify") {
    // identify to user
    ALOG(LOG_NOTICE, "Identify");
    identifyToUser();
  }
  else {
    inherited::handleNotification(aApiConnection, aMethod, aParams);
  }
}


void Device::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // remove from container management
  DevicePtr dev = DevicePtr(this);
  vdcP->removeDevice(dev, aForgetParams);
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
  disconnect(aForgetParams, NULL);
}


void Device::scheduleVanish(bool aForgetParams, MLMicroSeconds aDelay)
{
  MainLoop::currentMainLoop().executeOnce(boost::bind(&Device::hasVanished, this, aForgetParams), aDelay);
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



// MARK: ===== dimming

// dS Dimming rule for Light:
//  Rule 4 All devices which are turned on and not in local priority state take part in the dimming process.


// implementation of "dimChannel" vDC API command and legacy dimming
// Note: ensures dimming only continues for at most aAutoStopAfter
void Device::dimChannelForArea(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode, int aArea, MLMicroSeconds aAutoStopAfter)
{
  if (!aChannel) return;
  LOG(LOG_DEBUG, "dimChannelForArea: aChannel=%s, aDimMode=%d, aArea=%d", aChannel->getName(), aDimMode, aArea);
  // check basic dimmability (e.g. avoid dimming brightness for lights that are off)
  if (aDimMode!=dimmode_stop && !(output->canDim(aChannel))) {
    LOG(LOG_DEBUG, "- behaviour does not allow dimming channel '%s' now (e.g. because light is off)", aChannel->getName());
    return;
  }
  // check area if any
  if (aArea>0) {
    SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(deviceSettings);
    if (scenes) {
      // check area first
      SceneNo areaScene = mainSceneForArea(aArea);
      DsScenePtr scene = scenes->getScene(areaScene);
      if (scene->isDontCare()) {
        LOG(LOG_DEBUG, "- area main scene(%d) is dontCare -> suppress dimChannel for Area %d", areaScene, aArea);
        return; // not in this area, suppress dimming
      }
    }
  }
  else {
    // non-area dimming: suppress if device is in local priority
    // Note: aArea can be set -1 to override local priority checking, for example when using method for identify purposes
    if (aArea==0 && output->hasLocalPriority()) {
      LOG(LOG_DEBUG, "- Non-area dimming, localPriority set -> suppressed");
      return; // local priority active, suppress dimming
    }
  }
  // always give device chance to stop, even if no dimming is in progress
  if (aDimMode==dimmode_stop) {
    stopSceneActions();
  }
  // requested dimming this device, no area suppress active
  if (aDimMode!=currentDimMode || aChannel!=currentDimChannel) {
    // mode changes
    if (aDimMode!=dimmode_stop) {
      // start or change direction
      if (currentDimMode==dimmode_stop) {
        // start dimming from stopped state: install timeout
        dimTimeoutTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&Device::dimAutostopHandler, this, aChannel), aAutoStopAfter);
      }
      else {
        // change dimming direction or channel
        // - stop previous dimming operation
        dimChannel(currentDimChannel, dimmode_stop);
        // - start new
        MainLoop::currentMainLoop().rescheduleExecutionTicket(dimTimeoutTicket, aAutoStopAfter);
      }
    }
    else {
      // stop
      MainLoop::currentMainLoop().cancelExecutionTicket(dimTimeoutTicket);
    }
    // actually execute
    dimChannel(aChannel, aDimMode);
    currentDimMode = aDimMode;
    currentDimChannel = aChannel;
    // save for possibly needed restart of area dimming
    areaDimmed = aArea;
    areaDimMode = aDimMode;
  }
  else {
    // same dim mode, just retrigger if dimming right now
    if (aDimMode!=dimmode_stop) {
      MainLoop::currentMainLoop().rescheduleExecutionTicket(dimTimeoutTicket, aAutoStopAfter);
    }
  }
}



// autostop handler (for both dimChannel and legacy dimming)
void Device::dimAutostopHandler(ChannelBehaviourPtr aChannel)
{
  // timeout: stop dimming immediately
  dimTimeoutTicket = 0;
  dimChannel(aChannel, dimmode_stop);
  currentDimMode = dimmode_stop; // stopped now
}



#define DIM_STEP_INTERVAL_MS 300.0
#define DIM_STEP_INTERVAL (DIM_STEP_INTERVAL_MS*MilliSecond)

// actual dimming implementation, usually overridden by subclasses to provide more optimized/precise dimming
void Device::dimChannel(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode)
{
  if (aChannel) {
    ALOG(LOG_INFO,
      "dimChannel (generic): channel '%s' %s",
      aChannel->getName(), aDimMode==dimmode_stop ? "STOPS dimming" : (aDimMode==dimmode_up ? "starts dimming UP" : "starts dimming DOWN")
    );
    // Simple base class implementation just increments/decrements channel values periodically (and skips steps when applying values is too slow)
    if (aDimMode==dimmode_stop) {
      // stop dimming
      isDimming = false;
      MainLoop::currentMainLoop().cancelExecutionTicket(dimHandlerTicket);
    }
    else {
      // start dimming
      // make sure the start point is calculated if needed
      aChannel->getChannelValueCalculated();
      aChannel->setNeedsApplying(0); // force re-applying start point, no transition time
      // calculate increment
      double increment = (aDimMode==dimmode_up ? DIM_STEP_INTERVAL_MS : -DIM_STEP_INTERVAL_MS) * aChannel->getDimPerMS();
      // start ticking
      isDimming = true;
      // wait for all apply operations to really complete before starting to dim
      SimpleCB dd = boost::bind(&Device::dimDoneHandler, this, aChannel, increment, MainLoop::now()+10*MilliSecond);
      waitForApplyComplete(boost::bind(&Device::requestApplyingChannels, this, dd, false, false));
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
  if (isDimming) {
    // now schedule next inc/update step
    dimHandlerTicket = MainLoop::currentMainLoop().executeOnceAt(boost::bind(&Device::dimHandler, this, aChannel, aIncrement, _2), aNextDimAt);
  }
}


// MARK: ===== high level serialized hardware access

#define SERIALIZER_WATCHDOG 1
#define SERIALIZER_WATCHDOG_TIMEOUT (20*Second)

void Device::requestApplyingChannels(SimpleCB aAppliedOrSupersededCB, bool aForDimming, bool aModeChange)
{
  if (!aModeChange && output && !output->isEnabled()) {
    // disabled output and not a mode change -> no operation
    AFOCUSLOG("requestApplyingChannels called with output disabled -> NOP");
    // - just call back immediately
    if (aAppliedOrSupersededCB) aAppliedOrSupersededCB();
  }
  AFOCUSLOG("requestApplyingChannels entered");
  // Caller wants current channel values applied to hardware
  // Three possible cases:
  // a) hardware is busy applying new values already -> confirm previous request to apply as superseded
  // b) hardware is busy updating values -> wait until this is done
  // c) hardware is not busy -> start apply right now
  if (applyInProgress) {
    FOCUSLOG("- requestApplyingChannels called while apply already running");
    // case a) confirm previous request because superseded
    if (appliedOrSupersededCB) {
      FOCUSLOG("- confirming previous (superseded) apply request");
      SimpleCB cb = appliedOrSupersededCB;
      appliedOrSupersededCB = aAppliedOrSupersededCB; // in case current callback should request another change, callback is already installed
      cb(); // call back now, values have been superseded
      FOCUSLOG("- previous (superseded) apply request confirmed");
    }
    else {
      appliedOrSupersededCB = aAppliedOrSupersededCB;
    }
    // - when previous request actually terminates, we need another update to make sure finally settled values are correct
    missedApplyAttempts++;
    FOCUSLOG("- missed requestApplyingChannels requests now %d", missedApplyAttempts);
  }
  else if (updateInProgress) {
    FOCUSLOG("- requestApplyingChannels called while update running -> postpone apply");
    // case b) cannot execute until update finishes
    missedApplyAttempts++;
    appliedOrSupersededCB = aAppliedOrSupersededCB;
    applyInProgress = true;
  }
  else {
    // case c) applying is not currently in progress, can start updating hardware now
    AFOCUSLOG("ready, calling applyChannelValues()");
    #if SERIALIZER_WATCHDOG
    // - start watchdog
    MainLoop::currentMainLoop().executeTicketOnce(serializerWatchdogTicket, boost::bind(&Device::serializerWatchdog, this), 10*Second); // new
    FOCUSLOG("+++++ Serializer watchdog started for apply with ticket #%ld", serializerWatchdogTicket);
    #endif
    // - start applying
    appliedOrSupersededCB = aAppliedOrSupersededCB;
    applyInProgress = true;
    applyChannelValues(boost::bind(&Device::applyingChannelsComplete, this), aForDimming);
  }
}


void Device::waitForApplyComplete(SimpleCB aApplyCompleteCB)
{
  if (!applyInProgress) {
    // not applying anything, immediately call back
    FOCUSLOG("- waitForApplyComplete() called while no apply in progress -> immediately call back");
    aApplyCompleteCB();
  }
  else {
    // apply in progress, save callback, will be called once apply is complete
    if (applyCompleteCB) {
      // already regeistered, chain it
      FOCUSLOG("- waitForApplyComplete() called while apply in progress and another callback already set -> install callback fork");
      applyCompleteCB = boost::bind(&Device::forkDoneCB, this, applyCompleteCB, aApplyCompleteCB);
    }
    else {
      FOCUSLOG("- waitForApplyComplete() called while apply in progress and no callback already set -> install callback");
      applyCompleteCB = aApplyCompleteCB;
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
  FOCUSLOG("##### Serializer watchdog ticket #%ld expired", serializerWatchdogTicket);
  serializerWatchdogTicket = 0;
  if (applyInProgress) {
    ALOG(LOG_WARNING, "##### Serializer watchdog force-ends apply with %d missed attempts", missedApplyAttempts);
    missedApplyAttempts = 0;
    applyingChannelsComplete();
    FOCUSLOG("##### Force-ending apply complete");
  }
  if (updateInProgress) {
    ALOG(LOG_WARNING, "##### Serializer watchdog force-ends update");
    updatingChannelsComplete();
    FOCUSLOG("##### Force-ending complete");
  }
  #endif
}


bool Device::checkForReapply()
{
  ALOG(LOG_DEBUG, "checkForReapply - missed %d apply attempts in between", missedApplyAttempts);
  if (missedApplyAttempts>0) {
    // request applying again to make sure final values are applied
    // - re-use callback of most recent requestApplyingChannels(), will be called once this attempt has completed (or superseded again)
    FOCUSLOG("- checkForReapply now requesting final channel apply");
    missedApplyAttempts = 0; // clear missed
    applyInProgress = false; // must be cleared for requestApplyingChannels() to actually do something
    requestApplyingChannels(appliedOrSupersededCB, false); // final apply after missing other apply commands may not optimize for dimming
    // - done for now
    return true; // reapply needed and started
  }
  return false; // no repply pending
}



// hardware has completed applying values
void Device::applyingChannelsComplete()
{
  AFOCUSLOG("applyingChannelsComplete entered");
  #if SERIALIZER_WATCHDOG
  if (serializerWatchdogTicket) {
    FOCUSLOG("----- Serializer watchdog ticket #%ld cancelled - apply complete", serializerWatchdogTicket);
    MainLoop::currentMainLoop().cancelExecutionTicket(serializerWatchdogTicket); // cancel watchdog
  }
  #endif
  applyInProgress = false;
  // if more apply request have happened in the meantime, we need to reapply now
  if (!checkForReapply()) {
    // apply complete and no final re-apply pending
    // - confirm because finally applied
    FOCUSLOG("- applyingChannelsComplete - really completed, now checking callbacks");
    SimpleCB cb;
    if (appliedOrSupersededCB) {
      FOCUSLOG("- confirming apply (really) finalized");
      cb = appliedOrSupersededCB;
      appliedOrSupersededCB = NULL; // ready for possibly taking new callback in case current callback should request another change
      cb(); // call back now, values have been superseded
    }
    // check for independent operation waiting for apply complete
    if (applyCompleteCB) {
      FOCUSLOG("- confirming apply (really) finalized to waitForApplyComplete() client");
      cb = applyCompleteCB;
      applyCompleteCB = NULL;
      cb();
    }
    FOCUSLOG("- confirmed apply (really) finalized");
  }
}



/// request that channel values are updated by reading them back from the device's hardware
/// @param aUpdatedOrCachedCB will be called when values are updated with actual hardware values
///   or pending values are in process to be applied to the hardware and thus these cached values can be considered current.
/// @note this method is only called at startup and before saving scenes to make sure changes done to the outputs directly (e.g. using
///   a direct remote control for a lamp) are included. Just reading a channel state does not call this method.
void Device::requestUpdatingChannels(SimpleCB aUpdatedOrCachedCB)
{
  AFOCUSLOG("requestUpdatingChannels entered");
  // Caller wants current values from hardware
  // Three possible cases:
  // a) hardware is busy updating values already -> serve previous callback (with stale values) and install new callback
  // b) hardware is busy applying new values -> consider cache most recent
  // c) hardware is not busy -> start reading values
  if (updateInProgress) {
    // case a) serialize updates: terminate previous callback with stale values and install new one
    if (updatedOrCachedCB) {
      FOCUSLOG("- confirming channels updated for PREVIOUS request with stale values (as asked again)");
      SimpleCB cb = updatedOrCachedCB;
      updatedOrCachedCB = aUpdatedOrCachedCB; // install new
      cb(); // execute old
      FOCUSLOG("- confirmed channels updated for PREVIOUS request with stale values (as asked again)");
    }
    else {
      updatedOrCachedCB = aUpdatedOrCachedCB; // install new
    }
    // done, actual results will serve most recent request for values
  }
  else if (applyInProgress) {
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
    AFOCUSLOG("requestUpdatingChannels: hardware ready, calling syncChannelValues()");
    updatedOrCachedCB = aUpdatedOrCachedCB; // install new callback
    updateInProgress = true;
    #if SERIALIZER_WATCHDOG
    // - start watchdog
    MainLoop::currentMainLoop().executeTicketOnce(serializerWatchdogTicket, boost::bind(&Device::serializerWatchdog, this), SERIALIZER_WATCHDOG_TIMEOUT);
    FOCUSLOG("+++++ Serializer watchdog started for update with ticket #%ld", serializerWatchdogTicket);
    #endif
    // - trigger querying hardware
    syncChannelValues(boost::bind(&Device::updatingChannelsComplete, this));
  }
}


void Device::updatingChannelsComplete()
{
  #if SERIALIZER_WATCHDOG
  if (serializerWatchdogTicket) {
    FOCUSLOG("----- Serializer watchdog ticket #%ld cancelled - update complete", serializerWatchdogTicket);
    MainLoop::currentMainLoop().cancelExecutionTicket(serializerWatchdogTicket); // cancel watchdog
  }
  #endif
  if (updateInProgress) {
    AFOCUSLOG("endUpdatingChannels (while actually waiting for these updates!)");
    updateInProgress = false;
    if (updatedOrCachedCB) {
      FOCUSLOG("- confirming channels updated from hardware (= calling callback now)");
      SimpleCB cb = updatedOrCachedCB;
      updatedOrCachedCB = NULL; // ready for possibly taking new callback in case current callback should request another change
      cb(); // call back now, cached values are either updated from hardware or superseded by pending updates TO hardware
      FOCUSLOG("- confirmed channels updated from hardware (= callback has possibly launched apply already and returned now)");
    }
  }
  else {
    AFOCUSLOG("UNEXPECTED endUpdatingChannels -> discarded");
  }
  // if we have got apply requests in the meantime, we need to do a reapply now
  checkForReapply();
}



// MARK: ===== scene operations

void Device::callScene(SceneNo aSceneNo, bool aForce)
{
  // see if we have a scene table at all
  SceneDeviceSettingsPtr scenes = getScenes();
  if (scenes) {
    DsScenePtr scene = scenes->getScene(aSceneNo);
    SceneCmd cmd = scene->sceneCmd;
    SceneArea area = scene->sceneArea;
    // check special scene commands first
    if (cmd==scene_cmd_area_continue) {
      // area dimming continuation
      if (dimTimeoutTicket) {
        // timer still running (continue received in time) -> reschedule dimmer timeout to keep dimming
        MainLoop::currentMainLoop().rescheduleExecutionTicket(dimTimeoutTicket, LEGACY_DIM_STEP_TIMEOUT);
      }
      else if (areaDimmed!=0 && areaDimMode!=dimmode_stop) {
        // continue received too late, already stopped -> restart dimming
        ALOG(LOG_DEBUG, "Area scene dimming continue received too late -> restarting dimming, will not be smooth");
        dimChannelForArea(getChannelByIndex(0), areaDimMode, areaDimmed, LEGACY_DIM_STEP_TIMEOUT);
      }
      // - otherwise: NOP
      return;
    }
    // first check legacy (inc/dec scene) dimming
    if (cmd==scene_cmd_increment) {
      if (!prepareSceneCall(scene)) return;
      dimChannelForArea(getChannelByIndex(0), dimmode_up, area, LEGACY_DIM_STEP_TIMEOUT);
      return;
    }
    else if (cmd==scene_cmd_decrement) {
      if (!prepareSceneCall(scene)) return;
      dimChannelForArea(getChannelByIndex(0), dimmode_down, area, LEGACY_DIM_STEP_TIMEOUT);
      return;
    }
    else if (cmd==scene_cmd_stop) {
      if (!prepareSceneCall(scene)) return;
      dimChannelForArea(getChannelByIndex(0), dimmode_stop, area, 0);
      return;
    }
    // we get here only if callScene is not legacy dimming
    ALOG(LOG_NOTICE, "CallScene(%d) (non-dimming!):", aSceneNo);
    // make sure dimming stops for any non-dimming scene call
    if (currentDimMode!=dimmode_stop) {
      // any non-dimming scene call stops dimming
      LOG(LOG_NOTICE, "- interrupts dimming in progress");
      dimChannelForArea(currentDimChannel, dimmode_stop, area, 0);
    }
    // filter area scene calls via area main scene's (area x on, Tx_S1) dontCare flag
    if (area) {
      LOG(LOG_INFO, "- callScene(%d): is area #%d scene", aSceneNo, area);
      // check if device is in area (criteria used is dontCare flag OF THE AREA ON SCENE (other don't care flags are irrelevant!)
      DsScenePtr areamainscene = scenes->getScene(mainSceneForArea(area));
      if (areamainscene->isDontCare()) {
        LOG(LOG_INFO, "- area main scene(%d) is dontCare -> suppress", areamainscene->sceneNo);
        return; // not in this area, suppress callScene entirely
      }
      // call applies, if it is a off scene, it resets localPriority
      if (scene->sceneCmd==scene_cmd_off) {
        // area is switched off -> end local priority
        LOG(LOG_INFO, "- is area off scene -> ends localPriority now");
        output->setLocalPriority(false);
      }
    }
    if (!scene->isDontCare()) {
      // Scene found and dontCare not set, check details
      // - check and update local priority
      if (!area && output->hasLocalPriority()) {
        // non-area scene call, but device is in local priority
        if (!aForce && !scene->ignoresLocalPriority()) {
          // not forced nor localpriority ignored, localpriority prevents applying non-area scene
          LOG(LOG_DEBUG, "- Non-area scene, localPriority set, scene does not ignore local prio and not forced -> suppressed");
          return; // suppress scene call entirely
        }
        else {
          // forced or scene ignores local priority, scene is applied anyway, and also clears localPriority
          output->setLocalPriority(false);
        }
      }
      // - make sure we have the lastState pseudo-scene for undo
      if (!previousState) {
        previousState = scenes->newUndoStateScene();
      }
      // we remember the scene for which these are undo values in sceneNo of the pseudo scene
      // (but without actually re-configuring the scene according to that number!)
      previousState->sceneNo = aSceneNo;
      // - now capture current values and then apply to output
      if (output) {
        // Non-dimming scene: have output save its current state into the previousState pseudo scene
        // Note: the actual updating might happen later (when the hardware responds) but
        //   implementations must make sure access to the hardware is serialized such that
        //   the values are captured before values from applyScene() below are applied.
        output->captureScene(previousState, true, boost::bind(&Device::outputUndoStateSaved,this,output,scene)); // apply only after capture is complete
      } // if output
    } // not dontCare
    else {
      // Scene is dontCare
      // - possibly still do other scene actions now, although scene was not applied
      performSceneActions(scene, boost::bind(&Device::sceneActionsComplete, this, scene));
    }
  } // device with scenes
}


void Device::performSceneActions(DsScenePtr aScene, SimpleCB aDoneCB)
{
  if (output) {
    output->performSceneActions(aScene, aDoneCB);
  }
  else {
    if (aDoneCB) aDoneCB(); // nothing to do
  }
}


void Device::stopSceneActions()
{
  if (output) {
    output->stopSceneActions();
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




// deferred applying of state, after current state has been captured for this output
void Device::outputUndoStateSaved(DsBehaviourPtr aOutput, DsScenePtr aScene)
{
  if (prepareSceneCall(aScene)) {
    OutputBehaviourPtr output = boost::dynamic_pointer_cast<OutputBehaviour>(aOutput);
    if (output) {
      // apply scene logically
      if (output->performApplyScene(aScene)) {
        // prepare for apply
        if (prepareSceneApply(aScene)) {
          // now apply values to hardware
          requestApplyingChannels(boost::bind(&Device::sceneValuesApplied, this, aScene), false);
        }
      }
      else {
        // no apply to hardware needed, directly proceed to actions
        sceneValuesApplied(aScene);
      }
    }
  }
  else {
     ALOG(LOG_DEBUG, "Device level prepareSceneCall() returns false -> no more actions");
  }
}


void Device::sceneValuesApplied(DsScenePtr aScene)
{
  // now perform scene special actions such as blinking
  performSceneActions(aScene, boost::bind(&Device::sceneActionsComplete, this, aScene));
}


void Device::sceneActionsComplete(DsScenePtr aScene)
{
  // scene actions are now complete
  ALOG(LOG_INFO, "Scene actions for callScene(%d) complete -> now in final state", aScene->sceneNo);
}




void Device::undoScene(SceneNo aSceneNo)
{
  ALOG(LOG_NOTICE, "UndoScene(%d):", aSceneNo);
  if (previousState && previousState->sceneNo==aSceneNo) {
    // there is an undo pseudo scene we can apply
    // scene found, now apply it to the output (if any)
    if (output) {
      // now apply the pseudo state
      output->performApplyScene(previousState);
      // apply the values now, not dimming
      if (prepareSceneApply(previousState)) {
        requestApplyingChannels(NULL, false);
      }
    }
  }
}


void Device::setLocalPriority(SceneNo aSceneNo)
{
  SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(deviceSettings);
  if (scenes) {
    ALOG(LOG_NOTICE, "SetLocalPriority(%d):", aSceneNo);
    // we have a device-wide scene table, get the scene object
    DsScenePtr scene = scenes->getScene(aSceneNo);
    if (scene && !scene->isDontCare()) {
      LOG(LOG_DEBUG, "- setLocalPriority(%d): localPriority set", aSceneNo);
      output->setLocalPriority(true);
    }
  }
}


void Device::callSceneMin(SceneNo aSceneNo)
{
  SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(deviceSettings);
  if (scenes) {
    ALOG(LOG_NOTICE, "CallSceneMin(%d):", aSceneNo);
    // we have a device-wide scene table, get the scene object
    DsScenePtr scene = scenes->getScene(aSceneNo);
    if (scene && !scene->isDontCare()) {
      if (output) {
        output->onAtMinBrightness(scene);
        // apply the values now, not dimming
        if (prepareSceneApply(scene)) {
          requestApplyingChannels(NULL, false);
        }
      }
    }
  }
}




void Device::identifyToUser()
{
  if (output) {
    output->identifyToUser(); // pass on to behaviour by default
  }
  else {
    LOG(LOG_INFO, "***** device 'identify' called (for device with no real identify implementation) *****");
  }
}



void Device::saveScene(SceneNo aSceneNo)
{
  // see if we have a scene table at all
  ALOG(LOG_NOTICE, "SaveScene(%d)", aSceneNo);
  SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(deviceSettings);
  if (scenes) {
    // we have a device-wide scene table, get the scene object
    DsScenePtr scene = scenes->getScene(aSceneNo);
    if (scene) {
      // scene found, now capture to all of our outputs
      if (output) {
        // capture value from this output, reading from device (if possible) to catch e.g. color changes applied via external means (hue remote app etc.)
        output->captureScene(scene, true, boost::bind(&Device::outputSceneValueSaved, this, scene));
      }
    }
  }
}


void Device::outputSceneValueSaved(DsScenePtr aScene)
{
  // Check special area scene case: dontCare need to be updated depending on brightness (if zero, set don't care)
  SceneNo sceneNo = aScene->sceneNo;
  int area = aScene->sceneArea;
  if (area) {
    // detail check - set don't care when saving Area On-Scene
    if (sceneNo==mainSceneForArea(area)) {
      // saving Main ON scene - set dontCare flag when main/default channel is zero, otherwise clear dontCare
      ChannelBehaviourPtr ch = output->getChannelByType(channeltype_default);
      if (ch) {
        bool mustBeDontCare = ch->getChannelValue()==0;
        // update this main scene's dontCare
        aScene->setDontCare(mustBeDontCare);
        // also update the off scene's dontCare
        SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(deviceSettings);
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
  SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(deviceSettings);
  if (scenes && aScene->isDirty()) {
    scenes->updateScene(aScene);
  }
}



bool Device::processControlValue(const string &aName, double aValue)
{
  // default base class behaviour is letting know the output behaviour
  if (output) {
    return output->processControlValue(aName, aValue);
  }
  return false; // nothing to process
}



// MARK: ===== persistent device params


// load device settings - beaviours + scenes
ErrorPtr Device::load()
{
  ErrorPtr err;
  // if we don't have device settings at this point (created by subclass), this is a misconfigured device!
  if (!deviceSettings) {
    ALOG(LOG_ERR, "***** no settings at load() time! -> probably misconfigured");
    return WebError::webErr(500,"missing settings");
  }
  // load the device settings
  if (deviceSettings) {
    err = deviceSettings->loadFromStore(dSUID.getString().c_str());
    if (!Error::isOK(err)) ALOG(LOG_ERR,"Error loading settings: %s", err->description().c_str());
  }
  // load the behaviours
  for (BehaviourVector::iterator pos = buttons.begin(); pos!=buttons.end(); ++pos) (*pos)->load();
  for (BehaviourVector::iterator pos = binaryInputs.begin(); pos!=binaryInputs.end(); ++pos) (*pos)->load();
  for (BehaviourVector::iterator pos = sensors.begin(); pos!=sensors.end(); ++pos) (*pos)->load();
  if (output) output->load();
  // load settings from files
  loadSettingsFromFiles();
  return ErrorPtr();
}


ErrorPtr Device::save()
{
  ErrorPtr err;
  // save the device settings
  if (deviceSettings) err = deviceSettings->saveToStore(dSUID.getString().c_str(), false); // only one record per device
  if (!Error::isOK(err)) ALOG(LOG_ERR,"Error saving settings: %s", err->description().c_str());
  // save the behaviours
  for (BehaviourVector::iterator pos = buttons.begin(); pos!=buttons.end(); ++pos) (*pos)->save();
  for (BehaviourVector::iterator pos = binaryInputs.begin(); pos!=binaryInputs.end(); ++pos) (*pos)->save();
  for (BehaviourVector::iterator pos = sensors.begin(); pos!=sensors.end(); ++pos) (*pos)->save();
  if (output) output->save();
  return ErrorPtr();
}


bool Device::isDirty()
{
  // check the device settings
  if (deviceSettings && deviceSettings->isDirty()) return true;
  for (BehaviourVector::iterator pos = buttons.begin(); pos!=buttons.end(); ++pos) if ((*pos)->isDirty()) return true;
  for (BehaviourVector::iterator pos = binaryInputs.begin(); pos!=binaryInputs.end(); ++pos) if ((*pos)->isDirty()) return true;
  for (BehaviourVector::iterator pos = sensors.begin(); pos!=sensors.end(); ++pos) if ((*pos)->isDirty()) return true;
  if (output && output->isDirty()) return true;
  return false;
}


void Device::markClean()
{
  // check the device settings
  if (deviceSettings) deviceSettings->markClean();
  for (BehaviourVector::iterator pos = buttons.begin(); pos!=buttons.end(); ++pos) (*pos)->markClean();
  for (BehaviourVector::iterator pos = binaryInputs.begin(); pos!=binaryInputs.end(); ++pos) (*pos)->markClean();
  for (BehaviourVector::iterator pos = sensors.begin(); pos!=sensors.end(); ++pos) (*pos)->markClean();
  if (output) output->save();
}


ErrorPtr Device::forget()
{
  // delete the device settings
  if (deviceSettings) deviceSettings->deleteFromStore();
  // delete the behaviours
  for (BehaviourVector::iterator pos = buttons.begin(); pos!=buttons.end(); ++pos) (*pos)->forget();
  for (BehaviourVector::iterator pos = binaryInputs.begin(); pos!=binaryInputs.end(); ++pos) (*pos)->forget();
  for (BehaviourVector::iterator pos = sensors.begin(); pos!=sensors.end(); ++pos) (*pos)->forget();
  if (output) output->forget();
  return ErrorPtr();
}


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
  levelids[3] = vdcP->vdcClassIdentifier();
  for(int i=0; i<numLevels; ++i) {
    // try to open config file
    string fn = dir+"devicesettings_"+levelids[i]+".csv";
    // if device has already stored properties, only explicitly marked properties will be applied
    if (loadSettingsFromFile(fn.c_str(), deviceSettings->rowid!=0)) markClean();
  }
}



// MARK: ===== property access

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
  // device class
  deviceClass_key,
  deviceClassVersion_key,
  numDeviceFieldKeys
};


const int numBehaviourArrays = 4; // buttons, binaryInputs, Sensors, Channels
const int numDeviceProperties = numDeviceFieldKeys+3*numBehaviourArrays;



static char device_key;
static char device_output_key;

static char device_buttons_key;
static char device_inputs_key;
static char device_sensors_key;
static char device_channels_key;

static char device_scenes_key;

static char device_modelFeatures_key;


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
    return (int)buttons.size();
  }
  else if (aParentDescriptor->hasObjectKey(device_inputs_key)) {
    return (int)binaryInputs.size();
  }
  else if (aParentDescriptor->hasObjectKey(device_sensors_key)) {
    return (int)sensors.size();
  }
  else if (aParentDescriptor->hasObjectKey(device_channels_key)) {
    return numChannels(); // if no output, this returns 0
  }
  else if (aParentDescriptor->hasObjectKey(device_scenes_key)) {
    SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(deviceSettings);
    if (scenes)
      return MAX_SCENE_NO;
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
    { "primaryGroup", apivalue_uint64, colorClass_key, OKEY(device_key) },
    { "zoneID", apivalue_uint64, zoneID_key, OKEY(device_key) },
    { "progMode", apivalue_bool, progMode_key, OKEY(device_key) },
    { "implementationId", apivalue_string, implementationId_key, OKEY(device_key) },
    { "x-p44-softwareRemovable", apivalue_bool, softwareRemovable_key, OKEY(device_key) },
    { "x-p44-teachInSignals", apivalue_int64, teachinSignals_key, OKEY(device_key) },
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
    { "undoState", apivalue_object, undoState_key, OKEY(device_key) },
    // the modelFeatures (row from former dSS visibility matrix, controlling what is shown in the UI)
    { "modelFeatures", apivalue_object+propflag_container, modelFeatures_key, OKEY(device_modelFeatures_key) },
    // device class
    { "deviceClass", apivalue_string, deviceClass_key, OKEY(device_key) },
    { "deviceClassVersion", apivalue_uint64, deviceClassVersion_key, OKEY(device_key) }
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
  #if ACCESS_BY_ID
  else if (aParentDescriptor->isArrayContainer()) {
    // accessing one of the other containers: channels, buttons/inputs/sensors or scenes
    string id;
    if (aParentDescriptor->hasObjectKey(device_buttons_key)) {
      id = buttons[aPropIndex]->getApiId(aParentDescriptor->getApiVersion());
    }
    else if (aParentDescriptor->hasObjectKey(device_inputs_key)) {
      id = binaryInputs[aPropIndex]->getApiId(aParentDescriptor->getApiVersion());
    }
    else if (aParentDescriptor->hasObjectKey(device_sensors_key)) {
      id = sensors[aPropIndex]->getApiId(aParentDescriptor->getApiVersion());
    }
    else if (aParentDescriptor->hasObjectKey(device_channels_key)) {
      id = getChannelByIndex(aPropIndex)->getApiId(aParentDescriptor->getApiVersion());
    }
    else if (aParentDescriptor->hasObjectKey(device_scenes_key)) {
      // scenes are still named by their index
      id = string_format("%d", aPropIndex);
    }
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = id;
    descP->propertyType = apivalue_object;
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = aParentDescriptor->objectKey();
    return descP;
  }
  #endif
  return PropertyDescriptorPtr();
}



PropertyDescriptorPtr Device::getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor)
{
  #if ACCESS_BY_ID
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
    output &&
    output->numChannels()>0
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
  #else
  if (
    aParentDescriptor && aParentDescriptor->isArrayContainer() &&
    !aParentDescriptor->hasObjectKey(device_modelFeatures_key) // these are handled by base class via getDescriptorByIndex
  ) {
    // array-like container: channels, buttons/inputs/sensors or scenes
    PropertyDescriptorPtr propDesc;
    bool numericName = getNextPropIndex(aPropMatch, aStartIndex);
    if (numericName && aParentDescriptor->hasObjectKey(device_channels_key)) {
      // specific channel addressed by type, look up index for it
      DsChannelType ct = (DsChannelType)aStartIndex;
      aStartIndex = PROPINDEX_NONE; // default: not found
      // there is an output
      ChannelBehaviourPtr cb = getChannelByType(ct);
      if (cb) {
        aStartIndex = (int)cb->getChannelIndex();
      }
    }
    int n = numProps(aDomain, aParentDescriptor);
    if (aStartIndex!=PROPINDEX_NONE && aStartIndex<n) {
      // within range, create descriptor
      DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
      if (aParentDescriptor->hasObjectKey(device_channels_key)) {
        // is a channel
        if (numericName) {
          // query specified a channel number -> return same number in result (to return "0" when default channel "0" was explicitly queried)
          descP->propertyName = aPropMatch; // query = name of object
        }
        else {
          // wildcard, result object is named after channelType
          ChannelBehaviourPtr cb = getChannelByIndex(aStartIndex);
          if (cb) {
            descP->propertyName = string_format("%d", cb->getChannelType());
          }
        }
      }
      else {
        // is a scene/button/input/sensor, name by index
        descP->propertyName = string_format("%d", aStartIndex);
      }
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
  #endif
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
    return buttons[aPropertyDescriptor->fieldKey()];
  }
  else if (aPropertyDescriptor->hasObjectKey(device_inputs_key)) {
    return binaryInputs[aPropertyDescriptor->fieldKey()];
  }
  else if (aPropertyDescriptor->hasObjectKey(device_sensors_key)) {
    return sensors[aPropertyDescriptor->fieldKey()];
  }
  else if (aPropertyDescriptor->hasObjectKey(device_channels_key)) {
    if (!output) return PropertyContainerPtr(); // none
    return output->getChannelByIndex((int)aPropertyDescriptor->fieldKey());
  }
  else if (aPropertyDescriptor->hasObjectKey(device_scenes_key)) {
    SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(deviceSettings);
    if (scenes) {
      return scenes->getScene(aPropertyDescriptor->fieldKey());
    }
  }
  else if (aPropertyDescriptor->hasObjectKey(device_output_key)) {
    if (output && output->numDescProps()>0) {
      return output;
    }
    return NULL; // no output or special output with no standard properties (e.g. actionOutput)
  }
  else if (aPropertyDescriptor->hasObjectKey(device_key)) {
    // device level object properties
    if (aPropertyDescriptor->fieldKey()==undoState_key) {
      return previousState;
    }
  }
  // unknown here
  return NULL;
}



bool Device::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(device_key)) {
    // Device level field properties
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case colorClass_key:
          aPropValue->setUint16Value(colorClass); return true;
        case zoneID_key:
          if (deviceSettings) { aPropValue->setUint16Value(deviceSettings->zoneID); } return true;
        case progMode_key:
          aPropValue->setBoolValue(progMode); return true;
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
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        case zoneID_key:
          if (deviceSettings) {
            deviceSettings->setPVar(deviceSettings->zoneID, aPropValue->int32Value());
          }
          return true;
        case progMode_key:
          progMode = aPropValue->boolValue();
          return true;
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
    SceneDeviceSettingsPtr scenes = boost::dynamic_pointer_cast<SceneDeviceSettings>(deviceSettings);
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
    requestApplyingChannels(NULL, false);
  }
  return inherited::writtenProperty(aMode, aPropertyDescriptor, aDomain, aContainer);
}


// MARK: ===== Device description/shortDesc/status


string Device::description()
{
  string s = inherited::description(); // DsAdressable
  if (buttons.size()>0) string_format_append(s, "\n- Buttons: %lu", buttons.size());
  if (binaryInputs.size()>0) string_format_append(s, "\n- Binary Inputs: %lu", binaryInputs.size());
  if (sensors.size()>0) string_format_append(s, "\n- Sensors: %lu", sensors.size());
  if (numChannels()>0) string_format_append(s, "\n- Output Channels: %d", numChannels());
  return s;
}


string Device::getStatusText()
{
  string s;
  if (output) {
    s = output->getStatusText();
  }
  if (s.empty() && sensors.size()>0) {
    s = sensors[0]->getStatusText();
  }
  if (s.empty() && binaryInputs.size()>0) {
    s = binaryInputs[0]->getStatusText();
  }
  return s;
}
