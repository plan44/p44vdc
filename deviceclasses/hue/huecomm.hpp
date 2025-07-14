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

#ifndef __p44vdc__huecomm__
#define __p44vdc__huecomm__

#include "p44vdc_common.hpp"

#if ENABLE_HUE

#if !defined(HUE_DNSSD_DISCOVERY) && !DISABLE_DISCOVERY
  // modern hue bridge discovery is DNS-SD (Bonjour/Zeroconf)
  #define HUE_DNSSD_DISCOVERY 1
#endif
#ifndef HUE_SSDP_DISCOVERY
  // legacy hue bridge discovery is SSDP (UPnP)
  #define HUE_SSDP_DISCOVERY 1
#endif
#ifndef HUE_CLOUD_DISCOVERY
  // discovery via Philips/Signify cloud
  #define HUE_CLOUD_DISCOVERY 1
#endif

#if HUE_DNSSD_DISCOVERY
#include "dnssd.hpp"
#endif

#if HUE_SSDP_DISCOVERY
#include "ssdpsearch.hpp"
#endif

#include "jsonwebclient.hpp"
#include "operationqueue.hpp"

using namespace std;

namespace p44 {


  class HueCommError : public Error
  {
  public:
    // Errors
    enum {
      OK,
      ReservedForBridge = 1, ///< 1..999 are native bridge error codes
      UnauthorizedUser = 1, // invalid username, or no rights to modify the resource
      InvalidJSON = 2, // invalid JSON
      NotFound = 3, // resource does not exits (scene, group, light...)
      InvalidMethod = 4, // method is invalid for the resource addressed
      MissingParam = 5, // missing parameters
      InvalidParam = 6, // invalid/unknown parameter (in PUT)
      InvalidValue = 7, // value wrong / out of range
      ReadOnly = 8, // trying to change read-only parameter
      TooManyItems = 11, // too many items in list
      CloudRequired = 12, // connection to cloud/portal is required of the operation
      InternalError = 901, // internal problem in the bridge (not with the command sent)
      UuidNotFound = 1000, ///< bridge specified by uuid was not found
      ApiNotReady, ///< API not ready (bridge not yet found, no bridge paired)
      Description, ///< SSDP by uuid did find a device, but XML description was inaccessible or invalid
      InvalidUser, ///< bridge did not allow accessing the API with the username
      NoRegistration, ///< could not register with a bridge
      InvalidResponse, ///< invalid response from bridge (malformed JSON)
    };
    typedef int ErrorCodes;

    static const char *domain() { return "HueComm"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE{ return HueCommError::domain(); };
    explicit HueCommError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE;
    #endif // ENABLE_NAMED_ERRORS
  };


  class BridgeFinder;
  typedef boost::intrusive_ptr<BridgeFinder> BridgeFinderPtr;

  class HueComm;
  typedef boost::intrusive_ptr<HueComm> HueCommPtr;


  /// will be called to deliver api result
  /// @param aResult the result in case of success.
  /// - In case of PUT, POST and DELETE requests, it is the entire response object, but only if it is a success. Otherwise, aError will return an error.
  /// - In case of GET requests, it is the entire answer object
  /// @param aError error in case of failure, error code is either a HueCommErrors enum or the error code as
  ///   delivered by the hue brigde itself.
  typedef boost::function<void (JsonObjectPtr aResult, ErrorPtr aError)> HueApiResultCB;


  class HueApiOperation : public Operation
  {
    typedef Operation inherited;

  public:

    typedef enum {
      GET,
      POST,
      PUT,
      DELETE
    } HttpMethods;

    HueApiOperation(HueComm &aHueComm, HttpMethods aMethod, const char* aUrl, JsonObjectPtr aData, HueApiResultCB aResultHandler);
    virtual ~HueApiOperation();

    virtual bool initiate();
    virtual bool hasCompleted();
    virtual OperationPtr finalize();
    virtual void abortOperation(ErrorPtr aError);

