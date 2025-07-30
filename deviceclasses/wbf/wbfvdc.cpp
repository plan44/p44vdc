//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "wbfvdc.hpp"

#if ENABLE_WBF

#include "wbfdevice.hpp"
#include "jsonvdcapi.hpp"

using namespace p44;


WbfVdc::WbfVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  inherited(aInstanceNumber, aVdcHostP, aTag)
{
  mWbfComm.isMemberVariable();
}


WbfVdc::~WbfVdc()
{
  // release my devices before the maps they are registered in (via behaviours) are gone
  mDevices.clear();
}


void WbfVdc::setLogLevelOffset(int aLogLevelOffset)
{
  mWbfComm.setLogLevelOffset(aLogLevelOffset);
  inherited::setLogLevelOffset(aLogLevelOffset);
}


P44LoggingObj* WbfVdc::getTopicLogObject(const string aTopic)
{
  if (aTopic=="wbfcomm") return &mWbfComm;
  // unknown at this level
  return inherited::getTopicLogObject(aTopic);
}


const char *WbfVdc::vdcClassIdentifier() const
{
  return "wbf_Devices_Container";
}


bool WbfVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_wbf", aIcon, aWithData, aResolutionPrefix)) return true;
  return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string WbfVdc::getExtraInfo()
{
  return string_format("wbf gateway api%s: %s", mWbfComm.mFixedHostName.empty() ? "" : " (fixed)", mWbfComm.mResolvedHost.c_str());
}


string WbfVdc::hardwareGUID()
{
  string s;
  if (!mSerialNo.empty()) {
    s = "wbfsn:" + mSerialNo;
  }
  return s;
};



string WbfVdc::vendorName()
{
  return "Feller";
}



// MARK: - DB and initialisation

// Version history
//  1 : first version
#define WBF_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define WBF_SCHEMA_VERSION 1 // current version

string WbfPersistence::schemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create table group from scratch
		// - use standard globs table for schema version
    sql = inherited::schemaUpgradeSQL(aFromVersion, aToVersion);
		// - add fields to globs table
    sql.append(
      "ALTER TABLE $PREFIX_globs ADD fixedHost TEXT;"
      "ALTER TABLE $PREFIX_globs ADD dnssdHost TEXT;"
      "ALTER TABLE $PREFIX_globs ADD apisecret TEXT;"
    );
    // reached final version in one step
    aToVersion = WBF_SCHEMA_VERSION;
  }
  return sql;
}


#define WBF_RECOLLECT_INTERVAL (24*Hour)

void WbfVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // load persistent params for dSUID
  load();
  // load private data
  ErrorPtr err = initializePersistence(mDb, WBF_SCHEMA_VERSION, WBF_SCHEMA_MIN_VERSION);
  if (!getVdcFlag(vdcflag_flagsinitialized)) setVdcFlag(vdcflag_hidewhenempty, true); // hide by default
  aCompletedCB(err); // return status of DB init
  // schedule rescans
  setPeriodicRecollection(WBF_RECOLLECT_INTERVAL, rescanmode_incremental);
}



// MARK: - collect devices


int WbfVdc::getRescanModes() const
{
  // all modes make sense, exhaustive forces discovery instead of using cached API URL
  return rescanmode_incremental+rescanmode_normal+rescanmode_exhaustive;
}


void WbfVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  if (!(aRescanFlags & rescanmode_incremental)) {
    // full collect, remove all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
  }
  // load hue bridge uuid and token
  SQLiteTGQuery qry(mDb);
  if (Error::isOK(qry.prefixedPrepare("SELECT fixedHost, dnssdHost, apisecret FROM $PREFIX_globs"))) {
    sqlite3pp::query::iterator i = qry.begin();
    if (i!=qry.end()) {
      mWbfComm.mFixedHostName = nonNullCStr(i->get<const char *>(0));
      mWbfComm.mDNSSDHostName = nonNullCStr(i->get<const char *>(1));
      mWbfComm.mApiSecret = nonNullCStr(i->get<const char *>(2));
    }
  }
  if (mWbfComm.mFixedHostName.size()>0 || mWbfComm.mDNSSDHostName.size()>0) {
    // we know a gateway by direct API address or DNSSD host name
    connectGateway(aCompletedCB);
  }
  else {
    // no bridge known, can't collect anything at this time
    if (aCompletedCB) aCompletedCB(ErrorPtr());
  }
}


