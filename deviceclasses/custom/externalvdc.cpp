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
  mDeviceConnector(aDeviceConnector),
  mTag(aTag)
{
  mTypeIdentifier = "external";
  mModelNameString = "custom external device";
  mIconBaseName = "ext";
}


ExternalDevice::~ExternalDevice()
{
  OLOG(LOG_DEBUG, "destructed");
}


void ExternalDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // remove from connector
  mDeviceConnector->removeDevice(this);
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
  if (!mTag.empty()) {
    aMessage->add("tag", JsonObject::newString(mTag));
  }
  // now show and send
  POLOG(mDeviceConnector, LOG_INFO, "device <- externalVdc (JSON) message sent: %s", aMessage->c_strValue());
  mDeviceConnector->mDeviceConnection->sendMessage(aMessage);
}


void ExternalDevice::sendDeviceApiSimpleMessage(string aMessage)
{
  // prefix with tag if device has one
  if (!mTag.empty()) {
    aMessage = mTag+":"+aMessage;
  }
  POLOG(mDeviceConnector, LOG_INFO, "device <- externalVdc (simple) message sent: %s", aMessage.c_str());
  aMessage += "\n";
  mDeviceConnector->mDeviceConnection->sendRaw(aMessage);
}


// MARK: - external device connector

ExternalDeviceConnector::ExternalDeviceConnector(ExternalVdc &aExternalVdc, JsonCommPtr aDeviceConnection) :
  mExternalVdc(aExternalVdc),
  mDeviceConnection(aDeviceConnection),
  mSimpletext(false)
{
  mDeviceConnection->relatedObject = this;
  // install handlers on device connection
  mDeviceConnection->setConnectionStatusHandler(boost::bind(&ExternalDeviceConnector::handleDeviceConnectionStatus, this, _2));
  mDeviceConnection->setMessageHandler(boost::bind(&ExternalDeviceConnector::handleDeviceApiJsonMessage, this, _1, _2));
  mDeviceConnection->setClearHandlersAtClose(); // close must break retain cycles so this object won't cause a mem leak
  OLOG(LOG_DEBUG, "external device connector %p -> created", this);
}


int ExternalDeviceConnector::getLogLevelOffset()
{
  // follows vdc
  return mExternalVdc.getLogLevelOffset();
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
    while (mExternalDevices.size()>0) {
      mExternalDevices.begin()->second->hasVanished(false); // keep config
    }
  }
}


void ExternalDeviceConnector::removeDevice(ExternalDevicePtr aExtDev)
{
  for (ExternalDevicesMap::iterator pos = mExternalDevices.begin(); pos!=mExternalDevices.end(); ++pos) {
    if (pos->second==aExtDev) {
      mExternalDevices.erase(pos);
      break;
    }
  }
}


void ExternalDeviceConnector::closeConnection()
{
  // prevent further connection status callbacks
  mDeviceConnection->setConnectionStatusHandler(NULL);
  // close connection
  mDeviceConnection->closeConnection();
  // release the connection
  // Note: this should cause the connection to get deleted, which in turn also releases the relatedObject,
  //   so the device is only kept by the container (or not at all if it has not yet registered)
  mDeviceConnection.reset();
}


void ExternalDeviceConnector::sendDeviceApiJsonMessage(JsonObjectPtr aMessage, const char *aTag)
{
  // add in tag if device has one
  if (aTag && *aTag) {
    aMessage->add("tag", JsonObject::newString(aTag));
  }
  // now show and send
  OLOG(LOG_INFO, "device <- externalVdc (JSON) message sent: %s", aMessage->c_strValue());
  mDeviceConnection->sendMessage(aMessage);
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
  mDeviceConnection->sendRaw(aMessage);
}



