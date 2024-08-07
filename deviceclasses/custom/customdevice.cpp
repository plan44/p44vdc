//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2015-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "customdevice.hpp"

#if ENABLE_EXTERNAL || ENABLE_SCRIPTED

#include "shadowbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "colorlightbehaviour.hpp"
#include "climatecontrolbehaviour.hpp"

#if ENABLE_CUSTOM_EXOTIC
  #include "movinglightbehaviour.hpp"
  #include "audiobehaviour.hpp"
  #include "videobehaviour.hpp"
#endif

#if ENABLE_FCU_SUPPORT
  #include "ventilationbehaviour.hpp"
#endif


#if ENABLE_CUSTOM_SINGLEDEVICE
  #include "jsonvdcapi.hpp"
#endif

using namespace p44;


#if ENABLE_CUSTOM_SINGLEDEVICE

// MARK: - CustomDeviceAction

CustomDeviceAction::CustomDeviceAction(SingleDevice &aSingleDevice, const string aName, const string aDescription, const string aTitle, const string aCategory) :
  inherited(aSingleDevice, aName, aDescription, aTitle, aCategory),
  mCallback(NoOP)
{
}


CustomDeviceAction::~CustomDeviceAction()
{
  // execute callback if still pending
  if (mCallback) mCallback(WebError::webErr(410, "device gone"));
}


CustomDevice &CustomDeviceAction::getCustomDevice()
{
  return *(static_cast<CustomDevice *>(singleDeviceP));
}


void CustomDeviceAction::performCall(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  if (!getCustomDevice().mNoConfirmAction) {
    // remember callback
    mCallback = aCompletedCB;
  }
  // create JSON response
  JsonObjectPtr message = JsonObject::newObj();
  message->add("message", JsonObject::newString("invokeAction"));
  message->add("action", JsonObject::newString(actionId));
  // convert params
  JsonObjectPtr params = JsonApiValue::getAsJson(aParams);
  if (params) {
    message->add("params", params);
  }
  // send it
  getCustomDevice().sendDeviceApiJsonMessage(message);
  if (getCustomDevice().mNoConfirmAction) {
    // immediately confirm
    if (aCompletedCB) aCompletedCB(ErrorPtr());
  }
}


void CustomDeviceAction::callPerformed(JsonObjectPtr aStatusInfo)
{
  ErrorPtr err;
  if (aStatusInfo) {
    JsonObjectPtr o;
    ErrorCode ec = Error::OK;
    if (aStatusInfo->get("errorcode", o)) {
      ec = o->int32Value();
    }
    if (ec!=Error::OK) {
      string et;
      if (aStatusInfo->get("errortext", o)) {
        et = o->stringValue();
      }
      err = WebError::webErr(ec,"%s: %s", actionId.c_str(), et.c_str());
    }
  }
  if (mCallback) {
    mCallback(err); // will return status to caller of action
    mCallback = NoOP;
  }
}



#endif // ENABLE_CUSTOM_SINGLEDEVICE


// MARK: - CustomDevice


CustomDevice::CustomDevice(Vdc *aVdcP, bool aSimpleText) :
  #if ENABLE_CUSTOM_SINGLEDEVICE
  inherited(aVdcP, false), // do not enable single device mechanisms by default
  mNoConfirmAction(false), // expect action execution to be confirmed
  #else
  inherited(aVdcP),
  #endif
  mSimpletext(aSimpleText),
  mTypeIdentifier("custom"),
  mUseMovement(false), // no movement by default
  mQuerySync(false), // no sync query by default
  mSceneCommands(false), // no scene commands by default
  mSceneCalls(false), // no scene calls forwarded by default
  mForwardIdentify(false), // no identification forward by default
  mControlValues(false), // no control values by default
  mConfigured(false),
  mIconBaseName("cust"), // default icon name
  mModelNameString("custom device"),
  mVendorNameString("plan44.ch"),
  mDevClassVersion(0),
  mOpStateLevel(-1) // unknown operating state by default
  #if ENABLE_CUSTOM_EXOTIC
  ,mExtraModelFeatures(0)
  ,mMutedModelFeatures(0)
  #endif
{
}


CustomDevice::~CustomDevice()
{
}


bool CustomDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}


string CustomDevice::modelName()
{
  return mModelNameString;
}


string CustomDevice::modelVersion() const
{
  if (!mModelVersionString.empty()) {
    return mModelVersionString;
  }
  return inherited::modelVersion();
}


string CustomDevice::vendorName()
{
  return mVendorNameString;
}


string CustomDevice::oemModelGUID()
{
  return mOemModelGUIDString;
}


string CustomDevice::webuiURLString()
{
  if (!mConfigUrl.empty())
    return mConfigUrl;
  else
    return inherited::webuiURLString();
}




void CustomDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // perform normal disconnect
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}



bool CustomDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getClassColoredIcon(mIconBaseName.c_str(), getDominantColorClass(), aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


bool CustomDevice::canIdentifyToUser()
{
  return mForwardIdentify || inherited::canIdentifyToUser();
}


void CustomDevice::identifyToUser(MLMicroSeconds aDuration)
{
  if (mForwardIdentify) {
    sendDeviceApiFlagMessage("IDENTIFY");
  }
  else {
    inherited::identifyToUser(aDuration);
  }
}


#if ENABLE_CUSTOM_EXOTIC

static_assert(numModelFeatures<=64, "Too many modelfeatures, don't fit into 64bit mask");

Tristate CustomDevice::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // explicitly set or muted features override automatically derived ones in all cases
  if (mExtraModelFeatures & (1ll<<aFeatureIndex)) {
    return yes;
  }
  if (mMutedModelFeatures & (1ll<<aFeatureIndex)) {
    return no;
  }
  return inherited::hasModelFeature(aFeatureIndex);
}


#endif // ENABLE_CUSTOM_EXOTIC


void CustomDevice::sendDeviceApiFlagMessage(string aFlagWord)
{
  if (mSimpletext) {
    sendDeviceApiSimpleMessage(aFlagWord);
  }
  else {
    JsonObjectPtr message = JsonObject::newObj();
    message->add("message", JsonObject::newString(lowerCase(aFlagWord)));
    sendDeviceApiJsonMessage(message);
  }
}


void CustomDevice::deviceInitiatedSyncComplete()
{
  mSyncedCB = NoOP;
  // when device initiates multiple channel sync, report output when done
  getOutput()->reportOutputState();
}


