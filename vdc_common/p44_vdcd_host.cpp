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

P44ScriptManager::P44ScriptManager(ScriptingDomainPtr aScriptingDomain) :
  mScriptingDomain(aScriptingDomain)
{
  assert(mScriptingDomain);
  mScriptingDomain->setPauseHandler(boost::bind(&P44ScriptManager::pausedHandler, this, _1));
}


void P44ScriptManager::pausedHandler(ScriptCodeThreadPtr aPausedThread)
{
  if (aPausedThread->pauseReason()==nopause) {
    OLOG(LOG_ERR, "non-paused thread reported paused")
    return;
  }
  // actually paused
  P44PausedThread pausedThread;
  pausedThread.mPausedAt = MainLoop::now();
  pausedThread.mThread = aPausedThread;
  pausedThread.mScriptHost = mScriptingDomain->getHostForThread(aPausedThread);
  mPausedThreads.push_back(pausedThread);
}


bool P44ScriptManager::isDebugging() const
{
  return mScriptingDomain && mScriptingDomain->defaultPausingMode()>nopause;
}


void P44ScriptManager::setDebugging(bool aDebug)
{
  if (!mScriptingDomain) return;
  if (aDebug!=isDebugging()) {
    mScriptingDomain->setDefaultPausingMode(aDebug ? breakpoint : nopause);
    // restart all paused threads
    if (!aDebug) {
      OLOG(LOG_WARNING, "Debugging mode disabled: all paused threads are restarted now");
      while (mPausedThreads.size()>0) {
        PausedThreadsVector::iterator pos = mPausedThreads.begin();
        pos->mThread->continueWithMode(nopause);
        mPausedThreads.erase(pos);
      }
    }
  }
}


void P44ScriptManager::setResultAndPosInfo(ApiValuePtr aIntoApiValue, ScriptObjPtr aResult, const SourceCursor* aCursorP)
{
  aIntoApiValue->setType(apivalue_object);
  if (aResult) {
    if (!aResult->isErr()) {
      aIntoApiValue->add("result", aIntoApiValue->newScriptValue(aResult));
    }
    else {
      aIntoApiValue->add("error", aIntoApiValue->newString(aResult->errorValue()->getErrorMessage()));
    }
  }
  if (!aCursorP && aResult) aCursorP = aResult->cursor();
  if (aCursorP) {
    aIntoApiValue->add("at", aIntoApiValue->newUint64(aCursorP->textpos()));
    aIntoApiValue->add("line", aIntoApiValue->newUint64(aCursorP->lineno()));
    aIntoApiValue->add("char", aIntoApiValue->newUint64(aCursorP->charpos()));
  }
}


enum {
  // script manager level properties
  sources_key,
  pausedthreads_key,
  debugging_key,
  numScriptManagerProperties
};

static char scriptmanager_key;


enum {
  // source level properties
  sourcetext_key,
  sourcetitle_key,
  originlabel_key,
  contexttype_key,
  contextid_key,
  contextname_key,
  logprefix_key,
  unstored_key,
  numScriptHostProperties
};

static char scripthostslist_key;
static char scripthost_key;


enum {
  // thread level properties
  threadid_key,
  threadtitle_key,
  scripthostuid_key,
  result_key,
  pausereason_key,
  pausedat_key,
  numPausedThreadProperties
};

static char pausedthreadslist_key;
static char pausedthread_key;


int P44ScriptManager::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(scripthostslist_key)) {
    // script host container
    return static_cast<int>(domain().numRegisteredHosts());
  }
  else if (aParentDescriptor->hasObjectKey(pausedthreadslist_key)) {
    // paused threads list
    return static_cast<int>(mPausedThreads.size());
  }
  else if (aParentDescriptor->hasObjectKey(scripthost_key)) {
    // source properties
    return numScriptHostProperties;
  }
  else if (aParentDescriptor->hasObjectKey(pausedthread_key)) {
    // paused thread properties
    return numPausedThreadProperties;
  }
  // Note: P44ScriptManager is final, so no subclass adding properties must be considered
  // Always accessing properties at the scriptmanager (root) level
  return inherited::numProps(aDomain, aParentDescriptor)+numScriptManagerProperties;
}


PropertyContainerPtr P44ScriptManager::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  // all subcontainers are handled by myself
  return PropertyContainerPtr(this);
}


