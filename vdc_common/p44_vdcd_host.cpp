//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2014-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#if ENABLE_P44FEATURES
  #include "featureapi.hpp"
#endif

#if ENABLE_P44SCRIPT
  // global script extras
  #include "httpcomm.hpp"
#endif

using namespace p44;


// MARK: - self test runner

#if SELFTESTING_ENABLED

class SelfTestRunner
{
  StatusCB mCompletedCB;
  VdcMap::iterator mNextVdc;
  VdcHost &mVdcHost;
  ButtonInputPtr mButton;
  IndicatorOutputPtr mRedLED;
  IndicatorOutputPtr mGreenLED;
  MLTicket mErrorReportTicket;
  ErrorPtr mGlobalError;
  int mRealTests;
  bool mNoTestableHw;
public:
  static void initialize(VdcHost &aVdcHost, StatusCB aCompletedCB, ButtonInputPtr aButton, IndicatorOutputPtr aRedLED, IndicatorOutputPtr aGreenLED, bool aNoTestableHw)
  {
    // create new instance, deletes itself when finished
    new SelfTestRunner(aVdcHost, aCompletedCB, aButton, aRedLED, aGreenLED, aNoTestableHw);
  };
private:
  SelfTestRunner(VdcHost &aVdcHost, StatusCB aCompletedCB, ButtonInputPtr aButton, IndicatorOutputPtr aRedLED, IndicatorOutputPtr aGreenLED, bool aNoTestableHw) :
    mCompletedCB(aCompletedCB),
    mVdcHost(aVdcHost),
    mButton(aButton),
    mRedLED(aRedLED),
    mGreenLED(aGreenLED),
    mNoTestableHw(aNoTestableHw),
    mRealTests(0)
  {
    // start testing
    mNextVdc = mVdcHost.mVdcs.begin();
    testNextVdc();
  }


  void testNextVdc()
  {
    if (mNextVdc!=mVdcHost.mVdcs.end()) {
      // ok, test next
      // - start green/yellow blinking = test in progress
      mGreenLED->steadyOn();
      mRedLED->blinkFor(Infinite, 600*MilliSecond, 50);
      // - check for init errors
      ErrorPtr vdcErr = mNextVdc->second->getVdcErr();
      if (Error::isOK(vdcErr)) {
        // - run the test
        LOG(LOG_WARNING, "Starting Test of %s (Tag=%d, %s)", mNextVdc->second->vdcClassIdentifier(), mNextVdc->second->getTag(), mNextVdc->second->shortDesc().c_str());
        mNextVdc->second->selfTest(boost::bind(&SelfTestRunner::vdcTested, this, _1));
      }
      else {
        // - vdc is already in error -> can't run the test, report the initialisation error (vdc status)
        vdcTested(vdcErr);
      }
    }
    else {
      if (mRealTests==0) {
        // no real tests performed
        mGlobalError = Error::err<VdcError>(VdcError::NoHWTested, "self test had nothing to actually test (no HW tests performed)");
      }
      testCompleted(); // done
    }
  }


  void vdcTested(ErrorPtr aError)
  {
    if (Error::notOK(aError)) {
      if (!aError->isError(VdcError::domain(), VdcError::NoHWTested)) {
        // test failed
        LOG(LOG_ERR, "****** Test of '%s' FAILED with error: %s", mNextVdc->second->vdcClassIdentifier(), aError->text());
        // remember
        mGlobalError = aError;
        // morse out tag number of vDC failing self test until button is pressed
        mGreenLED->steadyOff();
        int numBlinks = mNextVdc->second->getTag();
        mRedLED->blinkFor(300*MilliSecond*numBlinks, 300*MilliSecond, 50);
        // call myself again later
        mErrorReportTicket.executeOnce(boost::bind(&SelfTestRunner::vdcTested, this, aError), 300*MilliSecond*numBlinks+2*Second);
        // also install button responder
        mButton->setButtonHandler(boost::bind(&SelfTestRunner::errorAcknowledged, this), false); // report only release
        return; // done for now
      }
    }
    else {
      // real test ok
      mRealTests++;
    }
    // test was ok
    LOG(LOG_ERR, "------ Test of '%s' OK", mNextVdc->second->vdcClassIdentifier());
    // check next
    ++mNextVdc;
    testNextVdc();
  }


  void errorAcknowledged()
  {
    // stop error morse
    mRedLED->steadyOff();
    mGreenLED->steadyOff();
    mErrorReportTicket.cancel();
    // test next (if any)
    ++mNextVdc;
    testNextVdc();
  }


  void testCompleted()
  {
    if (Error::isError(mGlobalError, VdcError::domain(), VdcError::NoHWTested) && mNoTestableHw) {
      LOG(LOG_ERR, "Self test OK - but without any actually tested hardware");
      mRedLED->steadyOff();
      mGreenLED->blinkFor(Infinite, 150, 80); // hectic green flickering = OK but no HW tested
    }
    if (Error::isOK(mGlobalError)) {
      LOG(LOG_ERR, "Self test OK");
      mRedLED->steadyOff();
      mGreenLED->blinkFor(Infinite, 500, 85); // slow green blinking = good
    }
    else  {
      LOG(LOG_ERR, "Self test has FAILED: %s", mGlobalError->text());
      mGreenLED->steadyOff();
      mRedLED->blinkFor(Infinite, 250, 60); // faster red blinking = not good
    }
    // callback, report last error seen
    mCompletedCB(mGlobalError);
    // done, delete myself
    delete this;
  }

};