ErrorPtr CustomDevice::processJsonMessage(string aMessageType, JsonObjectPtr aMessage)
{
  ErrorPtr err;
  if (aMessageType=="bye") {
    mConfigured = false; // cause device to get removed
    err = Error::ok(); // explicit ok
  }
  else {
    if (mConfigured) {
      if (aMessageType=="synced") {
        // device confirms having reported all channel states (in response to "sync" command)
        if (mSyncedCB) mSyncedCB();
        mSyncedCB = NoOP;
        return ErrorPtr(); // no answer
      }
      else if (aMessageType=="sync") {
        // device informs it intends to sync multiple channel states. NOP if already in vdcd-initiated sync state
        if (!mSyncedCB) mSyncedCB = boost::bind(&CustomDevice::deviceInitiatedSyncComplete, this);
        return ErrorPtr(); // no answer
      }
      else if (aMessageType=="active") {
        JsonObjectPtr o;
        if (aMessage->get("value", o)) {
          updatePresenceState(o->boolValue());
        }
        return ErrorPtr(); // no answer
      }
      else if (aMessageType=="opstate") {
        JsonObjectPtr o;
        if (aMessage->get("level", o)) {
          mOpStateLevel = o->int32Value();
        }
        if (aMessage->get("text", o)) {
          mOpStateText = o->stringValue();
        }
        return ErrorPtr(); // no answer
      }
      else if (aMessageType=="button") {
        err = processInputJson('B', aMessage);
      }
      else if (aMessageType=="input") {
        err = processInputJson('I', aMessage);
      }
      else if (aMessageType=="sensor") {
        err = processInputJson('S', aMessage);
      }
      else if (aMessageType=="channel") {
        err = processInputJson('C', aMessage);
      }
      else if (aMessageType=="channel_progress") {
        err = processInputJson('P', aMessage);
      }
      else if (aMessageType=="channel_config") {
        err = processInputJson('c', aMessage);
      }
      #if ENABLE_CUSTOM_SINGLEDEVICE
      else if (aMessageType=="confirmAction") {
        JsonObjectPtr o;
        if (aMessage->get("action", o)) {
          CustomDeviceActionPtr a = boost::dynamic_pointer_cast<CustomDeviceAction>(dynamicDeviceActions->getAction(o->stringValue()));
          if (!a) a = boost::dynamic_pointer_cast<CustomDeviceAction>(deviceActions->getAction(o->stringValue()));
          if (a) a->callPerformed(aMessage);
        }
        else {
          err = TextError::err("confirmAction must identify 'action'");
        }
      }
      else if (aMessageType=="updateProperty") {
        JsonObjectPtr o;
        if (aMessage->get("property", o)) {
          ValueDescriptorPtr prop = deviceProperties->getProperty(o->stringValue());
          if (prop) {
            if (aMessage->get("value", o, false)) {
              ApiValuePtr v = ApiValuePtr(JsonApiValue::newValueFromJson(o));
              ErrorPtr err = prop->conforms(v, true); // check and make internal
              if (Error::notOK(err)) return err;
              prop->setValue(v);
            }
            if (aMessage->get("push", o)) {
              if (o->boolValue()) {
                deviceProperties->pushProperty(prop);
              }
            }
          }
        }
      }
      else if (aMessageType=="pushNotification") {
        JsonObjectPtr o;
        // collect list of events
        DeviceEventsList evs;
        if (aMessage->get("events", o)) {
          for (int i=0; i<o->arrayLength(); i++) {
            string evname = o->arrayGet(i)->stringValue();
            DeviceEventPtr ev = deviceEvents->getEvent(evname);
            if (ev) {
              evs.push_back(ev);
            }
            else {
              return TextError::err("unknown event '%s'", evname.c_str());
            }
          }
        }
        // check for state change to be pushed
        if (aMessage->get("statechange", o)) {
          string key;
          JsonObjectPtr val;
          o->resetKeyIteration();
          if (o->nextKeyValue(key, val)) {
            DeviceStatePtr s = deviceStates->getState(key);
            if (s) {
              // set new value for state
              ApiValuePtr v = ApiValuePtr(JsonApiValue::newValueFromJson(val));
              ErrorPtr err = s->value()->conforms(v, true); // check and make internal
              if (Error::notOK(err)) return err;
              s->value()->setValue(v);
              // push state along with events
              s->pushWithEvents(evs);
            }
            else {
              return TextError::err("unknown state '%s'", key.c_str());
            }
          }
          else {
            return TextError::err("need to specify a state name in statechange field");
          }
        }
        else {
          // only push events without a state change
          deviceEvents->pushEvents(evs);
        }
      }
      else if (aMessageType=="dynamicAction") {
        JsonObjectPtr o;
        // dynamic action added/changed/deleted
        if (aMessage->get("changes", o)) {
          string actionId;
          JsonObjectPtr actionConfig;
          o->resetKeyIteration();
          if (o->nextKeyValue(actionId, actionConfig)) {
            err = updateDynamicActionFromJSON(actionId, actionConfig);
          }
        }
      }
      #endif
      else {
        err = TextError::err("Unknown message '%s'", aMessageType.c_str());
      }
    }
    else {
      err = TextError::err("Device must be sent 'init' message first");
    }
  }
  return err;
}


ErrorPtr CustomDevice::processSimpleMessage(string aMessageType, string aValue)
{
  if (aMessageType=="BYE") {
    mConfigured = false; // cause device to get removed
    return Error::ok(); // explicit ok
  }
  else if (aMessageType=="SYNCED") {
    // device confirms having reported all channel states (in response to "SYNC" command)
    if (mSyncedCB) mSyncedCB();
    mSyncedCB = NoOP;
    return ErrorPtr(); // no answer
  }
  else if (aMessageType=="ACTIVE") {
    int active = 0;
    if (sscanf(aValue.c_str(), "%d", &active)==1) {
      updatePresenceState(active);
    }
    return ErrorPtr(); // no answer
  }
  else if (aMessageType.size()>0) {
    // none of the other commands, try inputs
    char iotype = aMessageType[0];
    int index = 0;
    if (sscanf(aMessageType.c_str()+1, "%d", &index)==1) {
      // must be input
      double value = 0;
      sscanf(aValue.c_str(), "%lf", &value);
      return processInput(iotype, index, value, aValue=="undefined");
    }
  }
  return TextError::err("Unknown message '%s'", aMessageType.c_str());
}


static int channelIndexById(OutputBehaviourPtr aOB, const string aId)
{
  ChannelBehaviourPtr cb = aOB->getChannelById(aId);
  return cb ? cb->getChannelIndex() : -1;
}


// MARK: - process input (or log)

