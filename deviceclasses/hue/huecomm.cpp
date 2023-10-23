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
#define FOCUSLOGLEVEL IFNOTREDUCEDFOOTPRINT(7)

#include "huecomm.hpp"

#if ENABLE_HUE

using namespace p44;

namespace {
  const string MODEL_FREE_RTOS = "929000226503";
  const string MODEL_HOMEKIT_LINUX = "BSB002";
  const string NUPNP_PATH = "https://discovery.meethue.com/";
  const string HUE_API_V1_PATH = "api";
}


#if ENABLE_NAMED_ERRORS
const char* HueCommError::errorName() const
{
  switch(getErrorCode()) {
    case UnauthorizedUser: return "UnauthorizedUser";
    case InvalidJSON: return "InvalidJSON";
    case NotFound: return "NotFound";
    case InvalidMethod: return "InvalidMethod";
    case MissingParam: return "MissingParam";
    case InvalidParam: return "InvalidParam";
    case InvalidValue: return "InvalidValue";
    case ReadOnly: return "ReadOnly";
    case TooManyItems: return "TooManyItems";
    case CloudRequired: return "CloudRequired";
    case InternalError: return "InternalError";
    case UuidNotFound: return "UuidNotFound";
    case ApiNotReady: return "ApiNotReady";
    case Description: return "Description";
    case InvalidUser: return "InvalidUser";
    case NoRegistration: return "NoRegistration";
    case InvalidResponse: return "InvalidResponse";
  }
  return NULL;
}
#endif // ENABLE_NAMED_ERRORS



// MARK: - HueApiOperation

HueApiOperation::HueApiOperation(HueComm &aHueComm, HttpMethods aMethod, const char* aUrl, JsonObjectPtr aData, HueApiResultCB aResultHandler) :
  mHueComm(aHueComm),
  mMethod(aMethod),
  mUrl(aUrl),
  mData(aData),
  mResultHandler(aResultHandler),
  mCompleted(false)
{
}



HueApiOperation::~HueApiOperation()
{

}



bool HueApiOperation::initiate()
{
  // initiate the web request
  const char *methodStr;
  switch (mMethod) {
    case httpMethodPOST : methodStr = "POST"; break;
    case httpMethodPUT : methodStr = "PUT"; break;
    case httpMethodDELETE : methodStr = "DELETE"; break;
    default : methodStr = "GET"; mData.reset(); break;
  }
  if (mMethod==httpMethodPUT) {
    SOLOG(mHueComm, LOG_INFO, "Sending API action (PUT) command: %s: %s", mUrl.c_str(), mData ? mData->c_strValue() : "<no data>");
  }
  mHueComm.mBridgeAPIComm.jsonRequest(mUrl.c_str(), boost::bind(&HueApiOperation::processAnswer, this, _1, _2), methodStr, mData);
  // executed
  return inherited::initiate();
}



void HueApiOperation::processAnswer(JsonObjectPtr aJsonResponse, ErrorPtr aError)
{
  mError = aError;
  if (Error::isOK(mError)) {
    SOLOG(mHueComm, LOG_INFO, "Receiving API response: %s", aJsonResponse ? aJsonResponse->c_strValue() : "<no data>");
    // pre-process response in case of non-GET
    if (mMethod!=httpMethodGET) {
      // Expected:
      //  [{"error":{"type":xxx,"address":"yyy","description":"zzz"}}]
      // or
      //  [{"success": { "xxx": "xxxxxxxx" }]
      int errCode = HueCommError::InvalidResponse;
      string errMessage = "invalid response";
      for (int i=0; i<aJsonResponse->arrayLength(); i++) {
        JsonObjectPtr responseItem = aJsonResponse->arrayGet(i);
        responseItem->resetKeyIteration();
        JsonObjectPtr responseParams;
        string statusToken;
        if (responseItem->nextKeyValue(statusToken, responseParams)) {
          if (statusToken=="success" && responseParams) {
            // apparently successful, return entire response
            // Note: use getSuccessItem() to get success details
            mData = aJsonResponse;
            errCode = HueCommError::OK; // ok
            break;
          }
          else if (statusToken=="error" && responseParams) {
            // make Error object out of it
            JsonObjectPtr e = responseParams->get("type");
            if (e)
              errCode = e->int32Value();
            e = responseParams->get("description");
            if (e)
              errMessage = e->stringValue();
            break;
          }
        }
      } // for
      if (errCode!=HueCommError::OK) {
        mError = Error::err_str<HueCommError>(errCode, errMessage);
      }
    }
    else {
      // GET, just return entire data
      mData = aJsonResponse;
    }
  }
  else {
    SOLOG(mHueComm, LOG_WARNING, "API error: %s", mError->text());
  }
  // done
  mCompleted = true;
  // have queue reprocessed
  mHueComm.processOperations();
}