#endif // SELFTESTING_ENABLED


// MARK: - P44VdcHost

P44VdcHost::P44VdcHost(bool aWithLocalController, bool aWithPersistentChannels) :
  inherited(aWithLocalController, aWithPersistentChannels),
  webUiPort(0)
{
  #if P44SCRIPT_REGISTERED_SOURCE
  mScriptManager = new P44ScriptManager(&StandardScriptingDomain::sharedDomain());
  StandardScriptingDomain::setStandardScriptingDomain(&(mScriptManager->domain()));
  #endif // P44SCRIPT_REGISTERED_SOURCE
  #if P44SCRIPT_IMPLEMENTED_CUSTOM_API
  mScriptedApiLookup.isMemberVariable();
  StandardScriptingDomain::sharedDomain().registerMemberLookup(&mScriptedApiLookup);
  #endif // P44SCRIPT_IMPLEMENTED_CUSTOM_API
}


void P44VdcHost::selfTest(StatusCB aCompletedCB, ButtonInputPtr aButton, IndicatorOutputPtr aRedLED, IndicatorOutputPtr aGreenLED, bool aNoTestableHw)
{
  #if SELFTESTING_ENABLED
  SelfTestRunner::initialize(*this, aCompletedCB, aButton, aRedLED, aGreenLED, aNoTestableHw);
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


string P44VdcHost::nextModelVersion() const
{
  // check for nextversion file to read to obtain next available version
  string nextVersionFile;
  string nextVersion;
  if (CmdLineApp::sharedCmdLineApp()->getStringOption("nextversionfile", nextVersionFile)) {
    string_fgetfirstline(nextVersionFile, nextVersion);
  }
  return nextVersion;
}


void P44VdcHost::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  #if ENABLE_JSONCFGAPI
  // start config API, if we have one
  if (mConfigApi) {
    mConfigApi->mJsonApiServer->startServer(boost::bind(&P44VdcHost::configApiConnectionHandler, this, _1), 3);
  }
  #endif
  #if ENABLE_UBUS
  // start ubus API, if we have it
  if (mUbusApiServer) {
    mUbusApiServer->startServer();
  }
  #endif
  #if ENABLE_P44FEATURES && ENABLE_P44SCRIPT
  // register script access
  if (FeatureApi::existingSharedApi()) {
    StandardScriptingDomain::sharedDomain().registerMemberLookup(new FeatureApiLookup);
  }
  #endif
  // Note: bridge API will be started when all devices are initialized for the first time
  // now init rest of vdc host
  inherited::initialize(aCompletedCB, aFactoryReset);
}



#if ENABLE_UBUS

// MARK: - ubus API

static const struct blobmsg_policy vdcapi_policy[] = {
  { .name = "method", .type = BLOBMSG_TYPE_STRING },
  { .name = "notification", .type = BLOBMSG_TYPE_STRING },
  { .name = "dSUID", .type = BLOBMSG_TYPE_STRING },
  { .name = NULL, .type = BLOBMSG_TYPE_UNSPEC },
};


//static const struct blobmsg_policy cfgapi_policy[] = {
//  { .name = "method", .type = BLOBMSG_TYPE_STRING },
//  { .name = NULL, .type = BLOBMSG_TYPE_UNSPEC },
//};



void P44VdcHost::enableUbusApi()
{
  if (!mUbusApiServer) {
    // can be enabled only once
    mUbusApiServer = UbusServerPtr(new UbusServer());
    UbusObjectPtr u = new UbusObject("vdcd", boost::bind(&P44VdcHost::ubusApiRequestHandler, this, _1));
    u->addMethod("api", vdcapi_policy);
//    u->addMethod("cfg", cfgapi_policy);
    mUbusApiServer->registerObject(u);
  }
}