ErrorPtr CustomDevice::processInputJson(char aInputType, JsonObjectPtr aParams)
{
  int index = -1;
  JsonObjectPtr o = aParams->get("index");
  if (o) {
    index = o->int32Value();
  }
  else if ((aInputType=='C' || aInputType=='P' || aInputType=='c') && aParams->get("type", o)) {
    // channel specified by type, not index
    DsChannelType ty = (DsChannelType)o->int32Value();
    ChannelBehaviourPtr cb = getChannelByType(ty);
    if (cb) index = (uint32_t)cb->getChannelIndex();
  }
  else if (aParams->get("id", o)) {
    // access by id
    string id = o->stringValue();
    DsBehaviourPtr bhv;
    switch (aInputType) {
      case 'B': bhv = getButton(by_id, id); if (bhv) { index = (int)bhv->getIndex(); } break;
      case 'I': bhv = getInput(by_id, id); if (bhv) { index = (int)bhv->getIndex(); } break;
      case 'S': bhv = getSensor(by_id, id); if (bhv) { index = (int)bhv->getIndex(); } break;
      case 'C':
      case 'c':
      case 'P':
        index = channelIndexById(getOutput(), id); break;
    }
  }
  if (index<0) {
    return TextError::err("missing 'id', 'index' or 'type'");
  }
  if (aInputType=='c') {
    // custom channel config
    CustomChannelPtr cc = boost::dynamic_pointer_cast<CustomChannel>(getChannelByIndex(index));
    if (!cc) return TextError::err("channel is not configurable");
    if (aParams->get("min", o)) cc->setMin(o->doubleValue());
    if (aParams->get("max", o)) cc->setMax(o->doubleValue());
  }
  else if (aParams->get("value", o, false)) {
    // explicit NULL is allowed to set input to "undefined"
    double value = 0;
    if (o) {
      value = o->doubleValue();
    }
    return processInput(aInputType, index, value, !o);
  }
  else {
    return TextError::err("missing 'value'");
  }
  return ErrorPtr();
}


ErrorPtr CustomDevice::processInput(char aInputType, uint32_t aIndex, double aValue, bool aUndefined)
{
  switch (aInputType) {
    case 'B': {
      ButtonBehaviourPtr bb = getButton(aIndex);
      if (bb) {
        if (aUndefined) {
          // FIXME: buttons can get undefined, too
        }
        else if (aValue<0) {
          // direct click reporting
          int click = (int)(-aValue);
          switch (click) {
            case 1 : bb->injectClick(ct_tip_1x); break;
            case 2 : bb->injectClick(ct_tip_2x); break;
            case 3 : bb->injectClick(ct_tip_3x); break;
            case 4 : bb->injectClick(ct_tip_4x); break;
            case 10 : bb->injectClick(ct_hold_end); break;
            case 11 : bb->injectClick(ct_hold_start); break;
          }
        }
        else if (aValue>2) {
          // simulate a keypress of defined length in milliseconds
          bb->updateButtonState(true);
          mButtonReleaseTicket.executeOnce(boost::bind(&CustomDevice::releaseButton, this, bb), aValue*MilliSecond);
        }
        else {
          bb->updateButtonState(aValue!=0);
        }
      }
      else {
        return TextError::err("no button #%d", aIndex);
      }
      break;
    }
    case 'I': {
      BinaryInputBehaviourPtr ib = getInput(aIndex);
      if (ib) {
        if (aUndefined) {
          ib->invalidateInputState();
        }
        else {
          ib->updateInputState(aValue);
        }
      }
      else {
        return TextError::err("no input #%d", aIndex);
      }
      break;
    }
    case 'S': {
      SensorBehaviourPtr sb = getSensor(aIndex);
      if (sb) {
        if (aUndefined) {
          sb->invalidateSensorValue();
        }
        else {
          sb->updateSensorValue(aValue);
        }
      }
      else {
        return TextError::err("no sensor #%d", aIndex);
      }
      break;
    }
    case 'P': {
      // channel transition progress
      ChannelBehaviourPtr cb = getChannelByIndex(aIndex);
      if (cb) {
        cb->reportChannelProgress(aValue);
        getOutput()->reportOutputState();
      }
      else {
        return TextError::err("no channel #%d", aIndex);
      }
      break;
    }
    case 'C': {
      // final channel value
      ChannelBehaviourPtr cb = getChannelByIndex(aIndex);
      if (cb) {
        bool changed = cb->syncChannelValue(aValue, true);
        // check for shadow end contact reporting
        if (aIndex==0) {
          ShadowBehaviourPtr sb = getOutput<ShadowBehaviour>();
          if (sb) {
            if (aValue>=cb->getMax())
              sb->endReached(true); // reached top
            else if (aValue<=cb->getMin())
              sb->endReached(false); // reached bottom
          }
        }
        // check for color mode
        ColorLightBehaviourPtr cl = getOutput<ColorLightBehaviour>();
        if (cl) {
          DsChannelType ct = cb->getChannelType();
          switch (ct) {
            case channeltype_hue:
            case channeltype_saturation:
              cl->mColorMode = colorLightModeHueSaturation;
              break;
            case channeltype_cie_x:
            case channeltype_cie_y:
              cl->mColorMode = colorLightModeXY;
              break;
            case channeltype_colortemp:
              cl->mColorMode = colorLightModeCt;
              break;
          }
        }
        if (changed && !mSyncedCB) {
          // channel report is not part of active syncChannelValues, report changed output state
          getOutput()->reportOutputState();
        }
      }
      else {
        return TextError::err("no channel #%d", aIndex);
      }
      break;
    }
    default:
      break;
  }
  return ErrorPtr(); // no feedback for input processing
}


void CustomDevice::releaseButton(ButtonBehaviourPtr aButtonBehaviour)
{
  aButtonBehaviour->updateButtonState(false);
}



#if ENABLE_CUSTOM_EXOTIC

// MARK: - device configurations


void CustomDevice::getDeviceConfigurations(DeviceConfigurationsVector &aConfigurations, StatusCB aStatusCB)
{
  if (mConfigurations.size()>0) {
    aConfigurations = mConfigurations;
  }
  else {
    aConfigurations.clear(); // prevent singular config
  }
  if (aStatusCB) aStatusCB(ErrorPtr());
}


string CustomDevice::getDeviceConfigurationId()
{
  return mConfigurationId;
}


ErrorPtr CustomDevice::switchConfiguration(const string aConfigurationId)
{
  for (DeviceConfigurationsVector::iterator pos=mConfigurations.begin(); pos!=mConfigurations.end(); ++pos) {
    if ((*pos)->getId()==aConfigurationId) {
      // known configuration, apply it
      if (aConfigurationId==mConfigurationId) return ErrorPtr(); // no need to switch
      if (!mSimpletext) {
        JsonObjectPtr message = JsonObject::newObj();
        message->add("message", JsonObject::newString("setConfiguration"));
        message->add("id", JsonObject::newString(aConfigurationId));
        sendDeviceApiJsonMessage(message);
      }
      return ErrorPtr();
    }
  }
  return inherited::switchConfiguration(aConfigurationId); // unknown profile at this level
}

#endif // ENABLE_CUSTOM_EXOTIC


// MARK: - output control