void WbfVdc::handleGlobalEvent(VdchostEvent aEvent)
{
  if (aEvent==vdchost_network_reconnected) {
    // re-connecting to network should re-scan for gateway
    collectDevices(NoOP, rescanmode_incremental);
  }
  inherited::handleGlobalEvent(aEvent);
}



#define REFIND_RETRY_DELAY (30*Second)

void WbfVdc::connectGateway(StatusCB aCompletedCB)
{
  if (!getVdcHost().isNetworkConnected()) {
    OLOG(LOG_WARNING, "device has no IP yet -> must wait");
    mRefindTicket.executeOnce(boost::bind(&WbfVdc::connectGateway, this, aCompletedCB), REFIND_RETRY_DELAY);
    return;
  }
  // actually refind
  mWbfComm.refindGateway(boost::bind(&WbfVdc::refindResultHandler, this, aCompletedCB, _1));
}



void WbfVdc::refindResultHandler(StatusCB aCompletedCB, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // found already paired gateway again
    OLOG(LOG_INFO, "wbf gateway named '%s%s' found again @ %s", mWbfComm.mFixedHostName.c_str(), mWbfComm.mDNSSDHostName.c_str(), mWbfComm.mResolvedHost.c_str());
    startupGatewayApi(aCompletedCB);
  }
  else {
    // not found (usually timeout)
    OLOG(LOG_WARNING, "Error refinding gateway '%s', error = %s", mWbfComm.mDNSSDHostName.c_str(), aError->text());
    if (aCompletedCB) aCompletedCB(ErrorPtr()); // no gateway (but this is not a collect error)
  }
}



void WbfVdc::startupGatewayApi(StatusCB aCompletedCB)
{
  // make sure it is not already up
  mWbfComm.stopApi(boost::bind(&WbfVdc::apiIsStopped, this, aCompletedCB));
}


void WbfVdc::apiIsStopped(StatusCB aCompletedCB)
{
  // set auth header with secret we should have by now, start websocket
  mWbfComm.startupApi(
    boost::bind(&WbfVdc::gatewayWebsocketHandler, this, _1, _2),
    boost::bind(&WbfVdc::apiIsStarted, this, aCompletedCB, _1)
  );
}


void WbfVdc::apiIsStarted(StatusCB aCompletedCB, ErrorPtr aError)
{
  // API host address or fixed name is known now, query the basics
  if (Error::isOK(aError)) {
    mWbfComm.apiAction(WbfApiOperation::GET, "/info", nullptr, boost::bind(&WbfVdc::gatewayInfoHandler, this, aCompletedCB, _1, _2));
    return;
  }
  if (aCompletedCB) aCompletedCB(aError);
}


void WbfVdc::gatewayInfoHandler(StatusCB aCompletedCB, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (aResult) {
    JsonObjectPtr o;
    if (aResult->get("sn", o)) mSerialNo = o->stringValue();
    if (aResult->get("api", o)) mApiVersion = o->stringValue();
    if (aResult->get("sw", o)) mSwVersion = o->stringValue();
    OLOG(LOG_INFO, "gateway serial: %s, api version: %s, firmware version: %s", mSerialNo.c_str(), mApiVersion.c_str(), mSwVersion.c_str());
  }
  if (Error::isOK(aError)) {
    queryDevices(aCompletedCB);
    return;
  }
  if (aCompletedCB) aCompletedCB(aError);
}