void P44VdcHost::ubusApiRequestHandler(UbusRequestPtr aUbusRequest)
{
  signalActivity(); // ubus API calls are activity as well
  if (!aUbusRequest->msg()) {
    // we always need a ubus message containing actual call method/notification and params
    aUbusRequest->sendResponse(JsonObjectPtr(), UBUS_STATUS_INVALID_ARGUMENT);
    return;
  }
  LOG(LOG_DEBUG, "ubus -> vdcd (JSON) request received: %s", aUbusRequest->msg()->c_strValue());
  ErrorPtr err;
  UbusApiRequestPtr request = UbusApiRequestPtr(new UbusApiRequest(aUbusRequest));
  if (aUbusRequest->method()=="api") {
    string cmd;
    bool isMethod = false;
    // get method/notification and params
    JsonObjectPtr m = aUbusRequest->msg()->get("method");
    if (m) {
      // is a method call, expects answer
      isMethod = true;
    }
    else {
      // not method, may be notification
      m = aUbusRequest->msg()->get("notification");
    }
    if (!m) {
      err = Error::err<P44VdcError>(400, "invalid request, must specify 'method' or 'notification'");
    }
    else {
      // get method/notification name
      cmd = m->stringValue();
      // get params
      // Note: the "method" or "notification" param will also be in the params, but should not cause any problem
      ApiValuePtr params = JsonApiValue::newValueFromJson(aUbusRequest->msg());
      if (Error::isOK(err)) {
        if (isMethod) {
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
  }
  // err==NULL here means we don't have to do anything more
  // err containing an Error object here (even ErrorOK) means we must return status
  if (err) {
    request->sendResponse(JsonObjectPtr(), err);
  }
}



// MARK: - ubus API - UbusApiConnection

UbusApiConnection::UbusApiConnection()
{
  setApiVersion(VDC_API_VERSION_MAX);
}


ApiValuePtr UbusApiConnection::newApiValue()
{
  return ApiValuePtr(new JsonApiValue);
}


// MARK: - ubus API - UbusApiRequest

UbusApiRequest::UbusApiRequest(UbusRequestPtr aUbusRequest)
{
  ubusRequest = aUbusRequest;
}


VdcApiConnectionPtr UbusApiRequest::connection()
{
  return VdcApiConnectionPtr(new UbusApiConnection());
}


ErrorPtr UbusApiRequest::sendResult(ApiValuePtr aResult)
{
  LOG(LOG_DEBUG, "ubus <- vdcd (JSON) result sent: result=%s", aResult ? aResult->description().c_str() : "<none>");
  JsonApiValuePtr result = boost::dynamic_pointer_cast<JsonApiValue>(aResult);
  JsonObjectPtr r;
  if (result) r = result->jsonObject();
  sendResponse(r, ErrorPtr());
  return ErrorPtr();
}



ErrorPtr UbusApiRequest::sendError(ErrorPtr aError)
{
  ErrorPtr err;
  if (!aError) {
    aError = Error::ok();
  }
  LOG(LOG_DEBUG, "ubus <- vdcd (JSON) error sent: error=%d (%s)", aError->getErrorCode(), aError->getErrorMessage());
  sendResponse(JsonObjectPtr(), err);
  return ErrorPtr();
}



void UbusApiRequest::sendResponse(JsonObjectPtr aResult, ErrorPtr aError)
{
  // create response
  JsonObjectPtr response = JsonObject::newObj();
  if (Error::notOK(aError)) {
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
  LOG(LOG_DEBUG, "ubus response: %s", response->c_strValue());
  ubusRequest->sendResponse(response);
}


#endif


#if ENABLE_JSONCFGAPI


// MARK: - JSON config API

void P44VdcHost::enableConfigApi(const char *aServiceOrPort, bool aNonLocalAllowed)
{
  if (!mConfigApi) {
    // can be enabled only once
    mConfigApi = new P44CfgApiConnection(new SocketComm(MainLoop::currentMainLoop()));
    mConfigApi->mJsonApiServer->setConnectionParams(NULL, aServiceOrPort, SOCK_STREAM, AF_INET);
    mConfigApi->mJsonApiServer->setAllowNonlocalConnections(aNonLocalAllowed);
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
  string reqid; // not really needed for cfg API, but might be helpful for debugging
  // when coming from mg44, requests have the following form (peer from mg44 3.9 onwards)
  // - for GET requests like http://localhost:8080/api/json/myuri?foo=bar&this=that
  //   {"method":"GET", "uri":"myuri", "peer":"ip_addr", "uri_params":{"foo":"bar", "this":"that"}}
  // - for POST requests like
  //   curl "http://localhost:8080/api/json/myuri?foo=bar&this=that" --data-ascii "{ \"content\":\"data\", \"important\":false }"
  //   {"method":"POST", "uri":"myuri", "peer":"ip_addr", "uri_params":{"foo":"bar","this":"that"},"data":{"content":"data","important":false}}
  //   curl "http://localhost:8080/api/json/myuri" --data-ascii "{ \"content\":\"data\", \"important\":false }"
  //   {"method":"POST", "uri":"myuri", "peer":"ip_addr", "data":{"content":"data","important":false}}
  // processing:
  // - a JSON request must be either specified in the URL or in the POST data, not both
  // - if POST data ("data" member in the incoming request) is present, "uri_params" is ignored
  // - "uri" selects one of possibly multiple APIs
  signalActivity(); // P44 JSON API calls are activity as well
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
      // include the peer into request if we have it
      JsonObjectPtr o;
      if (aJsonObject->get("peer", o)) {
        request->add("peer", o);
      }
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
        aError = processVdcRequest(mConfigApi, aJsonComm, request, reqid);
      }
      #if ENABLE_LEGACY_P44CFGAPI
      else if (apiselector=="p44") {
        // process p44 specific requests
        aError = processP44Request(aJsonComm, request);
      }
      #endif
      #if ENABLE_P44FEATURES
      else if (apiselector=="featureapi") {
        // p44featured API wrapper
        FeatureApiPtr featureApi = FeatureApi::existingSharedApi();
        if (!featureApi) {
          aError = WebError::webErr(500, "no features instantiated, API not active");
        }
        else {
          ApiRequestPtr req = ApiRequestPtr(new APICallbackRequest(request, boost::bind(&P44VdcHost::sendJsonApiResponse, aJsonComm, _1, _2, "")));
          featureApi->handleRequest(req);
        }
      }
      #endif
      #if P44SCRIPT_IMPLEMENTED_CUSTOM_API
      else if (apiselector=="scriptapi") {
        // scripted parts of the (web) API
        if (!mScriptedApiLookup.hasSinks()) {
          // no script API active
          aError = WebError::webErr(500, "script API not active");
        }
        else {
          // API active, send request object to event sinks
          mScriptedApiLookup.sendEvent(new ApiRequestObj(aJsonComm, request));
        }
      }
      #endif // P44SCRIPT_IMPLEMENTED_CUSTOM_API
      else {
        // unknown API selector
        aError = Error::err<P44VdcError>(400, "invalid URI, unknown API");
      }
    }
  }
  // if error or explicit OK, send response now. Otherwise, request processing will create and send the response
  if (aError) {
    sendJsonApiResponse(aJsonComm, JsonObjectPtr(), aError, reqid);
  }
}


#if ENABLE_LEGACY_P44CFGAPI

// lecacy access to plan44 extras that historically were not part of the vdc API
ErrorPtr P44VdcHost::processP44Request(JsonCommPtr aJsonComm, JsonObjectPtr aRequest)
{
  ErrorPtr err;
  JsonObjectPtr m = aRequest->get("method");
  if (!m) {
    err = Error::err<P44VdcError>(400, "missing 'method'");
  }
  else {
    string method = "x-p44-" + m->stringValue();
    ApiValuePtr params = JsonApiValue::newValueFromJson(aRequest);
    P44JsonApiRequestPtr request = P44JsonApiRequestPtr(new P44JsonApiRequest(aJsonComm));
    // directly handle as vdchost method
    err = handleMethod(request, method, params);
  }
  // returning NULL means caller should not do anything more
  // returning an Error object (even ErrorOK) means caller should return status
  return err;
}

#endif // ENABLE_LEGACY_P44CFGAPI
#endif // ENABLE_JSONCFGAPI


#if ENABLE_JSONCFGAPI || ENABLE_JSONBRIDGEAPI

// MARK: - methods for JSON APIs

void P44VdcHost::sendJsonApiResponse(JsonCommPtr aJsonComm, JsonObjectPtr aResult, ErrorPtr aError, string aReqId)
{
  // create response
  JsonObjectPtr response = JsonObject::newObj();
  if (Error::notOK(aError)) {
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
  if (!aReqId.empty()) {
    response->add("id", JsonObject::newString(aReqId));
  }
  LOG(LOG_DEBUG, "JSON API response: %s", response->c_strValue());
  aJsonComm->sendMessage(response);
}


// access to vdc API methods and notifications via web requests
ErrorPtr P44VdcHost::processVdcRequest(VdcApiConnectionPtr aApi, JsonCommPtr aJsonComm, JsonObjectPtr aRequest, string &aReqId)
{
  ErrorPtr err;
  string cmd;
  bool isMethod = false;
  // requests might have an id which is reflected in answers
  JsonObjectPtr r = aRequest->get("id");
  if (r) aReqId = r->stringValue();
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
    P44JsonApiRequestPtr request = P44JsonApiRequestPtr(new P44JsonApiRequest(aJsonComm, aApi, aReqId));
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
  // returning NULL means caller should not do anything more
  // returning an Error object (even ErrorOK) means caller should return status
  return err;
}


// MARK: - P44JsonApiConnection

P44JsonApiConnection::P44JsonApiConnection(SocketCommPtr aJsonApiServer) :
  mJsonApiServer(aJsonApiServer)
{
  // JSON APIs always use the newest version
  setApiVersion(VDC_API_VERSION_MAX);
}


ApiValuePtr P44JsonApiConnection::newApiValue()
{
  return ApiValuePtr(new JsonApiValue);
}


// MARK: - P44JsonApiRequest

P44JsonApiRequest::P44JsonApiRequest(JsonCommPtr aRequestJsonComm, VdcApiConnectionPtr aJsonApi, string aRequestId) :
  mJsonComm(aRequestJsonComm),
  mJsonApi(aJsonApi),
  mRequestId(aRequestId)
{
}


VdcApiConnectionPtr P44JsonApiRequest::connection()
{
  return mJsonApi;
}


ErrorPtr P44JsonApiRequest::sendResult(ApiValuePtr aResult)
{
  LOG(LOG_DEBUG, "%s <- vdcd (JSON) result: %s", mJsonApi->apiName(), aResult ? aResult->description().c_str() : "<none>");
  JsonApiValuePtr result = boost::dynamic_pointer_cast<JsonApiValue>(aResult);
  if (result) {
    P44VdcHost::sendJsonApiResponse(mJsonComm, result->jsonObject(), ErrorPtr(), mRequestId);
  }
  else {
    // JSON APIs: always return SOMETHING as result
    P44VdcHost::sendJsonApiResponse(mJsonComm, JsonObject::newNull(), ErrorPtr(), mRequestId);
  }
  return ErrorPtr();
}


ErrorPtr P44JsonApiRequest::sendError(ErrorPtr aError)
{
  ErrorPtr err;
  if (!aError) {
    aError = Error::ok();
  }
  LOG(LOG_DEBUG, "%s <- vdcd (JSON) error: %ld (%s)", mJsonApi->apiName(), aError->getErrorCode(), aError->getErrorMessage());
  P44VdcHost::sendJsonApiResponse(mJsonComm, JsonObjectPtr(), aError, mRequestId);
  return ErrorPtr();
}


#endif // ENABLE_JSONCFGAPI || ENABLE_JSONBRIDGEAPI


#if ENABLE_JSONBRIDGEAPI

// MARK: - Bridge API

void P44VdcHost::handleGlobalEvent(VdchostEvent aEvent)
{
  if (aEvent==vdchost_devices_initialized) {
    #if P44SCRIPT_FULL_SUPPORT
    // after the first device initialisation run, start the bridge API if not already up
    if (mBridgeApi && !mBridgeApi->mJsonApiServer->isServing()) {
      // bridge API server is not yet up and running, start it now
      mBridgeApi->mJsonApiServer->startServer(boost::bind(&P44VdcHost::bridgeApiConnectionHandler, this, _1), 3);
    }
    #endif // P44SCRIPT_FULL_SUPPORT
  }
  inherited::handleGlobalEvent(aEvent);
}




BridgeInfoPtr P44VdcHost::getBridgeInfo(bool aInstantiate)
{
  if (!mBridgeInfo && aInstantiate) {
    mBridgeInfo = new BridgeInfo(*this);
  }
  return mBridgeInfo;
}


void P44VdcHost::enableBridgeApi(const char *aServiceOrPort, bool aNonLocalAllowed)
{
  if (!mBridgeApi) {
    // can be enabled only once
    // - enabling bridge API also instantiates bridge info
    getBridgeInfo(true);
    // - enable API
    mBridgeApi = new BridgeApiConnection(new SocketComm(MainLoop::currentMainLoop()));
    mBridgeApi->mJsonApiServer->setConnectionParams(NULL, aServiceOrPort, SOCK_STREAM, AF_INET);
    mBridgeApi->mJsonApiServer->setAllowNonlocalConnections(aNonLocalAllowed);
  }
}


size_t P44VdcHost::numBridgeApiClients()
{
  if (!mBridgeApi) return 0;
  return mBridgeApi->mJsonApiServer->numClients();
}



SocketCommPtr P44VdcHost::bridgeApiConnectionHandler(SocketCommPtr aServerSocketCommP)
{
  JsonCommPtr conn = JsonCommPtr(new JsonComm(MainLoop::currentMainLoop()));
  conn->setMessageHandler(boost::bind(&P44VdcHost::bridgeApiRequestHandler, this, conn, _1, _2));
  conn->setClearHandlersAtClose(); // close must break retain cycles so this object won't cause a mem leak
  getBridgeInfo()->resetInfo(); // reset bridge info
  return conn;
}


void P44VdcHost::bridgeApiRequestHandler(JsonCommPtr aJsonComm, ErrorPtr aError, JsonObjectPtr aJsonRequest)
{
  ErrorPtr err;
  signalActivity(); // bridge API calls are activity as well
  string reqid;
  if (Error::isOK(aError)) {
    // not JSON level error, try to process
    LOG(LOG_DEBUG, "bridge -> vdcd (JSON) request received: %s", aJsonRequest->c_strValue());
    aError = processVdcRequest(mBridgeApi, aJsonComm, aJsonRequest, reqid);
  }
  // if error or explicit OK, send response now. Otherwise, request processing will create and send the response
  if (aError) {
    sendJsonApiResponse(aJsonComm, JsonObjectPtr(), aError, reqid);
  }
}



ErrorPtr BridgeApiConnection::sendRequest(const string &aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler)
{
  // send to all connected bridges
  mJsonApiServer->eachClient(boost::bind(&BridgeApiConnection::notifyBridgeClient, this, _1, aMethod, aParams));
  // Note: we don't support responses, so ignoring aResponseHandler completely here
  return ErrorPtr();
}


void BridgeApiConnection::notifyBridgeClient(SocketCommPtr aSocketComm, const string &aNotification, ApiValuePtr aParams)
{
  if (!aParams) {
    // create params object because we need it for the notification
    aParams = newApiValue();
    aParams->setType(apivalue_object);
  }
  aParams->add("notification", aParams->newString(aNotification));
  JsonApiValuePtr notification = boost::dynamic_pointer_cast<JsonApiValue>(aParams);
  JsonCommPtr comm = dynamic_pointer_cast<JsonComm>(aSocketComm);
  if (comm && notification) {
    LOG(LOG_DEBUG, "JSON API notification: %s", notification->jsonObject()->c_strValue());
    comm->sendMessage(notification->jsonObject());
  }
}


// MARK: - BridgeInfo

BridgeInfo::BridgeInfo(P44VdcHost& aP44VdcHost) :
  mP44VdcHost(aP44VdcHost)
{
  resetInfo();
}


void BridgeInfo::resetInfo()
{
  mQRCodeData.clear();
  mManualPairingCode.clear();
  mCommissionable = false;
  mStarted = false;
}



enum {
  qrcodedata_key,
  manualpairingcode_key,
  started_key,
  commissionable_key,
  connected_key,
  numBrigeInfoProperties
};

static char bridgeinfo_key;

int BridgeInfo::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return numBrigeInfoProperties;
}


PropertyDescriptorPtr BridgeInfo::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numBrigeInfoProperties] = {
    { "qrcodedata", apivalue_string, qrcodedata_key, OKEY(bridgeinfo_key) },
    { "manualpairingcode", apivalue_string, manualpairingcode_key, OKEY(bridgeinfo_key) },
    { "started", apivalue_bool, started_key, OKEY(bridgeinfo_key) },
    { "commissionable", apivalue_bool, commissionable_key, OKEY(bridgeinfo_key) },
    { "connected", apivalue_bool, connected_key, OKEY(bridgeinfo_key) },
  };
  if (aParentDescriptor->isRootOfObject()) {
    // root level property of this object hierarchy
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  return PropertyDescriptorPtr();
}


bool BridgeInfo::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(bridgeinfo_key)) {
    if (aMode==access_read) {
      switch (aPropertyDescriptor->fieldKey()) {
        case qrcodedata_key: aPropValue->setStringValue(mQRCodeData); return true;
        case manualpairingcode_key: aPropValue->setStringValue(mManualPairingCode); return true;
        case started_key: aPropValue->setBoolValue(mP44VdcHost.numBridgeApiClients()>0 && mStarted); return true;
        case commissionable_key: aPropValue->setBoolValue(mCommissionable); return true;
        case connected_key: aPropValue->setBoolValue(mP44VdcHost.numBridgeApiClients()>0); return true;
      }
    }
    else {
      switch (aPropertyDescriptor->fieldKey()) {
        case qrcodedata_key: mQRCodeData = aPropValue->stringValue(); return true;
        case manualpairingcode_key: mManualPairingCode = aPropValue->stringValue(); return true;
        case started_key: mStarted = aPropValue->boolValue(); return true;
        case commissionable_key: mCommissionable = aPropValue->boolValue(); return true;
      }
    }
  }
  return false;
}