bool CustomDevice::prepareSceneCall(DsScenePtr aScene)
{
  if (mSceneCalls) {
    if (mSimpletext) {
      string m = string_format("CALLSCENE=%d", aScene->mSceneNo);
      sendDeviceApiSimpleMessage(m);
    }
    else {
      JsonObjectPtr message = JsonObject::newObj();
      message->add("message", JsonObject::newString("callscene"));
      message->add("sceneno", JsonObject::newInt32(aScene->mSceneNo));
      sendDeviceApiJsonMessage(message);
    }
  }
  if (mSceneCommands) {
    // forward (built-in, behaviour-defined) scene commands to external device
    const char *sceneCommandStr = NULL;
    switch (aScene->mSceneCmd) {
      case scene_cmd_none: break; // explicit NOP
      case scene_cmd_invoke: break; // no need to forward, the semantics are fully covered by applying channels
      case scene_cmd_off: sceneCommandStr = "OFF"; break;
      case scene_cmd_slow_off: sceneCommandStr = "SLOW_OFF"; break;
      case scene_cmd_min: sceneCommandStr = "MIN"; break;
      case scene_cmd_max: sceneCommandStr = "MAX"; break;
      case scene_cmd_increment: sceneCommandStr = "INC"; break;
      case scene_cmd_decrement: sceneCommandStr = "DEC"; break;
      case scene_cmd_stop: sceneCommandStr = "STOP"; break;
      case scene_cmd_climatecontrol_enable: sceneCommandStr = "CLIMATE_ENABLE"; break;
      case scene_cmd_climatecontrol_disable: sceneCommandStr = "CLIMATE_DISABLE"; break;
      case scene_cmd_climatecontrol_mode_heating: sceneCommandStr = "CLIMATE_HEATING"; break;
      case scene_cmd_climatecontrol_mode_cooling: sceneCommandStr = "CLIMATE_COOLING"; break;
      case scene_cmd_climatecontrol_mode_passive_cooling: sceneCommandStr = "CLIMATE_PASSIVE_COOLING"; break;
      default: break; // not implemented, ignore for now
    }
    // send scene command message
    if (sceneCommandStr) {
      if (mSimpletext) {
        string m = string_format("SCMD=%s", sceneCommandStr);
        sendDeviceApiSimpleMessage(m);
      }
      else {
        JsonObjectPtr message = JsonObject::newObj();
        message->add("message", JsonObject::newString("scenecommand"));
        message->add("cmd", JsonObject::newString(sceneCommandStr));
        sendDeviceApiJsonMessage(message);
      }
    }
  }
  // done
  return inherited::prepareSceneCall(aScene);
}


bool CustomDevice::prepareSceneApply(DsScenePtr aScene)
{
  // only implemented to catch "UNDO"
  if (mSceneCommands && aScene->mSceneCmd==scene_cmd_undo) {
    if (mSimpletext) {
      string m = string_format("SCMD=UNDO");
      sendDeviceApiSimpleMessage(m);
    }
    else {
      JsonObjectPtr message = JsonObject::newObj();
      message->add("message", JsonObject::newString("scenecommand"));
      message->add("cmd", JsonObject::newString("UNDO"));
      sendDeviceApiJsonMessage(message);
    }
  }
  return inherited::prepareSceneApply(aScene);
}




void CustomDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  // special behaviour for shadow behaviour
  ShadowBehaviourPtr sb = getOutput<ShadowBehaviour>();
  if (sb && mUseMovement) {
    // ask shadow behaviour to start movement sequence on default channel
    sb->applyBlindChannels(boost::bind(&CustomDevice::changeChannelMovement, this, 0, _1, _2), aDoneCB, aForDimming);
  }
  else {
    // check for special color light handling
    ColorLightBehaviourPtr cl = getOutput<ColorLightBehaviour>();
    if (cl) {
      // derive color mode from changed channel values
      // Note: external device cannot make use of colormode for now, but correct mode is important for saving scenes
      cl->deriveColorMode();
    }
    // generic channel apply
    for (int i=0; i<numChannels(); i++) {
      ChannelBehaviourPtr cb = getChannelByIndex(i);
      if (cb->needsApplying()) {
        // get value and apply mode
        double chval = cb->getChannelValue();
        chval = getOutput()->outputValueAccordingToMode(chval, i);
        // send channel value message
        if (mSimpletext) {
          string m = string_format("C%d=%lf", i, chval);
          sendDeviceApiSimpleMessage(m);
        }
        else {
          JsonObjectPtr message = JsonObject::newObj();
          message->add("message", JsonObject::newString("channel"));
          message->add("index", JsonObject::newInt32((int)i));
          message->add("type", JsonObject::newInt32(cb->getChannelType())); // informational
          message->add("id", JsonObject::newString(cb->getApiId(3))); // informational
          message->add("value", JsonObject::newDouble(cb->getChannelValue()));
          message->add("transition", JsonObject::newDouble((double)cb->transitionTimeToNewValue()/Second));
          message->add("dimming", JsonObject::newBool(aForDimming));
          sendDeviceApiJsonMessage(message);
        }
        cb->channelValueApplied();
      }
    }
  }
  inherited::applyChannelValues(aDoneCB, aForDimming);
}


void CustomDevice::dimChannel(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode, bool aDoApply)
{
  if (aChannel) {
    // start dimming
    ShadowBehaviourPtr sb = getOutput<ShadowBehaviour>();
    if (sb && mUseMovement && aDoApply) {
      // no channel check, there's only global dimming of the blind, no separate position/angle
      sb->dimBlind(boost::bind(&CustomDevice::changeChannelMovement, this, 0, _1, _2), aDimMode);
    }
    else if (mUseMovement && aDoApply) {
      // not shadow, but still use movement for dimming
      changeChannelMovement(aChannel->getChannelIndex(), NoOP, aDimMode);
    }
    else {
      inherited::dimChannel(aChannel, aDimMode, aDoApply);
    }
  }
}


void CustomDevice::changeChannelMovement(int aChannelIndex, SimpleCB aDoneCB, int aNewDirection)
{
  if (mSimpletext) {
    string m = string_format("MV%d=%d", aChannelIndex, aNewDirection);
    sendDeviceApiSimpleMessage(m);
  }
  else {
    JsonObjectPtr message = JsonObject::newObj();
    message->add("message", JsonObject::newString("move"));
    message->add("index", JsonObject::newInt32((int)aChannelIndex));
    message->add("direction", JsonObject::newInt32(aNewDirection));
    sendDeviceApiJsonMessage(message);
  }
  if (aDoneCB) aDoneCB();
}


void CustomDevice::syncChannelValues(SimpleCB aDoneCB)
{
  if (mQuerySync) {
    // save callback, to be called when "synced" message confirms sync done
    mSyncedCB = aDoneCB;
    // send sync command
    sendDeviceApiFlagMessage("SYNC");
  }
  else {
    inherited::syncChannelValues(aDoneCB);
  }
}


bool CustomDevice::processControlValue(const string &aName, double aValue)
{
  if (mControlValues) {
    // forward control messages
    if (mSimpletext) {
      string m = string_format("CTRL.%s=%lf", aName.c_str(), aValue);
      sendDeviceApiSimpleMessage(m);
    }
    else {
      JsonObjectPtr message = JsonObject::newObj();
      message->add("message", JsonObject::newString("control"));
      message->add("name", JsonObject::newString(aName));
      message->add("value", JsonObject::newDouble(aValue));
      sendDeviceApiJsonMessage(message);
    }
  }
  // Note: control values processed directly by the external device might change output values
  //   but do not need triggering applyChannelValues. In case the device changes
  //   channel values, it should sync them back normally.
  // Anyway, let inherited processing run as well (which might do channel changes
  // and trigger apply)
  return inherited::processControlValue(aName, aValue);
}