bool HueApiOperation::hasCompleted()
{
  return mCompleted;
}



OperationPtr HueApiOperation::finalize()
{
  if (mResultHandler) {
    mResultHandler(mData, mError);
    mResultHandler = NoOP; // call once only
  }
  return inherited::finalize();
}



void HueApiOperation::abortOperation(ErrorPtr aError)
{
  if (!aborted) {
    if (!mCompleted) {
      mHueComm.mBridgeAPIComm.cancelRequest();
    }
    if (mResultHandler && aError) {
      mResultHandler(JsonObjectPtr(), aError);
      mResultHandler = NoOP; // call once only
    }
  }
  inherited::abortOperation(aError);
}




// MARK: - BridgeFinder

class p44::BridgeFinder : public P44Obj
{
  HueComm &mHueComm;
  HueComm::HueBridgeFindCB mCallback;

  BridgeFinderPtr mKeepAlive;

public:

  typedef map<string, string> StringStringMap;

  // discovery
  #if HUE_SSDP_DISCOVERY
  SsdpSearchPtr mBridgeDetector;
  StringStringMap mBridgeCandiates; ///< possible candidates for hue bridges, key=description URL, value=uuid
  StringStringMap::iterator mCurrentBridgeCandidate; ///< next candidate for bridge
  #endif

  MLMicroSeconds mAuthTimeWindow;
  StringStringMap mAuthCandidates; ///< bridges to try auth with, key=uuid, value=v1 API baseURL
  StringStringMap::iterator mCurrentAuthCandidate; ///< next auth candiate
  MLMicroSeconds mStartedAuth; ///< when auth was started
  MLTicket mRetryLoginTicket;

  // params and results
  string mUserName; ///< the user name / token
  string mBaseURL; ///< base URL for API calls
  string mDeviceType; ///< app description for login

  BridgeFinder(HueComm &aHueComm, HueComm::HueBridgeFindCB aFindHandler) :
    mCallback(aFindHandler),
    mHueComm(aHueComm),
    mStartedAuth(Never)
  {
    mBridgeDetector = SsdpSearchPtr(new SsdpSearch(MainLoop::currentMainLoop()));
  }

  virtual ~BridgeFinder()
  {
  }

  void findNewBridge(const char *aDeviceType, MLMicroSeconds aAuthTimeWindow, HueComm::HueBridgeFindCB aFindHandler)
  {
    mCallback = aFindHandler;
    mUserName.clear();
    mDeviceType = nonNullCStr(aDeviceType);
    mAuthTimeWindow = aAuthTimeWindow;
    mKeepAlive = BridgeFinderPtr(this);
    if (mHueComm.mFixedBaseURL.empty()) {
      // actually search for a new bridge
      searchForBridges("");
    }
    else {
      // we have a pre-known base URL for the hue API, use this without any find operation
      // - just put it in as the only auth candidate
      mAuthCandidates.clear();
      mAuthCandidates[normalizedBridgeId(mHueComm.mBridgeIdentifier)] = mHueComm.mFixedBaseURL;
      startPairingWithCandidates();
    }
  };


  void refindBridge(HueComm::HueBridgeFindCB aFindHandler)
  {
    mCallback = aFindHandler;
    mUserName = mHueComm.mUserName;
    mKeepAlive = BridgeFinderPtr(this);
    if (mHueComm.mFixedBaseURL.empty()) {
      // actually search for bridge
      searchForBridges(mHueComm.mBridgeIdentifier);
    }
    else {
      // we have a pre-known base URL for the hue API, use this without any find operation
      // - do a check
      FOCUSSOLOG(mHueComm, "Using fixed API URL %s: %s -> testing if accessible...", mHueComm.mBridgeIdentifier.c_str(), mHueComm.mFixedBaseURL.c_str());
      mHueComm.apiAction(httpMethodGET, mHueComm.mFixedBaseURL.c_str(), JsonObjectPtr(), boost::bind(&BridgeFinder::apiTested, this, _2), true); // no auto url = works w/o API ready
    }
  };