PropertyDescriptorPtr P44ScriptManager::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // scriptmanager level properties
  static const PropertyDescription managerProperties[numScriptManagerProperties] = {
    // common device properties
    { "sources", apivalue_object, sources_key, OKEY(scripthostslist_key) },
    { "pausedthreads", apivalue_object, pausedthreads_key, OKEY(pausedthreadslist_key) },
    { "debugging", apivalue_bool, debugging_key, OKEY(scriptmanager_key) },
  };
  // scripthost level properties
  static const PropertyDescription scripthostProperties[numScriptHostProperties] = {
    { "sourcetext", apivalue_string, sourcetext_key, OKEY(scripthost_key) },
    { "title", apivalue_string, sourcetitle_key, OKEY(scripthost_key) },
    { "originlabel", apivalue_string, originlabel_key, OKEY(scripthost_key) },
    { "contexttype", apivalue_string, contexttype_key, OKEY(scripthost_key) },
    { "contextid", apivalue_string, contextid_key, OKEY(scripthost_key) },
    { "contextname", apivalue_string, contextname_key, OKEY(scripthost_key) },
    { "logprefix", apivalue_string, logprefix_key, OKEY(scripthost_key) },
    { "unstored", apivalue_bool, unstored_key, OKEY(scripthost_key) },
  };
  // pausedthread level properties
  static const PropertyDescription pausedthreadProperties[numPausedThreadProperties] = {
    { "threadid", apivalue_uint64, threadid_key, OKEY(pausedthread_key) },
    { "title", apivalue_string, threadtitle_key, OKEY(pausedthread_key) },
    { "scriptHostId", apivalue_string, scripthostuid_key, OKEY(pausedthread_key) },
    { "result", apivalue_null, result_key, OKEY(pausedthread_key) },
    { "pausereason", apivalue_string, pausereason_key, OKEY(pausedthread_key) },
    { "pausedAt", apivalue_uint64, pausedat_key, OKEY(pausedthread_key) },
  };
  // C++ object manages different levels, check objects
  if (aParentDescriptor->hasObjectKey(scripthostslist_key)) {
    // script hosts by their uid
    ScriptHostPtr host = domain().getHostByIndex(aPropIndex);
    if (!host) return nullptr;
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = host->scriptSourceUid();
    descP->propertyType = aParentDescriptor->type();
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(scripthost_key);
    return descP;
  }
  else if (aParentDescriptor->hasObjectKey(pausedthreadslist_key)) {
    // paused threads by thread id
    if (aPropIndex>=mPausedThreads.size()) return nullptr;
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = string_format("%d", aPropIndex);
    descP->propertyType = aParentDescriptor->type();
    descP->propertyFieldKey = aPropIndex;
    descP->propertyObjectKey = OKEY(pausedthread_key);
    return descP;
  }
  else if (aParentDescriptor->hasObjectKey(scripthost_key)) {
    // script host fields
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&scripthostProperties[aPropIndex], aParentDescriptor));
  }
  else if (aParentDescriptor->hasObjectKey(pausedthread_key)) {
    // paused threads fields
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&pausedthreadProperties[aPropIndex], aParentDescriptor));
  }
  // Note: P44ScriptManager is final, so no subclass adding properties must be considered
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&managerProperties[aPropIndex], aParentDescriptor));
}