// MARK: - external device configuration

#if ENABLE_CUSTOM_SINGLEDEVICE

ErrorPtr CustomDevice::actionFromJSON(DeviceActionPtr &aAction, JsonObjectPtr aJSONConfig, const string aActionId, const string aDescription, const string aCategory)
{
  aAction = DeviceActionPtr(new CustomDeviceAction(*this, aActionId, aDescription, "", aCategory));
  return ErrorPtr();
}


ErrorPtr CustomDevice::dynamicActionFromJSON(DeviceActionPtr &aAction, JsonObjectPtr aJSONConfig, const string aActionId, const string aDescription, const string aTitle, const string aCategory)
{
  aAction = DeviceActionPtr(new CustomDeviceAction(*this, aActionId, aDescription, aTitle, aCategory));
  return ErrorPtr();
}


#endif

bool CustomDevice::checkSimple(JsonObjectPtr aInitMsg, ErrorPtr &aErr)
{
  JsonObjectPtr o;
  bool simple = false;
  if (aInitMsg->get("protocol", o)) {
    string p = o->stringValue();
    if (p=="json")
      simple = false;
    else if (p=="simple")
      simple = true;
    else
      aErr = TextError::err("unknown protocol '%s'", p.c_str());
  }
  return simple;
}


ErrorPtr CustomDevice::configureDevice(JsonObjectPtr aInitParams)
{
  JsonObjectPtr o;
  ErrorPtr err;

  // options
  if (aInitParams->get("sync", o)) mQuerySync = o->boolValue();
  if (aInitParams->get("move", o)) mUseMovement = o->boolValue();
  if (aInitParams->get("scenecommands", o)) mSceneCommands = o->boolValue();
  if (aInitParams->get("scenecalls", o)) mSceneCalls = o->boolValue();
  if (aInitParams->get("identification", o)) mForwardIdentify = o->boolValue();
  // get unique ID
  string uniqueid;
  if (!aInitParams->get("uniqueid", o)) {
    uniqueid = defaultUniqueId();
  }
  else {
    uniqueid = o->stringValue();
    // check if uniqueid has a schema
    for (size_t i=0; i<uniqueid.size(); i++) {
      if (!isalnum(uniqueid[i])) {
        if (uniqueid[i]==':' && i>3) {
          // has three char or longer alphanumeric URN schema prefix: use it as hardware UID
          // Note: dSS does display the part after the URN prefix as shortId
          mHardwareGUID = uniqueid;
        }
        break;
      }
    }
  }
  if (uniqueid.empty()) {
    return TextError::err("missing 'uniqueid'");
  }
  // - try it natively (can be a dSUID or a UUID)
  if (!mDSUID.setAsString(uniqueid)) {
    // not suitable dSUID or UUID syntax, create hashed dSUID
    DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
    //   UUIDv5 with name = classcontainerinstanceid:uniqueid
    string s = mVdcP->vdcInstanceIdentifier();
    s += ':';
    s += uniqueid;
    mDSUID.setNameInSpace(s, vdcNamespace);
  }
  // - subdevice index can be set separately
  if (aInitParams->get("subdeviceindex", o)) {
    mDSUID.setSubdeviceIndex(o->int32Value());
  }
  // Output
  // - get group (overridden for some output types)
  mColorClass = class_undefined; // none set so far
  DsGroup defaultGroup = group_undefined; // none set so far
  if (aInitParams->get("group", o)) {
    defaultGroup = (DsGroup)o->int32Value(); // custom output color
  }
  if (aInitParams->get("colorclass", o)) {
    mColorClass = (DsClass)o->int32Value(); // custom color class
  }
  // - get output type
  string outputType;
  if (aInitParams->get("output", o)) {
    outputType = o->stringValue();
  }
  // - get hardwarename
  string hardwareName = outputType; // default to output type
  if (aInitParams->get("hardwarename", o)) {
    hardwareName = o->stringValue();
  }
  // - get model name
  if (aInitParams->get("modelname", o)) {
    mModelNameString = o->stringValue();
  }
  // - get model version
  if (aInitParams->get("modelversion", o)) {
    mModelVersionString = o->stringValue();
  }
  // - get vendor name
  if (aInitParams->get("vendorname", o)) {
    mVendorNameString = o->stringValue();
  }
  // - get OEM model guid
  if (aInitParams->get("oemmodelguid", o)) {
    mOemModelGUIDString = o->stringValue();
  }
  // - get icon base name
  if (aInitParams->get("iconname", o)) {
    mIconBaseName = o->stringValue();
  }
  // - get type identifier
  if (aInitParams->get("typeidentifier", o)) {
    mTypeIdentifier = o->stringValue();
  }
  // - get device class
  if (aInitParams->get("deviceclass", o)) {
    mDevClass = o->stringValue();
  }
  // - get device class version
  if (aInitParams->get("deviceclassversion", o)) {
    mDevClassVersion = o->int32Value();
  }
  // - get config URI
  if (aInitParams->get("configurl", o)) {
    mConfigUrl = o->stringValue();
  }
  #if ENABLE_JSONBRIDGEAPI
  // - get bridging hint
  if (aInitParams->get("bridgeas", o)) {
    mBridgeAs = o->stringValue();
  }
  #endif
  // - basic output behaviour
  VdcOutputFunction outputFunction = outputFunction_custom; // not defined yet
  if (aInitParams->get("dimmable", o)) {
    outputFunction = o->boolValue() ? outputFunction_dimmer : outputFunction_switch;
  }
  if (aInitParams->get("positional", o)) {
    if (o->boolValue()) outputFunction = outputFunction_positional;
  }
  // - create appropriate output behaviour
  #if ENABLE_CUSTOM_SINGLEDEVICE
  if (outputType=="action") {
    enableAsSingleDevice(); // even without actions defines, this makes the device a single device
    if (mColorClass==class_undefined) mColorClass = class_white_singledevices;
    if (defaultGroup==group_undefined) defaultGroup = group_black_variable;
    // - use command scene device settings
    installSettings(DeviceSettingsPtr(new CmdSceneDeviceSettings(*this)));
    OutputBehaviourPtr o = OutputBehaviourPtr(new ActionOutputBehaviour(*this));
    o->setGroupMembership(defaultGroup, true);
    o->setHardwareName(hardwareName);
    addBehaviour(o);
  }
  else
  #endif
  if (outputType=="light") {
    if (defaultGroup==group_undefined) defaultGroup = group_yellow_light;
    if (outputFunction==outputFunction_custom) outputFunction = outputFunction_dimmer;
    // - use light settings, which include a scene table
    installSettings(DeviceSettingsPtr(new LightDeviceSettings(*this)));
    // - add simple single-channel light behaviour
    LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*this));
    l->setHardwareOutputConfig(outputFunction, outputFunction==outputFunction_switch ? outputmode_binary : outputmode_gradual, usage_undefined, false, -1);
    l->setHardwareName(hardwareName);
    addBehaviour(l);
  }
  else if (outputType=="ctlight") {
    if (defaultGroup==group_undefined) defaultGroup = group_yellow_light;
    // - CT only lights use color light settings, which include a color scene table
    installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
    // - add two-channel color light behaviour in CT only mode
    RGBColorLightBehaviourPtr l = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this, true));
    l->setHardwareName(hardwareName);
    addBehaviour(l);
  }
  else if (outputType=="colorlight") {
    if (defaultGroup==group_undefined) defaultGroup = group_yellow_light;
    // - use color light settings, which include a color scene table
    installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
    // - add multi-channel color light behaviour (which adds a number of auxiliary channels)
    RGBColorLightBehaviourPtr l = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this, false));
    l->setHardwareName(hardwareName);
    addBehaviour(l);
  }
  #if ENABLE_CUSTOM_EXOTIC
  else if (outputType=="movinglight") {
    if (defaultGroup==group_undefined) defaultGroup = group_yellow_light;
    // - use moving light settings, which include a color+position scene table
    installSettings(DeviceSettingsPtr(new MovingLightDeviceSettings(*this)));
    // - add moving color light behaviour
    MovingLightBehaviourPtr ml = MovingLightBehaviourPtr(new MovingLightBehaviour(*this, false));
    ml->setHardwareName(hardwareName);
    addBehaviour(ml);
  }
  else if (outputType=="featurelight") {
    if (defaultGroup==group_undefined) defaultGroup = group_yellow_light;
    // - use feature light settings, which include a color+position+zoom+rotation+gradients scene table
    installSettings(DeviceSettingsPtr(new FeatureLightDeviceSettings(*this)));
    // - add feature light behaviour
    FeatureLightBehaviourPtr fl = FeatureLightBehaviourPtr(new FeatureLightBehaviour(*this, false));
    fl->setHardwareName(hardwareName);
    addBehaviour(fl);
  }
  else if (outputType=="audio") {
    if (defaultGroup==group_undefined) defaultGroup = group_cyan_audio;
    // - use audio settings, which include a volume+powerstate+contensource+sceneCmd scene table
    installSettings(DeviceSettingsPtr(new AudioDeviceSettings(*this)));
    // - add audio behaviour
    AudioBehaviourPtr ab = AudioBehaviourPtr(new AudioBehaviour(*this));
    ab->setHardwareName(hardwareName);
    addBehaviour(ab);
  }
  else if (outputType=="video") {
    if (defaultGroup==group_undefined) defaultGroup = group_magenta_video;
    // - use video settings, which include a volume+powerstate+contensource+sceneCmd scene table
    installSettings(DeviceSettingsPtr(new VideoDeviceSettings(*this)));
    // - add video behaviour
    VideoBehaviourPtr vb = VideoBehaviourPtr(new VideoBehaviour(*this));
    vb->setHardwareName(hardwareName);
    addBehaviour(vb);
  }
  #endif // ENABLE_CUSTOM_EXOTIC
  else if (outputType=="heatingvalve") {
    if (defaultGroup==group_undefined) defaultGroup = group_roomtemperature_control;
    // - valve needs climate control scene table (ClimateControlScene)
    installSettings(DeviceSettingsPtr(new ClimateDeviceSettings(*this)));
    // - create climate control valve output
    OutputBehaviourPtr cb = OutputBehaviourPtr(new ClimateControlBehaviour(*this, climatedevice_simple, hscapability_heatingAndCooling));
    cb->setGroupMembership(defaultGroup, true); // put into room temperature control group by default, NOT into standard blue)
    cb->setHardwareOutputConfig(outputFunction_positional, outputmode_gradual, usage_room, false, 0);
    cb->setHardwareName(hardwareName);
    addBehaviour(cb);
  }
  #if ENABLE_FCU_SUPPORT
  else if (outputType=="fancoilunit") {
    if (defaultGroup==group_undefined) defaultGroup = group_roomtemperature_control;
    mControlValues = true; // fan coil unit usually needs control values
    // - FCU device settings with scene table
    installSettings(DeviceSettingsPtr(new FanCoilUnitDeviceSettings(*this)));
    // - create climate control fan coil unit output
    OutputBehaviourPtr cb = OutputBehaviourPtr(new ClimateControlBehaviour(*this, climatedevice_fancoilunit, hscapability_heatingAndCooling));
    cb->setGroupMembership(defaultGroup, true); // put into room temperature control group...
    cb->setHardwareOutputConfig(outputFunction_internallyControlled, outputmode_gradual, usage_room, false, 0);
    cb->setHardwareName(hardwareName);
    addBehaviour(cb);
  }
  else if (outputType=="ventilation") {
    // - use ventilation scene settings
    installSettings(DeviceSettingsPtr(new VentilationDeviceSettings(*this)));
    VentilationDeviceKind vk = ventilationdevice_recirculation;
    if (aInitParams->get("kind", o)) {
      string k = o->stringValue();
      if (k=="ventilation")
        vk = ventilationdevice_ventilation;
      else if (k=="recirculation")
        vk = ventilationdevice_recirculation;
    }
    // default group according to ventilation kind
    if (defaultGroup==group_undefined) {
      defaultGroup = (vk==ventilationdevice_recirculation) ? group_blue_air_recirculation : group_ventilation_control;
    }
    // - add ventilation behaviour
    VentilationBehaviourPtr vb = VentilationBehaviourPtr(new VentilationBehaviour(*this, vk));
    vb->setGroupMembership(defaultGroup, true); // use the default group
    vb->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_room, false, -1);
    vb->setHardwareName(hardwareName);
    addBehaviour(vb);
  }
  #endif // ENABLE_FCU_SUPPORT
  else if (outputType=="shadow") {
    if (defaultGroup==group_undefined) defaultGroup = group_grey_shadow;
    // - use shadow scene settings
    installSettings(DeviceSettingsPtr(new ShadowDeviceSettings(*this)));
    // - add shadow behaviour
    ShadowBehaviourPtr sb = ShadowBehaviourPtr(new ShadowBehaviour(*this, defaultGroup));
    sb->setHardwareOutputConfig(outputFunction_positional, outputmode_gradual, usage_undefined, false, -1);
    sb->setHardwareName(hardwareName);
    ShadowDeviceKind sk = shadowdevice_jalousie; // default to jalousie
    if (aInitParams->get("kind", o)) {
      string k = o->stringValue();
      if (k=="roller")
        sk = shadowdevice_rollerblind;
      else if (k=="sun")
        sk = shadowdevice_sunblind;
    }
    bool endContacts = false; // with no end contacts
    if (aInitParams->get("endcontacts", o)) {
      endContacts = o->boolValue();
    }
    sb->setDeviceParams(sk, endContacts, 0, 0, 0, !mUseMovement); // no restrictions for move times, when "move" is not specified, device can do absolute positioning
    sb->mPosition->syncChannelValue(100, false, true); // assume fully up at beginning
    sb->mAngle->syncChannelValue(100, false, true); // assume fully open at beginning
    addBehaviour(sb);
  }
  else if (outputType=="basic") {
    if (defaultGroup==group_undefined) defaultGroup = group_black_variable;
    if (outputFunction==outputFunction_custom) outputFunction = outputFunction_switch;
    // - use simple scene settings
    installSettings(DeviceSettingsPtr(new SceneDeviceSettings(*this)));
    // - add generic output behaviour
    OutputBehaviourPtr out = OutputBehaviourPtr(new OutputBehaviour(*this));
    out->setHardwareOutputConfig(outputFunction, outputFunction==outputFunction_switch ? outputmode_binary : outputmode_gradual, usage_undefined, false, -1);
    out->setHardwareName(hardwareName);
    out->setGroupMembership(defaultGroup, true); // put into default group
    // - add channel
    string channelid;
    if (aInitParams->get("channelid", o)) {
      channelid = o->stringValue();
    }
    if (outputFunction==outputFunction_switch) {
      // on/off switch type, no further customisation
      out->addChannel(ChannelBehaviourPtr(new DigitalChannel(*out, channelid.empty() ? "basic_switch" : channelid)));
      channelid = "basic_switch";
    }
    else {
      // configurable
      CustomChannelPtr cc = new CustomChannel(*out, channelid.empty() ? "basic_dial" : channelid);
      if (aInitParams->get("min", o)) cc->setMin(o->doubleValue());
      if (aInitParams->get("max", o)) cc->setMax(o->doubleValue());
      if (aInitParams->get("resolution", o)) cc->setResolution(o->doubleValue());
      if (aInitParams->get("unit", o)) cc->setChannelUnit(stringToValueUnit(o->stringValue()));
      if (aInitParams->get("channelname", o)) cc->setName(o->stringValue());
      out->addChannel(cc);
    }
    addBehaviour(out);
  }
  else {
    // no output, just install minimal settings without scenes
    installSettings();
  }
  // set options that might have a default set by the output type
  if (aInitParams->get("controlvalues", o)) mControlValues = o->boolValue();
  // set primary group to black if group is not yet defined so far
  if (defaultGroup==group_undefined) defaultGroup = group_black_variable;
  if (mColorClass==class_undefined) mColorClass = colorClassFromGroup(defaultGroup);
  // check for groups definition, will override anything set so far
  if (aInitParams->get("groups", o) && getOutput()) {
    getOutput()->resetGroupMembership(); // clear all
    for (int i=0; i<o->arrayLength(); i++) {
      JsonObjectPtr o2 = o->arrayGet(i);
      DsGroup g = (DsGroup)o2->int32Value();
      getOutput()->setGroupMembership(g, true);
    }
  }
  // check for buttons
  if (aInitParams->get("buttons", o)) {
    for (int i=0; i<o->arrayLength(); i++) {
      JsonObjectPtr o2 = o->arrayGet(i);
      JsonObjectPtr o3;
      // set defaults
      int buttonId = 0;
      int combinables = 0; // fixed mode, not combinable
      VdcButtonType buttonType = buttonType_single;
      VdcButtonElement buttonElement = buttonElement_center;
      DsGroup group = defaultGroup; // default group for button is same as primary default
      string buttonName;
      string id;
      bool isLocalButton = false;
      // - optional params
      if (o2->get("id", o3)) {
        if (o3->isType(json_type_int))
          buttonId = o3->int32Value(); // for backwards compatibility only. Should now use "buttonid"
        else
          id = o3->stringValue();
      }
      if (o2->get("buttonid", o3)) buttonId = o3->int32Value();
      if (o2->get("buttontype", o3)) buttonType = (VdcButtonType)o3->int32Value();
      if (o2->get("localbutton", o3)) isLocalButton = o3->boolValue();
      if (o2->get("element", o3)) buttonElement = (VdcButtonElement)o3->int32Value();
      if (o2->get("group", o3)) group = (DsGroup)o3->int32Value();
      if (o2->get("combinables", o3)) combinables = (DsGroup)o3->int32Value();
      if (o2->get("hardwarename", o3)) buttonName = o3->stringValue(); else buttonName = string_format("button_id%d_el%d", buttonId, buttonElement);
      // - create behaviour
      ButtonBehaviourPtr bb = ButtonBehaviourPtr(new ButtonBehaviour(*this, id)); // automatic id if not specified
      bb->setHardwareButtonConfig(buttonId, buttonType, buttonElement, isLocalButton, buttonElement==buttonElement_down ? 1 : 0, combinables);
      bb->setGroup(group);
      bb->setHardwareName(buttonName);
      addBehaviour(bb);
    }
  }
  // check for binary inputs
  if (aInitParams->get("inputs", o)) {
    for (int i=0; i<o->arrayLength(); i++) {
      JsonObjectPtr o2 = o->arrayGet(i);
      JsonObjectPtr o3;
      // set defaults
      DsBinaryInputType inputType = binInpType_none;
      VdcUsageHint usage = usage_undefined;
      DsGroup group = defaultGroup; // default group for input is same as primary default
      MLMicroSeconds updateInterval = Never; // unknown
      MLMicroSeconds aliveSignInterval = Never; // no guaranteed alive sign interval
      string inputName;
      string id;
      // - optional params
      if (o2->get("id", o3)) id = o3->stringValue();
      if (o2->get("inputtype", o3)) inputType = (DsBinaryInputType)o3->int32Value();
      if (o2->get("usage", o3)) usage = (VdcUsageHint)o3->int32Value();
      if (o2->get("group", o3)) group = (DsGroup)o3->int32Value();
      if (o2->get("updateinterval", o3)) updateInterval = o3->doubleValue()*Second;
      if (o2->get("alivesigninterval", o3)) aliveSignInterval = o3->doubleValue()*Second;
      if (o2->get("hardwarename", o3)) inputName = o3->stringValue(); else inputName = string_format("input_ty%d", inputType);
      // - create behaviour
      BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this, id)); // automatic id if not specified
      ib->setHardwareInputConfig(inputType, usage, true, updateInterval, aliveSignInterval);
      ib->setGroup(group);
      ib->setHardwareName(inputName);
      addBehaviour(ib);
    }
  }
  // check for sensors
  if (aInitParams->get("sensors", o)) {
    for (int i=0; i<o->arrayLength(); i++) {
      JsonObjectPtr o2 = o->arrayGet(i);
      JsonObjectPtr o3;
      // set defaults
      VdcSensorType sensorType = sensorType_none;
      VdcUsageHint usage = usage_undefined;
      DsGroup group = defaultGroup; // default group for sensor is same as primary default
      double min = 0;
      double max = 100;
      double resolution = 1;
      MLMicroSeconds updateInterval = 5*Second; // assume mostly up-to-date
      MLMicroSeconds aliveSignInterval = Never; // no guaranteed alive sign interval
      MLMicroSeconds changesOnlyInterval = 5*Minute; // report same value again only after >=5min
      string sensorName;
      string id;
      // - optional params
      if (o2->get("id", o3)) id = o3->stringValue();
      if (o2->get("sensortype", o3)) sensorType = (VdcSensorType)o3->int32Value();
      if (o2->get("usage", o3)) usage = (VdcUsageHint)o3->int32Value();
      if (o2->get("group", o3)) group = (DsGroup)o3->int32Value();
      if (o2->get("updateinterval", o3)) updateInterval = o3->doubleValue()*Second;
      if (o2->get("alivesigninterval", o3)) aliveSignInterval = o3->doubleValue()*Second;
      if (o2->get("changesonlyinterval", o3)) changesOnlyInterval = o3->doubleValue()*Second;
      if (o2->get("hardwarename", o3)) sensorName = o3->stringValue(); else sensorName = string_format("sensor_ty%d", sensorType);
      if (o2->get("min", o3)) min = o3->doubleValue();
      if (o2->get("max", o3)) max = o3->doubleValue();
      if (o2->get("resolution", o3)) resolution = o3->doubleValue();
      // - create behaviour
      SensorBehaviourPtr sb = SensorBehaviourPtr(new SensorBehaviour(*this, id)); // automatic id if not specified
      sb->setHardwareSensorConfig(sensorType, usage, min, max, resolution, updateInterval, aliveSignInterval, changesOnlyInterval);
      sb->setGroup(group);
      sb->setHardwareName(sensorName);
      addBehaviour(sb);
    }
  }
  #if ENABLE_CUSTOM_EXOTIC
  // device configurations
  if (aInitParams->get("currentConfigId", o)) {
    mConfigurationId = o->stringValue();
  }
  if (aInitParams->get("configurations", o)) {
    if (mSimpletext) return TextError::err("Devices with multiple configurations must use JSON protocol");
    for (int i=0; i<o->arrayLength(); i++) {
      JsonObjectPtr o2 = o->arrayGet(i);
      JsonObjectPtr o3;
      string id;
      string description;
      // - optional params
      if (o2->get("id", o3)) id = o3->stringValue();
      if (o2->get("description", o3)) description = o3->stringValue();
      mConfigurations.push_back(DeviceConfigurationDescriptorPtr(new DeviceConfigurationDescriptor(id, description)));
    }
  }
  // explicit modelfeature control (mainly for debugging)
  if (aInitParams->get("modelfeatures", o)) {
    o->resetKeyIteration();
    string hn;
    JsonObjectPtr hv;
    while (o->nextKeyValue(hn, hv)) {
      // find feature
      for (int idx=0; idx<numModelFeatures; idx++) {
        if (uequals(modelFeatureNames[idx], hn.c_str())) {
          if (hv && hv->boolValue()) {
            // explicitly set feature
            mExtraModelFeatures |= (1ll<<idx);
          }
          else {
            // explicitly mute feature
            mMutedModelFeatures &= ~(1ll<<idx);
          }
        }
      }
    }
  }
  #endif // ENABLE_CUSTOM_EXOTIC
  #if ENABLE_CUSTOM_SINGLEDEVICE
  // create actions/states/events and properties from JSON
  if (aInitParams->get("noconfirmaction", o)) mNoConfirmAction = o->boolValue();
  err = configureFromJSON(aInitParams);
  if (Error::notOK(err)) return err;
  err = standardActionsFromJSON(aInitParams);
  if (Error::notOK(err)) return err;
  if (deviceProperties) deviceProperties->setPropertyChangedHandler(boost::bind(&CustomDevice::propertyChanged, this, _1));
  // if any of the singledevice features are selected, protocol must be JSON
  if (deviceActions && mSimpletext) {
    return TextError::err("Single devices must use JSON protocol");
  }
  #endif // ENABLE_CUSTOM_SINGLEDEVICE
  // check for default name
  if (aInitParams->get("name", o)) {
    initializeName(o->stringValue());
  }
  // configured
  mConfigured = true;
  //#if DEBUG
  //boost::intrusive_ptr<VideoDeviceSettings> vs = boost::dynamic_pointer_cast<VideoDeviceSettings>(deviceSettings);
  //if (vs) {
  //  vs->dumpDefaultScenes();
  //}
  //#endif
  // explicit ok
  return Error::ok();
}


