//
//  Copyright (c) 2015-2021 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

using namespace p44;


// MARK: - ExternalDevice


ExternalDevice::ExternalDevice(Vdc *aVdcP, ExternalDeviceConnectorPtr aDeviceConnector, string aTag, bool aSimpleText) :
  inherited(aVdcP, aSimpleText),
  deviceConnector(aDeviceConnector),
  tag(aTag)
{
  typeIdentifier = "external";
}


ExternalDevice::~ExternalDevice()
{
  OLOG(LOG_DEBUG, "destructed");
}


void ExternalDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // remove from connector
  deviceConnector->removeDevice(this);
  // otherwise perform normal disconnect
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}


ExternalVdc &ExternalDevice::getExternalVdc()
{
  return *(static_cast<ExternalVdc *>(vdcP));
}



void ExternalDevice::sendDeviceApiJsonMessage(JsonObjectPtr aMessage)
{
  // add in tag if device has one
  if (!tag.empty()) {
    aMessage->add("tag", JsonObject::newString(tag));
  }
  // now show and send
  POLOG(deviceConnector, LOG_INFO, "device <- externalVdc (JSON) message sent: %s", aMessage->c_strValue());
  deviceConnector->deviceConnection->sendMessage(aMessage);
}


void ExternalDevice::sendDeviceApiSimpleMessage(string aMessage)
{
  // prefix with tag if device has one
  if (!tag.empty()) {
    aMessage = tag+":"+aMessage;
  }
  POLOG(deviceConnector, LOG_INFO, "device <- externalVdc (simple) message sent: %s", aMessage.c_str());
  aMessage += "\n";
  deviceConnector->deviceConnection->sendRaw(aMessage);
}


void ExternalDevice::sendDeviceApiFlagMessage(string aFlagWord)
{
  deviceConnector->sendDeviceApiFlagMessage(aFlagWord, tag.c_str());
}



// MARK: - external device connector

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
  OLOG(LOG_DEBUG, "external device connector %p -> created", this);
}


int ExternalDeviceConnector::getLogLevelOffset()
{
  // follows vdc
  return externalVdc.getLogLevelOffset();
}



ExternalDeviceConnector::~ExternalDeviceConnector()
{
  OLOG(LOG_DEBUG, "external device connector %p -> destructed", this);
}


void ExternalDeviceConnector::handleDeviceConnectionStatus(ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    closeConnection();
    OLOG(LOG_NOTICE, "external device connection closed (%s) -> disconnecting all devices", aError->text());
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
  if (aTag && *aTag) {
    aMessage->add("tag", JsonObject::newString(aTag));
  }
  // now show and send
  OLOG(LOG_INFO, "device <- externalVdc (JSON) message sent: %s", aMessage->c_strValue());
  deviceConnection->sendMessage(aMessage);
}


void ExternalDeviceConnector::sendDeviceApiSimpleMessage(string aMessage, const char *aTag)
{
  // prefix with tag if device has one
  if (aTag && *aTag) {
    aMessage.insert(0, ":");
    aMessage.insert(0, aTag);
  }
  OLOG(LOG_INFO, "device <- externalVdc (simple) message sent: %s", aMessage.c_str());
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
    if (Error::notOK(aError)) {
      OLOG(LOG_INFO, "device API error: %s", aError->text());
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


void ExternalDeviceConnector::sendDeviceApiFlagMessage(string aFlagWord, const char *aTag)
{
  if (simpletext) {
    sendDeviceApiSimpleMessage(aFlagWord, aTag);
  }
  else {
    JsonObjectPtr message = JsonObject::newObj();
    message->add("message", JsonObject::newString(lowerCase(aFlagWord)));
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
    OLOG(LOG_INFO, "device -> externalVdc (JSON) message received: %s", aMessage->c_strValue());
    // JSON array can carry multiple messages
    if (aMessage->arrayLength()>0) {
      for (int i=0; i<aMessage->arrayLength(); ++i) {
        aError = handleDeviceApiJsonSubMessage(aMessage->arrayGet(i));
        if (Error::notOK(aError)) break;
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
        extDev = ExternalDevicePtr(new ExternalDevice(&externalVdc, this, tag, simpletext));
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
      if (aMessage->get("modelversion", o)) {
        externalVdc.modelVersionString = o->stringValue();
      }
      // - get icon base name
      if (aMessage->get("iconname", o)) {
        externalVdc.iconBaseName = o->stringValue();
      }
      // - get config URI
      if (aMessage->get("configurl", o)) {
        externalVdc.configUrl = o->stringValue();
      }
      // - get default name
      if (aMessage->get("name", o)) {
        externalVdc.initializeName(o->stringValue());
      }
      // - always visible (even when empty)
      if (aMessage->get("alwaysVisible", o)) {
        // Note: this is now a (persistent!) vdc level property, which can be set from external API this way
        externalVdc.setVdcFlag(vdcflag_hidewhenempty, !o->boolValue());
      }
      // - forward vdc-level identification
      if (aMessage->get("identification", o)) {
        externalVdc.forwardIdentify = o->boolValue();
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
        if (a) { OLOG(logLevel,"External Device %s: %s", a->shortDesc().c_str(), o->c_strValue()); }
        else { OLOG(logLevel,"External Device vDC %s: %s", externalVdc.shortDesc().c_str(), o->c_strValue()); }
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
  if (extDev && !(extDev->isConfigured())) {
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
    OLOG(LOG_INFO, "device -> externalVdc (simple) message received: %s", aMessage.c_str());
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
      if (a) { OLOG(level,"External Device %s: %s", a->shortDesc().c_str(), val.c_str()); }
      else { OLOG(level,"External Device vDC %s: %s", externalVdc.shortDesc().c_str(), val.c_str()); }
    }
    else {
      extDev = findDeviceByTag(tag, false);
      if (extDev) {
        aError = extDev->processSimpleMessage(msg,val);
      }
    }
  }
  // remove device that are not configured now
  if (extDev && !(extDev->isConfigured())) {
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



// MARK: - external device container



ExternalVdc::ExternalVdc(int aInstanceNumber, const string &aSocketPathOrPort, bool aNonLocal, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
  forwardIdentify(false),
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
  if (!getVdcFlag(vdcflag_flagsinitialized)) setVdcFlag(vdcflag_hidewhenempty, true); // hide by default
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


string ExternalVdc::vdcModelVersion() const
{
  return modelVersionString;
};




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


string ExternalVdc::webuiURLString()
{
  if (!configUrl.empty())
    return configUrl;
  else
    return inherited::webuiURLString();
}


bool ExternalVdc::canIdentifyToUser()
{
  return forwardIdentify || inherited::canIdentifyToUser();
}


void ExternalVdc::identifyToUser()
{
  if (forwardIdentify) {
    // TODO: %%% send "VDCIDENTIFY" or maybe "vdc:IDENTIFY"
    //   to all connectors - we need to implement a connector list for that
    OLOG(LOG_WARNING, "vdc level identify forwarding not yet implemented")
  }
  else {
    inherited::identifyToUser();
  }
}


void ExternalVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // we have no real collecting process (devices just connect when possible),
  // but we force all devices to re-connect when a exhaustive collect is requested (mainly for debug purposes)
  if (aRescanFlags & rescanmode_exhaustive) {
    // remove all, so they will need to reconnect
    removeDevices(aRescanFlags & rescanmode_clearsettings);
  }
  // assume ok
  aCompletedCB(ErrorPtr());
}


#endif // ENABLE_EXTERNAL
