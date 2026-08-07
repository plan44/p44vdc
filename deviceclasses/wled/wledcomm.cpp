//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Michael Troß <digitalstrom@tross.org>
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

#include "wledcomm.hpp"

#if ENABLE_WLED

#include "mainloop.hpp"

using namespace p44;

#if ENABLE_NAMED_ERRORS
const char* WledCommError::errorName() const
{
  static const char *errorNames[] = {
    "OK",
    "DeviceNotFound",
    "InvalidResponse",
    "ApiError",
    "NoCapabilities",
    "InvalidParameter",
    "Timeout",
    "NetworkError",
    "InvalidDeviceState",
    "NoDeviceInfo",
  };
  if (getErrorCode() < sizeof(errorNames)/sizeof(errorNames[0]))
    return errorNames[getErrorCode()];
  return "UnknownError";
}
#endif


// MARK: - WledApiOperation

WledApiOperation::WledApiOperation(WledComm &aWledComm, HttpMethods aMethod, const string &aUrl, JsonObjectPtr aData, WledApiResultCB aResultHandler) :
  mWledComm(aWledComm),
  mMethod(aMethod),
  mUrl(aUrl),
  mData(aData),
  mCompleted(false),
  mResultHandler(aResultHandler)
{
  LOG(LOG_DEBUG, "WledApiOperation constructor - %s %s", aMethod==GET ? "GET" : (aMethod==POST ? "POST" : "PUT"), aUrl.c_str());
}


WledApiOperation::~WledApiOperation()
{
}


bool WledApiOperation::initiate()
{
  LOG(LOG_INFO, "WledApiOperation::initiate - %s %s", mMethod==GET ? "GET" : (mMethod==POST ? "POST" : "PUT"), mUrl.c_str());

  try {
    string methodStr = (mMethod==GET ? "GET" : (mMethod==POST ? "POST" : "PUT"));
    mWledComm.mJsonClient.jsonRequest(mUrl.c_str(), boost::bind(&WledApiOperation::processAnswer, this, _1, _2), methodStr.c_str(), mData);
    return inherited::initiate();
  }
  catch (const std::exception &e) {
    LOG(LOG_ERR, "WledApiOperation::initiate - Exception: %s", e.what());
    mError = Error::err<WledCommError>(WledCommError::NetworkError);
    mCompleted = true;
    return true;
  }
}


bool WledApiOperation::hasCompleted()
{
  return mCompleted;
}


OperationPtr WledApiOperation::finalize()
{
  if (mResultHandler) {
    mResultHandler(mData, mError);
    mResultHandler = NoOP; // call once only
  }
  return inherited::finalize();
}


void WledApiOperation::abortOperation(ErrorPtr aError)
{
  mError = aError;
  mCompleted = true;
}


void WledApiOperation::processAnswer(JsonObjectPtr aJsonResponse, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    mError = aError;
    mData = JsonObjectPtr(); // clear request payload — callbacks must not see it on error
    SOLOG(mWledComm, LOG_WARNING, "API error: %s", mError->text());
  }
  else {
    mData = aJsonResponse;
    SOLOG(mWledComm, LOG_INFO, "API response: %s", JsonObject::text(aJsonResponse));
  }

  // done
  mCompleted = true;

  // have queue reprocessed
  mWledComm.processOperations();
}


// MARK: - WledComm

WledComm::WledComm() :
  inherited(MainLoop::currentMainLoop()),
  mJsonClient(MainLoop::currentMainLoop()),
  mBaseURL(""),
  mCachedState(NULL),
  mCachedInfo(NULL)
  #if ENABLE_JSON_WEBSOCKET
  , mWebsocketClient(NULL),
    mWebsocketEnabled(false)
  #endif
{
  mJsonClient.isMemberVariable();
  mJsonClient.setTimeout(5*Second); // 5s timeout for WLED API calls
  
  #if ENABLE_JSON_WEBSOCKET
  // Initialize WebSocket client (but don't connect yet).
  // Only setMessageCallback here — the status callback is registered via the
  // connect() call in websocketConnect() to avoid double-firing on connect events.
  mWebsocketClient = new JsonWebsocketClient(MainLoop::currentMainLoop());
  mWebsocketClient->setMessageCallback(
    boost::bind(&WledComm::onWebsocketMessage, this, _1, _2)
  );
  #endif
}



