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

#include "externalvdc.hpp"

#if ENABLE_EXTERNAL

#include "movinglightbehaviour.hpp"
#include "shadowbehaviour.hpp"
#include "climatecontrolbehaviour.hpp"
#include "ventilationbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "sensorbehaviour.hpp"

#if ENABLE_EXTERNAL_SINGLEDEVICE
  #include "jsonvdcapi.hpp"
#endif

using namespace p44;


#if ENABLE_EXTERNAL_SINGLEDEVICE

// MARK: ===== ExternalDeviceAction

ExternalDeviceAction::ExternalDeviceAction(SingleDevice &aSingleDevice, const string aName, const string aDescription) :
  inherited(aSingleDevice, aName, aDescription),
  callback(NULL)
{
}


ExternalDeviceAction::~ExternalDeviceAction()
{
  // execute callback if still pending
  if (callback) callback(WebError::webErr(410, "device gone"));
}


ExternalDevice &ExternalDeviceAction::getExternalDevice()
{
  return *(static_cast<ExternalDevice *>(singleDeviceP));
}


void ExternalDeviceAction::performCall(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  if (!getExternalDevice().noConfirmAction) {
    // remember callback
    callback = aCompletedCB;
  }
  // create JSON response
  JsonObjectPtr message = JsonObject::newObj();
  message->add("message", JsonObject::newString("invokeAction"));
  message->add("action", JsonObject::newString(actionId));
  // convert params
  if (aParams) {
    JsonApiValuePtr params = JsonApiValuePtr(new JsonApiValue()); // must be JSON so we can pass it as part of the message
    *params = *aParams; // copy to convert to JSON in all cases
    message->add("params", params->jsonObject());
  }
  // send it
  getExternalDevice().sendDeviceApiJsonMessage(message);
  if (getExternalDevice().noConfirmAction) {
    // immediately confirm
    if (aCompletedCB) aCompletedCB(ErrorPtr());
  }
}


void ExternalDeviceAction::callPerformed(JsonObjectPtr aStatusInfo)
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
  if (callback) {
    callback(err); // will return status to caller of action
    callback = NULL;
  }
}



#endif // ENABLE_EXTERNAL_SINGLEDEVICE


// MARK: ===== ExternalDevice


ExternalDevice::ExternalDevice(Vdc *aVdcP, ExternalDeviceConnectorPtr aDeviceConnector, string aTag) :
  #if ENABLE_EXTERNAL_SINGLEDEVICE
  inherited(aVdcP, false), // do not enable single device mechanisms by default
  noConfirmAction(false),
  #else
  inherited(aVdcP),
  #endif
  deviceConnector(aDeviceConnector),
  typeIdentifier("external"),
  tag(aTag),
  useMovement(false), // no movement by default
  querySync(false), // no sync query by default
  controlValues(false), // no control values by default
  configured(false),
  iconBaseName("ext"), // default icon name
  modelNameString("plan44 p44vdc external device"),
  vendorNameString("plan44.ch"),
  devClassVersion(0)
{
}


ExternalDevice::~ExternalDevice()
{
  ALOG(LOG_DEBUG, "destructed");
}


bool ExternalDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}


string ExternalDevice::modelName()
{
  return modelNameString;
}


string ExternalDevice::vendorName()
{
  return vendorNameString;
}


string ExternalDevice::oemModelGUID()
{
  return oemModelGUIDString;
}




void ExternalDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // remove from connector
  deviceConnector->removeDevice(this);
  // otherwise perform normal disconnect
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}



bool ExternalDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getClassColoredIcon(iconBaseName.c_str(), getDominantColorClass(), aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}



ExternalVdc &ExternalDevice::getExternalVdc()
{
  return *(static_cast<ExternalVdc *>(vdcP));
}



void ExternalDevice::handleDeviceApiJsonMessage(JsonObjectPtr aMessage)
{
  ErrorPtr err;
  LOG(LOG_INFO, "device -> externalVdc (JSON) message received: %s", aMessage->c_strValue());
  // extract message type
  JsonObjectPtr o = aMessage->get("message");
  if (o) {
    err = processJsonMessage(o->stringValue(), aMessage);
  }
  else {
    sendDeviceApiStatusMessage(TextError::err("missing 'message' field"));
  }
  // if error or explicit OK, send response now. Otherwise, request processing will create and send the response
  if (err) {
    sendDeviceApiStatusMessage(err);
  }
}


void ExternalDevice::handleDeviceApiSimpleMessage(string aMessage)
{
  ErrorPtr err;
  LOG(LOG_INFO, "device -> externalVdc (simple) message received: %s", aMessage.c_str());
  // extract message type
  string msg;
  string val;
  if (keyAndValue(aMessage, msg, val, '=')) {
    err = processSimpleMessage(msg,val);
  }
  else {
    err = processSimpleMessage(aMessage,"");
  }
  // if error or explicit OK, send response now. Otherwise, request processing will create and send the response
  if (err) {
    sendDeviceApiStatusMessage(err);
  }
}