void WbfVdc::gatewayWebsocketHandler(const string aMessage, ErrorPtr aError)
{
  DBGOLOG(LOG_INFO, "websocket: error: %s, message: %s", Error::text(aError), aMessage.c_str());
  if (Error::notOK(aError)) {
    // TODO: maybe re-establish websocket
    // for now, just ignore
    return;
  }
  JsonObjectPtr msg = JsonObject::objFromText(aMessage.c_str());
  if (!msg) return; // ignore
  OLOG(LOG_INFO, "websocket json message: %s", JsonObject::text(msg));
  JsonObjectPtr o;
  JsonObjectPtr part;
  if (msg->get("sensor", part)) {
    if (part->get("id", o)) {
      // Note: sensor has no nested state or cmd
      PartIdToBehaviourMap::iterator pos = mSensorsMap.find(o->int32Value());
      if (pos!=mSensorsMap.end()) {
        WbfDevice& dev = static_cast<WbfDevice&>(pos->second->getDevice());
        dev.handleSensorState(part, pos->second);
      }
    }
  }
  if (msg->get("button", part)) {
    if (part->get("id", o)) {
      part = part->get("cmd"); // unpack the cmd
      PartIdToBehaviourMap::iterator pos = mButtonsMap.find(o->int32Value());
      if (pos!=mButtonsMap.end()) {
        WbfDevice& dev = static_cast<WbfDevice&>(pos->second->getDevice());
        dev.handleButtonCmd(part, pos->second);
      }
    }
  }
  else if (msg->get("load", part)) {
    if (part->get("id", o)) {
      part = part->get("state"); // unpack the state
      if (part) {
        PartIdToBehaviourMap::iterator pos = mLoadsMap.find(o->int32Value());
        if (pos!=mLoadsMap.end()) {
          WbfDevice& dev = static_cast<WbfDevice&>(pos->second->getDevice());
          dev.handleLoadState(part, pos->second);
          dev.getOutput()->reportOutputState();
        }
      }
    }
  }
  else if (msg->get("findme", part)) {
    // {"findme":{"button":213}}
    // {"findme":{"button":{"channel":1,"device":"00014929"}}}
    if (part->get("button", o)) {
      requestButtonActivation(o);
    }
  }
}





#define WBF_BUTTONACTIVATION_DEFAULT_MINS 2 // Minutes

ErrorPtr WbfVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="registerWbfGateway") {
    // hue specific addition, only via genericRequest
    ApiValuePtr a = aParams->get("gatewayHost");
    if (a) {
      // needs new pairing, forget current devices
      removeDevices(false);
      // register by fixed gateway host address
      mWbfComm.mFixedHostName = a->stringValue();
      mWbfComm.mApiSecret.clear();
      mWbfComm.mDNSSDHostName.clear();
      respErr = Error::ok(mDb.prefixedExecute(
        "UPDATE $PREFIX_globs SET fixedHost='%q', dnssdHost='', apisecret=''",
        mWbfComm.mFixedHostName.c_str()
      ));
    }
    else {
      // register by dnssdhost/secret (for migration)
      respErr = checkStringParam(aParams, "dnssdName", mWbfComm.mDNSSDHostName);
      if (Error::notOK(respErr)) return respErr;
      respErr = checkStringParam(aParams, "secret", mWbfComm.mApiSecret);
      if (Error::notOK(respErr)) return respErr;
      // save the bridge parameters
      respErr = Error::ok(mDb.prefixedExecute(
        "UPDATE $PREFIX_globs SET fixedHost='', dnssdHost='%q', apisecret='%q'",
        mWbfComm.mDNSSDHostName.c_str(),
        mWbfComm.mApiSecret.c_str()
      ));
      if (Error::isOK(respErr)) {
        // now collect from the new gateway bridge, remove all settings from previous gateway
        collectDevices(boost::bind(&DsAddressable::methodCompleted, this, aRequest, _1), rescanmode_clearsettings);
      }
    }
  }
  else if (aMethod=="wbfapicall") {
    // direct wbf API call
    ApiValuePtr a = aParams->get("websocketmsg");
    if (a) {
      JsonObjectPtr msg = JsonApiValue::getAsJson(a);
      mWbfComm.sendWebSocketJsonMsg(msg);
      return Error::ok();
    }
    string method;
    respErr = checkStringParam(aParams, "httpmethod", method);
    if (Error::notOK(respErr)) return respErr;
    WbfApiOperation::HttpMethods m;
    if (uequals(method, "POST")) m = WbfApiOperation::POST;
    else if (uequals(method, "PUT")) m = WbfApiOperation::PUT;
    else if (uequals(method, "PATCH")) m = WbfApiOperation::PATCH;
    else if (uequals(method, "DELETE")) m = WbfApiOperation::DELETE;
    else m = WbfApiOperation::GET;
    string endpoint;
    respErr = checkStringParam(aParams, "endpoint", endpoint);
    if (Error::notOK(respErr)) return respErr;
    JsonObjectPtr request;
    a = aParams->get("request");
    if (a) request = JsonApiValue::getAsJson(a); // request data
    mWbfComm.apiAction(m, endpoint.c_str(), request, boost::bind(&WbfVdc::wbfapicallResponse, this, aRequest, _1, _2));
  }
  else if (aMethod=="buttonActivation") {
    bool turnOn;
    respErr = checkBoolParam(aParams, "on", turnOn);
    if (Error::isOK(respErr)) {
      if (!turnOn) {
        endButtonActivation();
        return Error::ok();
      }
      int minutes = WBF_BUTTONACTIVATION_DEFAULT_MINS;
      ApiValuePtr a = aParams->get("minutes");
      if (a) minutes = a->int32Value();
      JsonObjectPtr data = JsonObject::newObj();
      data->add("on", JsonObject::newBool(turnOn));
      data->add("color", JsonObject::newString("#FFCC00"));
      data->add("time", JsonObject::newInt32(minutes));
      mWbfComm.apiAction(WbfApiOperation::PUT, "/buttons/findme", data, boost::bind(&WbfVdc::buttonActivationStarted, this, _1, _2));
      // auto-terminate
      mButtonActivationRequest = aRequest;
      mButtonActivationTimeout.executeOnce(boost::bind(&WbfVdc::endButtonActivation, this), minutes*Minute);
    }
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}


