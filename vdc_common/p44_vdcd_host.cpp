//
//  Copyright (c) 2014-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


// MARK: - self test runner

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
    if (Error::notOK(aError)) {
      if (!aError->isError("Vdc", VdcError::NoHWTested)) {
        // test failed
        LOG(LOG_ERR, "****** Test of '%s' FAILED with error: %s", nextVdc->second->vdcClassIdentifier(), aError->text());
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
      LOG(LOG_ERR, "Self test has FAILED: %s", globalError->text());
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


// MARK: - P44VdcHost


P44VdcHost::P44VdcHost(bool aWithLocalController, bool aWithPersistentChannels) :
  inherited(aWithLocalController, aWithPersistentChannels),
  webUiPort(0)
{
  #if ENABLE_P44SCRIPT
  vdcHostScriptContext = StandardScriptingDomain::sharedDomain().newContext();
  // vdchost is the global context for this app, so register its members in the standard scripting
  // domain making them accessible in all scripts
  StandardScriptingDomain::sharedDomain().registerMemberLookup(P44VdcHostLookup::sharedLookup());
  #endif
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
  #if ENABLE_JSONCFGAPI
  // start config API, if we have one
  if (configApiServer) {
    configApiServer->startServer(boost::bind(&P44VdcHost::configApiConnectionHandler, this, _1), 3);
  }
  #endif
  #if ENABLE_UBUS
  // start ubus API, if we have it
  if (ubusApiServer) {
    ubusApiServer->startServer();
  }
  #endif
  // now init rest of vdc host
  inherited::initialize(aCompletedCB, aFactoryReset);
}



void P44VdcHost::handleGlobalEvent(VdchostEvent aEvent)
{
  if (aEvent==vdchost_devices_initialized) {
    #if P44SCRIPT_FULL_SUPPORT || EXPRESSION_SCRIPT_SUPPORT
    // this is the moment to (re)run global scripts
    runInitScripts();
    #endif // EXPRESSION_SCRIPT_SUPPORT
  }
  inherited::handleGlobalEvent(aEvent);
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
  if (!ubusApiServer) {
    // can be enabled only once
    ubusApiServer = UbusServerPtr(new UbusServer(MainLoop::currentMainLoop()));
    UbusObjectPtr u = new UbusObject("vdcd", boost::bind(&P44VdcHost::ubusApiRequestHandler, this, _1, _2, _3));
    u->addMethod("api", vdcapi_policy);
//    u->addMethod("cfg", cfgapi_policy);
    ubusApiServer->registerObject(u);
  }
}


void P44VdcHost::ubusApiRequestHandler(UbusRequestPtr aUbusRequest, const string aMethod, JsonObjectPtr aJsonRequest)
{
  signalActivity(); // ubus API calls are activity as well
  if (!aJsonRequest) {
    // we always need a ubus message containing actual call method/notification and params
    aUbusRequest->sendResponse(JsonObjectPtr(), UBUS_STATUS_INVALID_ARGUMENT);
    return;
  }
  LOG(LOG_DEBUG, "ubus -> vdcd (JSON) request received: %s", aJsonRequest->c_strValue());
  ErrorPtr err;
  UbusApiRequestPtr request = UbusApiRequestPtr(new UbusApiRequest(aUbusRequest));
  if (aMethod=="api") {
    string cmd;
    bool isMethod = false;
    // get method/notification and params
    JsonObjectPtr m = aJsonRequest->get("method");
    if (m) {
      // is a method call, expects answer
      isMethod = true;
    }
    else {
      // not method, may be notification
      m = aJsonRequest->get("notification");
    }
    if (!m) {
      err = Error::err<P44VdcError>(400, "invalid request, must specify 'method' or 'notification'");
    }
    else {
      // get method/notification name
      cmd = m->stringValue();
      // get params
      // Note: the "method" or "notification" param will also be in the params, but should not cause any problem
      ApiValuePtr params = JsonApiValue::newValueFromJson(aJsonRequest);
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
      #if ENABLE_LEGACY_P44CFGAPI
      else if (apiselector=="p44") {
        // process p44 specific requests
        aError = processP44Request(aJsonComm, request);
      }
      #endif
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


// MARK: - config API - P44JsonApiConnection

P44JsonApiConnection::P44JsonApiConnection()
{
  setApiVersion(VDC_API_VERSION_MAX);
}


ApiValuePtr P44JsonApiConnection::newApiValue()
{
  return ApiValuePtr(new JsonApiValue);
}


// MARK: - config API - P44JsonApiRequest

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
  LOG(LOG_DEBUG, "cfg <- vdcd (JSON) result: %s", aResult ? aResult->description().c_str() : "<none>");
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


ErrorPtr P44JsonApiRequest::sendError(ErrorPtr aError)
{
  ErrorPtr err;
  if (!aError) {
    aError = Error::ok();
  }
  LOG(LOG_DEBUG, "cfg <- vdcd (JSON) error: %ld (%s)", aError->getErrorCode(), aError->getErrorMessage());
  P44VdcHost::sendCfgApiResponse(jsonComm, JsonObjectPtr(), aError);
  return ErrorPtr();
}

#endif // ENABLE_JSONCFGAPI


#if P44SCRIPT_FULL_SUPPORT || EXPRESSION_SCRIPT_SUPPORT

// MARK: - script API - ScriptCallConnection

ScriptCallConnection::ScriptCallConnection()
{
  setApiVersion(VDC_API_VERSION_MAX);
}


ApiValuePtr ScriptCallConnection::newApiValue()
{
  return ApiValuePtr(new JsonApiValue);
}


// MARK: - script API - ScriptApiRequest

VdcApiConnectionPtr ScriptApiRequest::connection()
{
  return VdcApiConnectionPtr(new ScriptCallConnection());
}


ErrorPtr ScriptApiRequest::sendResult(ApiValuePtr aResult)
{
  LOG(LOG_DEBUG, "script <- vdcd (JSON) result: %s", aResult ? aResult->description().c_str() : "<none>");
  JsonApiValuePtr result = boost::dynamic_pointer_cast<JsonApiValue>(aResult);
  #if ENABLE_P44SCRIPT
  mBuiltinFunctionContext->finish(result ? ScriptObjPtr(new JsonValue(result->jsonObject())) : ScriptObjPtr(new AnnotatedNullValue("no vdcapi result")));
  #else
  ExpressionValue res;
  if (result) {
    res.setJson(result->jsonObject());
  }
  else {
    res.setNull();
  }
  scriptContext->continueWithAsyncFunctionResult(res);
  #endif
  return ErrorPtr();
}


ErrorPtr ScriptApiRequest::sendError(ErrorPtr aError)
{
  ErrorPtr err;
  if (!aError) {
    aError = Error::ok();
  }
  LOG(LOG_DEBUG, "script <- vdcd (JSON) error: %ld (%s)", aError->getErrorCode(), aError->getErrorMessage());
  #if ENABLE_P44SCRIPT
  mBuiltinFunctionContext->finish(ScriptObjPtr(new ErrorValue(aError)));
  #else
  ExpressionValue res;
  res.setError(aError);
  scriptContext->continueWithAsyncFunctionResult(res);
  #endif
  return ErrorPtr();
}


// MARK: - global vdc host scripts


#if ENABLE_P44SCRIPT

void P44VdcHost::runInitScripts()
{
  // command line provided script
  string scriptFn;
  string script;
  if (CmdLineApp::sharedCmdLineApp()->getStringOption("initscript", scriptFn)) {
    scriptFn = Application::sharedApplication()->resourcePath(scriptFn);
    ErrorPtr err = string_fromfile(scriptFn, script);
    if (Error::notOK(err)) {
      OLOG(LOG_ERR, "cannot open initscript: %s", err->text());
    }
    else {
      ScriptSource initScript("initscript", this);
      initScript.setSource(script, scriptbody|floatingGlobs);
      initScript.setSharedMainContext(vdcHostScriptContext);
      initScript.run(queue);
    }
  }
}

#else

class P44VdcHostScriptContext : public ScriptExecutionContext
{
  typedef ScriptExecutionContext inherited;
  VdcHost &p44VdcHost;

public:

  P44VdcHostScriptContext(P44VdcHost &aP44VdcHost) :
    inherited(&aP44VdcHost.geolocation),
    p44VdcHost(aP44VdcHost)
  {
  }

  bool evaluateAsyncFunction(const string &aFunc, const FunctionArguments &aArgs, bool &aNotYielded)
  {
    if (aFunc=="vdcapi" && aArgs.size()==1) {
      // vdcapi(jsoncall)
      if (aArgs[0].notValue()) return errorInArg(aArgs[0], false); // return error from argument
      // get method/notification and params
      JsonObjectPtr rq = aArgs[0].jsonValue();
      JsonObjectPtr m = rq->get("method");
      bool isMethod = false;
      ErrorPtr err;
      if (m) {
        isMethod = true;
      }
      else {
        m = rq->get("notification");
      }
      if (!m) {
        return throwError(Error::err<P44VdcError>(400, "invalid API request, must specify 'method' or 'notification'"));
      }
      else {
        // Note: the "method" or "notification" param will also be in the params, but should not cause any problem
        ApiValuePtr params = JsonApiValue::newValueFromJson(rq);
        VdcApiRequestPtr request = VdcApiRequestPtr(new ScriptApiRequest(this));
        if (isMethod) {
          err = p44VdcHost.handleMethodForParams(request, m->stringValue(), params);
          // Note: if method returns NULL, it has sent or will send results itself.
          //   Otherwise, even if Error is ErrorOK we must send a generic response
        }
        else {
          // handle notification
          err = p44VdcHost.handleNotificationForParams(request->connection(), m->stringValue(), params);
          // Notifications are always immediately confirmed, so make sure there's an explicit ErrorOK
          if (!err) {
            err = ErrorPtr(new Error(Error::OK));
          }
        }
        if (!err) {
          // API result will arrive later and complete function then
          aNotYielded = false;
        }
        else {
          request->sendError(err);
        }
      }
      return true;
    }
    return inherited::evaluateAsyncFunction(aFunc, aArgs, aNotYielded);
  }

};

void P44VdcHost::runInitScripts()
{
  globalScripts.clear();
  // command line provided script
  string scriptFn;
  string script;
  if (CmdLineApp::sharedCmdLineApp()->getStringOption("initscript", scriptFn)) {
    scriptFn = Application::sharedApplication()->resourcePath(scriptFn);
    ErrorPtr err = string_fromfile(scriptFn, script);
    if (Error::notOK(err)) {
      OLOG(LOG_ERR, "cannot open initscript: %s", err->text());
    }
    else {
      ScriptExecutionContextPtr s = ScriptExecutionContextPtr(new P44VdcHostScriptContext(*this));
      s->setCode(script);
      s->setContextInfo("initscript", this);
      globalScripts.queueScript(s);
    }
  }
}

#endif

#endif // P44SCRIPT_FULL_SUPPORT || EXPRESSION_SCRIPT_SUPPORT


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
      setUserActionMonitor(NULL);
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
  #if P44SCRIPT_FULL_SUPPORT
  else if (aMethod=="x-p44-scriptExec") {
    // direct execution of a script command line
    ApiValuePtr o = aParams->get("script");
    if (o) {
      ScriptSource src("scriptExec", this);
      src.setSource(o->stringValue(), sourcecode);
      src.setSharedMainContext(vdcHostScriptContext);
      src.run(sourcecode+regular+keepvars+concurrently+floatingGlobs, boost::bind(&P44VdcHost::scriptExecHandler, this, aRequest, _1));
    }
    else {
      aRequest->sendStatus(NULL); // no script -> NOP
    }
  }
  #endif // P44SCRIPT_FULL_SUPPORT
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}


#if P44SCRIPT_FULL_SUPPORT
void P44VdcHost::scriptExecHandler(VdcApiRequestPtr aRequest, ScriptObjPtr aResult)
{
  ApiValuePtr ans = aRequest->newApiValue();
  ans->setType(apivalue_object);
  if (aResult) {
    if (aResult->isErr()) {
      ans->add("error", ans->newString(aResult->errorValue()->text()));
    }
    else {
      ans->add("result", ans->newScriptValue(aResult));
    }
    ans->add("annotation", ans->newString(aResult->getAnnotation()));
    SourceCursor *cursorP = aResult->cursor();
    if (cursorP) {
      ans->add("sourceline", ans->newString(cursorP->linetext()));
      ans->add("at", ans->newUint64(cursorP->textpos()));
      ans->add("line", ans->newUint64(cursorP->lineno()));
      ans->add("char", ans->newUint64(cursorP->charpos()));
    }
  }
  aRequest->sendResult(ans);
}
#endif


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
  setUserActionMonitor(NULL);
  learnIdentifyRequest.reset();
}


#if P44SCRIPT_FULL_SUPPORT

using namespace P44Script;

// MARK: - VdcHost global members and functions

// vdcapi(jsoncall)
static const BuiltInArgDesc vdcapi_args[] = { { json+structured } };
static const size_t vdcapi_numargs = sizeof(vdcapi_args)/sizeof(BuiltInArgDesc);
static void vdcapi_func(BuiltinFunctionContextPtr f)
{
  // get method/notification and params
  JsonObjectPtr rq = f->arg(0)->jsonValue();
  JsonObjectPtr m = rq->get("method");
  bool isMethod = false;
  ErrorPtr err;
  if (m) {
    isMethod = true;
  }
  else {
    m = rq->get("notification");
  }
  if (!m) {
    f->finish(new ErrorValue(Error::err<P44VdcError>(400, "invalid API request, must specify 'method' or 'notification'")));
    return;
  }
  else {
    // Note: the "method" or "notification" param will also be in the params, but should not cause any problem
    ApiValuePtr params = JsonApiValue::newValueFromJson(rq);
    VdcApiRequestPtr request = VdcApiRequestPtr(new ScriptApiRequest(f));
    if (isMethod) {
      err = VdcHost::sharedVdcHost()->handleMethodForParams(request, m->stringValue(), params);
      // Note: if method returns NULL, it has sent or will send results itself.
      //   Otherwise, even if Error is ErrorOK we must send a generic response
    }
    else {
      // handle notification
      err = VdcHost::sharedVdcHost()->handleNotificationForParams(request->connection(), m->stringValue(), params);
      // Notifications are always immediately confirmed, so make sure there's an explicit ErrorOK
      if (!err) {
        err = ErrorPtr(new Error(Error::OK));
      }
    }
    if (err) {
      // no API result will arrive later, so finish here
      request->sendError(err);
      f->finish();
      return;
    }
    // otherwise, method will finish function call
  }
}


// device(device_name_or_dSUID)
static const BuiltInArgDesc device_args[] = { { text } };
static const size_t device_numargs = sizeof(device_args)/sizeof(BuiltInArgDesc);
static void device_func(BuiltinFunctionContextPtr f)
{
  DevicePtr device = VdcHost::sharedVdcHost()->getDeviceByNameOrDsUid(f->arg(0)->stringValue());
  if (!device) {
    f->finish(new ErrorValue(ScriptError::NotFound, "no device '%s' found", f->arg(0)->stringValue().c_str()));
    return;
  }
  f->finish(new DeviceObj(device));
}


static const BuiltinMemberDescriptor p44VdcHostMembers[] = {
  { "vdcapi", json, vdcapi_numargs, vdcapi_args, &vdcapi_func },
  { "device", any, device_numargs, device_args, &device_func },
  { NULL } // terminator
};

P44VdcHostLookup::P44VdcHostLookup() :
  inherited(p44VdcHostMembers)
{
}

static MemberLookupPtr sharedVdcHostLookup;

MemberLookupPtr P44VdcHostLookup::sharedLookup()
{
  if (!sharedVdcHostLookup) {
    sharedVdcHostLookup = new P44VdcHostLookup;
    sharedVdcHostLookup->isMemberVariable(); // disable refcounting
  }
  return sharedVdcHostLookup;
}


#endif // P44SCRIPT_FULL_SUPPORT
