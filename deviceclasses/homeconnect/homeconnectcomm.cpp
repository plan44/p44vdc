//
//  Copyright (c) 2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "homeconnectcomm.hpp"

#if ENABLE_HOMECONNECT

using namespace p44;

#define DEVELOPER_BASE_URL "https://simulator.home-connect.com"
#define PRODUCTION_BASE_URL "https://api.home-connect.com"

#define OAUTH_TOKEN_PATH "/security/oauth/token"

// MARK: ===== HomeConnectApiOperation

HomeConnectApiOperation::HomeConnectApiOperation(HomeConnectComm &aHomeConnectComm, const string aMethod, const string aUrlPath, JsonObjectPtr aData, HomeConnectApiResultCB aResultHandler) :
  homeConnectComm(aHomeConnectComm),
  method(aMethod),
  urlPath(aUrlPath),
  data(aData),
  resultHandler(aResultHandler),
  completed(false)
{
}



HomeConnectApiOperation::~HomeConnectApiOperation()
{

}



bool HomeConnectApiOperation::initiate()
{
  if (!canInitiate())
    return false;
  // send request
  if (homeConnectComm.accessToken.size()==0) {
    // no token, don't even try to connect, need to get access token via refresh first
    refreshAccessToken();
  }
  else {
    // we have a token, try to send request (we might get an expired error which will cause a re-run)
    sendRequest();
  }
  // mark operation as initiated
  return inherited::initiate();
}


void HomeConnectApiOperation::sendRequest()
{
  // initiate the web request
  // - set up the extra auth headers
  homeConnectComm.httpAPIComm.clearRequestHeaders();
  homeConnectComm.httpAPIComm.addRequestHeader("Authorization", string_format("Bearer %s", homeConnectComm.accessToken.c_str()));
  homeConnectComm.httpAPIComm.addRequestHeader("Accept", "application/vnd.bsh.sdk.v1+json");
  homeConnectComm.httpAPIComm.addRequestHeader("Cache-Control", "no-cache");
  // - issue the request
  homeConnectComm.httpAPIComm.jsonRequest((homeConnectComm.baseUrl()+urlPath).c_str(), boost::bind(&HomeConnectApiOperation::processAnswer, this, _1, _2), method.c_str(), data, "application/vnd.bsh.sdk.v1+json", true);
}



void HomeConnectApiOperation::refreshAccessToken()
{
  // initiate the web request
  // - set up the extra auth headers
  homeConnectComm.httpAPIComm.clearRequestHeaders();
  homeConnectComm.httpAPIComm.addRequestHeader("Cache-Control", "no-cache");
  string postdata = "grant_type=refresh_token&refresh_token=" + homeConnectComm.refreshToken;
  // - issue the request
  homeConnectComm.httpAPIComm.jsonReturningRequest(
    (homeConnectComm.baseUrl()+OAUTH_TOKEN_PATH).c_str(),
    boost::bind(&HomeConnectApiOperation::processRefreshAnswer, this, _1, _2),
    "POST",
    postdata,
    "application/x-www-form-urlencoded",
    true
  );
}


void HomeConnectApiOperation::processRefreshAnswer(JsonObjectPtr aJsonResponse, ErrorPtr aError)
{
  error = aError;
  if (Error::isOK(error)) {
    // check for errors
    JsonObjectPtr a;
    if (aJsonResponse->get("access_token", a)) {
      // here's a new access token
      homeConnectComm.accessToken = a->stringValue();
      // now re-run the original request
      sendRequest();
      return;
    }
  }
  // if refresh fails, treat it as a normal response
  LOG(LOG_WARNING, "HomeConnect: token refresh has failed");
  aError = WebError::webErr(401, "Not authorized - requesting access token using refresh token has failed");
  processAnswer(JsonObjectPtr(), aError);
}