#endif // ENABLE_JSONBRIDGEAPI


// MARK: - vdchost property access

static char bridge_obj;
static char scripts_obj;

enum {
  #if ENABLE_JSONBRIDGEAPI
  bridge_key,
  #endif
  #if P44SCRIPT_REGISTERED_SOURCE
  scripts_key,
  #endif
  numP44VdcHostProperties
};



int P44VdcHost::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return inherited::numProps(aDomain, aParentDescriptor)+numP44VdcHostProperties;
}


// note: is only called when getDescriptorByName does not resolve the name
PropertyDescriptorPtr P44VdcHost::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // TODO: remove outer if as soon as we have non-conditional other properties
  // to avoid declaring an array of length 0 (which works in clang/gcc but is not allowed by ISO standard)
  #if ENABLE_JSONBRIDGEAPI
  static const PropertyDescription properties[numP44VdcHostProperties] = {
    #if ENABLE_JSONBRIDGEAPI
    { "x-p44-bridge", apivalue_object, bridge_key, OKEY(bridge_obj) },
    #endif
    #if P44SCRIPT_REGISTERED_SOURCE
    { "x-p44-scripts", apivalue_object, scripts_key, OKEY(scripts_obj) },
    #endif
  };
  int n = inherited::numProps(aDomain, aParentDescriptor);
  if (aPropIndex<n)
    return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
  aPropIndex -= n; // rebase to 0 for my own first property
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  #else
  return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
  #endif
}