WledComm::~WledComm()
{
  #if ENABLE_JSON_WEBSOCKET
  // Disconnect WebSocket if connected
  if (mWebsocketClient) {
    mWebsocketClient->disconnect();
  }
  #endif
  // Destructor - parent class OperationQueue will be destructed automatically
}


void WledComm::setLogLevelOffset(int aLogLevelOffset)
{
  P44LoggingObj::setLogLevelOffset(aLogLevelOffset);
}


bool WledComm::getIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  return false;
}


void WledComm::setDeviceURL(const string &aBaseURL)
{
  mBaseURL = aBaseURL;
  if (!mBaseURL.empty() && mBaseURL.back() == '/') {
    mBaseURL.pop_back(); // Remove trailing slash
  }
  LOG(LOG_INFO, "WLED device URL set to: %s", mBaseURL.c_str());
}


// Note: HTTP request handling is now done via JsonWebClient in WledApiOperation::initiate()


void WledComm::getState(WledApiResultCB aResultCallback)
{
  queueApiCall("state", "GET", NULL, aResultCallback);
}


void WledComm::setState(JsonObjectPtr aStateUpdate, WledApiResultCB aResultCallback)
{
  queueApiCall("state", "POST", aStateUpdate, aResultCallback);
}


void WledComm::getInfo(WledApiResultCB aResultCallback)
{
  queueApiCall("info", "GET", NULL, aResultCallback);
}


void WledComm::getEffects(WledApiResultCB aResultCallback)
{
  queueApiCall("eff", "GET", NULL, aResultCallback);
}


void WledComm::getPalettes(WledApiResultCB aResultCallback)
{
  queueApiCall("pal", "GET", NULL, aResultCallback);
}


void WledComm::getNetwork(WledApiResultCB aResultCallback)
{
  queueApiCall("net", "GET", NULL, aResultCallback);
}


void WledComm::queueApiCall(const string &aPath, const string &aMethod,
                            JsonObjectPtr aData, WledApiResultCB aResultCallback)
{
  if (mBaseURL.empty()) {
    ErrorPtr error = Error::err<WledCommError>(WledCommError::InvalidDeviceState);
    if (aResultCallback) {
      aResultCallback(NULL, error);
    }
    return;
  }

  // Build full URL
  string fullUrl = mBaseURL + "/json/" + aPath;

  WledApiOperationPtr op = new WledApiOperation(*this, 
    aMethod == "GET" ? WledApiOperation::GET : 
    (aMethod == "POST" ? WledApiOperation::POST : WledApiOperation::PUT),
    fullUrl, aData, aResultCallback);

  op->setInitiationDelay(100*MilliSecond, true); // do not start next command earlier than 100mS after the previous one
  queueOperation(op);
  processOperations();
}


void WledComm::discoverDevices(WledDiscoveryResultCB aResultCallback)
{
  // Discovery is now handled by DNS-SD at the VDC level
  // This method is deprecated but kept for compatibility
  if (aResultCallback) {
    aResultCallback("", NULL, ErrorPtr());
  }
}


#if ENABLE_JSON_WEBSOCKET

void WledComm::enableWebsocket(bool aEnable)
{
  OLOG(LOG_INFO, "WebSocket support %s", aEnable ? "enabled" : "disabled");
  mWebsocketEnabled = aEnable;
  
  if (!aEnable) {
    // Disable WebSocket - disconnect if connected
    websocketDisconnect();
  }
}


bool WledComm::isWebsocketConnected() const
{
  if (!mWebsocketClient) return false;
  return mWebsocketClient->isConnected();
}