void ExternalDeviceConnector::sendDeviceApiStatusMessage(ErrorPtr aError, const char *aTag)
{
  if (mSimpletext) {
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


ExternalDevicePtr ExternalDeviceConnector::findDeviceByTag(string aTag, bool aNoError)
{
  ExternalDevicePtr dev;
  if (aTag.empty() && mExternalDevices.size()>1) {
    if (!aNoError) sendDeviceApiStatusMessage(TextError::err("missing 'tag' field"));
  }
  else {
    ExternalDevicesMap::iterator pos = mExternalDevices.end();
    if (mExternalDevices.size()>1 || !aTag.empty()) {
      // device must be addressed by tag
      pos = mExternalDevices.find(aTag);
    }
    else if (mExternalDevices.size()==1) {
      // just one device, always use that
      pos = mExternalDevices.begin();
    }
    if (pos==mExternalDevices.end()) {
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
    if (mExternalDevices.size()==0) mDeviceConnection->closeAfterSend();
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
      if (mExternalDevices.size()==0) {
        mSimpletext = CustomDevice::checkSimple(aMessage, err);
        if (Error::isOK(err)) {
          // switch message decoder if we have simpletext
          if (mSimpletext) {
            mDeviceConnection->setRawMessageHandler(boost::bind(&ExternalDeviceConnector::handleDeviceApiSimpleMessage, this, _1, _2));
          }
        }
      }
      // check for tag, we need one if this is not the first (and only) device
      if (mExternalDevices.size()>0) {
        if (tag.empty()) {
          err = TextError::err("missing tag (needed for multiple devices on this connection)");
        }
        else if (mExternalDevices.find(tag)!=mExternalDevices.end()) {
          err = TextError::err("device with tag '%s' already exists", tag.c_str());
        }
      }
      if (Error::isOK(err)) {
        // ok to create new device
        extDev = ExternalDevicePtr(new ExternalDevice(&mExternalVdc, this, tag, mSimpletext));
        // - let it initalize
        err = extDev->configureDevice(aMessage);
      }
      if (Error::isOK(err)) {
        // device configured, add it now
        if (!mExternalVdc.simpleIdentifyAndAddDevice(extDev)) {
          err = TextError::err("device could not be added (duplicate uniqueid could be a reason, see p44vdc log)");
          extDev.reset(); // forget it
        }
        else {
          // added ok, also add to my own list
          mExternalDevices[tag] = extDev;
        }
      }
    }
    else if (msg=="initvdc") {
      mExternalVdc.handleInitVdcMessage(aMessage);
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
        else { OLOG(logLevel,"External Device vDC %s: %s", mExternalVdc.shortDesc().c_str(), o->c_strValue()); }
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
      else { OLOG(level,"External Device vDC %s: %s", mExternalVdc.shortDesc().c_str(), val.c_str()); }
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
    if (mExternalDevices.size()==0) mDeviceConnection->closeAfterSend();
  }
}



// MARK: - external device container



ExternalVdc::ExternalVdc(int aInstanceNumber, const string &aSocketPathOrPort, bool aNonLocal, VdcHost *aVdcHostP, int aTag) :
  CustomVdc(aInstanceNumber, aVdcHostP, aTag)
{
  // set default icon base name
  mIconBaseName = "vdc_ext";
  // create device API server and set connection specifications
  mExternalDeviceApiServer = SocketCommPtr(new SocketComm(MainLoop::currentMainLoop()));
  mExternalDeviceApiServer->setConnectionParams(NULL, aSocketPathOrPort.c_str(), SOCK_STREAM, PF_UNSPEC);
  mExternalDeviceApiServer->setAllowNonlocalConnections(aNonLocal);
}


void ExternalVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // start device API server
  ErrorPtr err = mExternalDeviceApiServer->startServer(boost::bind(&ExternalVdc::deviceApiConnectionHandler, this, _1), 10);
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


const char *ExternalVdc::vdcClassIdentifier() const
{
  return "External_Device_Container";
}



void ExternalVdc::identifyToUser()
{
  if (mForwardIdentify) {
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