  void apiTested(ErrorPtr aError)
  {
    if (Error::isOK(aError)) {
      FOCUSSOLOG(mHueComm, "API URL %s tested accessible ok", mHueComm.mFixedBaseURL.c_str());
      mHueComm.mBaseURL = mHueComm.mFixedBaseURL; // use it
      mHueComm.mApiReady = true; // can use API now
    }
    else {
      SOLOG(mHueComm, LOG_WARNING, "API URL %s is not accessible: %s", mHueComm.mFixedBaseURL.c_str(), aError->text());
    }
    mCallback(aError); // success
    mKeepAlive.reset(); // will delete object if nobody else keeps it
  };


  void searchForBridges(const string aBridgeSearchId)
  {
    #if HUE_DNSSD_DISCOVERY
    // hue discovery is 1st prio
    searchBridgesViaDNSSD(aBridgeSearchId);
    #elif HUE_SSDP_DISCOVERY
    // if we have no hue discovery, we need to start SSDP here already (otherwise we'll start it when hue is done)
    searchBridgesViaSSDP(aBridgeSearchId);
    #elif HUE_CLOUD_DISCOVERY
    // if we have no local discovery, we can only do cloud discovery
    if (hueComm.useNUPnP) {
      // also try N-UPnP
      hueComm.searchBridgesViaHueCloud(boost::bind(&BridgeFinder::nupnpDiscoveryHandler, this, _1, aBridgeSearchId));
    }
    else {
      // we have no discovery method other than cloud, and it is disabled -> error
      callback(ErrorPtr(new HueCommError(HueCommError::CloudRequired)));
      keepAlive.reset(); // will delete object if nobody else keeps it
      return;
    }
    #else
    #error "at least one discovery method must be enabled"
    #endif
  }


  static string normalizedBridgeId(const string aId)
  {
    // SSDP UUID has the form      : 2f402f80-da50-11e1-9b23-001788....123456
    // DNSSD bridgeid has the form :                         001788fffe123456
    string bi = lowerCase(aId);
    if (bi.size()==16) return bi; // as-is
    return bi.substr(bi.size()-12,6)+"fffe"+bi.substr(bi.size()-6,6);
  }


  #if HUE_DNSSD_DISCOVERY

  void searchBridgesViaDNSSD(const string &aBridgeSearchId)
  {
    DnsSdManager::sharedDnsSdManager().browse("_hue._tcp", boost::bind(&BridgeFinder::bridgeDNSSDDiscoveryHandler, this, _1, _2, aBridgeSearchId));
  }


  bool bridgeDNSSDDiscoveryHandler(ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo, const string aBridgeSearchId)
  {
    if (Error::isOK(aError)) {
      // devices that advertise _hue._tcp should be hue bridges
      DnsSdServiceInfo::TxtRecordsMap::iterator b = aServiceInfo->txtRecords.find("bridgeid");
      if (b!=aServiceInfo->txtRecords.end()) {
        string bridgeid = normalizedBridgeId(b->second);
        #if P44_BUILD_DIGI
        if (aServiceInfo->port==443) {
          // old digiESP platform does not have proper crypto for https with hue bridge
          aServiceInfo->port = 80; // degrade to plain http
        }
        #endif
        string url = aServiceInfo->url()+"/"+HUE_API_V1_PATH;
        if (aBridgeSearchId.empty()) {
          SOLOG(mHueComm, LOG_INFO, "DNS-SD: bridge device found at %s, bridgeid=%s", aServiceInfo->url().c_str(), b->second.c_str());
          // put directly into auth candidates
          registerAuthCandidate(bridgeid, url, true);
        }
        else {
          // searching a specific one
          if (bridgeid==normalizedBridgeId(aBridgeSearchId)) {
            foundSpecificBridgeAt(url, bridgeid); // pass normalized id, as we want to migrate from uuid-based SSDP to DNSSD when possible
            return false; // no need to look further
          }
        }
      }
      return true; // continue looking for hue bridges
    }
    else {
      FOCUSSOLOG(mHueComm, "discovery ended, error = %s (usually: allfornow)", aError->text());
      #if !HUE_SSDP_DISCOVERY
      // we just have hue discovery, no SSDP to wait for
      #if HUE_CLOUD_DISCOVERY
      if (hueComm.useNUPnP) {
        // also try N-UPnP
        hueComm.searchBridgesViaHueCloud(boost::bind(&BridgeFinder::nupnpDiscoveryHandler, this, _1, aBridgeSearchId));
      }
      else
      #endif
      {
        candidatesCollectedFor(aBridgeSearchId);
      }
      #else
      // we also have SSDP, check that, too
      searchBridgesViaSSDP(aBridgeSearchId);
      #endif
      return false; // do not continue DNS-SD search
    }
  }