PropertyContainerPtr P44VdcHost::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  #if ENABLE_JSONBRIDGEAPI
  if (aPropertyDescriptor->hasObjectKey(bridge_obj)) {
    return mBridgeInfo; // can be NULL if bridge api is not enabled
  }
  #endif
  #if P44SCRIPT_REGISTERED_SOURCE
  if (aPropertyDescriptor->hasObjectKey(scripts_obj)) {
    return mScriptManager; // can be NULL if no script manager is set
  }
  #endif
  // unknown here
  return inherited::getContainer(aPropertyDescriptor, aDomain);
}


// MARK: - P44 specific vdchost level methods

ErrorPtr P44VdcHost::handleMethod(VdcApiRequestPtr aRequest,  const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="x-p44-learn") {
    // check proximity check disabling
    bool disableProximity = false;
    ApiValuePtr o = aParams->get("disableProximityCheck");
    if (o) {
      disableProximity = o->boolValue();
    }
    // get timeout
    o = aParams->get("seconds");
    int seconds = 30; // default to 30
    if (o) seconds = o->int32Value();
    if (seconds==0) {
      // end learning prematurely
      stopLearning();
      learnIdentifyTicket.cancel();
      // - close still running learn request
      if (learnIdentifyRequest) {
        learnIdentifyRequest->sendStatus(Error::err<P44VdcError>(410, "Learn cancelled"));
        learnIdentifyRequest.reset();
      }
      // - confirm abort with no result
      aRequest->sendStatus(NULL); // ok
    }
    else {
      // start learning
      learnIdentifyRequest = aRequest; // remember so we can cancel it when we receive a separate cancel request
      startLearning(boost::bind(&P44VdcHost::learnHandler, this, aRequest, _1, _2), disableProximity);
      learnIdentifyTicket.executeOnce(boost::bind(&P44VdcHost::learnHandler, this, aRequest, false, Error::err<P44VdcError>(408, "learn timeout")), seconds*Second);
    }
  }
  else if (aMethod=="x-p44-identify") {
    // get timeout
    ApiValuePtr o = aParams->get("seconds");
    int seconds = 30; // default to 30
    if (o) seconds = o->int32Value();
    if (seconds==0) {
      // end reporting user activity
      setUserActionMonitor(NoOP);
      learnIdentifyTicket.cancel();
      // - close still running identify request
      if (learnIdentifyRequest) {
        learnIdentifyRequest->sendStatus(Error::err<P44VdcError>(410, "Identify cancelled"));
        learnIdentifyRequest.reset();
      }
      // - confirm abort with no result
      aRequest->sendStatus(NULL); // ok
    }
    else {
      // wait for next user activity
      learnIdentifyRequest = aRequest; // remember so we can cancel it when we receive a separate cancel request
      setUserActionMonitor(boost::bind(&P44VdcHost::identifyHandler, this, aRequest, _1));
      learnIdentifyTicket.executeOnce(boost::bind(&P44VdcHost::identifyHandler, this, aRequest, DevicePtr()), seconds*Second);
    }
  }
  #if ENABLE_JSONBRIDGEAPI
  else if (aMethod=="x-p44-notifyBridge") {
    // send a notification to the bridge(s)
    if (!mBridgeApi) {
      respErr = Error::err<P44VdcError>(404, "no bridge connected");
    }
    else {
      ApiValuePtr o = aParams->get("bridgenotification");
      if (!o) {
        respErr = Error::err<P44VdcError>(415, "missing 'bridgenotification'");
      }
      else {
        string n = o->stringValue();
        aParams->del("bridgenotification");
        aParams->del("method");
        aParams->del("notification");
        aParams->del("dSUID");
        o = aParams->get("bridgeUID");
        if (o) {
          aParams->add("dSUID", o);
          aParams->del("bridgeUID");
        }
        respErr = mBridgeApi->sendRequest(n, aParams);
        if (Error::isOK(respErr)) respErr = Error::ok(); // return NULL response
      }
    }
  }
  #endif // ENABLE_JSONBRIDGEAPI
  #if P44SCRIPT_REGISTERED_SOURCE
  else if (mScriptManager && mScriptManager->handleScriptManagerMethod(respErr, aRequest, aMethod, aParams)) {
    return respErr;
  }
  #endif // ENABLE_JSONBRIDGEAPI
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}


