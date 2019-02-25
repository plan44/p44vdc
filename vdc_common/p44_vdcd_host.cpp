//
//  Copyright (c) 2014-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of vdcd.
//
//  vdcd is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  vdcd is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with vdcd. If not, see <http://www.gnu.org/licenses/>.
//

#include "p44_vdcd_host.hpp"

#include "vdc.hpp"
#include "device.hpp"

#include "jsonvdcapi.hpp"

#include "macaddress.hpp"


using namespace p44;

// MARK: ===== config API - P44JsonApiConnection


P44JsonApiConnection::P44JsonApiConnection()
{
  setApiVersion(VDC_API_VERSION_MAX);
}


ApiValuePtr P44JsonApiConnection::newApiValue()
{
  return ApiValuePtr(new JsonApiValue);
}





// MARK: ===== config API - P44JsonApiRequest


P44JsonApiRequest::P44JsonApiRequest(JsonCommPtr aJsonComm)
{
  jsonComm = aJsonComm;
}


VdcApiConnectionPtr P44JsonApiRequest::connection()
{
  return VdcApiConnectionPtr(new P44JsonApiConnection());
}




ErrorPtr P44JsonApiRequest::sendResult(ApiValuePtr aResult)
{
  LOG(LOG_DEBUG, "cfg <- vdcd (JSON) result sent: result=%s", aResult ? aResult->description().c_str() : "<none>");
  JsonApiValuePtr result = boost::dynamic_pointer_cast<JsonApiValue>(aResult);
  if (result) {
    P44VdcHost::sendCfgApiResponse(jsonComm, result->jsonObject(), ErrorPtr());
  }
  else {
    // always return SOMETHING
    P44VdcHost::sendCfgApiResponse(jsonComm, JsonObject::newNull(), ErrorPtr());
  }
  return ErrorPtr();
}



ErrorPtr P44JsonApiRequest::sendError(uint32_t aErrorCode, string aErrorMessage, ApiValuePtr aErrorData, VdcErrorType aErrorType, string aUserFacingMessage)
{
  ErrorPtr err;
  LOG(LOG_DEBUG, "cfg <- vdcd (JSON) error sent: error=%d (%s)", aErrorCode, aErrorMessage.c_str());
  if (aErrorType!=0 || !aUserFacingMessage.empty()) {
    err = VdcApiErrorPtr(new VdcApiError(aErrorCode, aErrorMessage, aErrorType, aUserFacingMessage));
  }
  else {
    err = ErrorPtr(new Error(aErrorCode, aErrorMessage)); // re-pack into error object
  }
  P44VdcHost::sendCfgApiResponse(jsonComm, JsonObjectPtr(), err);
  return ErrorPtr();
}


// MARK: ===== self test runner

#if SELFTESTING_ENABLED

class SelfTestRunner
{
  StatusCB completedCB;
  VdcMap::iterator nextVdc;
  VdcHost &vdcHost;
  ButtonInputPtr button;
  IndicatorOutputPtr redLED;
  IndicatorOutputPtr greenLED;
  MLTicket errorReportTicket;
  ErrorPtr globalError;
  int realTests;
public:
  static void initialize(VdcHost &aVdcHost, StatusCB aCompletedCB, ButtonInputPtr aButton, IndicatorOutputPtr aRedLED, IndicatorOutputPtr aGreenLED)
  {
    // create new instance, deletes itself when finished
    new SelfTestRunner(aVdcHost, aCompletedCB, aButton, aRedLED, aGreenLED);
  };
private:
  SelfTestRunner(VdcHost &aVdcHost, StatusCB aCompletedCB, ButtonInputPtr aButton, IndicatorOutputPtr aRedLED, IndicatorOutputPtr aGreenLED) :
    completedCB(aCompletedCB),
    vdcHost(aVdcHost),
    button(aButton),
    redLED(aRedLED),
    greenLED(aGreenLED),
    realTests(0)
  {
    // start testing
    nextVdc = vdcHost.vdcs.begin();
    testNextVdc();
  }


  void testNextVdc()
  {
    if (nextVdc!=vdcHost.vdcs.end()) {
      // ok, test next
      // - start green/yellow blinking = test in progress
      greenLED->steadyOn();
      redLED->blinkFor(Infinite, 600*MilliSecond, 50);
      // - check for init errors
      ErrorPtr vdcErr = nextVdc->second->getVdcStatus();
      if (Error::isOK(vdcErr)) {
        // - run the test
        LOG(LOG_WARNING, "Starting Test of %s (Tag=%d, %s)", nextVdc->second->vdcClassIdentifier(), nextVdc->second->getTag(), nextVdc->second->shortDesc().c_str());
        nextVdc->second->selfTest(boost::bind(&SelfTestRunner::vdcTested, this, _1));
      }
      else {
        // - vdc is already in error -> can't run the test, report the initialisation error (vdc status)
        vdcTested(vdcErr);
      }
    }
    else {
      if (realTests==0) {
        // no real tests performed
        globalError = Error::err<VdcError>(VdcError::NoHWTested, "self test had nothing to actually test (no HW tests performed)");
      }
      testCompleted(); // done
    }
  }