  #endif // HUE_DNSSD_DISCOVERY


  #if HUE_CLOUD_DISCOVERY

  void searchBridgesViaHueCloud(const string& aBridgeSearchId)
  {
    // call hue cloud
    mHueComm.mBridgeAPIComm.httpRequest(
      NUPNP_PATH.c_str(),
      boost::bind(&BridgeFinder::gotBridgeNupnpResponse, this, _1, _2, aBridgeSearchId),
      "GET"
    );
  }


  void gotBridgeNupnpResponse(const string &aResponse, ErrorPtr aError, const string aBridgeSearchId)
  {
    ErrorPtr err;
    JsonObjectPtr ans = JsonObject::objFromText(aResponse.c_str(), -1, &err);
    if (Error::isOK(err)) {
      // add bridges to the auth candidates
      // response format is like:
      // [{"id":"001788fffe123456","internalipaddress":"192.168.12.34","port":443}, {...}, ...]
      for (int i=0; i<ans->arrayLength(); i++) {
        JsonObjectPtr br = ans->arrayGet(i);
        JsonObjectPtr o;
        if (br->get("id", o)) {
          string bridgeId = normalizedBridgeId(o->stringValue());
          if (br->get("internalipaddress", o)) {
            string ipaddr = o->stringValue();
            int port = 80; // default to http
            if (br->get("port", o)) {
              port = o->int32Value();
            }
            string url = string_format("http%s://%s:%d/%s", port==443 ? "s" : "", ipaddr.c_str(), port, HUE_API_V1_PATH.c_str());
            if (!aBridgeSearchId.empty()) {
              // searching for specific bridge
              if (bridgeId==normalizedBridgeId(aBridgeSearchId)) {
                // searched-for bridge found: success!
                foundSpecificBridgeAt(url, aBridgeSearchId); // keep original search id, as we do NOT want to migrate from SSDP to NuPNP/cloud permanently
                return;
              }
            }
            else {
              // discovering bridges
              // - put directly into auth candidates (overwriting same URL obtained via other methods)
              SOLOG(mHueComm, LOG_INFO, "hue cloud: bridge found at %s bridgeid=%s", url.c_str(), bridgeId.c_str());
              registerAuthCandidate(bridgeId, url, true);
            }
          }
        }
      }
    }
    else {
      LOG(LOG_WARNING, "hue cloud discovery failed: %s", err->text());
    }
    #if HUE_SSDP_DISCOVERY
    // need SSDP candidate evaluation first
    processBridgeCandidates(aBridgeSearchId);
    #else
    candidatesCollectedFor(aBridgeSearchId);
    #endif
  }

  #endif // HUE_CLOUD_DISCOVERY


  #if HUE_SSDP_DISCOVERY

  void searchBridgesViaSSDP(const string &aBridgeSearchId)
  {
    if (!aBridgeSearchId.empty() && aBridgeSearchId.size()<36) {
      // sought for ID is not an UUID, so it can't possibly originate from a SSDP-only bridge
      // -> bypass SSDP, just consider search complete and skip to auth step
      candidatesCollectedFor(aBridgeSearchId);
    }
    else {
      mBridgeDetector->startSearch(boost::bind(&BridgeFinder::bridgeSSDPDiscoveryHandler, this, _1, _2, aBridgeSearchId), aBridgeSearchId.empty() ? NULL : aBridgeSearchId.c_str());
    }
  }