void HomeConnectApiOperation::processAnswer(JsonObjectPtr aJsonResponse, ErrorPtr aError)
{
  error = aError;
  if (Error::isOK(error) || error->isDomain(WebError::domain())) {
    // check if there is a proper response
    if (aJsonResponse) {
      JsonObjectPtr e;
      if (aJsonResponse->get("error", e)) {
        // there is an error
        JsonObjectPtr o;
        string errorkey;
        if (e->get("key", o)) {
          errorkey = o->stringValue();
          if (errorkey=="invalid_token") {
            // the access token has expired, we need to do a refresh operation
            homeConnectComm.apiReady = false;
            refreshAccessToken();
            return;
          } else if (errorkey=="429") {
            // this is a rate limit error, try to get the missing time from the error description and set lockdown on the comm
            // the description should contain the following text:
            // The rate limit \"10 successive error calls in 10 minutes\" was reached. Requests are blocked during the remaining period of 397 seconds.
            homeConnectComm.setLockDownTime(homeConnectComm.calculateLockDownTime() * Second);
          }
          string errordesc;
          if (e->get("description", o)) {
            errordesc = o->stringValue();
          }

          if (Error::isOK(error)) {
            // if no comm error is set then create application level error
            error = TextError::err("%s: %s", errorkey.c_str(), errordesc.c_str());
          }
        }
      }
      // we got a response from server (it can be also error description)
      homeConnectComm.apiReady = true;
    }
  } else {
    // error during communication
    homeConnectComm.apiReady = false;
  }

  // now save return data (but not above, because we might need reqest "data" to re-run the request after a token refresh
  data = aJsonResponse;
  // done
  completed = true;
  // have queue reprocessed
  homeConnectComm.processOperations();
}



bool HomeConnectApiOperation::hasCompleted()
{
  return completed;
}



OperationPtr HomeConnectApiOperation::finalize()
{
  if (resultHandler) {
    resultHandler(data, error);
    resultHandler = NULL; // call once only
  }
  return inherited::finalize();
}



void HomeConnectApiOperation::abortOperation(ErrorPtr aError)
{
  if (!aborted) {
    if (!completed) {
      homeConnectComm.httpAPIComm.cancelRequest();
    }
    if (resultHandler) {
      resultHandler(JsonObjectPtr(), aError);
      resultHandler = NULL; // call once only
    }
  }
  inherited::abortOperation(aError);
}

// MARK: ===== HomeConnectEventMonitor


HomeConnectEventMonitor::HomeConnectEventMonitor(HomeConnectComm &aHomeConnectComm, const char *aUrlPath, HomeConnectEventResultCB aEventCB) :
  inherited(MainLoop::currentMainLoop()),
  homeConnectComm(aHomeConnectComm),
  urlPath(aUrlPath),
  eventCB(aEventCB),
  eventGotID(false),
  ticket(0)
{
  sendGetEventRequest();
  setServerCertVfyDir("");
}


HomeConnectEventMonitor::~HomeConnectEventMonitor()
{
  MainLoop::currentMainLoop().cancelExecutionTicket(ticket);
  
}

#define EVENT_STREAM_RESTART_DELAY (15*Second)

void HomeConnectEventMonitor::sendGetEventRequest()
{
	// connecting to event stream make sense only when the homm channel is working
  if (homeConnectComm.apiReady && !homeConnectComm.isLockDown())
  {
	  // - set up the extra auth headers
	  eventBuffer.clear();
	  clearRequestHeaders();
	  addRequestHeader("Authorization", string_format("Bearer %s", homeConnectComm.accessToken.c_str()));
	  addRequestHeader("Accept", "text/event-stream");
	  addRequestHeader("Cache-Control", "no-cache");
	  // - make the call
	  FOCUSLOG(">>> Sending event stream GET request to '%s' with token '%s'", urlPath.c_str(), homeConnectComm.accessToken.c_str());
	  httpRequest(
		(homeConnectComm.baseUrl()+urlPath).c_str(),
		boost::bind(&HomeConnectEventMonitor::processEventData, this, _1, _2),
		"GET",
		NULL, // no request body
		NULL, // no body content type
		-1, // not saving into file descriptor
		false, // no need to save headers
		true // stream result
	  );
  } else {
    LOG(LOG_WARNING, "Event stream, Api not ready yet wait 15s");
    // not ready yet - schedule restart
    ticket = MainLoop::currentMainLoop().executeOnce(boost::bind(&HomeConnectEventMonitor::sendGetEventRequest, this), EVENT_STREAM_RESTART_DELAY);
  }
}

EventType HomeConnectEventMonitor::getEventType()
{
  if (eventTypeString == "NOTIFY") {
    return eventType_Notify;
  }
  if (eventTypeString == "STATUS") {
    return eventType_Status;
  }
  if (eventTypeString == "EVENT") {
    return eventType_Event;
  }
  if (eventTypeString == "DISCONNECTED") {
    return eventType_Disconnected;
  }
  if (eventTypeString == "CONNECTED") {
    return eventType_Connected;
  }
  return eventType_Unknown;
}