#if ENABLE_CUSTOM_SINGLEDEVICE

void CustomDevice::propertyChanged(ValueDescriptorPtr aChangedProperty)
{
  // create JSON response
  JsonObjectPtr message = JsonObject::newObj();
  message->add("message", JsonObject::newString("setProperty"));
  message->add("property", JsonObject::newString(aChangedProperty->getName()));
  JsonApiValuePtr v = JsonApiValuePtr(new JsonApiValue());
  if (!aChangedProperty->getValue(v)) {
    v->setNull();
  }
  message->add("value", v->jsonObject());
  // send it
  sendDeviceApiJsonMessage(message);
}


#endif // ENABLE_CUSTOM_SINGLEDEVICE


// MARK: - custom device container


CustomVdc::CustomVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
  mForwardIdentify(false)
{
  mIconBaseName = "vdc_cust";
}


string CustomVdc::modelName()
{
  if (!mModelNameString.empty())
    return mModelNameString;
  return inherited::modelName();
}


string CustomVdc::vdcModelVersion() const
{
  return mModelVersionString;
};


bool CustomVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon(mIconBaseName.c_str(), aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}



string CustomVdc::webuiURLString()
{
  if (!mConfigUrl.empty())
    return mConfigUrl;
  else
    return inherited::webuiURLString();
}