void WledComm::websocketConnect(WledWebsocketStatusCB aStatusCallback)
{
  if (!mWebsocketEnabled) {
    OLOG(LOG_WARNING, "WebSocket not enabled");
    if (aStatusCallback) {
      aStatusCallback(false, ErrorPtr(new WledCommError(WledCommError::InvalidDeviceState)));
    }
    return;
  }

  if (mBaseURL.empty()) {
    OLOG(LOG_WARNING, "Device URL not set");
    if (aStatusCallback) {
      aStatusCallback(false, ErrorPtr(new WledCommError(WledCommError::InvalidDeviceState)));
    }
    return;
  }

  // Store the callback for later
  if (aStatusCallback) {
    mWebsocketStatusCB = aStatusCallback;
  }

  // Build WebSocket URL from HTTP URL
  // Convert "http://192.168.1.100/json" to "ws://192.168.1.100/ws"
  string wsUrl = mBaseURL;
  size_t httpPos = wsUrl.find("http://");
  if (httpPos == 0) {
    wsUrl.replace(0, 7, "ws://");
  } else {
    size_t httpsPos = wsUrl.find("https://");
    if (httpsPos == 0) {
      wsUrl.replace(0, 8, "wss://");
    }
  }
  
  // Remove "/json" if present and replace with "/ws"
  size_t jsonPos = wsUrl.rfind("/json");
  if (jsonPos != string::npos) {
    wsUrl.erase(jsonPos);
  }
  wsUrl += "/ws";

  OLOG(LOG_INFO, "Connecting to WebSocket: %s", wsUrl.c_str());

  // The connect() callback is the sole status handler — no setStatusCallback() in
  // the constructor, so this fires exactly once per event.
  mWebsocketClient->connect(wsUrl, boost::bind(&WledComm::onWebsocketConnected, this, _1, _2));
}


void WledComm::websocketDisconnect()
{
  if (mWebsocketClient) {
    OLOG(LOG_INFO, "Disconnecting WebSocket");
    mWebsocketClient->disconnect();
  }
}


void WledComm::setWebsocketUpdateCallback(WledWebsocketUpdateCB aUpdateCallback)
{
  mWebsocketUpdateCB = aUpdateCallback;
}


void WledComm::onWebsocketMessage(JsonObjectPtr aMessage, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    OLOG(LOG_WARNING, "WebSocket message error: %s", aError->text());
    return;
  }

  if (!aMessage) {
    OLOG(LOG_WARNING, "Empty WebSocket message");
    return;
  }

  OLOG(LOG_DEBUG, "WebSocket message received: %s", aMessage->json_str().c_str());

  // Update cached state from WebSocket message
  // WLED sends: {"state": {...}, "info": {...}} or just {"state": {...}}
  JsonObjectPtr stateObj;
  if (aMessage->get("state", stateObj)) {
    mCachedState = stateObj;
    OLOG(LOG_INFO, "State updated from WebSocket");
  }

  JsonObjectPtr infoObj;
  if (aMessage->get("info", infoObj)) {
    mCachedInfo = infoObj;
    OLOG(LOG_INFO, "Info updated from WebSocket");
  }

  // Call the registered callback if any
  if (mWebsocketUpdateCB) {
    mWebsocketUpdateCB(aMessage, ErrorPtr());
  }
}


void WledComm::onWebsocketConnected(bool aConnected, ErrorPtr aError)
{
  if (aConnected) {
    OLOG(LOG_NOTICE, "WebSocket connected to device");
    
    // Request initial state
    JsonObjectPtr requestObj = JsonObject::newObj();
    requestObj->add("v", JsonObject::newBool(true)); // Request full state
    bool sent = mWebsocketClient->sendJson(requestObj, [this](JsonObjectPtr aResponse, ErrorPtr aError) {
      if (Error::notOK(aError)) {
        OLOG(LOG_WARNING, "Error sending initial state request");
      }
    });
    
    if (!sent) {
      OLOG(LOG_WARNING, "Failed to queue initial state request");
    }
  } else {
    OLOG(LOG_NOTICE, "WebSocket disconnected from device");
    if (Error::notOK(aError)) {
      OLOG(LOG_WARNING, "WebSocket error: %s", aError->text());
    }
  }

  // Call the registered callback if any
  if (mWebsocketStatusCB) {
    mWebsocketStatusCB(aConnected, aError);
  }
}

#endif // ENABLE_JSON_WEBSOCKET

#endif // ENABLE_WLED