void WbfVdc::wbfapicallResponse(VdcApiRequestPtr aRequest, JsonObjectPtr aResult, ErrorPtr aError)
{
  ApiValuePtr v = aRequest->newApiValue();
  JsonApiValue::setAsJson(v, aResult);
  if (Error::isOK(aError)) aRequest->sendResult(v);
  else aRequest->sendError(aError);
}


void WbfVdc::buttonActivationStarted(JsonObjectPtr aResult, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // mButtonActivationRequest set signals ongoing activation
    OLOG(LOG_NOTICE, "started button activation");
  }
  else if (mButtonActivationRequest) {
    mButtonActivationRequest->sendError(aError);
    mButtonActivationRequest.reset();
  }
}


void WbfVdc::endButtonActivation()
{
  mButtonActivationTimeout.cancel();
  OLOG(LOG_NOTICE, "ending button activation");
  if (mButtonActivationRequest) {
    mButtonActivationRequest->sendError(TextError::err("no button activation performed"));
    mButtonActivationRequest.reset();
  }
  mWbfComm.apiAction(WbfApiOperation::PUT, "/buttons/findme", JsonObject::newBool(false)->wrapAs("on"), NoOP);
}


void WbfVdc::requestButtonActivation(JsonObjectPtr aButtonInfo)
{
  if (mButtonActivationRequest) {
    // we are actually waiting for a button activation
    if (aButtonInfo->isType(json_type_int)) {
      // {"findme":{"button":213}}
      // button is already activated
      mButtonActivationRequest->sendError(TextError::err("button is already activated"));
      mButtonActivationRequest.reset();
    }
    else {
      // button does not yet have an ID -> activate it
      // {"findme":{"button":{"channel":1,"device":"00014929"}}}
      mWbfComm.apiAction(WbfApiOperation::POST, "/smartbuttons", aButtonInfo, boost::bind(&WbfVdc::buttonActivated, this, _1, _2));
    }
  }
}