bool CustomVdc::canIdentifyToUser()
{
  return mForwardIdentify || inherited::canIdentifyToUser();
}




ErrorPtr CustomVdc::handleInitVdcMessage(JsonObjectPtr aVdcInitMessage)
{
  ErrorPtr err;

  // vdc-level information
  // - model name
  JsonObjectPtr o;
  if (aVdcInitMessage->get("modelname", o)) {
    mModelNameString = o->stringValue();
  }
  if (aVdcInitMessage->get("modelversion", o)) {
    mModelVersionString = o->stringValue();
  }
  // - get icon base name
  if (aVdcInitMessage->get("iconname", o)) {
    mIconBaseName = o->stringValue();
  }
  // - get config URI
  if (aVdcInitMessage->get("configurl", o)) {
    mConfigUrl = o->stringValue();
  }
  // - get default name
  if (aVdcInitMessage->get("name", o)) {
    initializeName(o->stringValue());
  }
  // - always visible (even when empty)
  if (aVdcInitMessage->get("alwaysVisible", o)) {
    // Note: this is now a (persistent!) vdc level property, which can be set from external API this way
    setVdcFlag(vdcflag_hidewhenempty, !o->boolValue());
  }
  // - forward vdc-level identification
  if (aVdcInitMessage->get("identification", o)) {
    mForwardIdentify = o->boolValue();
  }
  return err;
}



#endif // ENABLE_EXTERNAL || ENABLE_SCRIPTED
