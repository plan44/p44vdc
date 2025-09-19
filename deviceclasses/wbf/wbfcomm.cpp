//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "wbfcomm.hpp"

#if ENABLE_WBF

using namespace p44;


#if ENABLE_NAMED_ERRORS
const char* WbfCommError::errorName() const
{
  switch(getErrorCode()) {
    case Failure: return "Failure";
    case ApiNotReady: return "API not ready";
    case PairingTimeout: return "Pairing timeout";
    case FindTimeout: return "Re-find timeout";
    case ResponseErr: return "Response content error";
    case ApiError: return "API returns error";
  }
  return NULL;
}
#endif // ENABLE_NAMED_ERRORS


// MARK: - WbfApiOperation

#define WBFAPI_DEFAULT_TIMEOUT (5*Second)

WbfApiOperation::WbfApiOperation(WbfComm &aWbfComm, HttpMethods aMethod, const char* aUrl, JsonObjectPtr aData, WbfApiResultCB aResultHandler, MLMicroSeconds aTimeout) :
  mWbfComm(aWbfComm),
  mMethod(aMethod),
  mUrl(aUrl),
  mData(aData),
  mResultHandler(aResultHandler),
  mTimeout(aTimeout),
  mCompleted(false)
{
}



WbfApiOperation::~WbfApiOperation()
{
}



bool WbfApiOperation::initiate()
{
  // initiate the web request
  const char *methodStr;
  switch (mMethod) {
    case POST : methodStr = "POST"; break;
    case PUT : methodStr = "PUT"; break;
    case PATCH : methodStr = "PATCH"; break;
    case DELETE : methodStr = "DELETE"; break;
    default : methodStr = "GET"; mData.reset(); break;
  }
  SOLOG(mWbfComm, LOG_INFO, "Sending API request (%s) command: %s: %s", methodStr, mUrl.c_str(), JsonObject::text(mData));
  mWbfComm.mGatewayAPIComm.setTimeout(mTimeout<=-2 ? WBFAPI_DEFAULT_TIMEOUT : mTimeout);
  mWbfComm.mGatewayAPIComm.jsonRequest(mUrl.c_str(), boost::bind(&WbfApiOperation::processAnswer, this, _1, _2), methodStr, mData);
  // executed
  return inherited::initiate();
}


void WbfApiOperation::processAnswer(JsonObjectPtr aJsonResponse, ErrorPtr aError)
{
  mError = aError;
  if (Error::isOK(mError)) {
    SOLOG(mWbfComm, LOG_INFO, "Receiving API response: %s", JsonObject::text(aJsonResponse));
    if (aJsonResponse) {
      JsonObjectPtr o;
      if (aJsonResponse->get("status", o)) {
        string s = o->stringValue();
        if (s=="success") {
          mData = aJsonResponse->get("data"); // return the "data" field, if any
          if (!mData) {
            mError = Error::err<WbfCommError>(WbfCommError::ResponseErr, "missing 'data' field");
          }
        }
        else if (s=="error") {
          string msg = "<none>";
          aJsonResponse->get("message", o);
          if (o) msg = o->stringValue();
          mError = Error::err<WbfCommError>(WbfCommError::ApiError, "message: %s", msg.c_str());
          mData.reset();
        }
      }
      else {
        mError = Error::err<WbfCommError>(WbfCommError::ResponseErr, "missing 'status' field");
        mData.reset(); // no data
      }
    }
    else {
      mError = Error::err<WbfCommError>(WbfCommError::ResponseErr, "no data");
      mData.reset();
    }
  }
  else {
    SOLOG(mWbfComm, LOG_WARNING, "API error: %s", mError->text());
  }
  // done
  mCompleted = true;
  // have queue reprocessed
  mWbfComm.processOperations();
}



bool WbfApiOperation::hasCompleted()
{
  return mCompleted;
}



OperationPtr WbfApiOperation::finalize()
{
  if (mResultHandler) {
    mResultHandler(mData, mError);
    mResultHandler = NoOP; // call once only
  }
  return inherited::finalize();
}