void P44VdcHost::learnHandler(VdcApiRequestPtr aRequest, bool aLearnIn, ErrorPtr aError)
{
  learnIdentifyTicket.cancel();
  stopLearning();
  if (Error::isOK(aError)) {
    ApiValuePtr v = aRequest->newApiValue();
    v->setType(apivalue_bool);
    v->setBoolValue(aLearnIn);
    aRequest->sendResult(v);
  }
  else {
    aRequest->sendError(aError);
  }
  learnIdentifyRequest.reset();
}


void P44VdcHost::identifyHandler(VdcApiRequestPtr aRequest, DevicePtr aDevice)
{
  learnIdentifyTicket.cancel();
  if (aDevice) {
    ApiValuePtr v = aRequest->newApiValue();
    v->setType(apivalue_string);
    v->setStringValue(aDevice->getDsUid().getString());
    aRequest->sendResult(v);
  }
  else {
    aRequest->sendError(Error::err<P44VdcError>(408, "identify timeout"));
  }
  // end monitor mode
  setUserActionMonitor(NoOP);
  learnIdentifyRequest.reset();
}

// MARK: - P44ScriptManager

#if P44SCRIPT_REGISTERED_SOURCE

/// script manager specific method handling
bool P44ScriptManager::handleScriptManagerMethod(ErrorPtr &aError, VdcApiRequestPtr aRequest,  const string &aMethod, ApiValuePtr aParams)
{
  // FIXME: implement %%%
  return false;
}