  void bridgeSSDPDiscoveryHandler(SsdpSearchPtr aSsdpSearch, ErrorPtr aError, const string aBridgeSearchId)
  {
    if (!aBridgeSearchId.empty()) {
      // searching for specific bridge
      if (Error::isOK(aError)) {
        // found specific bridge by uuid
        // - put it into queue as the only candidate
        mBridgeCandiates.clear();
        mBridgeCandiates[aSsdpSearch->locationURL] = aSsdpSearch->uuid;
        // process the candidate
        processBridgeCandidates(aBridgeSearchId);
      }
      else {
        if (mHueComm.mUseHueCloudDiscovery) {
          // could not find bridge, try N-UPnP
          searchBridgesViaHueCloud(aBridgeSearchId);
        }
        else {
          // no bridge found
          mCallback(ErrorPtr(new HueCommError(HueCommError::UuidNotFound)));
          mKeepAlive.reset(); // will delete object if nobody else keeps it
        }
      }
    }
    else {
      // collecting possible bridges to pair/unpair
      if (Error::isOK(aError)) {
        // check device for possibility of being a hue bridge
        if (aSsdpSearch->server.find("IpBridge")!=string::npos) {
          SOLOG(mHueComm, LOG_INFO, "SSDP: bridge candidate device found at %s, server=%s, uuid=%s", aSsdpSearch->locationURL.c_str(), aSsdpSearch->server.c_str(), aSsdpSearch->uuid.c_str());
          // put into map
          mBridgeCandiates[aSsdpSearch->locationURL] = aSsdpSearch->uuid;
        }
      }
      else {
        FOCUSSOLOG(mHueComm, "discovery ended, error = %s (usually: timeout)", aError->text());
        aSsdpSearch->stopSearch();
        #if HUE_CLOUD_DISCOVERY
        if (mHueComm.mUseHueCloudDiscovery) {
          // first try cloud to get more bridges
          searchBridgesViaHueCloud("");
        }
        else
        #endif
        {
          // just process the local SSDP results
          processBridgeCandidates("");
        }
      }
    }
  }


  void processBridgeCandidates(const string &aBridgeSearchId)
  {
    mCurrentBridgeCandidate = mBridgeCandiates.begin();
    processCurrentBridgeCandidate(aBridgeSearchId);
  }


  void processCurrentBridgeCandidate(const string& aBridgeSearchId)
  {
    if (mCurrentBridgeCandidate!=mBridgeCandiates.end()) {
      // request description XML
      mHueComm.mBridgeAPIComm.httpRequest(
        (mCurrentBridgeCandidate->first).c_str(),
        boost::bind(&BridgeFinder::handleServiceDescriptionAnswer, this, _1, _2, aBridgeSearchId),
        "GET"
      );
    }
    else {
      // done with all candidates
      mBridgeCandiates.clear();
      candidatesCollectedFor(aBridgeSearchId);
    }
  }


  void handleServiceDescriptionAnswer(const string &aResponse, ErrorPtr aError, const string aBridgeSearchId)
  {
    if (Error::isOK(aError)) {
      // show
      //FOCUSSOLOG(hueComm, "Received bridge description:\n%s", aResponse.c_str());
      FOCUSSOLOG(mHueComm, "Received service description XML");
      string manufacturer;
      string model;
      string urlbase;
      pickTagContents(aResponse, "manufacturer", manufacturer);
      pickTagContents(aResponse, "modelNumber", model);
      pickTagContents(aResponse, "URLBase", urlbase);
      if (
        aResponse.find("hue")!=string::npos && // the word "hue" must be contained, but no longer Philips (it's Signify now, who knows when they will change again...)
        (model == MODEL_FREE_RTOS || model == MODEL_HOMEKIT_LINUX) &&
        !urlbase.empty() &&
        !mCurrentBridgeCandidate->second.empty()
      ) {
        // create the base address for the API
        urlbase += HUE_API_V1_PATH;
        if (!aBridgeSearchId.empty()) {
          // looking for one specific bridge only
          if (aBridgeSearchId==mCurrentBridgeCandidate->second) {
            // that's my known hue bridge, save the URL and report success
            foundSpecificBridgeAt(urlbase, aBridgeSearchId); // keep original search id (which is an uuid for SSDP)
            return;
          }
        }
        else {
          // finding bridges: that's a hue bridge, remember it for trying to authorize
          SOLOG(mHueComm, LOG_INFO, "SSDP: bridge device found at %s, bridgeid=%s", urlbase.c_str(), mCurrentBridgeCandidate->second.c_str());
          registerAuthCandidate(mCurrentBridgeCandidate->second, urlbase, false);
        }
      }
    }
    else {
      FOCUSSOLOG(mHueComm, "Error accessing bridge description: %s", aError->text());
    }
    // try next
    ++mCurrentBridgeCandidate;
    processCurrentBridgeCandidate(aBridgeSearchId); // process next, if any
  }