bool P44ScriptManager::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(scriptmanager_key)) {
    // script manager level property
    if (aMode==access_read) {
      switch (aPropertyDescriptor->fieldKey()) {
        case debugging_key: aPropValue->setBoolValue(isDebugging()); return true;
      }
    }
    else {
      switch (aPropertyDescriptor->fieldKey()) {
        case debugging_key: setDebugging(aPropValue->boolValue()); return true;
      }
    }
  }
  else if (aPropertyDescriptor->hasObjectKey(scripthost_key)) {
    // script host level property
    // - get the host
    ScriptHostPtr host = domain().getHostByIndex(aPropertyDescriptor->parentDescriptor->fieldKey());
    if (host) {
      if (aMode==access_read) {
        // read properties
        P44LoggingObj* cobj;
        switch (aPropertyDescriptor->fieldKey()) {
          case sourcetext_key:
            aPropValue->setStringValue(host->getSource());
            return true;
          case sourcetitle_key:
            aPropValue->setStringValue(host->getScriptTitle());
            return true;
          case originlabel_key:
            aPropValue->setStringValue(host->getOriginLabel());
            return true;
          case contexttype_key:
            cobj = host->getLoggingContext();
            if (!cobj) return false;
            aPropValue->setStringValue(cobj->contextType());
            return true;
          case contextid_key:
            cobj = host->getLoggingContext();
            if (!cobj) return false;
            aPropValue->setStringValue(cobj->contextId());
            return true;
          case contextname_key:
            cobj = host->getLoggingContext();
            if (!cobj) return false;
            aPropValue->setStringValue(cobj->contextName());
            return true;
          case logprefix_key:
            cobj = host->getLoggingContext();
            if (!cobj) return false;
            aPropValue->setStringValue(cobj->logContextPrefix());
            return true;
          case unstored_key:
            aPropValue->setBoolValue(host->isUnstored());
            return true;
        }
      }
      else {
        // write properties
        switch (aPropertyDescriptor->fieldKey()) {
          case sourcetext_key:
            host->setAndStoreSource(aPropValue->stringValue());
            return true;
        }
      }
    }
  }
  else if (aPropertyDescriptor->hasObjectKey(pausedthread_key)) {
    // paused thread level property
    // - get the paused thread
    P44PausedThread& pausedthread = mPausedThreads[aPropertyDescriptor->parentDescriptor->fieldKey()];
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case scripthostuid_key:
          aPropValue->setStringValue(pausedthread.mScriptHost->scriptSourceUid());
          return true;
        case threadid_key:
          aPropValue->setUint32Value(pausedthread.mThread->threadId());
          return true;
        case threadtitle_key:
          aPropValue->setStringValue(string_format("thread_%4d",pausedthread.mThread->threadId()));
          return true;
        case result_key:
          P44ScriptManager::setResultAndPosInfo(aPropValue, pausedthread.mThread->currentResult(), &pausedthread.mThread->cursor());
          return true;
        case pausereason_key:
          aPropValue->setStringValue(ScriptCodeThread::pausingName(pausedthread.mThread->pauseReason()));
          return true;
        case pausedat_key:
          aPropValue->setUint64Value(pausedthread.mPausedAt);
          return true;
      }
    }
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


ScriptCodeThreadPtr P44ScriptManager::pausedThreadById(int aThreadId)
{
  for (PausedThreadsVector::iterator pos = mPausedThreads.begin(); pos!=mPausedThreads.end(); ++pos) {
    if (pos->mThread->threadId()==aThreadId) {
      return pos->mThread;
    }
  }
  return nullptr;
}


void P44ScriptManager::removePausedThread(ScriptCodeThreadPtr aThread)
{
  for (PausedThreadsVector::iterator pos = mPausedThreads.begin(); pos!=mPausedThreads.end(); ++pos) {
    if (pos->mThread==aThread) {
      mPausedThreads.erase(pos);
      return;
    }
  }
}


/// script manager specific method handling
bool P44ScriptManager::handleScriptManagerMethod(ErrorPtr &aError, VdcApiRequestPtr aRequest,  const string &aMethod, ApiValuePtr aParams)
{
  if (aMethod=="x-p44-scriptContinue") {
    ApiValuePtr o;
    aError = DsAddressable::checkParam(aParams, "threadid", o);
    if (Error::isOK(aError)) {
      ScriptCodeThreadPtr thread = pausedThreadById(o->int32Value());
      if (!thread) {
        aError = Error::err<VdcApiError>(404, "no such thread");
      }
      else {
        aError = DsAddressable::checkParam(aParams, "mode", o);
        if (Error::isOK(aError)) {
          PausingMode m = ScriptCodeThread::pausingModeNamed(o->stringValue());
          // remove thread from list of paused threads
          removePausedThread(thread);
          // continue
          thread->continueWithMode(m);
          aError = Error::ok(); // ok answer right now
        }
      }
    }
    return true;
  }
  return false;
}