void WbfApiOperation::abortOperation(ErrorPtr aError)
{
  if (!mAborted) {
    if (!mCompleted) {
      mWbfComm.mGatewayAPIComm.cancelRequest();
    }
    if (mResultHandler && aError) {
      mResultHandler(JsonObjectPtr(), aError);
      mResultHandler = NoOP; // call once only
    }
  }
  inherited::abortOperation(aError);
}




// MARK: - WbfComm


WbfComm::WbfComm() :
  inherited(MainLoop::currentMainLoop()),
  mGatewayAPIComm(MainLoop::currentMainLoop()),
  mApiReady(false)
{
  mGatewayAPIComm.isMemberVariable();
  mGatewayWebsocket.isMemberVariable();
  mGatewayAPIComm.setServerCertVfyDir("");
  // do not wait too long for API responses, but long enough to tolerate some lag in slow bridge or wifi network
  mGatewayAPIComm.setTimeout(10*Second);
}


WbfComm::~WbfComm()
{
  stopApi(NoOP);
}

// MARK: - Websocket

#define WEBSOCKET_OPEN_DELAY (2*Second)

void WbfComm::startupApi(WebSocketMessageCB aOnMessageCB, StatusCB aStartupCB)
{
  mWebSocketCB = aOnMessageCB;
  // set auth header for normal API accesses
  mGatewayAPIComm.clearRequestHeaders();
  if (mApiSecret.size()>0) {
    mGatewayAPIComm.addRequestHeader("Authorization", string_format("Bearer %s", mApiSecret.c_str()));
  }
  mApiReady = true;
  // start web socket
  mWebsocketTicket.executeOnce(boost::bind(&WbfComm::webSocketStart, this, aStartupCB), WEBSOCKET_OPEN_DELAY);
}


void WbfComm::stopApi(StatusCB aStopCB)
{
  if (mApiReady) {
    mWebsocketTicket.cancel();
    mGatewayWebsocket.close(aStopCB);
    mApiReady = false;
  }
  else {
    if (aStopCB) aStopCB(ErrorPtr());
  }
}


#define PING_INTERVAL (1*Minute)

void WbfComm::webSocketStart(StatusCB aStartupCB)
{
  mWebsocketTicket.cancel();
  mGatewayWebsocket.setMessageHandler(mWebSocketCB);
  mGatewayWebsocket.connectTo(
    boost::bind(&WbfComm::webStocketStatus, this, aStartupCB, _1),
    #ifdef __APPLE__
    string_format("ws://%s/api", mResolvedHost.c_str()), // we don't have a SSL-enabled uwsc on macOS
    #else
    string_format("wss://%s/api", mResolvedHost.c_str()),
    #endif
    PING_INTERVAL,
    string_format("Authorization: Bearer %s\r\n", mApiSecret.c_str())
  );
}

#define WEBSOCKET_REOPEN_WAITTIME (10*Second)

void WbfComm::webStocketStatus(StatusCB aStartupCB, ErrorPtr aError)
{
  mWebsocketTicket.cancel();
  if (Error::notOK(aError)) {
    OLOG(LOG_WARNING, "Websocket error: %s -> will retry opening after delay", aError->text());
    mWebsocketTicket.executeOnce(boost::bind(&WbfComm::webSocketStart, this, aStartupCB), WEBSOCKET_REOPEN_WAITTIME);
    return;
  }
  OLOG(LOG_INFO, "websocket connection established");
  if (aStartupCB) aStartupCB(aError);
}


ErrorPtr WbfComm::sendWebSocketTextMsg(const string aTextMessage)
{
  return mGatewayWebsocket.send(aTextMessage);
}


ErrorPtr WbfComm::sendWebSocketJsonMsg(JsonObjectPtr aJsonMessage)
{
  ErrorPtr err;
  if (aJsonMessage) {
    err = sendWebSocketTextMsg(aJsonMessage->json_str());
  }
  return err;
}



// MARK: - REST API

