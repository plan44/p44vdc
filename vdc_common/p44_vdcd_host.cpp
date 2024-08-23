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
      mGlobalError.reset(); // report as OK
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
  webUiPort(0),
  mPlayground(sourcecode|regular|keepvars, "playground", "p44script playground", this)
{
  #if P44SCRIPT_REGISTERED_SOURCE
  mScriptManager = new P44ScriptManager(&StandardScriptingDomain::sharedDomain());
  StandardScriptingDomain::setStandardScriptingDomain(&(mScriptManager->domain()));
  // playground script in vdc host script (mainscript) context
  mPlayground.setScriptHostUid("p44script_playground", true); // unstored
  mPlayground.setSharedMainContext(mVdcHostScriptContext);
  mPlayground.loadSource();
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

void P44VdcHost::enableConfigApi(const char *aServiceOrPort, bool aNonLocalAllowed, int aProtocolFamily)
{
  if (!mConfigApi) {
    // can be enabled only once
    mConfigApi = new P44CfgApiConnection(new SocketComm(MainLoop::currentMainLoop()));
    mConfigApi->mJsonApiServer->setConnectionParams(NULL, aServiceOrPort, SOCK_STREAM, aProtocolFamily);
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
      #if ENABLE_LEDCHAIN
      else if (apiselector=="leddata") {
        // raw LED data for a LED chain
        JsonObjectPtr o;
        if (mLedChainArrangement) {
          // which view to look at
          P44ViewPtr view;
          if (request->get("view", o)) {
            view = mLedChainArrangement->getRootView()->findView(o->stringValue());
          }
          if (!view) {
            view = mLedChainArrangement->getRootView();
          }
          if (request->get("dx", o)) {
            // specific area from view
            PixelRect area;
            area.dx = o->int32Value();
            area.dy = 1;
            if (area.dx>0 && area.dy>0) {
              if (request->get("dy", o)) area.dy = o->int32Value();
              area.x = 0;
              if (request->get("x", o)) area.x = o->int32Value();
              area.y = 0;
              if (request->get("y", o)) area.y = o->int32Value();
              string rawrgb;
              view->ledRGBdata(rawrgb, area);
              rawrgb += "\n"; // message terminator
              aJsonComm->sendRaw(rawrgb);
              return;
            }
          }
          else if (request->get("status", o) && o->boolValue()) {
            // get view status
            sendJsonApiResponse(aJsonComm, view->viewStatus(), ErrorPtr(), reqid);
            return;
          }
          else if (request->get("configure", o)) {
            // configure view
            aError = Error::ok(view->configureView(o));
          }
          else if (request->get("cover", o) && o->boolValue()) {
            // return the covered area
            JsonObjectPtr res = JsonObject::newObj();
            PixelRect cover = mLedChainArrangement->totalCover();
            res->add("x", JsonObject::newInt32(cover.x));
            res->add("y", JsonObject::newInt32(cover.y));
            res->add("dx", JsonObject::newInt32(cover.dx));
            res->add("dy", JsonObject::newInt32(cover.dy));
            sendJsonApiResponse(aJsonComm, res, ErrorPtr(), reqid);
            return;
          }
          else {
            aError = Error::err<P44VdcError>(400, "invalid leddata parameters");
          }
        }
        else {
          aError = Error::err<P44VdcError>(400, "LED subsystem not initialized");
        }
      }
      #endif
      #if P44SCRIPT_IMPLEMENTED_CUSTOM_API
      #define SCRIPTAPI_NAME "scriptapi"
      else if (uequals(apiselector.c_str(), SCRIPTAPI_NAME, strlen(SCRIPTAPI_NAME))) {
        // scripted parts of the (web) API
        if (!mScriptedApiLookup.hasSinks()) {
          // no script API active
          aError = WebError::webErr(500, "script API not active");
        }
        else {
          // API active, send request object to event sinks
          // - extract endpoint (part of url beyond SCRIPTAPI_NAME)
          if (apiselector.size()>strlen(SCRIPTAPI_NAME)+1 && apiselector[strlen(SCRIPTAPI_NAME)]=='/') {
            request->add("endpoint", request->newString(apiselector.substr(strlen(SCRIPTAPI_NAME)+1)));
          }
          if (!mScriptedApiLookup.sendEvent(new ApiRequestObj(aJsonComm, request))) {
            // no sink reached - probably means no matching endpoint
            aError = WebError::webErr(404, "unknown script API endpoint");
          }
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


void P44VdcHost::enableBridgeApi(const char *aServiceOrPort, bool aNonLocalAllowed, int aProtocolFamily)
{
  if (!mBridgeApi) {
    // can be enabled only once
    // - enabling bridge API also instantiates bridge info
    getBridgeInfo(true);
    // - enable API
    mBridgeApi = new BridgeApiConnection(new SocketComm(MainLoop::currentMainLoop()));
    mBridgeApi->mJsonApiServer->setConnectionParams(NULL, aServiceOrPort, SOCK_STREAM, aProtocolFamily);
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
  bridgetype_key,
  config_url_key,
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
    { "bridgetype", apivalue_string, bridgetype_key, OKEY(bridgeinfo_key) },
    { "configURL", apivalue_string, config_url_key, OKEY(bridgeinfo_key) },
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
        case bridgetype_key: aPropValue->setStringValue(mBridgeType); return true;
        case config_url_key: aPropValue->setStringValue(mConfigURL); return true;
        case qrcodedata_key: aPropValue->setStringValue(mQRCodeData); return true;
        case manualpairingcode_key: aPropValue->setStringValue(mManualPairingCode); return true;
        case started_key: aPropValue->setBoolValue(mP44VdcHost.numBridgeApiClients()>0 && mStarted); return true;
        case commissionable_key: aPropValue->setBoolValue(mCommissionable); return true;
        case connected_key: aPropValue->setBoolValue(mP44VdcHost.numBridgeApiClients()>0); return true;
      }
    }
    else {
      switch (aPropertyDescriptor->fieldKey()) {
        case bridgetype_key: mBridgeType = aPropValue->stringValue(); return true;
        case config_url_key: mConfigURL = aPropValue->stringValue(); return true;
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


PropertyContainerPtr P44VdcHost::getContainer(const PropertyDescriptorPtr aPropertyDescriptor, int &aDomain)
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
  mScriptingDomain(aScriptingDomain),
  mDebuggerTimeout(Never)
{
  assert(mScriptingDomain);
  mScriptingDomain->setPauseHandler(boost::bind(&P44ScriptManager::pausedHandler, this, _1));
}


void P44ScriptManager::pausedHandler(ScriptCodeThreadPtr aPausedThread)
{
  if (aPausedThread->pauseReason()==running) {
    OLOG(LOG_ERR, "non-paused thread reported paused")
    return;
  }
  // actually paused
  P44PausedThread pausedThread;
  pausedThread.mPausedAt = MainLoop::now();
  pausedThread.mThread = aPausedThread;
  pausedThread.mSourceHost = mScriptingDomain->getHostForThread(aPausedThread);
  mPausedThreads.push_back(pausedThread);
}


bool P44ScriptManager::isDebugging() const
{
  return mScriptingDomain && mScriptingDomain->defaultPausingMode()>running;
}


void P44ScriptManager::setDebugging(bool aDebug)
{
  if (!mScriptingDomain) return;
  if (aDebug!=isDebugging()) {
    mScriptingDomain->setDefaultPausingMode(aDebug ? breakpoint : running);
    if (aDebug) {
      // enable log collector
      SETLOGHANDLER(boost::bind(&P44ScriptManager::logCollectHandler, this, _1, _2, _3), true);
    }
    else {
      // disable log collector
      SETLOGHANDLER(NoOP, true);
      mCollectedLogText.clear(); // free the storage
      // restart all paused threads
      OLOG(LOG_WARNING, "Debugging mode disabled: all paused threads continue running now");
      while (mPausedThreads.size()>0) {
        PausedThreadsVector::iterator pos = mPausedThreads.begin();
        pos->mThread->continueWithMode(running);
        mPausedThreads.erase(pos);
      }
    }
  }
}


#define MAX_COLLECTED_LOG_SIZE 30000
#define OVERFLOW_CUT_SIZE 5000

void P44ScriptManager::logCollectHandler(int aLevel, const char *aLinePrefix, const char *aLogMessage)
{
  mCollectedLogText += aLinePrefix;
  mCollectedLogText += aLogMessage;
  mCollectedLogText += "\n";
  if (mCollectedLogText.size()>MAX_COLLECTED_LOG_SIZE) {
    mCollectedLogText.erase(0,OVERFLOW_CUT_SIZE);
    mCollectedLogText.insert(0, "\n...overflow - some lines lost...\n");
  }
}



void P44ScriptManager::setResultAndPosInfo(ApiValuePtr aIntoApiValue, ScriptObjPtr aResult, const SourceCursor* aCursorP)
{
  aIntoApiValue->setType(apivalue_object);
  if (aResult) {
    aResult = aResult->calculationValue(); // make sure we have the calculation value
    if (!aResult->isErr()) {
      aIntoApiValue->add("result", aIntoApiValue->newScriptValue(aResult));
    }
    else {
      aIntoApiValue->add("error", aIntoApiValue->newString(aResult->errorValue()->text()));
    }
    aIntoApiValue->add("annotation", aIntoApiValue->newString(aResult->getAnnotation()));
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
  logtext_key,
  loglevel_key,
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
  isScript_key,
  breakpoints_key,
  numScriptHostProperties
};

static char sourcehostslist_key;
static char sourcehost_key;

static char breakpoints_list_key;

enum {
  // thread level properties
  threadid_key,
  threadtitle_key,
  sourcehostuid_key,
  sourcehosttitle_key,
  result_key,
  pausereason_key,
  pausedat_key,
  numPausedThreadProperties
};

static char pausedthreadslist_key;
static char pausedthread_key;


int P44ScriptManager::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(sourcehostslist_key)) {
    // script host container
    return static_cast<int>(domain().numRegisteredHosts());
  }
  else if (aParentDescriptor->hasObjectKey(breakpoints_list_key)) {
    // breakpoints (all type of sourcehosts could have breakpoints, not all actually support them)
    SourceHostPtr host = domain().getHostByIndex(aParentDescriptor->mParentDescriptor->fieldKey());
    return static_cast<int>(host->numBreakpoints());
  }
  else if (aParentDescriptor->hasObjectKey(pausedthreadslist_key)) {
    // paused threads list
    debuggerWatchdog(); ///< querying paused threads also triggers debugger watchdog
    return static_cast<int>(mPausedThreads.size());
  }
  else if (aParentDescriptor->hasObjectKey(sourcehost_key)) {
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


PropertyContainerPtr P44ScriptManager::getContainer(const PropertyDescriptorPtr aPropertyDescriptor, int &aDomain)
{
  // all subcontainers are handled by myself
  return PropertyContainerPtr(this);
}


PropertyDescriptorPtr P44ScriptManager::getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(breakpoints_list_key)) {
    // array-like container
    PropertyDescriptorPtr propDesc;
    bool lineNoSpecified = getNextPropIndex(aPropMatch, aStartIndex);
    int n = numProps(aDomain, aParentDescriptor);
    if (lineNoSpecified || (aStartIndex!=PROPINDEX_NONE && aStartIndex<n)) {
      int lineNo;
      if (lineNoSpecified) {
        // what we scanned as startindex is the lineNo
        lineNo = aStartIndex;
        aStartIndex = 0;
      }
      else {
        SourceHostPtr host = domain().getHostByIndex(aParentDescriptor->mParentDescriptor->fieldKey());
        if (!host) { aStartIndex = PROPINDEX_NONE; return propDesc; } // safety only, because numProps should prevent us being called for non-scripts
        // Note: we need to enumerate the set, so we must convert it to a vector here, which is
        //   acceptable when assuming a small number of breakpoints (which IS realistic)
        vector<int> bpVec(host->breakpoints()->begin(), host->breakpoints()->end());
        lineNo = bpVec[aStartIndex];
      }
      DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
      descP->mPropertyName = string_format("%d", lineNo); // name of the breakpoint is the line
      descP->mPropertyType = aParentDescriptor->type();
      descP->mPropertyFieldKey = lineNo; // key is the line number (by which we can easily obtain true or false value)
      descP->mPropertyObjectKey = aParentDescriptor->objectKey();
      propDesc = PropertyDescriptorPtr(descP);
      // advance index
      aStartIndex++;
    }
    if (aStartIndex>=n || lineNoSpecified) {
      // no more descriptors OR specific descriptor accessed -> no "next" descriptor
      aStartIndex = PROPINDEX_NONE;
    }
    return propDesc;
  }
  // None of the containers within Device - let base class handle Device-Level properties
  return inherited::getDescriptorByName(aPropMatch, aStartIndex, aDomain, aMode, aParentDescriptor);
}




PropertyDescriptorPtr P44ScriptManager::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // scriptmanager level properties
  static const PropertyDescription managerProperties[numScriptManagerProperties] = {
    // common device properties
    { "sources", apivalue_object, sources_key, OKEY(sourcehostslist_key) },
    { "pausedthreads", apivalue_object, pausedthreads_key, OKEY(pausedthreadslist_key) },
    { "debugging", apivalue_bool, debugging_key, OKEY(scriptmanager_key) },
    { "logtext", apivalue_string, logtext_key, OKEY(scriptmanager_key) },
    { "loglevel", apivalue_int64, loglevel_key, OKEY(scriptmanager_key) },
  };
  // sourcehost level properties
  static const PropertyDescription scripthostProperties[numScriptHostProperties] = {
    { "sourcetext", apivalue_string, sourcetext_key, OKEY(sourcehost_key) },
    { "title", apivalue_string, sourcetitle_key, OKEY(sourcehost_key) },
    { "originlabel", apivalue_string, originlabel_key, OKEY(sourcehost_key) },
    { "contexttype", apivalue_string, contexttype_key, OKEY(sourcehost_key) },
    { "contextid", apivalue_string, contextid_key, OKEY(sourcehost_key) },
    { "contextname", apivalue_string, contextname_key, OKEY(sourcehost_key) },
    { "logprefix", apivalue_string, logprefix_key, OKEY(sourcehost_key) },
    { "unstored", apivalue_bool, unstored_key, OKEY(sourcehost_key) },
    { "script", apivalue_bool, isScript_key, OKEY(sourcehost_key) },
    { "breakpoints", apivalue_bool+propflag_container, breakpoints_key, OKEY(breakpoints_list_key) },
  };
  // pausedthread level properties
  static const PropertyDescription pausedthreadProperties[numPausedThreadProperties] = {
    { "threadid", apivalue_uint64, threadid_key, OKEY(pausedthread_key) },
    { "title", apivalue_string, threadtitle_key, OKEY(pausedthread_key) },
    { "sourceHostId", apivalue_string, sourcehostuid_key, OKEY(pausedthread_key) },
    { "sourceTitle", apivalue_string, sourcehosttitle_key, OKEY(pausedthread_key) },
    { "result", apivalue_null, result_key, OKEY(pausedthread_key) },
    { "pausereason", apivalue_string, pausereason_key, OKEY(pausedthread_key) },
    { "pausedAt", apivalue_uint64, pausedat_key, OKEY(pausedthread_key) },
  };
  // C++ object manages different levels, check objects
  if (aParentDescriptor->hasObjectKey(sourcehostslist_key)) {
    // script hosts by their uid
    SourceHostPtr host = domain().getHostByIndex(aPropIndex);
    if (!host) return nullptr;
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->mPropertyName = host->getSourceUid();
    descP->mPropertyType = aParentDescriptor->type();
    descP->mPropertyFieldKey = aPropIndex;
    descP->mPropertyObjectKey = OKEY(sourcehost_key);
    return descP;
  }
  else if (aParentDescriptor->hasObjectKey(pausedthreadslist_key)) {
    // paused threads by thread id
    if (aPropIndex>=mPausedThreads.size()) return nullptr;
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->mPropertyName = string_format("%d", aPropIndex);
    descP->mPropertyType = aParentDescriptor->type();
    descP->mPropertyFieldKey = aPropIndex;
    descP->mPropertyObjectKey = OKEY(pausedthread_key);
    return descP;
  }
  else if (aParentDescriptor->hasObjectKey(sourcehost_key)) {
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
        case debugging_key:
          aPropValue->setBoolValue(isDebugging());
          return true;
        case logtext_key:
          aPropValue->setStringValue(mCollectedLogText);
          mCollectedLogText.clear();
          return true;
        case loglevel_key:
          aPropValue->setInt32Value(LOGLEVEL);
          return true;
      }
    }
    else {
      switch (aPropertyDescriptor->fieldKey()) {
        case debugging_key:
          setDebugging(aPropValue->boolValue());
          return true;
      }
    }
  }
  else if (aPropertyDescriptor->hasObjectKey(sourcehost_key)) {
    // script host level property
    // - get the host
    SourceHostPtr host = domain().getHostByIndex(aPropertyDescriptor->mParentDescriptor->fieldKey());
    if (host) {
      if (aMode==access_read) {
        // read properties
        P44LoggingObj* cobj;
        switch (aPropertyDescriptor->fieldKey()) {
          case sourcetext_key:
            aPropValue->setStringValue(host->getSource());
            return true;
          case sourcetitle_key:
            aPropValue->setStringValue(host->getSourceTitle());
            return true;
          case originlabel_key:
            aPropValue->setStringValue(host->getOriginLabel());
            return true;
          case contexttype_key:
            aPropValue->setStringValue(host->getContextType());
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
          case isScript_key:
            aPropValue->setBoolValue(host->isScript());
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
  else if (aPropertyDescriptor->hasObjectKey(breakpoints_list_key)) {
    // all sourcehosts have the breakpoint() method, but not all actually support them (by now, only scripts and includes)
    SourceHostPtr host = domain().getHostByIndex(aPropertyDescriptor->mParentDescriptor->mParentDescriptor->fieldKey());
    if (!host) return false; // should not happen
    size_t line = aPropertyDescriptor->fieldKey();
    if (!host->breakpoints()) return false;
    if (aMode==access_read) {
      // breakpoint
      if (host->breakpoints()->find(line)!=host->breakpoints()->end()) {
        aPropValue->setBoolValue(true);
        return true;
      }
      return false;
    }
    else {
      // write breakpoint flag on line
      if (aPropValue->boolValue()) host->breakpoints()->insert(line);
      else host->breakpoints()->erase(line);
      return true;
    }
  }
  else if (aPropertyDescriptor->hasObjectKey(pausedthread_key)) {
    // paused thread level property
    // - get the paused thread
    P44PausedThread& pausedthread = mPausedThreads[aPropertyDescriptor->mParentDescriptor->fieldKey()];
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case sourcehostuid_key:
          aPropValue->setStringValue(pausedthread.mSourceHost->getSourceUid());
          return true;
        case sourcehosttitle_key:
          aPropValue->setStringValue(pausedthread.mSourceHost->getSourceTitle());
          return true;
        case threadid_key:
          aPropValue->setUint32Value(pausedthread.mThread->threadId());
          return true;
        case threadtitle_key:
          aPropValue->setStringValue(string_format("thread_%04d",pausedthread.mThread->threadId()));
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


void P44ScriptManager::debuggerWatchdog()
{
  // re-arm watchdog
  if (mDebuggerTimeout>0) {
    mDebuggerTimer.executeOnce(boost::bind(&P44ScriptManager::debuggerTimedOut, this), mDebuggerTimeout);
  }
}


void P44ScriptManager::debuggerTimedOut()
{
  OLOG(LOG_ERR, "Debugger API timeout -> shutting down debugger");
  setDebugging(false);
}


/// script manager specific method handling
bool P44ScriptManager::handleScriptManagerMethod(ErrorPtr &aError, VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  if (aMethod=="x-p44-debuggerActivate") {
    ApiValuePtr o;
    aError = DsAddressable::checkParam(aParams, "timeout", o);
    if (Error::isOK(aError)) {
      mDebuggerTimeout = o->doubleValue()*Second;
      setDebugging(true);
      debuggerWatchdog();
      aError = Error::ok(); // ok answer right now
    }
    return true;
  }
  else if (aMethod=="x-p44-scriptContinue") {
    ApiValuePtr o;
    aError = DsAddressable::checkParam(aParams, "threadid", o);
    if (Error::isOK(aError)) {
      debuggerWatchdog();
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
  else if (aMethod=="x-p44-scriptCommand") {
    ApiValuePtr o;
    aError = DsAddressable::checkParam(aParams, "scriptsourceuid", o);
    if (Error::isOK(aError)) {
      debuggerWatchdog();
      ScriptHostPtr script = boost::dynamic_pointer_cast<ScriptHost>(domain().getHostByUid(o->stringValue()));
      if (!script) {
        aError = Error::err<VdcApiError>(404, "no such script");
      }
      else {
        aError = DsAddressable::checkParam(aParams, "command", o);
        if (Error::isOK(aError)) {
          string cmd = o->stringValue();
          ScriptCommand c;
          if (cmd=="check") c = check;
          else if (cmd=="debug") c = debug;
          else if (cmd=="restart") c = restart;
          else if (cmd=="start") c = start;
          else if (cmd=="stop") c = stop;
          else {
            aError = Error::err<VdcApiError>(400, "unknown script command");
          }
          // always return as result, error or not
          ScriptObjPtr result;
          if (Error::notOK(aError)) {
            result = new ErrorValue(aError);
          }
          else {
            o = aParams->get("evalresult");
            ScriptObjPtr result;
            if (o && o->boolValue()) {
              // run the script and capture the final evaluation result
              c = static_cast<ScriptCommand>(c|evaluate);
              script->runCommand(c, boost::bind(&P44ScriptManager::scriptResultReport, this, aRequest, _1), nullptr);
            }
            else {
              // run the script and have its content see the result via default result handler set in scripthost
              ScriptObjPtr result = script->runCommand(c);
              ApiValuePtr ans = aParams->newObject();
              scriptResultReport(aRequest, result);
            }
          }
          aError.reset(); // result already sent or will be sent later
        }
      }
    }
    return true;
  }
  else if (aMethod=="x-p44-scriptExec") {
    // Note: replaces x-p44-scriptExec in vdchost, not backwards compatible
    // direct execution of script code, optionally in a thread's or scripthost's context
    ApiValuePtr o;
    aError = DsAddressable::checkParam(aParams, "scriptcode", o);
    if (Error::isOK(aError)) {
      debuggerWatchdog();
      string code = trimWhiteSpace(o->stringValue());
      ScriptCodeThreadPtr thread;
      ScriptCodeContextPtr ctx;
      // need thread or script context
      o = aParams->get("threadid");
      if (o) {
        // in a paused thread's context
        thread = pausedThreadById(o->int32Value());
        if (thread) {
          ctx = thread->owner();
        }
        else {
          aError = Error::err<VdcApiError>(404, "no such thread");
        }
      }
      else {
        o = aParams->get("scriptsourceuid");
        if (o) {
          // in a scripthost's shared main context
          ScriptHostPtr script = boost::dynamic_pointer_cast<ScriptHost>(domain().getHostByUid(o->stringValue()));
          if (script) {
            ctx = script->sharedMainContext();
          }
          if (!script || !ctx) {
            aError = Error::err<VdcApiError>(404, "no context found for this scriptsourceuid");
          }
        }
      }
      MLMicroSeconds maxRunTime = 5*Second;
      o = aParams->get("maxruntime");
      if (o) maxRunTime = o->doubleValue()<0 ? Infinite : o->doubleValue()*Second;
      if (Error::isOK(aError)) {
        if (code.empty()) {
          scriptResultReport(aRequest, new AnnotatedNullValue("nothing to execute"));
        }
        else {
          // use domain's default context if none set
          if (!ctx) ctx = domain().newContext(); // independent context
          // non-debuggable source container (text not available to IDE editor)
          SourceContainerPtr source = new SourceContainer("scriptExec", this, code);
          // get the main context
          ScriptMainContextPtr mctx = ctx->scriptmain();
          assert(mctx.get());
          // compile
          EvaluationFlags flags = sourcecode|regular|keepvars|concurrently|ephemeralSource|neverpause|implicitreturn;
          ScriptCompiler compiler(ctx->domain());
          CompiledFunctionPtr compiledcode = new CompiledFunction("interactive");
          ScriptObjPtr res = compiler.compile(source, compiledcode, flags, mctx);
          if (res->isErr()) {
            // compiler error
            scriptResultReport(aRequest, res);
          }
          else {
            // now execute the code in the very context
            ctx->execute(
              compiledcode, flags,
              boost::bind(&P44ScriptManager::scriptResultReport, this, aRequest, _1),
              nullptr, // not chained
              thread ? thread->threadLocals() : nullptr, // also see into the current thread's threadvars
              maxRunTime // run time limit
            );
          }
        }
        aError.reset(); // result is or will be sent by scriptExecHandler
      }
    }
    return true;
  }
  return false;
}


void P44ScriptManager::scriptResultReport(VdcApiRequestPtr aRequest, ScriptObjPtr aResult)
{
  ApiValuePtr ans = aRequest->newApiValue();
  setResultAndPosInfo(ans, aResult);
  aRequest->sendResult(ans);
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
FUNC_ARG_DEFS(answer, { anyvalid|error|optionalarg } );
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
  FUNC_DEF_W_ARG(answer, executable|anyvalid);


const ScriptObjPtr ApiRequestObj::memberByName(const string aName, TypeInfo aMemberAccessFlags) const
{
  ScriptObjPtr val;
  if (uequals(aName, "answer")) {
    val = new BuiltinFunctionObj(&answer_desc, const_cast<ApiRequestObj*>(this), NULL);
  }
  else {
    val = inherited::memberByName(aName, aMemberAccessFlags);
  }
  return val;
}


class WebRequestUriFilter : public EventFilter
{
  string mEndPoint;
  string mPeer;
public:
  WebRequestUriFilter(const string aEndPoint, const string aPeer) : mEndPoint(aEndPoint), mPeer(aPeer) {};

  virtual bool filteredEventObj(ScriptObjPtr &aEventObj) P44_OVERRIDE
  {
    if (!aEventObj) return false;
    if (!mEndPoint.empty()) {
      // we have an endpoint filter to check
      ScriptObjPtr ep = aEventObj->memberByName("endpoint");
      if (!(ep && ep->stringValue()==mEndPoint)) return false; // no endpoint match
    }
    if (!mPeer.empty()) {
      // we have a peer filter to check
      ScriptObjPtr peer = aEventObj->memberByName("peer");
      if (!(peer && peer->stringValue()==mPeer)) return false; // no peer match
    }
    return true;
  }
};


// webrequest()                event source for (script API) web request
// webrequest(endpoint)        filtered event source for specific sub-endpoint of the script API
// webrequest(endpoint, peer)  filtered event source for specific sub-endpoint of the script API and coming from a specific peer
FUNC_ARG_DEFS(webrequest, { text|optionalarg }, { text|optionalarg } );
static void webrequest_func(BuiltinFunctionContextPtr f)
{
  // return API request event source place holder, actual value will be delivered via event
  P44VdcHost* h = dynamic_cast<P44VdcHost*>(VdcHost::sharedVdcHost().get());
  string ep, peer;
  if (f->arg(0)->defined()) ep = f->arg(0)->stringValue();
  if (f->arg(1)->defined()) peer = f->arg(1)->stringValue();
  f->finish(new OneShotEventNullValue(&h->mScriptedApiLookup, "web request", new WebRequestUriFilter(ep, peer)));
}

static const BuiltinMemberDescriptor scriptApiGlobals[] = {
  FUNC_DEF_W_ARG(webrequest, executable|structured|null),
  { NULL } // terminator
};


ScriptApiLookup::ScriptApiLookup() : inherited(scriptApiGlobals)
{
}

#endif // P44SCRIPT_IMPLEMENTED_CUSTOM_API