#ifdef _DUMMY2

  /* from vdchost.cpp
  if (aMethod=="x-p44-scriptExec") {
    // direct execution of a script command line in the common main/initscript context
    ApiValuePtr o = aParams->get("script");
    if (o) {
      ScriptHost src(sourcecode|regular|keepvars|concurrently|ephemeralSource, "scriptExec/REPL", nullptr , this);
      src.setSource(o->stringValue());
      src.setSharedMainContext(mVdcHostScriptContext);
      src.registerUnstoredScript("scriptExec");
      src.run(inherit, boost::bind(&VdcHost::scriptExecHandler, this, aRequest, _1));
    }
    else {
      aRequest->sendStatus(NULL); // no script -> NOP
    }
    return ErrorPtr();
  }
  if (aMethod=="x-p44-restartMain") {
    // re-run the main script
    OLOG(LOG_NOTICE, "Re-starting global main script");
    mMainScript.run(stopall, boost::bind(&VdcHost::globalScriptEnds, this, _1, mMainScript.getOriginLabel(), ""), ScriptObjPtr(), Infinite);
    return Error::ok();
  }
  if (aMethod=="x-p44-stopMain") {
    // stop the main script
    OLOG(LOG_NOTICE, "Stopping global main script");
    mVdcHostScriptContext->abort(stopall, new ErrorValue(ScriptError::Aborted, "main script stopped"));
    return Error::ok();
  }
  if (aMethod=="x-p44-checkMain") {
    // check the main script for syntax errors (but do not re-start it)
    ScriptObjPtr res = mMainScript.syntaxcheck();
    ApiValuePtr checkResult = aRequest->newApiValue();
    checkResult->setType(apivalue_object);
    if (!res || !res->isErr()) {
      OLOG(LOG_NOTICE, "Checked global main script: syntax OK");
      checkResult->add("result", checkResult->newNull());
    }
    else {
      OLOG(LOG_NOTICE, "Error in global main script: %s", res->errorValue()->text());
      checkResult->add("error", checkResult->newString(res->errorValue()->getErrorMessage()));
      SourceCursor* cursor = res->cursor();
      if (cursor) {
        checkResult->add("at", checkResult->newUint64(cursor->textpos()));
        checkResult->add("line", checkResult->newUint64(cursor->lineno()));
        checkResult->add("char", checkResult->newUint64(cursor->charpos()));
      }
    }
    aRequest->sendResult(checkResult);
    return ErrorPtr();
  }


  /* from localcontroller */

  {
  if (aMethod=="x-p44-queryScenes") {
    // query scenes usable for a zone/group combination
    ApiValuePtr o;
    aError = DsAddressable::checkParam(aParams, "zoneID", o);
    if (Error::isOK(aError)) {
     DsZoneID zoneID = (DsZoneID)o->uint16Value();
     // get zone
     ZoneDescriptorPtr zone = mLocalZones.getZoneById(zoneID, false);
     if (!zone) {
       aError = WebError::webErr(400, "Zone %d not found (never used, no devices)", (int)zoneID);
     }
     else {
       aError = DsAddressable::checkParam(aParams, "group", o);
       if (Error::isOK(aError) || zoneID==zoneId_global) {
         DsGroup group = zoneID==zoneId_global ? group_undefined : (DsGroup)o->uint16Value();
         // optional scene kind flags
         SceneKind required = scene_preset;
         SceneKind forbidden = scene_extended|scene_area;
         o = aParams->get("required"); if (o) { forbidden = 0; required = o->uint32Value(); } // no auto-exclude when explicitly including
         o = aParams->get("forbidden"); if (o) forbidden = o->uint32Value();
         // query possible scenes for this zone/group
         SceneIdsVector scenes = zone->getZoneScenes(group, required, forbidden);
         // create answer object
         ApiValuePtr result = aRequest->newApiValue();
         result->setType(apivalue_object);
         for (size_t i = 0; i<scenes.size(); ++i) {
           ApiValuePtr s = result->newObject();
           s->add("id", s->newString(scenes[i].stringId()));
           s->add("no", s->newUint64(scenes[i].mSceneNo));
           s->add("name", s->newString(scenes[i].getName()));
           s->add("action", s->newString(scenes[i].getActionName()));
           s->add("kind", s->newUint64(scenes[i].getKindFlags()));
           result->add(string_format("%zu", i), s);
         }
         aRequest->sendResult(result);
         aError.reset(); // make sure we don't send an extra ErrorOK
       }
     }
    }
    return true;
  }
  else if (aMethod=="x-p44-queryGroups") {
   // query groups that are in use (in a zone or globally)
   DsGroupMask groups = 0;
   ApiValuePtr o = aParams->get("zoneID");
   if (o) {
     // specific zone
     DsZoneID zoneID = (DsZoneID)o->uint16Value();
     ZoneDescriptorPtr zone = mLocalZones.getZoneById(zoneID, false);
     if (!zone) {
       aError = WebError::webErr(400, "Zone %d not found (never used, no devices)", (int)zoneID);
       return true;
     }
     groups = zone->getZoneGroups();
   }
   else {
     // globally
     for (DsDeviceMap::iterator pos = mVdcHost.mDSDevices.begin(); pos!=mVdcHost.mDSDevices.end(); ++pos) {
       OutputBehaviourPtr ob = pos->second->getOutput();
       if (ob) groups |= ob->groupMemberships();
     }
   }
   bool allGroups = false;
   o = aParams->get("all"); if (o) allGroups = o->boolValue();
   if (!allGroups) groups = standardRoomGroups(groups);
   // create answer object
   ApiValuePtr result = aRequest->newApiValue();
   result->setType(apivalue_object);
   for (int i = 0; i<64; ++i) {
     if (groups & (1ll<<i)) {
       const GroupDescriptor* gi = groupInfo((DsGroup)i);
       ApiValuePtr g = result->newObject();
       g->add("name", g->newString(gi ? gi->name : "UNKNOWN"));
       g->add("kind", g->newUint64(gi ? gi->kind : 0));
       g->add("color", g->newString(string_format("#%06X", gi ? gi->hexcolor : 0x999999)));
       result->add(string_format("%d", i), g);
     }
   }
   aRequest->sendResult(result);
   aError.reset(); // make sure we don't send an extra ErrorOK
   return true;
  }
  else if (aMethod=="x-p44-checkTriggerCondition" || aMethod=="x-p44-testTriggerAction" || aMethod=="x-p44-stopTriggerAction") {
   // check the trigger condition of a trigger
   ApiValuePtr o;
   aError = DsAddressable::checkParam(aParams, "triggerID", o);
   if (Error::isOK(aError)) {
     int triggerId = o->int32Value();
     TriggerPtr trig = mLocalTriggers.getTrigger(triggerId);
     if (!trig) {
       aError = WebError::webErr(400, "Trigger %d not found", triggerId);
     }
     else {
       if (aMethod=="x-p44-testTriggerAction") {
         ScriptObjPtr triggerParam;
         ApiValuePtr o = aParams->get("triggerParam");
         if (o) {
           // has a trigger parameter
           triggerParam = new StringValue(o->stringValue());
         }
         trig->handleTestActions(aRequest, triggerParam); // asynchronous!
       }
       else if (aMethod=="x-p44-stopTriggerAction") {
         trig->stopActions();
         aError = Error::ok();
       }
       else {
         aError = trig->handleCheckCondition(aRequest);
       }
     }
   }
   return true;
  }
  else {
   return false; // unknown at the localController level
  }
}