  void vdcTested(ErrorPtr aError)
  {
    if (!Error::isOK(aError)) {
      if (!aError->isError("Vdc", VdcError::NoHWTested)) {
        // test failed
        LOG(LOG_ERR, "****** Test of '%s' FAILED with error: %s", nextVdc->second->vdcClassIdentifier(), aError->description().c_str());
        // remember
        globalError = aError;
        // morse out tag number of vDC failing self test until button is pressed
        greenLED->steadyOff();
        int numBlinks = nextVdc->second->getTag();
        redLED->blinkFor(300*MilliSecond*numBlinks, 300*MilliSecond, 50);
        // call myself again later
        errorReportTicket.executeOnce(boost::bind(&SelfTestRunner::vdcTested, this, aError), 300*MilliSecond*numBlinks+2*Second);
        // also install button responder
        button->setButtonHandler(boost::bind(&SelfTestRunner::errorAcknowledged, this), false); // report only release
        return; // done for now
      }
    }
    else {
      // real test ok
      realTests++;
    }
    // test was ok
    LOG(LOG_ERR, "------ Test of '%s' OK", nextVdc->second->vdcClassIdentifier());
    // check next
    ++nextVdc;
    testNextVdc();
  }


  void errorAcknowledged()
  {
    // stop error morse
    redLED->steadyOff();
    greenLED->steadyOff();
    errorReportTicket.cancel();
    // test next (if any)
    ++nextVdc;
    testNextVdc();
  }


  void testCompleted()
  {
    if (Error::isOK(globalError)) {
      LOG(LOG_ERR, "Self test OK");
      redLED->steadyOff();
      greenLED->blinkFor(Infinite, 500, 85); // slow green blinking = good
    }
    else  {
      LOG(LOG_ERR, "Self test has FAILED: %s", globalError->description().c_str());
      greenLED->steadyOff();
      redLED->blinkFor(Infinite, 250, 60); // faster red blinking = not good
    }
    // callback, report last error seen
    completedCB(globalError);
    // done, delete myself
    delete this;
  }

};

#endif // SELFTESTING_ENABLED


// MARK: ===== P44VdcHost


P44VdcHost::P44VdcHost(bool aWithLocalController) :
  inherited(aWithLocalController),
  webUiPort(0)
{
}


void P44VdcHost::selfTest(StatusCB aCompletedCB, ButtonInputPtr aButton, IndicatorOutputPtr aRedLED, IndicatorOutputPtr aGreenLED)
{
  #if SELFTESTING_ENABLED
  SelfTestRunner::initialize(*this, aCompletedCB, aButton, aRedLED, aGreenLED);
  #else
  aCompletedCB(TextError::err("Fatal: Testing is not included in this build"));
  #endif
}



string P44VdcHost::webuiURLString()
{
  if (webUiPort)
    return string_format("http://%s:%d%s", ipv4ToString(getIpV4Address()).c_str(), webUiPort, webUiPath.c_str());
  else
    return inherited::webuiURLString();
}


void P44VdcHost::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // start config API, if we have one
  if (configApiServer) {
    configApiServer->startServer(boost::bind(&P44VdcHost::configApiConnectionHandler, this, _1), 3);
  }
  // now init rest of vdc host
  inherited::initialize(aCompletedCB, aFactoryReset);
}


void P44VdcHost::enableConfigApi(const char *aServiceOrPort, bool aNonLocalAllowed)
{
  if (!configApiServer) {
    // can be enabled only once
    configApiServer = SocketCommPtr(new SocketComm(MainLoop::currentMainLoop()));
    configApiServer->setConnectionParams(NULL, aServiceOrPort, SOCK_STREAM, AF_INET);
    configApiServer->setAllowNonlocalConnections(aNonLocalAllowed);
  }
}



SocketCommPtr P44VdcHost::configApiConnectionHandler(SocketCommPtr aServerSocketCommP)
{
  JsonCommPtr conn = JsonCommPtr(new JsonComm(MainLoop::currentMainLoop()));
  conn->setMessageHandler(boost::bind(&P44VdcHost::configApiRequestHandler, this, conn, _1, _2));
  conn->setClearHandlersAtClose(); // close must break retain cycles so this object won't cause a mem leak
  return conn;
}