void ExternalDevice::sendDeviceApiJsonMessage(JsonObjectPtr aMessage)
{
  // add in tag if device has one
  if (!tag.empty()) {
    aMessage->add("tag", JsonObject::newString(tag));
  }
  // now show and send
  LOG(LOG_INFO, "device <- externalVdc (JSON) message sent: %s", aMessage->c_strValue());
  deviceConnector->deviceConnection->sendMessage(aMessage);
}


void ExternalDevice::sendDeviceApiSimpleMessage(string aMessage)
{
  // prefix with tag if device has one
  if (!tag.empty()) {
    aMessage = tag+":"+aMessage;
  }
  LOG(LOG_INFO, "device <- externalVdc (simple) message sent: %s", aMessage.c_str());
  aMessage += "\n";
  deviceConnector->deviceConnection->sendRaw(aMessage);
}



void ExternalDevice::sendDeviceApiStatusMessage(ErrorPtr aError)
{
  if (deviceConnector->simpletext) {
    // simple text message
    string msg;
    if (Error::isOK(aError))
      msg = "OK";
    else
      msg = string_format("ERROR=%s",aError->getErrorMessage());
    // send it
    sendDeviceApiSimpleMessage(msg);
  }
  else {
    // create JSON response
    JsonObjectPtr message = JsonObject::newObj();
    message->add("message", JsonObject::newString("status"));
    if (!Error::isOK(aError)) {
      LOG(LOG_INFO, "device API error: %s", aError->description().c_str());
      // error, return error response
      message->add("status", JsonObject::newString("error"));
      message->add("errorcode", JsonObject::newInt32((int32_t)aError->getErrorCode()));
      message->add("errormessage", JsonObject::newString(aError->getErrorMessage()));
      message->add("errordomain", JsonObject::newString(aError->getErrorDomain()));
    }
    else {
      // no error, return result (if any)
      message->add("status", JsonObject::newString("ok"));
    }
    // send it
    sendDeviceApiJsonMessage(message);
  }
}