#endif // _DUMMY2



#ifdef _DUMMY

// Evaluator Action

// - x-p44-testEvaluatorAction

else if (aMethod=="x-p44-testEvaluatorAction") {
  ApiValuePtr vp = aParams->get("result");
  Tristate state = mEvaluatorState;
  if (vp) {
    state = vp->boolValue() ? yes : no;
  }
  // now test
  evaluatorSettings()->mEvaluatorContext->setMemberByName("result", new BoolValue(state==yes));
  evaluatorSettings()->mAction.run(stopall, boost::bind(&EvaluatorDevice::testActionExecuted, this, aRequest, _1), ScriptObjPtr(), Infinite);
  return ErrorPtr();
}

// - x-p44-stopEvaluatorAction

else if (aMethod=="x-p44-stopEvaluatorAction") {
  evaluatorSettings()->mEvaluatorContext->abort(stopall, new ErrorValue(ScriptError::Aborted, "evaluator action stopped"));
  return Error::ok();
}


void EvaluatorDevice::testActionExecuted(VdcApiRequestPtr aRequest, ScriptObjPtr aResult)
{
  ApiValuePtr testResult = aRequest->newApiValue();
  testResult->setType(apivalue_object);
  if (!aResult->isErr()) {
    testResult->add("result", testResult->newScriptValue(aResult));
  }
  else {
    testResult->add("error", testResult->newString(aResult->errorValue()->getErrorMessage()));
    SourceCursor* cursor = aResult->cursor();
    if (cursor) {
      testResult->add("at", testResult->newUint64(cursor->textpos()));
      testResult->add("line", testResult->newUint64(cursor->lineno()));
      testResult->add("char", testResult->newUint64(cursor->charpos()));
    }
  }
  aRequest->sendResult(testResult);
}