void P44VdcHost::configApiRequestHandler(JsonCommPtr aJsonComm, ErrorPtr aError, JsonObjectPtr aJsonObject)
{
  ErrorPtr err;
  // when coming from mg44, requests have the following form
  // - for GET requests like http://localhost:8080/api/json/myuri?foo=bar&this=that
  //   {"method":"GET","uri":"myuri","uri_params":{"foo":"bar","this":"that"}}
  // - for POST requests like
  //   curl "http://localhost:8080/api/json/myuri?foo=bar&this=that" --data-ascii "{ \"content\":\"data\", \"important\":false }"
  //   {"method":"POST","uri":"myuri","uri_params":{"foo":"bar","this":"that"},"data":{"content":"data","important":false}}
  //   curl "http://localhost:8080/api/json/myuri" --data-ascii "{ \"content\":\"data\", \"important\":false }"
  //   {"method":"POST","uri":"myuri","data":{"content":"data","important":false}}
  // processing:
  // - a JSON request must be either specified in the URL or in the POST data, not both
  // - if POST data ("data" member in the incoming request) is present, "uri_params" is ignored
  // - "uri" selects one of possibly multiple APIs
  if (Error::isOK(aError)) {
    // not JSON level error, try to process
    LOG(LOG_DEBUG, "cfg -> vdcd (JSON) request received: %s", aJsonObject->c_strValue());
    // find out which one is our actual JSON request
    // - try POST data first
    JsonObjectPtr request = aJsonObject->get("data");
    if (!request) {
      // no POST data, try uri_params
      request = aJsonObject->get("uri_params");
    }
    if (!request) {
      // empty query, that's an error
      aError = Error::err<P44VdcError>(415, "empty request");
    }
    else {
      // have the request processed
      string apiselector;
      JsonObjectPtr uri = aJsonObject->get("uri");
      if (uri) apiselector = uri->stringValue();
      // dispatch according to API
      if (apiselector=="vdc") {
        // process request that basically is a vdc API request, but as simple webbish JSON, not as JSON-RPC 2.0
        // and without the need to start a vdc session
        // Notes:
        // - if dSUID is specified invalid or empty, the vdc host itself is addressed.
        // - use x-p44-vdcs and x-p44-devices properties to find dsuids
        aError = processVdcRequest(aJsonComm, request);
      }
      else if (apiselector=="p44") {
        // process p44 specific requests
        aError = processP44Request(aJsonComm, request);
      }
      else {
        // unknown API selector
        aError = Error::err<P44VdcError>(400, "invalid URI, unknown API");
      }
    }
  }
  // if error or explicit OK, send response now. Otherwise, request processing will create and send the response
  if (aError) {
    sendCfgApiResponse(aJsonComm, JsonObjectPtr(), aError);
  }
}


void P44VdcHost::sendCfgApiResponse(JsonCommPtr aJsonComm, JsonObjectPtr aResult, ErrorPtr aError)
{
  // create response
  JsonObjectPtr response = JsonObject::newObj();
  if (!Error::isOK(aError)) {
    // error, return error response
    response->add("error", JsonObject::newInt32((int32_t)aError->getErrorCode()));
    response->add("errormessage", JsonObject::newString(aError->getErrorMessage()));
    response->add("errordomain", JsonObject::newString(aError->getErrorDomain()));
    VdcApiErrorPtr ve = boost::dynamic_pointer_cast<VdcApiError>(aError);
    if (ve) {
      response->add("errortype", JsonObject::newInt32(ve->getErrorType()));
      response->add("userfacingmessage", JsonObject::newString(ve->getUserFacingMessage()));
    }
  }
  else {
    // no error, return result (if any)
    response->add("result", aResult);
  }
  LOG(LOG_DEBUG, "Config API response: %s", response->c_strValue());
  aJsonComm->sendMessage(response);
}


// access to vdc API methods and notifications via web requests
ErrorPtr P44VdcHost::processVdcRequest(JsonCommPtr aJsonComm, JsonObjectPtr aRequest)
{
  ErrorPtr err;
  string cmd;
  bool isMethod = false;
  // get method/notification and params
  JsonObjectPtr m = aRequest->get("method");
  if (m) {
    // is a method call, expects answer
    isMethod = true;
  }
  else {
    // not method, may be notification
    m = aRequest->get("notification");
  }
  if (!m) {
    err = Error::err<P44VdcError>(400, "invalid request, must specify 'method' or 'notification'");
  }
  else {
    // get method/notification name
    cmd = m->stringValue();
    // get params
    // Note: the "method" or "notification" param will also be in the params, but should not cause any problem
    ApiValuePtr params = JsonApiValue::newValueFromJson(aRequest);
    P44JsonApiRequestPtr request = P44JsonApiRequestPtr(new P44JsonApiRequest(aJsonComm));
    if (Error::isOK(err)) {
      // operation method
      if (isMethod) {
        // create request
        // check for old-style name/index and generate basic query (1 or 2 levels)
        ApiValuePtr query = params->newObject();
        ApiValuePtr name = params->get("name");
        if (name) {
          ApiValuePtr index = params->get("index");
          ApiValuePtr subquery = params->newNull();
          if (index) {
            // subquery
            subquery->setType(apivalue_object);
            subquery->add(index->stringValue(), subquery->newNull());
          }
          string nm = trimWhiteSpace(name->stringValue()); // to allow a single space for deep recursing wildcard
          query->add(nm, subquery);
          params->add("query", query);
        }
        // have method handled
        err = handleMethodForParams(request, cmd, params);
        // Note: if method returns NULL, it has sent or will send results itself.
        //   Otherwise, even if Error is ErrorOK we must send a generic response
      }
      else {
        // handle notification
        err = handleNotificationForParams(request->connection(), cmd, params);
        // Notifications are always immediately confirmed, so make sure there's an explicit ErrorOK
        if (!err) {
          err = ErrorPtr(new Error(Error::OK));
        }
      }
    }
  }
  // returning NULL means caller should not do anything more
  // returning an Error object (even ErrorOK) means caller should return status
  return err;
}