void WbfVdc::buttonActivated(JsonObjectPtr aResult, ErrorPtr aError)
{
  if (mButtonActivationRequest) {
    OLOG(LOG_INFO, "button activation result: %s", JsonObject::text(aResult));
    // report the status
    mButtonActivationRequest->sendStatus(aError);
    mButtonActivationRequest.reset();
    // stop activation in Wiser
    endButtonActivation();
  }
}


void WbfVdc::activatedAndRescanned(ErrorPtr aError)
{
  if (mButtonActivationRequest) {
    OLOG(LOG_INFO, "devices rescanned, status: %s", Error::text(aError));
    mButtonActivationRequest->sendStatus(aError);
    mButtonActivationRequest.reset();
  }
}




void WbfVdc::setLearnMode(bool aEnableLearning, bool aDisableProximityCheck, Tristate aOnlyEstablish)
{
  if (aEnableLearning) {
    mWbfComm.pairGateway(boost::bind(&WbfVdc::pairResultHandler, this, aOnlyEstablish, !mWbfComm.mApiSecret.empty(), _1));
  }
  else {
    // stop learning
    mWbfComm.stopPairing();
  }
}


void WbfVdc::pairResultHandler(Tristate aOnlyEstablish, bool aWasPaired, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // found and authenticated bridge
    OLOG(LOG_INFO, "wbf gateway '%s%s' found @ %s and paired ok", mWbfComm.mFixedHostName.c_str(), mWbfComm.mDNSSDHostName.c_str(), mWbfComm.mResolvedHost.c_str());
    // check if we found the already learned-in bridge
    Tristate learnedIn = undefined;
    if (aWasPaired) {
      // we were paired before
      if (aOnlyEstablish!=yes) {
        learnedIn = no;
        // - delete it from the whitelist
        mWbfComm.apiAction(WbfApiOperation::DELETE, "/account", nullptr, NoOP);
        // - forget uuid + user name
        mWbfComm.mDNSSDHostName.clear();
        mWbfComm.mApiSecret.clear();
        mWbfComm.mResolvedHost.clear();
      }
    }
    else {
      // new bridge found
      if (aOnlyEstablish!=no) {
        learnedIn = yes;
      }
    }
    if (learnedIn!=undefined) {
      // learning in or out requires all devices to be removed first
      // (on learn-in, the gateway's devices will be added afterwards)
      removeDevices(false);
      // actual learn-in or -out has happened
      aError = mDb.prefixedExecute("UPDATE $PREFIX_globs SET dnssdHost='%q', apisecret='%q'",
        mWbfComm.mDNSSDHostName.c_str(),
        mWbfComm.mApiSecret.c_str()
      );
      if (Error::notOK(aError)) {
        OLOG(LOG_ERR, "Error saving pairing params: %s", aError->text());
      }
      // now process the learn in/out
      if (learnedIn==yes) {
        // now connect to the gateway API and enumerate device
        connectGateway(boost::bind(&WbfVdc::learnedInComplete, this, _1));
        return;
      }
      // report successful learn event
      getVdcHost().reportLearnEvent(learnedIn==yes, ErrorPtr());
    }
  }
  else {
    // not found (usually timeout)
    OLOG(LOG_NOTICE, "No wbs gateway found to register, error = %s", aError->text());
  }
}


void WbfVdc::learnedInComplete(ErrorPtr aError)
{
  getVdcHost().reportLearnEvent(true, aError);
}


// Note from uGateway docs: /api/devices/*: This service takes very long time at the first call!
//   Approx. 1 second per device. So with 60 devices it takes 1 minute.
#define WBFAPI_DEVICETREE_TIMEOUT (150*Second)

void WbfVdc::queryDevices(StatusCB aCompletedCB)
{
  mWbfComm.apiQuery("/devices/*", boost::bind(&WbfVdc::devicesListHandler, this, aCompletedCB, _1, _2), WBFAPI_DEVICETREE_TIMEOUT);
}


void WbfVdc::devicesListHandler(StatusCB aCompletedCB, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    mWbfComm.apiQuery("/loads", boost::bind(&WbfVdc::loadsListHandler, this, aCompletedCB, aResult, _1, _2));
    return;
  }
  if (aCompletedCB) aCompletedCB(aError);
}