// Scripted Device Implementation

// - x-p44-restartImpl

if (aMethod=="x-p44-restartImpl") {
  // re-run the device implementation script
  restartImplementation();
  return Error::ok();
}




// - x-p44-checkImpl

if (aMethod=="x-p44-checkImpl") {
  // check the implementation script for syntax errors (but do not re-start it)
  ScriptObjPtr res = mImplementation.mScript.syntaxcheck();
  ApiValuePtr checkResult = aRequest->newApiValue();
  checkResult->setType(apivalue_object);
  if (!res || !res->isErr()) {
    OLOG(LOG_NOTICE, "Checked implementation script: syntax OK");
    checkResult->add("result", checkResult->newNull());
  }
  else {
    OLOG(LOG_NOTICE, "Error in implementation: %s", res->errorValue()->text());
    checkResult->add("error", checkResult->newString(res->errorValue()->getErrorMessage()));
    SourceCursor* cursor = res->cursor();
    if (cursor) {
      checkResult->add("at", checkResult->newUint64(cursor->textpos()));
      checkResult->add("line", checkResult->newUint64(cursor->lineno()));
      checkResult->add("char", checkResult->newUint64(cursor->charpos()));
    }
  }
  aRequest->sendResult(checkResult);
  return ErrorPtr();
}

// - x-p44-stopImpl

if (aMethod=="x-p44-stopImpl") {
  // stop the device implementation script
  stopImplementation();
  return Error::ok();
}



void ScriptedDevice::restartImplementation()
{
  OLOG(LOG_NOTICE, "(Re-)starting device implementation script");
  mImplementation.mRestartTicket.cancel();
  mImplementation.mContext->clearVars(); // clear vars and (especially) context local handlers
  mImplementation.mScript.run(stopall, boost::bind(&ScriptedDevice::implementationEnds, this, _1), ScriptObjPtr(), Infinite);
}


void ScriptedDevice::stopImplementation()
{
  OLOG(LOG_NOTICE, "Stopping device implementation script");
  mImplementation.mRestartTicket.cancel();
  if (!mImplementation.mContext->abort(stopall, new ErrorValue(ScriptError::Aborted, "device implementation script stopped"))) {
    // nothing to abort, make sure handlers are gone (otherwise, they will get cleared in implementationEnds())
    mImplementation.mContext->clearVars();
  }
}



// Scene:

// - start: just callScene()

// - stop Scene

void OutputBehaviour::stopSceneActions()
{
  #if ENABLE_SCENE_SCRIPT
  mDevice.getDeviceScriptContext()->abort(stopall, new ErrorValue(ScriptError::Aborted, "scene actions stopped"));
  #endif // ENABLE_SCENE_SCRIPT
}


// Trigger:

// - x-p44-testTriggerAction

ErrorPtr Trigger::handleTestActions(VdcApiRequestPtr aRequest, ScriptObjPtr aTriggerParam)
{
  ScriptObjPtr threadLocals;
  if (aTriggerParam) {
    threadLocals = new SimpleVarContainer();
    threadLocals->setMemberByName("triggerparam", aTriggerParam);
  }
  mTriggerAction.run(stopall, boost::bind(&Trigger::testTriggerActionExecuted, this, aRequest, _1), threadLocals, Infinite);
  return ErrorPtr(); // will send result later
}


void Trigger::testTriggerActionExecuted(VdcApiRequestPtr aRequest, ScriptObjPtr aResult)
{
  ApiValuePtr testResult = aRequest->newApiValue();
  testResult->setType(apivalue_object);
  if (!aResult->isErr()) {
    testResult->add("result", testResult->newScriptValue(aResult));
  }
  else {
    testResult->add("error", testResult->newString(aResult->errorValue()->getErrorMessage()));
    SourceCursor* cursor = aResult->cursor();
    if (cursor) {
      testResult->add("at", testResult->newUint64(cursor->textpos()));
      testResult->add("line", testResult->newUint64(cursor->lineno()));
      testResult->add("char", testResult->newUint64(cursor->charpos()));
    }
  }
  aRequest->sendResult(testResult);
}


// - x-p44-stopTriggerAction

void Trigger::stopActions()
{
  mTriggerContext->abort(stopall, new ErrorValue(ScriptError::Aborted, "trigger action stopped"));
}


#endif // _DUMMY



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
