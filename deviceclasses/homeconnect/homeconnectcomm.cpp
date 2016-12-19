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


#define DEVELOPER_BASE_URL "https://developer.home-connect.com"
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
    // FIXME: remove!
//    #warning "%%% Fixed token for now"
//    if (!homeConnectComm.developerApi) {
//      homeConnectComm.accessToken="eyJjdHkiOiJKV1QiLCJlbmMiOiJBMTI4Q0JDLUhTMjU2IiwiYWxnIjoiZGlyIn0..gqrxQXJub7bw0FVr9DCECw.BTsENqG88JzPThtxpoOR77pyW9D0KeCl2ne-iKnimtPxDSO0Zka3rCk5rUz0y1V_BEC4mjNwpp42fdnSX0bYiIqGabyA1isKYXTt89kJhwhnLwcq6OYD1TC7MqkynOqLn5QzNezB_ZJzwU9NFGIraQqz29_XX0iocnPa6ZYumChlE5SaHQdhnMPSlqSRQ7wgQ-RqJ_I_geD4kruvA4zE8PkrTsAcwdPauqTo2YiuO8jzbIGTEmkrmciRXz9znWQLZAUJNu4rUPb7rS9s9XD4hE0raDJo3W-_CyRNO3efSOpxPINOTdBH_8Mfu8yEF9J0M5QcEdGb_ZnvvDCI60AHxSwcSOskSJJkfe0_7X-L_UM3I4N3BLynl-nBKVBKTriXYwOkDiDDhZcBJ5wkY3VfBNJgZJwzwnOhGkSOdlBHWKuFlQzBMpjviS05Luwumep0qMnKhx1ORN-j05_5STG_qfS_Jo_1e-bk0z_Jss02z9w518dnrXOGmhNKo9NaDeSjy1_ByYkdLVqRxfzW2EzPpJ_OnxSIsdp1PVb6OpmllJH9iWo52iL4idghNEyPykxSbLsX03SpA3aTPuW2S1PJlUkeZOKtL86uxk4s70KCuuoEyNffGOBfyb2pmXPW5KcYm0Z9GJ7EjJjFMtDT5FSq2KtjjwT8I1ml8boj5jrbpxc7M5ydodyLn2CEa0OUJlIOn2OX_xI77Cdq54I-njfoh5NMati1vKrFH8VQWc2vYPEkXdwzOxAxwxKe_2D2mi-Ot2MFuP8_H1YRxrP2J0koHWpWP7rvQ2RkkLYXmvtwPYNqPl6nQVRd1JNZ4GD4ot1vy84mak8Yx6quCfK5oL9UpbaxEZzubo2j-0eh_LPcG5L2vOxwmvnxXo9y3u2KsUAMrzma27EJBE8FV9z8Mh8m6IgFpO2f2MHdbbLZiXEDAtnvKapDlpgNHYGc4IB6ld_G-sZ5QVhNHXwSUcrIMcANlBlmGhREC9ngdQRFWyc9oGHS9s8kHlDWe6pnLzfhYiEYibrKnydwXM8SbcJZpRtluvZ8u5pD_8yia2vENwU-1F_x1OWCXIfS1pgMF1MLQvQfvrp7JU8qEiHEnSozmbE6eSJFRtgRS8tTBkqldWiQKXR_lS6e9X8rQpf4in3nbCn5vHBoiif6bLvcnYjDmT3rAJ6e7DiH-HR6HhzkV-qNemIdI4dzKxEZtuH1PhVU249TdNgU0sZxbOviuSgaNfn8vEIpMl-hdAq7yX5DdNbQyxblP-rUHTi0l9v0gNXiOt2egh1_w6-pPlWJnrpUlkZuKg8sgTBdo0BB6R8wj1sOhjuBNKO7jO51Rg-Ev4b21SkTBsd7Fhb6FMPH45YyEyWUNnwr5aSU1OTQT00ucn-CIH0u2NpZBpt4toLIOcYN2-VITheDw-D1iMrFmmusmm2c9XUruLzwuLxvQ98tuQZo9jTwChoxNOPv1VuI_Jcx4SBMcxSNa59NtKGw1b2DnSN2j9g2t89tcQpQ3_cuXACDwIKACfBCnbO3cdvBVfiH6HeayrY6WT9juDRt9B4RbAiNGTIzBxxXvmk1J-xUnZX02GA.KPhpOOQAuxry4UUBg9JNdg";
//      sendRequest();
//      return inherited::initiate();
//    }
//    #warning "%%% End hacking code"
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
  homeConnectComm.httpAPIComm.jsonRequest((homeConnectComm.baseUrl()+urlPath).c_str(), boost::bind(&HomeConnectApiOperation::processAnswer, this, _1, _2), method.c_str(), data, "application/vnd.bsh.sdk.v1+json");
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
    "application/x-www-form-urlencoded"
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
  if (Error::isOK(error)) {
    // check for application level errors
    JsonObjectPtr e;
    if (aJsonResponse && aJsonResponse->get("error", e)) {
      // there is an error
      JsonObjectPtr o;
      string errorkey;
      if (e->get("key", o)) {
        errorkey = o->stringValue();
        if (errorkey=="invalid_token") {
          // the access token has expired, we need to do a refresh operation
          refreshAccessToken();
          return;
        }
        string errordesc;
        if (e->get("description", o)) {
          errordesc = o->stringValue();
        }
        // other application level error, create text error from it
        error = TextError::err("%s: %s", errorkey.c_str(), errordesc.c_str());
      }
    }
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
  eventCB(aEventCB)
{
  sendGetEventRequest();
}


HomeConnectEventMonitor::~HomeConnectEventMonitor()
{
  
}


void HomeConnectEventMonitor::sendGetEventRequest()
{
  // - set up the extra auth headers
  eventBuffer.clear();
  clearRequestHeaders();
  addRequestHeader("Authorization", string_format("Bearer %s", homeConnectComm.accessToken.c_str()));
  addRequestHeader("Accept", "text/event-stream");
  addRequestHeader("Cache-Control", "no-cache");
  // - make the call
  FOCUSLOG(">>> Sending event stream GET request to '%s'", urlPath.c_str());
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
}


#define EVENT_STREAM_RESTART_DELAY (15*Second)

void HomeConnectEventMonitor::processEventData(const string &aResponse, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    if (aResponse.empty()) {
      // end of stream - schedule restart
      MainLoop::currentMainLoop().executeOnce(boost::bind(&HomeConnectEventMonitor::sendGetEventRequest, this), EVENT_STREAM_RESTART_DELAY);
      return;
    }
    eventBuffer += aResponse;
    FOCUSLOG(">>> Accumulated Event data: %s", aResponse.c_str());
    // process
    const char *cu = eventBuffer.c_str();
    string line;
    while (nextLine(cu, line)) {
      // process line
      string field;
      string data;
      if (keyAndValue(line, field, data, ':')) {
        if (field=="event") {
          eventType = data;
          FOCUSLOG(">>> Got event type: %s", eventType.c_str());
        }
        else if (field=="data") {
          eventData = data;
          FOCUSLOG(">>> Got event data: %s", eventData.c_str());
        }
      }
      else {
        if (line.empty()) {
          // empty line signals complete event but only if at least a type was found
          if (!eventType.empty()) {
            FOCUSLOG(">>> Event is complete, dispatch now, one callback per item");
            if (eventType!="KEEP-ALIVE") {
              // convert data to JSON
              bool reporteditem = false;
              JsonObjectPtr jsondata = JsonObject::objFromText(eventData.c_str());
              if (jsondata) {
                // iterate trough items
                JsonObjectPtr items;
                if (jsondata->get("items", items)) {
                  for (int i=0; i<items->arrayLength(); i++) {
                    JsonObjectPtr item = items->arrayGet(i);
                    if (item) {
                      // deliver
                      reporteditem = true;
                      if (eventCB) eventCB(eventType, item, ErrorPtr());
                    }
                  }
                }
              }
              if (!reporteditem) {
                // event w/o data or without items array in data, report event type along with raw data (or no data)
                if (eventCB) eventCB(eventType, jsondata, ErrorPtr());
              }
            }
          }
          // done
          eventData.clear();
          eventType.clear();
        }
      }
    }
    // remove processed data
    eventBuffer.erase(0,cu-eventBuffer.c_str());
  }
  else {
    LOG(LOG_WARNING, "HomeConnect Event stream '%s' error: %s", urlPath.c_str(), aError->description().c_str());
  }
}





// MARK: ===== HomeConnectComm

HomeConnectComm::HomeConnectComm() :
  inherited(MainLoop::currentMainLoop()),
  httpAPIComm(MainLoop::currentMainLoop()),
  developerApi(false)
{
}


void HomeConnectComm::setAuthentication(string aAuthData)
{
  accessToken.clear();
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
  HomeConnectApiOperationPtr op = HomeConnectApiOperationPtr(new HomeConnectApiOperation(*this, aMethod, aUrlPath, aData, aResultHandler));
  queueOperation(op);
  // process operations
  processOperations();
}



#endif // ENABLE_HOMECONNECT