void HomeConnectEventMonitor::processEventData(const string &aResponse, ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    LOG(LOG_WARNING, "HomeConnect Event stream '%s' error: %s, message: '%s'", urlPath.c_str(), aError->description().c_str(), aResponse.c_str());
    // error in comm - schedule restart
    if (aError->getErrorCode() == 401) {
      // this is invalid token issue - refresh token by sending dummy request
      homeConnectComm.apiQuery("/api/homeappliances", boost::bind(&HomeConnectEventMonitor::apiQueryDone, this, _1, _2));
    } else if (aError->getErrorCode() == 429 ){
      LOG(LOG_WARNING, "HomeConnect Event stream Error 429 : locking down communication!");
      homeConnectComm.setLockDownTime(homeConnectComm.calculateLockDownTime() * Second);
    } else {
      ticket = MainLoop::currentMainLoop().executeOnce(boost::bind(&HomeConnectEventMonitor::sendGetEventRequest, this), EVENT_STREAM_RESTART_DELAY);
    }
    return;
  }

  if (aResponse.empty()) {
    // end of stream - schedule restart
    ticket = MainLoop::currentMainLoop().executeOnce(
        boost::bind(&HomeConnectEventMonitor::sendGetEventRequest, this),
        EVENT_STREAM_RESTART_DELAY);
    return;
  }

  // check if there is 429 error
  JsonObjectPtr jsonresponse ( JsonObject::objFromText(aResponse.c_str()) );
  if( jsonresponse ){
    JsonObjectPtr e;
    if( jsonresponse->get("error", e) ){
      JsonObjectPtr ekey;
      if( e->get("key", ekey) ){
        if( ekey->stringValue() == "429" ){
          LOG(LOG_WARNING, "HomeConnect Error 429 : locking down communication!");
          homeConnectComm.setLockDownTime(homeConnectComm.calculateLockDownTime() * Second);
          return;
        }
      }
    }
  }

  eventBuffer += aResponse;
  FOCUSLOG(">>> Accumulated Event from '%s' data: %s", urlPath.c_str(), aResponse.c_str());

  // process
  const char *cu = eventBuffer.c_str();
  string line;
  while (nextLine(cu, line)) {
    parseLine(line);
  }
  // remove processed data
  eventBuffer.erase(0,cu-eventBuffer.c_str());
}

void HomeConnectEventMonitor::parseLine(const string& aLine)
{
  if (aLine.empty()) {
    // empty line signals complete event but only if at least a type was found
    if (!eventTypeString.empty()) {
      FOCUSLOG(">>> Event is complete, dispatch now, one callback per item");
      if (eventTypeString != "KEEP-ALIVE") {
        completeEvent();
      }
    }
    // done
    eventData.clear();
    eventTypeString.clear();
    eventGotID = false;
  } else {
    string field;
    string data;
    if (keyAndValue(aLine, field, data, ':')) {
      if (field == "event") {
        eventTypeString = data;
        FOCUSLOG(">>> Got event type: %s", eventTypeString.c_str());
      } else if (field == "data") {
        eventData += data;
        FOCUSLOG(">>> Got event data: %s", eventData.c_str());
      } else if (field == "id") {
        eventGotID = true;
        FOCUSLOG(">>> Got field id: %s", data.c_str());
      } else if( !eventGotID ) {
        eventData += aLine;
        FOCUSLOG(">>> Got more data: %s", aLine.c_str());
        FOCUSLOG(">>> Event data: %s", eventData.c_str());
      }
    }
  }
}

void HomeConnectEventMonitor::completeEvent()
{
  // convert data to JSON
  bool reporteditem = false;
  JsonObjectPtr jsondata = JsonObject::objFromText(eventData.c_str());
  if (jsondata) {
    // iterate trough items
    JsonObjectPtr items;
    if (jsondata->get("items", items)) {
      for (int i = 0; i < items->arrayLength(); i++) {
        JsonObjectPtr item = items->arrayGet(i);
        if (item) {
          // deliver
          reporteditem = true;
          if (eventCB) {
            eventCB(getEventType(), item, ErrorPtr());
          }
        }
      }
    }
  }
  if (!reporteditem) {
    // event w/o data or without items array in data, report event type along with raw data (or no data)
    if (eventCB)
      eventCB(getEventType(), jsondata, ErrorPtr());
  }
}