  private:

    HueComm &mHueComm;
    HttpMethods mMethod;
    string mUrl;
    JsonObjectPtr mData;
    bool mCompleted;
    ErrorPtr mError;
    HueApiResultCB mResultHandler;

    void processAnswer(JsonObjectPtr aJsonResponse, ErrorPtr aError);

  };
  typedef boost::intrusive_ptr<HueApiOperation> HueApiOperationPtr;


  class BridgeFinder;

  class HueComm : public OperationQueue
  {
    typedef OperationQueue inherited;
    friend class BridgeFinder;

    bool mFindInProgress;
    bool mApiReady;

  public:

    HueComm();
    virtual ~HueComm();

    /// @return type (such as: device, element, vdc, trigger) of the context object
    virtual string contextType() const P44_OVERRIDE { return "hue"; }

    // HTTP communication object
    JsonWebClient mBridgeAPIComm;

    // volatile vars
    string mBaseURL; ///< base URL for API calls

    /// @name settings
    /// @{

    string mFixedBaseURL; ///< fixed hue API base URL, bypasses any SSDP/DNS-SD/hue cloud discovery methods
    string mBridgeIdentifier; ///< the identifier for finding the hue bridge (via SSDP: uuid, via DNS-SD or hue cloud: bridgeId)
    string mUserName; ///< the user name
    bool mUseHueCloudDiscovery; ///< if set, N-UPnP is used as a fallback to find bridges

    /// @}

    /// @name executing regular API calls
    /// @{

    /// Query information from the API
    /// @param aUrlSuffix the suffix to append to the baseURL+userName (including leading slash)
    /// @param aResultHandler will be called with the result
    void apiQuery(const char* aUrlSuffix, HueApiResultCB aResultHandler);

    /// Send information to the API
    /// @param aMethod the HTTP method to use
    /// @param aUrlSuffix the suffix to append to the baseURL+userName (including leading slash)
    /// @param aData the data for the action to perform (JSON body of the request)
    /// @param aResultHandler will be called with the result
    /// @param aNoAutoURL if set, aUrlSuffix must be the complete URL (baseURL and userName will not be used automatically)
    void apiAction(HueApiOperation::HttpMethods aMethod, const char* aUrlSuffix, JsonObjectPtr aData, HueApiResultCB aResultHandler, bool aNoAutoURL = false);

    /// helper to get success from apiAction results
    /// @param aResult a result as delivered by apiAction
    /// @param aIndex the index of the success item, defaults to 0
    /// @return contents of "success" item, if any.
    static JsonObjectPtr getSuccessItem(JsonObjectPtr aResult, int aIndex = 0);

    /// @}


    /// @name discovery and pairing
    /// @{

    /// will be called when findBridge completes
    /// @param aError error if find/learn was not successful. If no error, HueComm is now ready to
    ///   send API commands
    typedef boost::function<void (ErrorPtr aError)> HueBridgeFindCB;

    /// find and try to pair new hue bridge
    /// @param aDeviceType a short description to identify the type of device/software accessing the hue bridge
    /// @param aAuthTimeWindow how long we should look for hue bridges with link button pressed among the candidates
    /// @note on success, the ssdpUuid, apiToken and baseURL string member variables will be set (when aFindHandler is called)
    void findNewBridge(const char *aDeviceType, MLMicroSeconds aAuthTimeWindow, HueBridgeFindCB aFindHandler);

    /// stop finding bridges
    void stopFind();

    /// find an already known bridge again (might have different IP in DHCP environment)
    /// @param aFindHandler called to deliver find result
    /// @note ssdpUuid and apiToken member variables must be set to the pre-know bridge's parameters before calling this
    void refindBridge(HueBridgeFindCB aFindHandler);

    /// @}

  };
  
} // namespace p44

#endif // ENABLE_HUE
#endif // __p44vdc__huecomm__