static char sourceslist_key;

enum {
  // singledevice level properties
  sources_key,
  numScriptManagerProperties
};

static char source_key;

enum {
  // singledevice level properties
  sourcetext_key,
  numScriptSourceProperties
};


int P44ScriptManager::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(sourceslist_key)) {
    // script sources container
    return static_cast<int>(domain().numRegisteredSources());
  }
  else if (aParentDescriptor->hasObjectKey(source_key)) {
    // source properties
    return numScriptSourceProperties;
  }
  // Note: P44ScriptManager is final, so no subclass adding properties must be considered
  // Always accessing properties at the scriptmanager (root) level
  return inherited::numProps(aDomain, aParentDescriptor)+numScriptManagerProperties;
}


PropertyContainerPtr P44ScriptManager::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  // the only subcontainer are the sources, handled by myself
  return PropertyContainerPtr(this);
}


PropertyDescriptorPtr P44ScriptManager::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // scriptmanager level properties
  static const PropertyDescription managerProperties[numScriptManagerProperties] = {
    // common device properties
    { "sources", apivalue_object, sources_key, OKEY(sourceslist_key) },
  };
  // scriptsource level properties
  static const PropertyDescription sourceProperties[numScriptManagerProperties] = {
    // common device properties
    { "sourcetext", apivalue_string, sourcetext_key, OKEY(source_key) },
  };
  // C++ object manages different levels, check objects
  if (aParentDescriptor->hasObjectKey(sourceslist_key)) {
    // script sources by their uid
    ScriptSource* src = domain().getSourceByIndex(aPropIndex);
    if (!src) return nullptr;
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = src->scriptSourceUid();
    descP->propertyType = aParentDescriptor->type();
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(source_key);
    return descP;
  }
  else if (aParentDescriptor->hasObjectKey(source_key)) {
    // source fields
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&sourceProperties[aPropIndex], aParentDescriptor));
  }
  // Note: P44ScriptManager is final, so no subclass adding properties must be considered
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&managerProperties[aPropIndex], aParentDescriptor));
}