// access to plan44 extras that are not part of the vdc API
ErrorPtr P44VdcHost::processP44Request(JsonCommPtr aJsonComm, JsonObjectPtr aRequest)
{
  ErrorPtr err;
  JsonObjectPtr m = aRequest->get("method");
  if (!m) {
    err = Error::err<P44VdcError>(400, "missing 'method'");
  }
  else {
    string method = m->stringValue();
    if (method=="learn") {
      // check proximity check disabling
      bool disableProximity = false;
      JsonObjectPtr o = aRequest->get("disableProximityCheck");
      if (o) {
        disableProximity = o->boolValue();
      }
      // get timeout
      o = aRequest->get("seconds");
      int seconds = 30; // default to 30
      if (o) seconds = o->int32Value();
      if (seconds==0) {
        // end learning prematurely
        stopLearning();
        learnIdentifyTicket.cancel();
        // - close still running learn request
        if (learnIdentifyRequest) {
          learnIdentifyRequest->closeConnection();
          learnIdentifyRequest.reset();
        }
        // - confirm abort with no result
        sendCfgApiResponse(aJsonComm, JsonObjectPtr(), ErrorPtr());
      }
      else {
        // start learning
        learnIdentifyRequest = aJsonComm; // remember so we can cancel it when we receive a separate cancel request
        startLearning(boost::bind(&P44VdcHost::learnHandler, this, aJsonComm, _1, _2), disableProximity);
        learnIdentifyTicket.executeOnce(boost::bind(&P44VdcHost::learnHandler, this, aJsonComm, false, Error::err<P44VdcError>(408, "learn timeout")), seconds*Second);
      }
    }
    else if (method=="identify") {
      // get timeout
      JsonObjectPtr o = aRequest->get("seconds");
      int seconds = 30; // default to 30
      if (o) seconds = o->int32Value();
      if (seconds==0) {
        // end reporting user activity
        setUserActionMonitor(NULL);
        learnIdentifyTicket.cancel();
        // - close still running identify request
        if (learnIdentifyRequest) {
          learnIdentifyRequest->closeConnection();
          learnIdentifyRequest.reset();
        }
        // - confirm abort with no result
        sendCfgApiResponse(aJsonComm, JsonObjectPtr(), ErrorPtr());
      }
      else {
        // wait for next user activity
        learnIdentifyRequest = aJsonComm; // remember so we can cancel it when we receive a separate cancel request
        setUserActionMonitor(boost::bind(&P44VdcHost::identifyHandler, this, aJsonComm, _1));
        learnIdentifyTicket.executeOnce(boost::bind(&P44VdcHost::identifyHandler, this, aJsonComm, DevicePtr()), seconds*Second);
      }
    }
    else {
      err = Error::err<P44VdcError>(400, "unknown method");
    }
  }
  return err;
}


void P44VdcHost::learnHandler(JsonCommPtr aJsonComm, bool aLearnIn, ErrorPtr aError)
{
  learnIdentifyTicket.cancel();
  stopLearning();
  sendCfgApiResponse(aJsonComm, JsonObject::newBool(aLearnIn), aError);
  learnIdentifyRequest.reset();
}


void P44VdcHost::identifyHandler(JsonCommPtr aJsonComm, DevicePtr aDevice)
{
  learnIdentifyTicket.cancel();
  if (aDevice) {
    sendCfgApiResponse(aJsonComm, JsonObject::newString(aDevice->getDsUid().getString()), ErrorPtr());
    // end monitor mode
    setUserActionMonitor(NULL);
  }
  else {
    sendCfgApiResponse(aJsonComm, JsonObjectPtr(), Error::err<P44VdcError>(408, "identify timeout"));
    setUserActionMonitor(NULL);
  }
  learnIdentifyRequest.reset();
}


// MARK: ===== self test procedure