void WbfVdc::loadsListHandler(StatusCB aCompletedCB, JsonObjectPtr aDevicesArray, JsonObjectPtr aLoadsArray, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    mWbfComm.apiQuery("/loads/state", boost::bind(&WbfVdc::loadsStateHandler, this, aCompletedCB, aDevicesArray, aLoadsArray, _1, _2));
    return;
  }
  if (aCompletedCB) aCompletedCB(aError);
}



void WbfVdc::loadsStateHandler(StatusCB aCompletedCB, JsonObjectPtr aDevicesArray, JsonObjectPtr aLoadsArray, JsonObjectPtr aStatesArray, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    mWbfComm.apiQuery("/sensors", boost::bind(&WbfVdc::sensorsListHandler, this, aCompletedCB, aDevicesArray, aLoadsArray, aStatesArray, _1, _2));
    return;
  }
  if (aCompletedCB) aCompletedCB(aError);
}


void WbfVdc::sensorsListHandler(StatusCB aCompletedCB, JsonObjectPtr aDevicesArray, JsonObjectPtr aLoadsArray, JsonObjectPtr aStatesArray, JsonObjectPtr aSensorsArray, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    mWbfComm.apiQuery("/buttons", boost::bind(&WbfVdc::buttonsListHandler, this, aCompletedCB, aDevicesArray, aLoadsArray, aStatesArray, aSensorsArray, _1, _2));
    return;
  }
  if (aCompletedCB) aCompletedCB(aError);
}