bool P44ScriptManager::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(source_key)) {
    // scriptsource level property
    // - get the source
    ScriptSource* src = domain().getSourceByIndex(aPropertyDescriptor->parentDescriptor->fieldKey());
    if (src) {
      if (aMode==access_read) {
        // read properties
        switch (aPropertyDescriptor->fieldKey()) {
          case sourcetext_key:
            aPropValue->setStringValue(src->getSource());
            return true;
        }
      }
      else {
        // write properties
        switch (aPropertyDescriptor->fieldKey()) {
          case sourcetext_key:
            src->setAndStoreSource(aPropValue->stringValue());
            return true;
        }
      }
    }
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}

#endif // P44SCRIPT_REGISTERED_SOURCE


// MARK: - Scripted Custom API Support

#if P44SCRIPT_IMPLEMENTED_CUSTOM_API

using namespace P44Script;

ApiRequestObj::ApiRequestObj(JsonCommPtr aConnection, JsonObjectPtr aRequest) :
  inherited(aRequest),
  mConnection(aConnection)
{
}

void ApiRequestObj::sendResponse(JsonObjectPtr aResponse, ErrorPtr aError)
{
  if (mConnection) P44VdcHost::sendJsonApiResponse(mConnection, aResponse, aError, "" /* no reqid */);
  mConnection.reset(); // done now
}

string ApiRequestObj::getAnnotation() const
{
  return "API request";
}

// answer([answer value|error])        answer the request
static const BuiltInArgDesc answer_args[] = { { any|error|optionalarg } };
static const size_t answer_numargs = sizeof(answer_args)/sizeof(BuiltInArgDesc);
static void answer_func(BuiltinFunctionContextPtr f)
{
  ApiRequestObj* reqObj = dynamic_cast<ApiRequestObj *>(f->thisObj().get());
  if (f->arg(0)->isErr()) {
    reqObj->sendResponse(JsonObjectPtr(), f->arg(0)->errorValue());
  }
  else {
    reqObj->sendResponse(f->arg(0)->jsonValue(), ErrorPtr());
  }
  f->finish();
}
static const BuiltinMemberDescriptor answer_desc =
  { "answer", executable|any, answer_numargs, answer_args, &answer_func };


const ScriptObjPtr ApiRequestObj::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  ScriptObjPtr val;
  if (uequals(aName, "answer")) {
    val = new BuiltinFunctionObj(&answer_desc, this, NULL);
  }
  else {
    val = inherited::memberByName(aName, aMemberAccessFlags);
  }
  return val;
}


// webrequest()        event source for (script API) web request
static void webrequest_func(BuiltinFunctionContextPtr f)
{
  // return API request event source place holder, actual value will be delivered via event
  P44VdcHost* h = dynamic_cast<P44VdcHost*>(VdcHost::sharedVdcHost().get());
  f->finish(new OneShotEventNullValue(&h->mScriptedApiLookup, "web request"));
}

static const BuiltinMemberDescriptor scriptApiGlobals[] = {
  { "webrequest", executable|json|null, 0, NULL, &webrequest_func },
  { NULL } // terminator
};


ScriptApiLookup::ScriptApiLookup() : inherited(scriptApiGlobals)
{
}

#endif // P44SCRIPT_IMPLEMENTED_CUSTOM_API