void WbfComm::apiQuery(const char* aUrlSuffix, WbfApiResultCB aResultHandler, MLMicroSeconds aTimeout)
{
  apiAction(WbfApiOperation::GET, aUrlSuffix, JsonObjectPtr(), aResultHandler, aTimeout, false);
}


void WbfComm::apiAction(WbfApiOperation::HttpMethods aMethod, const char* aUrlSuffix, JsonObjectPtr aData, WbfApiResultCB aResultHandler, MLMicroSeconds aTimeout, bool aNoAutoURL)
{
  if (!mApiReady && !aNoAutoURL) {
    if (aResultHandler) aResultHandler(JsonObjectPtr(), ErrorPtr(new WbfCommError(WbfCommError::ApiNotReady)));
  }
  string url;
  if (aNoAutoURL) {
    url = aUrlSuffix;
  }
  else {
    url = string_format("https://%s/api", mResolvedHost.c_str());
    url += nonNullCStr(aUrlSuffix);
  }
  WbfApiOperationPtr op = new WbfApiOperation(*this, aMethod, url.c_str(), aData, aResultHandler, aTimeout);
  // op->setInitiationDelay(100*MilliSecond, true); // in case we need to tame access, hopefully not
  queueOperation(op);
  // process operations
  processOperations();
}


// MARK: - Pairing and re-finding gateway


#define PAIRING_TIMEOUT (30*Second)

void WbfComm::pairGateway(StatusCB aPairingResultCB)
{
  mSearchTicket.executeOnce(boost::bind(&WbfComm::pairingTimeout, this, aPairingResultCB), PAIRING_TIMEOUT);
  if (!mFixedHostName.empty()) {
    // just try to claim on this gateway
    claimAccount(aPairingResultCB, mFixedHostName, "", mApiUserName);
  }
  else {
    #if DISABLE_DISCOVERY
    aPairingResultCB(TextError::err("No DNS-SD, must specify fixed gateway IP or hostname"));
    #else
    // use DNSSD to find candidates
    DnsSdManager::sharedDnsSdManager().browse("_http._tcp", boost::bind(&WbfComm::dnsSdPairingResultHandler, this, _1, _2, aPairingResultCB));
    #endif
  }
}


void WbfComm::stopPairing()
{
  // block all further callbacks
  mSearchTicket.cancel();
}



void WbfComm::pairingTimeout(StatusCB aPairingResultCB)
{
  aPairingResultCB(new WbfCommError(WbfCommError::PairingTimeout));
}


#if !DISABLE_DISCOVERY

bool WbfComm::dnsSdPairingResultHandler(ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo, StatusCB aPairingResultCB)
{
  if (!mSearchTicket) return false; // not searching any more, ignore result and abort further search
  if (Error::isOK(aError)) {
    // many devices advertise _http._tcp, select by name
    if (aServiceInfo->name.substr(0,6)!="wiser-") return true; // ignore, is not a wiser gateway, continue searching
    // extra safety, should also have a "type" TXT record
    DnsSdServiceInfo::TxtRecordsMap::iterator b = aServiceInfo->txtRecords.find("type");
    if (b==aServiceInfo->txtRecords.end()) return true; // ignore, is not a wyser gateway, continue searching
    // now this IS most probably a wiser gateway, try to claim the account
    claimAccount(aPairingResultCB, aServiceInfo->hostaddress, aServiceInfo->hostname, mApiUserName);
    return true; // look for others
  }
  else {
    FOCUSOLOG("discovery ended, error = %s (usually: allfornow)", aError->text());
    return false; // do not continue DNS-SD search
  }
}

#endif // !DISABLE_DISCOVERY


#define CLAIM_TIMEOUT (1*Minute)

void WbfComm::claimAccount(StatusCB aPairingResultCB, const string aResolvedHost, const string aHostName, const string aUserName)
{
  JsonObjectPtr claimParams = JsonObject::newObj();
  claimParams->add("user", JsonObject::newString(aUserName));
  // clone the account (and device names) from installer setup (eSetup app)
  claimParams->add("source", JsonObject::newString("installer"));
  apiAction(
    WbfApiOperation::POST,
    string_format("https://%s/api/account/claim", aResolvedHost.c_str()).c_str(),
    claimParams,
    boost::bind(&WbfComm::claimResultHander, this, aPairingResultCB, aResolvedHost, aHostName, _1, _2),
    CLAIM_TIMEOUT,
    true // api not yet ready, full url, no auth
  );
}