void WbfVdc::buttonsListHandler(StatusCB aCompletedCB, JsonObjectPtr aDevicesArray, JsonObjectPtr aLoadsArray, JsonObjectPtr aStatesArray, JsonObjectPtr aSensorsArray, JsonObjectPtr aButtonsArray, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    // could not get buttons list
    OLOG(LOG_WARNING, "Could not get button list, needs uGateway Firmware >= 6.0.35: %s", aError->text());
  }
  // now process the lists
  for(int didx = 0; didx<aDevicesArray->arrayLength(); didx++) {
    JsonObjectPtr devDesc = aDevicesArray->arrayGet(didx);
    JsonObjectPtr o;
    if (!devDesc->get("id", o)) continue; // cannot process device w/o id
    string wbfId = o->stringValue();
    int subDeviceIndex = 0;
    // process the inputs (sensors, buttons) first, will be passed to devices to pick from later
    JsonObjectPtr inpArr;
    if (devDesc->get("inputs", inpArr)) {
      for (int iidx = 0; iidx<inpArr->arrayLength(); iidx++) {
        JsonObjectPtr inpDesc = inpArr->arrayGet(iidx);
        if (inpDesc->get("sensor", o)) {
          // find the sensor
          long sensorId = o->int32Value();
          for (int sidx = 0; sidx<aSensorsArray->arrayLength(); sidx++) {
            JsonObjectPtr sensorDesc = aSensorsArray->arrayGet(sidx);
            if (sensorDesc->get("id", o)) {
              if (o->int32Value()==sensorId) {
                // found the corresponding sensor, add it to the input description
                inpDesc->add("sensor_info", sensorDesc);
                break;
              }
            }
          }
        }
        // Note: as of 6.0.35, buttons do not have a "button":id entry in inputs[],
        //   need to search in reverse by matching device "id" and "channel"
        if (aButtonsArray && inpDesc->get("type", o) && o->stringValue()=="button") {
          // this input index (channel) describes a button
          // find the button
          for (int bidx = 0; bidx<aButtonsArray->arrayLength(); bidx++) {
            JsonObjectPtr buttonDesc = aButtonsArray->arrayGet(bidx);
            if (buttonDesc->get("device", o) && o->stringValue()==wbfId) {
              // is a button in the current device, check channel
              if (buttonDesc->get("channel", o) && o->int32Value()==iidx) {
                // found the corresponding button, add it to the input description
                inpDesc->add("button_info", buttonDesc);
                break;
              }
            }
          }
        }
      }
    }
    // each output creates a separate device
    // device decides which and how many inputs to consume
    int inputsUsed;
    JsonObjectPtr outArr;
    if (devDesc->get("outputs", outArr)) {
      int numOutputs = outArr->arrayLength();
      if (numOutputs==0) {
        // only inputs, create device(s) for it
        while (inpArr->arrayLength()>0) {
          WbfDevicePtr newDev = new WbfDevice(this, subDeviceIndex, devDesc, nullptr, inpArr, inputsUsed);
          if (inputsUsed==0) break; // do not add devices w/o any input
          addWbfDevice(newDev);
          subDeviceIndex++;
        }
      }
      else {
        for (int oidx = 0; oidx<numOutputs; oidx++) {
          JsonObjectPtr outDesc = outArr->arrayGet(oidx);
          // find the load
          JsonObjectPtr o;
          if (!outDesc->get("load", o)) continue; // ignore outputs w/o load
          long loadId = o->int32Value();
          bool foundLoad = false;
          for (int lidx = 0; lidx<aLoadsArray->arrayLength(); lidx++) {
            JsonObjectPtr loadDesc = aLoadsArray->arrayGet(lidx);
            if (loadDesc->get("id", o)) {
              if (o->int32Value()==loadId) {
                // found the corresponding load
                // - find the corresponding state
                for (int lsidx = 0; lsidx<aStatesArray->arrayLength(); lsidx++) {
                  JsonObjectPtr loadState = aStatesArray->arrayGet(lidx);
                  if (loadState->get("id", o)) {
                    long stateId = o->int32Value();
                    if (stateId==loadId) {
                      // add load state to the load descriptor
                      loadDesc->add("state", loadState->get("state"));
                      break;
                    }
                  }
                }
                // add load descriptor to the output description
                outDesc->add("load_info", loadDesc);
                foundLoad = true;
                break;
              }
            }
          }
          if (!foundLoad) continue; // no load, ignore output
          // create one device per output
          // Device can pick inputs from inpArr, and must delete those it picks!
          // more devices w/o output are created for additional inputs
          do {
            WbfDevicePtr newDev = new WbfDevice(this, subDeviceIndex, devDesc, outDesc, inpArr, inputsUsed);
            if (inputsUsed==0 && !outDesc) break; // no more mappable inputs, input-only device -> do not add it
            addWbfDevice(newDev);
            outDesc.reset(); // forget the output, it is consumed
            subDeviceIndex++;
          } while (oidx+1>=numOutputs); // do not repeat (but let the next output pick inputs) when we're not on the last output
        } // for all outputs
      } // device(s) with output(s)
    } // output processing
    // report unused inputs
    for (int i=0; i<inpArr->arrayLength(); i++) {
      OLOG(LOG_INFO, "- Unmapped input: %s", JsonObject::text(inpArr->arrayGet(i)));
    }
  } // for all devices
  // now that all devices are set up, trigger a complete state update on the websocket
  mWbfComm.sendWebSocketTextMsg("{ \"command\": \"dump_loads\" }");
  mWbfComm.sendWebSocketTextMsg("{ \"command\": \"dump_sensors\" }");
  if (aCompletedCB) aCompletedCB(ErrorPtr());
}


bool WbfVdc::addWbfDevice(WbfDevicePtr aNewDev)
{
  if (simpleIdentifyAndAddDevice(aNewDev)) {
    // actually added, no duplicate
    return true; // added
  }
  return false; // not actually added
}


void WbfVdc::removeDevice(DevicePtr aDevice, bool aForget)
{
  WbfDevicePtr dev = boost::dynamic_pointer_cast<WbfDevice>(aDevice);
  if (dev) {
    // - remove device
    inherited::removeDevice(aDevice, aForget);
  }
}


void WbfVdc::unregisterBehaviourMap(PartIdToBehaviourMap &aMap, DsBehaviourPtr aBehaviour)
{
  for (PartIdToBehaviourMap::iterator pos = aMap.begin(); pos!=aMap.end(); ++pos) {
    if (pos->second==aBehaviour) {
      aMap.erase(pos);
      return;
    }
  }
}




#endif // ENABLE_WBF