  #endif // HUE_SSDP_DISCOVERY


  void registerAuthCandidate(const string aOriginalBridgeId, const string aBaseUrl, bool aOverride)
  {
    string bridgeid = normalizedBridgeId(aOriginalBridgeId);
    StringStringMap::iterator pos;
    // possibly overwrite
    for (pos = mAuthCandidates.begin(); pos!=mAuthCandidates.end(); ++pos) {
      if (normalizedBridgeId(pos->first)==bridgeid) break;
    }
    // insert new or possibly override existing
    if (pos!=mAuthCandidates.end()) {
      // entry with same normalized bridgeid (but possibly uuid vs bridgeid form) exists
      if (!aOverride) return; // leave existing entry untouched
      // override: first remove similar (but possibly differently keyed) entry
      mAuthCandidates.erase(pos);
    }
    // register with actual original form of bridgeid key
    mAuthCandidates[aOriginalBridgeId] = aBaseUrl;
  }


  void foundSpecificBridgeAt(const string &aUrlbase, const string aBridgeIdentifier)
  {
    mHueComm.mBaseURL = aUrlbase; // save it
    mHueComm.mBridgeIdentifier = aBridgeIdentifier; // also the bridgeId relevant for the successful find method
    mHueComm.mApiReady = true; // can use API now
    FOCUSSOLOG(mHueComm, "pre-known bridge %s found at %s", mHueComm.mBridgeIdentifier.c_str(), mHueComm.mBaseURL.c_str());
    mCallback(ErrorPtr()); // success
    mKeepAlive.reset(); // will delete object if nobody else keeps it
  }


  void candidatesCollectedFor(const string &aBridgeSearchId)
  {
    if (aBridgeSearchId.empty()) {
      // finding new bridges - attempt user login
      startPairingWithCandidates();
    }
    else {
      // searching for a specific bridge, but none found (if we'd found it, we'd not get here)
      mCallback(ErrorPtr(new HueCommError(HueCommError::UuidNotFound)));
      mKeepAlive.reset(); // will delete object if nobody else keeps it
    }
  }

  void startPairingWithCandidates()
  {
    // now attempt to pair with one of the candidates
    mStartedAuth = MainLoop::now();
    processAuthCandidates();
  }


  void processAuthCandidates()
  {
    mCurrentAuthCandidate = mAuthCandidates.begin();
    processCurrentAuthCandidate();
  }


  void processCurrentAuthCandidate()
  {
    if (mCurrentAuthCandidate!=mAuthCandidates.end() && mHueComm.mFindInProgress) {
      // try to authorize
      FOCUSSOLOG(mHueComm, "Auth candidate: bridgeid=%s, baseURL=%s -> try creating user", mCurrentAuthCandidate->first.c_str(), mCurrentAuthCandidate->second.c_str());
      JsonObjectPtr request = JsonObject::newObj();
      request->add("devicetype", JsonObject::newString(mDeviceType));
      mHueComm.apiAction(httpMethodPOST, mCurrentAuthCandidate->second.c_str(), request, boost::bind(&BridgeFinder::handleCreateUserAnswer, this, _1, _2), true);
    }
    else {
      // done with all candidates (or find aborted in hueComm)
      if (mAuthCandidates.size()>0 && MainLoop::now()<mStartedAuth+mAuthTimeWindow && mHueComm.mFindInProgress) {
        // we have still candidates and time to do a retry in a second, and find is not aborted
        mRetryLoginTicket.executeOnce(boost::bind(&BridgeFinder::processAuthCandidates, this), 1*Second);
        return;
      }
      else {
        // all candidates tried, nothing found in given time
        SOLOG(mHueComm, LOG_NOTICE, "Could not register with a bridge");
        mHueComm.mFindInProgress = false;
        mCallback(Error::err<HueCommError>(HueCommError::NoRegistration, "No hue bridge found ready to register"));
        // done!
        mKeepAlive.reset(); // will delete object if nobody else keeps it
        return;
      }
    }
  }