void WbfComm::claimResultHander(StatusCB aPairingResultCB, const string aResolvedHost, const string aHostName, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (!mSearchTicket) return; // search is over, no longer interested
  if (!aResult) return; // no result, claim failed
  // get the info
  JsonObjectPtr o;
  if (aResult->get("secret", o)) {
    // get secret
    mApiSecret = o->stringValue();
    // also remember host address and name for later re-finding
    mResolvedHost = aResolvedHost;
    mDNSSDHostName = aHostName;
    // successful pairing!
    mSearchTicket.cancel();
    aPairingResultCB(ErrorPtr());
    return;
  }
  else {
    aError = Error::err<WbfCommError>(WbfCommError::ResponseErr, "missing data or secret in claim response");
  }
  // just log errors here, claiming will end with timeout
  OLOG(LOG_WARNING, "Unsuccessful attempt to claim gateway @ %s: %s", aResolvedHost.c_str(), Error::text(aError));
}



#define REFIND_TIMEOUT (30*Second)

void WbfComm::refindGateway(StatusCB aFindingResultCB)
{
  mSearchTicket.executeOnce(boost::bind(&WbfComm::refindTimeout, this, aFindingResultCB), REFIND_TIMEOUT);
  if (!mFixedHostName.empty()) {
    // we have a fixed address, no finding needed
    mResolvedHost = mFixedHostName; // just use this one
    foundGateway(aFindingResultCB);
  }
  else {
    #if DISABLE_DISCOVERY
    mSearchTicket.cancel();
    aFindingResultCB(Error::err<WbfCommError>(WbfCommError::NotPaired, "No DNS-SD, must specify fixed gateway IP or hostname"));
    #else
    if (mDNSSDHostName.empty()) {
      mSearchTicket.cancel();
      aFindingResultCB(Error::err<WbfCommError>(WbfCommError::NotPaired, "No gateway paired"));
    }
    else {
      // use DNSSD to find candidates
      DnsSdManager::sharedDnsSdManager().browse("_lisa._tcp", boost::bind(&WbfComm::dnsSdRefindResultHandler, this, _1, _2, aFindingResultCB));
    }
    #endif
  }
}


void WbfComm::refindTimeout(StatusCB aFindingResultCB)
{
  aFindingResultCB(Error::err<WbfCommError>(WbfCommError::FindTimeout, "re-find timeout"));
}



#if !DISABLE_DISCOVERY

bool WbfComm::dnsSdRefindResultHandler(ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo, StatusCB aFindingResultCB)
{
  if (!mSearchTicket) return false; // not searching any more, ignore result and abort further search
  if (Error::isOK(aError)) {
    // check if this is our gateway
    if (aServiceInfo->hostname!=mDNSSDHostName) return true; // not our gateway, continue searching
    // found it!
    mSearchTicket.cancel();
    mResolvedHost = aServiceInfo->hostaddress;
    aFindingResultCB(ErrorPtr()); // success
    return false; // stop searching
  }
  else {
    mSearchTicket.cancel();
    FOCUSOLOG("discovery ended, error = %s (usually: allfornow)", aError->text());
    aFindingResultCB(Error::err<WbfCommError>(WbfCommError::FindTimeout, "dnssd ends: %s", aError->text()));
    return false; // do not continue DNS-SD search
  }
}

#endif // !DISABLE_DISCOVERY


void WbfComm::foundGateway(StatusCB aFindingResultCB)
{
  mSearchTicket.cancel();
  ErrorPtr err;
  if (mApiSecret.empty()) {
    err = Error::err<WbfCommError>(WbfCommError::NotPaired, "gateway @ %s is not paired", mResolvedHost.c_str());
    mResolvedHost.clear(); // not a valid address
  }
  aFindingResultCB(err);
}




#endif // ENABLE_WBF