ErrorPtr ExternalDevice::processJsonMessage(string aMessageType, JsonObjectPtr aMessage)
{
  ErrorPtr err;
  if (aMessageType=="bye") {
    configured = false; // cause device to get removed
    err = Error::ok(); // explicit ok
  }
  else {
    if (configured) {
      if (aMessageType=="synced") {
        // device confirms having reported all channel states (in response to "sync" command)
        if (syncedCB) syncedCB();
        syncedCB = NULL;
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
      #if ENABLE_EXTERNAL_SINGLEDEVICE
      else if (aMessageType=="confirmAction") {
        JsonObjectPtr o;
        if (aMessage->get("action", o)) {
          ExternalDeviceActionPtr a = boost::dynamic_pointer_cast<ExternalDeviceAction>(deviceActions->getAction(o->stringValue()));
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
            if (aMessage->get("value", o)) {
              ApiValuePtr v = ApiValuePtr(JsonApiValue::newValueFromJson(o));
              ErrorPtr err = prop->conforms(v, true); // check and make internal
              if (!Error::isOK(err)) return err;
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
              if (!Error::isOK(err)) return err;
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


ErrorPtr ExternalDevice::processSimpleMessage(string aMessageType, string aValue)
{
  if (aMessageType=="BYE") {
    configured = false; // cause device to get removed
    return Error::ok(); // explicit ok
  }
  else if (aMessageType=="SYNCED") {
    // device confirms having reported all channel states (in response to "SYNC" command)
    if (syncedCB) syncedCB();
    syncedCB = NULL;
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
      return processInput(iotype, index, value);
    }
  }
  return TextError::err("Unknown message '%s'", aMessageType.c_str());
}


static int behaviourIndexById(BehaviourVector &aBV, const string aId)
{
  for (int i=0; i<aBV.size(); i++) {
    if (aId==aBV[i]->getApiId(3)) {
      return i;
    }
  }
  return -1;
}


static int channelIndexById(OutputBehaviourPtr aOB, const string aId)
{
  ChannelBehaviourPtr cb = aOB->getChannelById(aId);
  return cb ? cb->getChannelIndex() : -1;
}



ErrorPtr ExternalDevice::processInputJson(char aInputType, JsonObjectPtr aParams)
{
  int index = -1;
  JsonObjectPtr o = aParams->get("index");
  if (o) {
    index = o->int32Value();
  }
  else if (aInputType=='C' && aParams->get("type", o)) {
    // channel specified by type, not index
    DsChannelType ty = (DsChannelType)o->int32Value();
    ChannelBehaviourPtr cb = getChannelByType(ty);
    if (cb) index = (uint32_t)cb->getChannelIndex();
  }
  else if (aParams->get("id", o)) {
    // access by id
    string id = o->stringValue();
    switch (aInputType) {
      case 'B': index = behaviourIndexById(buttons, id); break;
      case 'I': index = behaviourIndexById(binaryInputs, id); break;
      case 'S': index = behaviourIndexById(sensors, id); break;
      case 'C': index = channelIndexById(output, id); break;
    }
  }
  if (index<0) {
    return TextError::err("missing id, index or type");
  }
  o = aParams->get("value");
  if (o) {
    double value = o->doubleValue();
    return processInput(aInputType, index, value);
  }
  else {
    return TextError::err("missing value");
  }
  return ErrorPtr();
}


// MARK: ===== process input (or log)

ErrorPtr ExternalDevice::processInput(char aInputType, uint32_t aIndex, double aValue)
{
  switch (aInputType) {
    case 'B': {
      if (aIndex<buttons.size()) {
        ButtonBehaviourPtr bb = boost::dynamic_pointer_cast<ButtonBehaviour>(buttons[aIndex]);
        if (bb) {
          if (aValue>2) {
            // simulate a keypress of defined length in milliseconds
            bb->buttonAction(true);
            MainLoop::currentMainLoop().executeOnce(boost::bind(&ExternalDevice::releaseButton, this, bb), aValue*MilliSecond);
          }
          else {
            bb->buttonAction(aValue!=0);
          }
        }
      }
      break;
    }
    case 'I': {
      if (aIndex<binaryInputs.size()) {
        BinaryInputBehaviourPtr ib = boost::dynamic_pointer_cast<BinaryInputBehaviour>(binaryInputs[aIndex]);
        if (ib) {
          ib->updateInputState(aValue!=0);
        }
      }
      break;
    }
    case 'S': {
      if (aIndex<sensors.size()) {
        SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(sensors[aIndex]);
        if (sb) {
          sb->updateSensorValue(aValue);
        }
      }
      break;
    }
    case 'C': {
      ChannelBehaviourPtr cb = getChannelByIndex(aIndex);
      if (cb) {
        cb->syncChannelValue(aValue, true);
        // check for shadow end contact reporting
        if (aIndex==0) {
          ShadowBehaviourPtr sb = boost::dynamic_pointer_cast<ShadowBehaviour>(output);
          if (sb) {
            if (aValue>=cb->getMax())
              sb->endReached(true); // reached top
            else if (aValue<=cb->getMin())
              sb->endReached(false); // reached bottom
          }
        }
        // check for color mode
        ColorLightBehaviourPtr cl = boost::dynamic_pointer_cast<ColorLightBehaviour>(output);
        if (cl) {
          DsChannelType ct = cb->getChannelType();
          switch (ct) {
            case channeltype_hue:
            case channeltype_saturation:
              cl->colorMode = colorLightModeHueSaturation;
              break;
            case channeltype_cie_x:
            case channeltype_cie_y:
              cl->colorMode = colorLightModeXY;
              break;
            case channeltype_colortemp:
              cl->colorMode = colorLightModeCt;
              break;
          }
        }
      }
      break;
    }
    default:
      break;
  }
  return ErrorPtr(); // no feedback for input processing
}


void ExternalDevice::releaseButton(ButtonBehaviourPtr aButtonBehaviour)
{
  aButtonBehaviour->buttonAction(false);
}



// MARK: ===== output control


bool ExternalDevice::prepareSceneCall(DsScenePtr aScene)
{
  if (sceneCommands) {
    // forward (built-in, behaviour-defined) scene commands to external device
    const char *sceneCommandStr = NULL;
    switch (aScene->sceneCmd) {
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
      default: break; // not implemented, ignore for now
    }
    // send scene command message
    if (sceneCommandStr) {
      if (deviceConnector->simpletext) {
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



void ExternalDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  // special behaviour for shadow behaviour
  ShadowBehaviourPtr sb = boost::dynamic_pointer_cast<ShadowBehaviour>(output);
  if (sb && useMovement) {
    // ask shadow behaviour to start movement sequence on default channel
    sb->applyBlindChannels(boost::bind(&ExternalDevice::changeChannelMovement, this, 0, _1, _2), aDoneCB, aForDimming);
  }
  else {
    // check for special color light handling
    ColorLightBehaviourPtr cl = boost::dynamic_pointer_cast<ColorLightBehaviour>(output);
    if (cl) {
      // derive color mode from changed channel values
      // Note: external device cannot make use of colormode for now, but correct mode is important for saving scenes
      cl->deriveColorMode();
    }
    // generic channel apply
    for (size_t i=0; i<numChannels(); i++) {
      ChannelBehaviourPtr cb = getChannelByIndex(i);
      if (cb->needsApplying()) {
        // get value and apply mode
        double chval = cb->getChannelValue();
        chval = output->outputValueAccordingToMode(chval, i);
        // send channel value message
        if (deviceConnector->simpletext) {
          string m = string_format("C%zu=%lf", i, chval);
          sendDeviceApiSimpleMessage(m);
        }
        else {
          JsonObjectPtr message = JsonObject::newObj();
          message->add("message", JsonObject::newString("channel"));
          message->add("index", JsonObject::newInt32((int)i));
          message->add("type", JsonObject::newInt32(cb->getChannelType())); // informational
          message->add("id", JsonObject::newString(cb->getApiId(3))); // informational
          message->add("value", JsonObject::newDouble(cb->getChannelValue()));
          sendDeviceApiJsonMessage(message);
        }
        cb->channelValueApplied();
      }
    }
  }
  inherited::applyChannelValues(aDoneCB, aForDimming);
}


void ExternalDevice::dimChannel(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode)
{
  if (aChannel) {
    // start dimming
    ShadowBehaviourPtr sb = boost::dynamic_pointer_cast<ShadowBehaviour>(output);
    if (sb && useMovement) {
      // no channel check, there's only global dimming of the blind, no separate position/angle
      sb->dimBlind(boost::bind(&ExternalDevice::changeChannelMovement, this, 0, _1, _2), aDimMode);
    }
    else if (useMovement) {
      // not shadow, but still use movement for dimming
      changeChannelMovement(aChannel->getChannelIndex(), NULL, aDimMode);
    }
    else {
      inherited::dimChannel(aChannel, aDimMode);
    }
  }
}


void ExternalDevice::changeChannelMovement(int aChannelIndex, SimpleCB aDoneCB, int aNewDirection)
{
  if (deviceConnector->simpletext) {
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


void ExternalDevice::syncChannelValues(SimpleCB aDoneCB)
{
  if (querySync) {
    // save callback, to be called when "synced" message confirms sync done
    syncedCB = aDoneCB;
    // send sync command
    if (deviceConnector->simpletext) {
      sendDeviceApiSimpleMessage("SYNC");
    }
    else {
      JsonObjectPtr message = JsonObject::newObj();
      message->add("message", JsonObject::newString("sync"));
      sendDeviceApiJsonMessage(message);
    }
  }
  else {
    inherited::syncChannelValues(aDoneCB);
  }
}


bool ExternalDevice::processControlValue(const string &aName, double aValue)
{
  if (controlValues) {
    // forward control messages
    if (deviceConnector->simpletext) {
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


// MARK: ===== external device configuration

#if ENABLE_EXTERNAL_SINGLEDEVICE

ErrorPtr ExternalDevice::actionFromJSON(DeviceActionPtr &aAction, JsonObjectPtr aJSONConfig, const string aActionId, const string aDescription)
{
  // base class just creates a unspecific action
  aAction = DeviceActionPtr(new ExternalDeviceAction(*this, aActionId, aDescription));
  return ErrorPtr();
}

#endif


ErrorPtr ExternalDevice::configureDevice(JsonObjectPtr aInitParams)
{
  JsonObjectPtr o;
  ErrorPtr err;

  // options
  if (aInitParams->get("sync", o)) querySync = o->boolValue();
  if (aInitParams->get("move", o)) useMovement = o->boolValue();
  if (aInitParams->get("scenecommands", o)) sceneCommands = o->boolValue();
  // get unique ID
  if (!aInitParams->get("uniqueid", o)) {
    return TextError::err("missing 'uniqueid'");
  }
  // - try it natively (can be a dSUID or a UUID)
  if (!dSUID.setAsString(o->stringValue())) {
    // not suitable dSUID or UUID syntax, create hashed dSUID
    DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
    //   UUIDv5 with name = classcontainerinstanceid:uniqueid
    string s = vdcP->vdcInstanceIdentifier();
    s += ':';
    s += o->stringValue();
    dSUID.setNameInSpace(s, vdcNamespace);
  }
  // - subdevice index can be set separately
  if (aInitParams->get("subdeviceindex", o)) {
    dSUID.setSubdeviceIndex(o->int32Value());
  }
  // Output
  // - get group (overridden for some output types)
  colorClass = class_undefined; // none set so far
  DsGroup defaultGroup = group_undefined; // none set so far
  if (aInitParams->get("group", o)) {
    defaultGroup = (DsGroup)o->int32Value(); // custom output color
  }
  if (aInitParams->get("colorclass", o)) {
    colorClass = (DsClass)o->int32Value(); // custom color class
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
    modelNameString = o->stringValue();
  }
  // - get vendor name
  if (aInitParams->get("vendorname", o)) {
    vendorNameString = o->stringValue();
  }
  // - get OEM model guid
  if (aInitParams->get("oemmodelguid", o)) {
    oemModelGUIDString = o->stringValue();
  }
  // - get icon base name
  if (aInitParams->get("iconname", o)) {
    iconBaseName = o->stringValue();
  }
  // - get type identifier
  if (aInitParams->get("typeidentifier", o)) {
    typeIdentifier = o->stringValue();
  }
  // - get device class
  if (aInitParams->get("deviceclass", o)) {
    devClass = o->stringValue();
  }
  // - get device class version
  if (aInitParams->get("deviceclassversion", o)) {
    devClassVersion = o->int32Value();
  }
  // - basic output behaviour
  VdcOutputFunction outputFunction = outputFunction_custom; // not defined yet
  if (aInitParams->get("dimmable", o)) {
    outputFunction = o->boolValue() ? outputFunction_dimmer : outputFunction_switch;
  }
  if (aInitParams->get("positional", o)) {
    if (o->boolValue()) outputFunction = outputFunction_positional;
  }
  // - create appropriate output behaviour
  #if ENABLE_EXTERNAL_SINGLEDEVICE
  if (outputType=="action") {
    enableAsSingleDevice(); // even without actions defines, this makes the device a single device
    if (colorClass==class_undefined) colorClass = class_white_singledevices;
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
  else if (outputType=="colorlight") {
    if (defaultGroup==group_undefined) defaultGroup = group_yellow_light;
    // - use color light settings, which include a color scene table
    installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
    // - add multi-channel color light behaviour (which adds a number of auxiliary channels)
    RGBColorLightBehaviourPtr l = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this));
    l->setHardwareName(hardwareName);
    addBehaviour(l);
  }
  else if (outputType=="movinglight") {
    if (defaultGroup==group_undefined) defaultGroup = group_yellow_light;
    // - use moving light settings, which include a color+position scene table
    installSettings(DeviceSettingsPtr(new MovingLightDeviceSettings(*this)));
    // - add moving color light behaviour
    MovingLightBehaviourPtr ml = MovingLightBehaviourPtr(new MovingLightBehaviour(*this));
    ml->setHardwareName(hardwareName);
    addBehaviour(ml);
  }
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
  else if (outputType=="fancoilunit") {
    if (defaultGroup==group_undefined) defaultGroup = group_roomtemperature_control;
    controlValues = true; // fan coil unit usually needs control values
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
  else if (outputType=="shadow") {
    if (defaultGroup==group_undefined) defaultGroup = group_grey_shadow;
    // - use shadow scene settings
    installSettings(DeviceSettingsPtr(new ShadowDeviceSettings(*this)));
    // - add shadow behaviour
    ShadowBehaviourPtr sb = ShadowBehaviourPtr(new ShadowBehaviour(*this));
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
    sb->setDeviceParams(sk, endContacts, 0, 0, 0); // no restrictions for move times
    sb->position->syncChannelValue(100); // assume fully up at beginning
    sb->angle->syncChannelValue(100); // assume fully open at beginning
    addBehaviour(sb);
  }
  else if (outputType=="basic") {
    if (defaultGroup==group_undefined) defaultGroup = group_black_variable;
    if (outputFunction==outputFunction_custom) outputFunction = outputFunction_switch;
    // - use simple scene settings
    installSettings(DeviceSettingsPtr(new SceneDeviceSettings(*this)));
    // - add generic output behaviour
    OutputBehaviourPtr o = OutputBehaviourPtr(new OutputBehaviour(*this));
    o->setHardwareOutputConfig(outputFunction, outputFunction==outputFunction_switch ? outputmode_binary : outputmode_gradual, usage_undefined, false, -1);
    o->setHardwareName(hardwareName);
    o->setGroupMembership(defaultGroup, true); // put into default group
    o->addChannel(ChannelBehaviourPtr(new DigitalChannel(*o, "basic")));
    addBehaviour(o);
  }
  else {
    // no output, just install minimal settings without scenes
    installSettings();
  }
  // set options that might have a default set by the output type
  if (aInitParams->get("controlvalues", o)) controlValues = o->boolValue();
  // set primary group to black if group is not yet defined so far
  if (defaultGroup==group_undefined) defaultGroup = group_black_variable;
  if (colorClass==class_undefined) colorClass = colorClassFromGroup(defaultGroup);
  // check for groups definition, will override anything set so far
  if (aInitParams->get("groups", o) && output) {
    output->resetGroupMembership(); // clear all
    for (int i=0; i<o->arrayLength(); i++) {
      JsonObjectPtr o2 = o->arrayGet(i);
      DsGroup g = (DsGroup)o2->int32Value();
      output->setGroupMembership(g, true);
    }
  }
  // check for buttons
  if (aInitParams->get("buttons", o)) {
    for (int i=0; i<o->arrayLength(); i++) {
      JsonObjectPtr o2 = o->arrayGet(i);
      JsonObjectPtr o3;
      // set defaults
      int buttonId = 0;
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
      if (o2->get("hardwarename", o3)) buttonName = o3->stringValue(); else buttonName = string_format("button_id%d_el%d", buttonId, buttonElement);
      // - create behaviour
      ButtonBehaviourPtr bb = ButtonBehaviourPtr(new ButtonBehaviour(*this, id));
      bb->setHardwareButtonConfig(buttonId, buttonType, buttonElement, isLocalButton, buttonElement==buttonElement_down ? 1 : 0, true); // fixed mode
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
      string inputName;
      string id;
      // - optional params
      if (o2->get("id", o3)) id = o3->stringValue();
      if (o2->get("inputtype", o3)) inputType = (DsBinaryInputType)o3->int32Value();
      if (o2->get("usage", o3)) usage = (VdcUsageHint)o3->int32Value();
      if (o2->get("group", o3)) group = (DsGroup)o3->int32Value();
      if (o2->get("updateinterval", o3)) updateInterval = o3->doubleValue()*Second;
      if (o2->get("hardwarename", o3)) inputName = o3->stringValue(); else inputName = string_format("input_ty%d", inputType);
      // - create behaviour
      BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this, id));
      ib->setHardwareInputConfig(inputType, usage, true, updateInterval);
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
      MLMicroSeconds aliveSignInterval = 0; // no guaranteed alive sign interval
      string sensorName;
      string id;
      // - optional params
      if (o2->get("id", o3)) id = o3->stringValue();
      if (o2->get("sensortype", o3)) sensorType = (VdcSensorType)o3->int32Value();
      if (o2->get("usage", o3)) usage = (VdcUsageHint)o3->int32Value();
      if (o2->get("group", o3)) group = (DsGroup)o3->int32Value();
      if (o2->get("updateinterval", o3)) updateInterval = o3->doubleValue()*Second;
      if (o2->get("alivesigninterval", o3)) aliveSignInterval = o3->doubleValue()*Second;
      if (o2->get("hardwarename", o3)) sensorName = o3->stringValue(); else sensorName = string_format("sensor_ty%d", sensorType);
      if (o2->get("min", o3)) min = o3->doubleValue();
      if (o2->get("max", o3)) max = o3->doubleValue();
      if (o2->get("resolution", o3)) resolution = o3->doubleValue();
      // - create behaviour
      SensorBehaviourPtr sb = SensorBehaviourPtr(new SensorBehaviour(*this, id));
      sb->setHardwareSensorConfig(sensorType, usage, min, max, resolution, updateInterval, aliveSignInterval); // no default changesOnlyInterval, external device should prevent unneeded updates
      sb->setGroup(group);
      sb->setHardwareName(sensorName);
      addBehaviour(sb);
    }
  }
  #if ENABLE_EXTERNAL_SINGLEDEVICE
  // create actions/states/events and properties from JSON
  err = configureFromJSON(aInitParams);
  if (!Error::isOK(err)) return err;
  if (deviceProperties) deviceProperties->setPropertyChangedHandler(boost::bind(&ExternalDevice::propertyChanged, this, _1));
  // if any of the singledevice features are selected, protocol must be JSON
  if (deviceActions && deviceConnector->simpletext) {
    return TextError::err("Single devices must use JSON protocol");
  }
  #endif // ENABLE_EXTERNAL_SINGLEDEVICE
  // check for default name
  if (aInitParams->get("name", o)) {
    initializeName(o->stringValue());
  }
  // configured
  configured = true;
  // explicit ok
  return Error::ok();
}


#if ENABLE_EXTERNAL_SINGLEDEVICE

void ExternalDevice::propertyChanged(ValueDescriptorPtr aChangedProperty)
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




#endif // ENABLE_EXTERNAL_SINGLEDEVICE


// MARK: ===== external device connector

ExternalDeviceConnector::ExternalDeviceConnector(ExternalVdc &aExternalVdc, JsonCommPtr aDeviceConnection) :
  externalVdc(aExternalVdc),
  deviceConnection(aDeviceConnection),
  simpletext(false)
{
  deviceConnection->relatedObject = this;
  // install handlers on device connection
  deviceConnection->setConnectionStatusHandler(boost::bind(&ExternalDeviceConnector::handleDeviceConnectionStatus, this, _2));
  deviceConnection->setMessageHandler(boost::bind(&ExternalDeviceConnector::handleDeviceApiJsonMessage, this, _1, _2));
  deviceConnection->setClearHandlersAtClose(); // close must break retain cycles so this object won't cause a mem leak
  LOG(LOG_DEBUG, "external device connector %p -> created", this);
}


ExternalDeviceConnector::~ExternalDeviceConnector()
{
  LOG(LOG_DEBUG, "external device connector %p -> destructed", this);
}


void ExternalDeviceConnector::handleDeviceConnectionStatus(ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    closeConnection();
    LOG(LOG_NOTICE, "external device connection closed (%s) -> disconnecting all devices", aError->description().c_str());
    // devices have vanished for now, but will keep parameters in case it reconnects later
    while (externalDevices.size()>0) {
      externalDevices.begin()->second->hasVanished(false); // keep config
    }
  }
}


void ExternalDeviceConnector::removeDevice(ExternalDevicePtr aExtDev)
{
  for (ExternalDevicesMap::iterator pos = externalDevices.begin(); pos!=externalDevices.end(); ++pos) {
    if (pos->second==aExtDev) {
      externalDevices.erase(pos);
      break;
    }
  }
}


void ExternalDeviceConnector::closeConnection()
{
  // prevent further connection status callbacks
  deviceConnection->setConnectionStatusHandler(NULL);
  // close connection
  deviceConnection->closeConnection();
  // release the connection
  // Note: this should cause the connection to get deleted, which in turn also releases the relatedObject,
  //   so the device is only kept by the container (or not at all if it has not yet registered)
  deviceConnection.reset();
}


void ExternalDeviceConnector::sendDeviceApiJsonMessage(JsonObjectPtr aMessage, const char *aTag)
{
  // add in tag if device has one
  if (aTag) {
    aMessage->add("tag", JsonObject::newString(aTag));
  }
  // now show and send
  LOG(LOG_INFO, "device <- externalVdc (JSON) message sent: %s", aMessage->c_strValue());
  deviceConnection->sendMessage(aMessage);
}


void ExternalDeviceConnector::sendDeviceApiSimpleMessage(string aMessage, const char *aTag)
{
  // prefix with tag if device has one
  if (aTag && *aTag) {
    aMessage.insert(0, ":");
    aMessage.insert(0, aTag);
  }
  LOG(LOG_INFO, "device <- externalVdc (simple) message sent: %s", aMessage.c_str());
  aMessage += "\n";
  deviceConnection->sendRaw(aMessage);
}



void ExternalDeviceConnector::sendDeviceApiStatusMessage(ErrorPtr aError, const char *aTag)
{
  if (simpletext) {
    // simple text message
    string msg;
    if (Error::isOK(aError))
      msg = "OK";
    else
      msg = string_format("ERROR=%s",aError->getErrorMessage());
    // send it
    sendDeviceApiSimpleMessage(msg, aTag);
  }
  else {
    // create JSON response
    JsonObjectPtr message = JsonObject::newObj();
    message->add("message", JsonObject::newString("status"));
    if (!Error::isOK(aError)) {
      LOG(LOG_INFO, "device API error: %s", aError->description().c_str());
      // error, return error response
      message->add("status", JsonObject::newString("error"));
      message->add("errorcode", JsonObject::newInt32((int32_t)aError->getErrorCode()));
      message->add("errormessage", JsonObject::newString(aError->getErrorMessage()));
      message->add("errordomain", JsonObject::newString(aError->getErrorDomain()));
    }
    else {
      // no error, return result (if any)
      message->add("status", JsonObject::newString("ok"));
    }
    // send it
    sendDeviceApiJsonMessage(message, aTag);
  }
}



ExternalDevicePtr ExternalDeviceConnector::findDeviceByTag(string aTag, bool aNoError)
{
  ExternalDevicePtr dev;
  if (aTag.empty() && externalDevices.size()>1) {
    if (!aNoError) sendDeviceApiStatusMessage(TextError::err("missing 'tag' field"));
  }
  else {
    ExternalDevicesMap::iterator pos = externalDevices.end();
    if (externalDevices.size()>1 || !aTag.empty()) {
      // device must be addressed by tag
      pos = externalDevices.find(aTag);
    }
    else if (externalDevices.size()==1) {
      // just one device, always use that
      pos = externalDevices.begin();
    }
    if (pos==externalDevices.end()) {
      if (!aNoError) sendDeviceApiStatusMessage(TextError::err("no device tagged '%s' found", aTag.c_str()));
    }
    else {
      dev = pos->second;
    }
  }
  return dev;
}


void ExternalDeviceConnector::handleDeviceApiJsonMessage(ErrorPtr aError, JsonObjectPtr aMessage)
{
  // device API request
  ExternalDevicePtr extDev;
  if (Error::isOK(aError)) {
    // not JSON level error, try to process
    LOG(LOG_INFO, "device -> externalVdc (JSON) message received: %s", aMessage->c_strValue());
    // JSON array can carry multiple messages
    if (aMessage->arrayLength()>0) {
      for (int i=0; i<aMessage->arrayLength(); ++i) {
        aError = handleDeviceApiJsonSubMessage(aMessage->arrayGet(i));
        if (!Error::isOK(aError)) break;
      }
    }
    else {
      // single message
      aError = handleDeviceApiJsonSubMessage(aMessage);
    }
  }
  // if error or explicit OK, send response now. Otherwise, request processing will create and send the response
  if (aError) {
    // send response
    sendDeviceApiStatusMessage(aError);
    // make sure we disconnect after response is fully sent
    if (externalDevices.size()==0) deviceConnection->closeAfterSend();
  }
}


ErrorPtr ExternalDeviceConnector::handleDeviceApiJsonSubMessage(JsonObjectPtr aMessage)
{
  ErrorPtr err;
  ExternalDevicePtr extDev;
  // extract tag if there is one
  string tag;
  JsonObjectPtr o = aMessage->get("tag");
  if (o) tag = o->stringValue();
  // extract message type
  o = aMessage->get("message");
  if (!o) {
    sendDeviceApiStatusMessage(TextError::err("missing 'message' field"));
  }
  else {
    // check for init message
    string msg = o->stringValue();
    if (msg=="init") {
      // only first device can set protocol type or vDC model
      if (externalDevices.size()==0) {
        if (aMessage->get("protocol", o)) {
          string p = o->stringValue();
          if (p=="json")
            simpletext = false;
          else if (p=="simple")
            simpletext = true;
          else
            err = TextError::err("unknown protocol '%s'", p.c_str());
        }
        // switch message decoder if we have simpletext
        if (simpletext) {
          deviceConnection->setRawMessageHandler(boost::bind(&ExternalDeviceConnector::handleDeviceApiSimpleMessage, this, _1, _2));
        }
      }
      // check for tag, we need one if this is not the first (and only) device
      if (externalDevices.size()>0) {
        if (tag.empty()) {
          err = TextError::err("missing tag (needed for multiple devices on this connection)");
        }
        else if (externalDevices.find(tag)!=externalDevices.end()) {
          err = TextError::err("device with tag '%s' already exists", tag.c_str());
        }
      }
      if (Error::isOK(err)) {
        // ok to create new device
        extDev = ExternalDevicePtr(new ExternalDevice(&externalVdc, this, tag));
        // - let it initalize
        err = extDev->configureDevice(aMessage);
      }
      if (Error::isOK(err)) {
        // device configured, add it now
        if (!externalVdc.simpleIdentifyAndAddDevice(extDev)) {
          err = TextError::err("device could not be added (duplicate uniqueid could be a reason, see p44vdc log)");
          extDev.reset(); // forget it
        }
        else {
          // added ok, also add to my own list
          externalDevices[tag] = extDev;
        }
      }
    }
    else if (msg=="initvdc") {
      // vdc-level information
      // - model name
      if (aMessage->get("modelname", o)) {
        externalVdc.modelNameString = o->stringValue();
      }
      // - get icon base name
      if (aMessage->get("iconname", o)) {
        externalVdc.iconBaseName = o->stringValue();
      }
    }
    else if (msg=="log") {
      // log something
      int logLevel = LOG_NOTICE; // default to normally displayed (5)
      JsonObjectPtr o = aMessage->get("level");
      if (o) logLevel = o->int32Value();
      o = aMessage->get("text");
      if (o) {
        DsAddressablePtr a = findDeviceByTag(tag, true);
        if (a) { LOG(logLevel,"External Device %s: %s", a->shortDesc().c_str(), o->c_strValue()); }
        else { LOG(logLevel,"External Device vDC %s: %s", externalVdc.shortDesc().c_str(), o->c_strValue()); }
      }
    }
    else {
      // must be a message directed to an already existing device
      extDev = findDeviceByTag(tag, false);
      if (extDev) {
        err = extDev->processJsonMessage(o->stringValue(), aMessage);
      }
    }
  }
  // remove device that are not configured now
  if (extDev && !(extDev->configured)) {
    // disconnect
    extDev->hasVanished(false);
  }
  return err;
}


void ExternalDeviceConnector::handleDeviceApiSimpleMessage(ErrorPtr aError, string aMessage)
{
  // device API request
  string tag;
  ExternalDevicePtr extDev;
  if (Error::isOK(aError)) {
    // not connection level error, try to process
    aMessage = trimWhiteSpace(aMessage);
    LOG(LOG_INFO, "device -> externalVdc (simple) message received: %s", aMessage.c_str());
    // extract message type
    string taggedmsg;
    string val;
    if (!keyAndValue(aMessage, taggedmsg, val, '=')) {
      taggedmsg = aMessage; // just message...
      val.clear(); // no value
    }
    // check for tag
    string msg;
    if (!keyAndValue(taggedmsg, tag, msg, ':')) {
      // no tag
      msg = taggedmsg;
      tag.clear(); // no tag
    }
    if (msg[0]=='L') {
      // log
      int level = LOG_ERR;
      sscanf(msg.c_str()+1, "%d", &level);
      DsAddressablePtr a = findDeviceByTag(tag, true);
      if (a) { LOG(level,"External Device %s: %s", a->shortDesc().c_str(), val.c_str()); }
      else { LOG(level,"External Device vDC %s: %s", externalVdc.shortDesc().c_str(), val.c_str()); }
    }
    else {
      extDev = findDeviceByTag(tag, false);
      if (extDev) {
        aError = extDev->processSimpleMessage(msg,val);
      }
    }
  }
  // remove device that are not configured now
  if (extDev && !(extDev->configured)) {
    // disconnect
    extDev->hasVanished(false);
  }
  // if error or explicit OK, send response now. Otherwise, request processing will create and send the response
  if (aError) {
    // send response
    sendDeviceApiStatusMessage(aError, tag.c_str());
    // make sure we disconnect after response is fully sent
    if (externalDevices.size()==0) deviceConnection->closeAfterSend();
  }
}



// MARK: ===== external device container



ExternalVdc::ExternalVdc(int aInstanceNumber, const string &aSocketPathOrPort, bool aNonLocal, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
  iconBaseName("vdc_ext") // default icon name
{
  // create device API server and set connection specifications
  externalDeviceApiServer = SocketCommPtr(new SocketComm(MainLoop::currentMainLoop()));
  externalDeviceApiServer->setConnectionParams(NULL, aSocketPathOrPort.c_str(), SOCK_STREAM, PF_UNSPEC);
  externalDeviceApiServer->setAllowNonlocalConnections(aNonLocal);
}


void ExternalVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // start device API server
  ErrorPtr err = externalDeviceApiServer->startServer(boost::bind(&ExternalVdc::deviceApiConnectionHandler, this, _1), 10);
  aCompletedCB(err); // return status of starting server
}


SocketCommPtr ExternalVdc::deviceApiConnectionHandler(SocketCommPtr aServerSocketCommP)
{
  JsonCommPtr conn = JsonCommPtr(new JsonComm(MainLoop::currentMainLoop()));
  // new connection means new device connector (which will add devices to container once it has received proper init message(s))
  ExternalDeviceConnectorPtr extDevConn = ExternalDeviceConnectorPtr(new ExternalDeviceConnector(*this, conn));
  return conn;
}


string ExternalVdc::modelName()
{
  if (!modelNameString.empty())
    return modelNameString;
  return inherited::modelName();
}


bool ExternalVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon(iconBaseName.c_str(), aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}



const char *ExternalVdc::vdcClassIdentifier() const
{
  return "External_Device_Container";
}


void ExternalVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // we have no real collecting process (devices just connect when possibl),
  // but we force all devices to re-connect when a exhaustive collect is requested (mainly for debug purposes)
  if (aRescanFlags & rescanmode_exhaustive) {
    // remove all, so they will need to reconnect
    removeDevices(aRescanFlags & rescanmode_clearsettings);
  }
  // assume ok
  aCompletedCB(ErrorPtr());
}


#endif // ENABLE_EXTERNAL