void HomeConnectEventMonitor::apiQueryDone(JsonObjectPtr aResult, ErrorPtr aError)
{
  LOG(LOG_WARNING, "Api request finished, try to reopen the event channel");
  // the request was done, try to open event channel again
  ticket = MainLoop::currentMainLoop().executeOnce(boost::bind(&HomeConnectEventMonitor::sendGetEventRequest, this), 1*Second);
}


// MARK: ===== HomeConnectComm

HomeConnectComm::HomeConnectComm() :
  inherited(MainLoop::currentMainLoop()),
  httpAPIComm(MainLoop::currentMainLoop()),
  findInProgress(false), apiReady(false),
  developerApi(false), lockdownTicket(0)
{
  httpAPIComm.isMemberVariable();
  httpAPIComm.setServerCertVfyDir(""); // Use empty string to not verify server certificate
}


void HomeConnectComm::setAuthentication(string aAuthData)
{
  accessToken.clear();
  refreshToken.clear();
  JsonObjectPtr auth = JsonObject::objFromText(aAuthData.c_str());
  if (auth) {
    JsonObjectPtr o;
    if (auth->get("access_token", o)) accessToken = o->stringValue();
    if (auth->get("refresh_token", o)) refreshToken = o->stringValue();
  }
}



string HomeConnectComm::baseUrl()
{
  return developerApi ? DEVELOPER_BASE_URL : PRODUCTION_BASE_URL;
}



HomeConnectComm::~HomeConnectComm()
{
}


void HomeConnectComm::apiQuery(const char* aUrlPath, HomeConnectApiResultCB aResultHandler)
{
  apiAction("GET", aUrlPath, JsonObjectPtr(), aResultHandler);
}


void HomeConnectComm::apiAction(const string aMethod, const string aUrlPath, JsonObjectPtr aData, HomeConnectApiResultCB aResultHandler)
{
  if (!isLockDown()) {
    HomeConnectApiOperationPtr op = HomeConnectApiOperationPtr(new HomeConnectApiOperation(*this, aMethod, aUrlPath, aData, aResultHandler));
    queueOperation(op);
    // process operations
    processOperations();
  } else {
    LOG(LOG_INFO, "Cannot send command during lock down.");
    if (aResultHandler) {
      aResultHandler(NULL, WebError::webErr(429, "Communication temporally disabled"));
    }
  }
}

MLMicroSeconds HomeConnectComm::calculateLockDownTime()
{
  // try to get the missing time from response header
  // the description should contain the following text:
  // The rate limit \"10 successive error calls in 10 minutes\" was reached. Requests are blocked during the remaining period of 397 seconds.

  int lockdownTimeoutInSeconds = HomeConnectComm::MaxLockdownTimeout / Second;

  // if we have access to response headers
  if (httpAPIComm.responseHeaders) {
    // try to get the Retry-After header
    std::map<string, string>::iterator it =
        httpAPIComm.responseHeaders->find("Retry-After");
    if (it != httpAPIComm.responseHeaders->end()) {
      lockdownTimeoutInSeconds = atoi(it->second.c_str());
    }
  }

  return lockdownTimeoutInSeconds;
}

void HomeConnectComm::setLockDownTime(MLMicroSeconds aLockDownTime)
{
  if (aLockDownTime > MaxLockdownTimeout) {
    LOG(LOG_INFO, "Requested timeout %i s to big! Limiting to %i s", (int)(aLockDownTime / Second), (int)(MaxLockdownTimeout / Second));
    aLockDownTime = MaxLockdownTimeout;
  }

  // cancel potential previous lockdown
  cancelLockDown();

  LOG(LOG_INFO, "Set lock down for %i s", (int)(aLockDownTime / Second));
  lockdownTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&HomeConnectComm::setLockDownTimeExpired, this), aLockDownTime);
}


void HomeConnectComm::setLockDownTimeExpired()
{
  LOG(LOG_INFO, "Lock down finished!");
  lockdownTicket = 0;
}

void HomeConnectComm::cancelLockDown()
{
  // check and cancel potential previous lockdown
  if (lockdownTicket != 0) {
    LOG(LOG_INFO, "Cancel previous lockdown");
    MainLoop::currentMainLoop().cancelExecutionTicket(lockdownTicket);
  }
}

#endif // ENABLE_HOMECONNECT