  void handleCreateUserAnswer(JsonObjectPtr aJsonResponse, ErrorPtr aError)
  {
    if (Error::isOK(aError)) {
      FOCUSSOLOG(mHueComm, "Received success answer:\n%s", JsonObject::text(aJsonResponse));
      JsonObjectPtr s = HueComm::getSuccessItem(aJsonResponse);
      // apparently successful, extract user name
      if (s) {
        JsonObjectPtr u = s->get("username");
        if (u) {
          mHueComm.mUserName = u->stringValue();
          mHueComm.mBridgeIdentifier = mCurrentAuthCandidate->first;
          mHueComm.mBaseURL = mCurrentAuthCandidate->second;
          mHueComm.mApiReady = true; // can use API now
          FOCUSSOLOG(mHueComm, "Bridge %s @ %s: successfully registered as user %s", mHueComm.mBridgeIdentifier.c_str(), mHueComm.mBaseURL.c_str(), mHueComm.mUserName.c_str());
          // successfully registered with hue bridge, let caller know
          mCallback(ErrorPtr());
          // done!
          mKeepAlive.reset(); // will delete object if nobody else keeps it
          return;
        }
      }
    }
    else {
      SOLOG(mHueComm, LOG_INFO, "Bridge: Cannot create user: %s", aError->text());
    }
    // try next
    ++mCurrentAuthCandidate;
    processCurrentAuthCandidate(); // process next, if any
  }

}; // BridgeFinder



// MARK: - hueComm


HueComm::HueComm() :
  inherited(MainLoop::currentMainLoop()),
  mBridgeAPIComm(MainLoop::currentMainLoop()),
  mFindInProgress(false),
  mApiReady(false),
  mUseHueCloudDiscovery(false)
{
  mBridgeAPIComm.setServerCertVfyDir("");
  mBridgeAPIComm.isMemberVariable();
  // do not wait too long for API responses, but long enough to tolerate some lag in slow bridge or wifi network
  mBridgeAPIComm.setTimeout(10*Second);
}


HueComm::~HueComm()
{
}


void HueComm::apiQuery(const char* aUrlSuffix, HueApiResultCB aResultHandler)
{
  apiAction(httpMethodGET, aUrlSuffix, JsonObjectPtr(), aResultHandler);
}


void HueComm::apiAction(HttpMethods aMethod, const char* aUrlSuffix, JsonObjectPtr aData, HueApiResultCB aResultHandler, bool aNoAutoURL)
{
  if (!mApiReady && !aNoAutoURL) {
    if (aResultHandler) aResultHandler(JsonObjectPtr(), ErrorPtr(new HueCommError(HueCommError::ApiNotReady)));
  }
  string url;
  if (aNoAutoURL) {
    url = aUrlSuffix;
  }
  else {
    url = mBaseURL;
    if (mUserName.length()>0)
      url += "/" + mUserName;
    url += nonNullCStr(aUrlSuffix);
  }
  HueApiOperationPtr op = HueApiOperationPtr(new HueApiOperation(*this, aMethod, url.c_str(), aData, aResultHandler));
  // Philips says: no more than 10 API calls per second
  // see http://www.developers.meethue.com/faq-page
  // Q: How many commands you can send per second?
  // A: You can send commands to the lights too fast. If you stay roughly around 10 commands per
  //    second to the /lights resource as maximum you should be fine.
  op->setInitiationDelay(100*MilliSecond, true); // do not start next command earlier than 100mS after the previous one
  queueOperation(op);
  // process operations
  processOperations();
}


JsonObjectPtr HueComm::getSuccessItem(JsonObjectPtr aResult, int aIndex)
{
  if (aResult && aIndex<aResult->arrayLength()) {
    JsonObjectPtr responseItem = aResult->arrayGet(aIndex);
    JsonObjectPtr successItem;
    if (responseItem && responseItem->get("success", successItem, false)) {
      return successItem;
    }
  }
  return JsonObjectPtr();
}



void HueComm::findNewBridge(const char *aDeviceType, MLMicroSeconds aAuthTimeWindow, HueBridgeFindCB aFindHandler)
{
  mFindInProgress = true;
  BridgeFinderPtr bridgeFinder = BridgeFinderPtr(new BridgeFinder(*this, aFindHandler));
  bridgeFinder->findNewBridge(aDeviceType, aAuthTimeWindow, aFindHandler);
};


void HueComm::stopFind()
{
  mFindInProgress = false;
}


void HueComm::refindBridge(HueBridgeFindCB aFindHandler)
{
  mApiReady = false; // not yet found, API disabled
  BridgeFinderPtr bridgeFinder = BridgeFinderPtr(new BridgeFinder(*this, aFindHandler));
  bridgeFinder->refindBridge(aFindHandler);
};


#endif // ENABLE_HUE
