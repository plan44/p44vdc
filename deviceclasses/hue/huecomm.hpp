//
//  Copyright (c) 1-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "ssdpsearch.hpp"
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
      UuidNotFound = 1000, ///< bridge specified by uuid was not found
      ApiNotReady, ///< API not ready (bridge not yet found, no bridge paired)
      Description, ///< SSDP by uuid did find a device, but XML description was inaccessible or invalid
      InvalidUser, ///< bridge did not allow accessing the API with the username
      NoRegistration, ///< could not register with a bridge
      InvalidResponse, ///< invalid response from bridge (malformed JSON)
    };
    typedef int ErrorCodes;

    static const char *domain() { return "HueComm"; }
    virtual const char *getErrorDomain() const { return HueCommError::domain(); };
    explicit HueCommError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
  };


  typedef enum {
    httpMethodGET,
    httpMethodPOST,
    httpMethodPUT,
    httpMethodDELETE
  } HttpMethods;


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

    HueComm &hueComm;
    HttpMethods method;
    string url;
    JsonObjectPtr data;
    bool completed;
    ErrorPtr error;
    HueApiResultCB resultHandler;

    void processAnswer(JsonObjectPtr aJsonResponse, ErrorPtr aError);

  public:

    HueApiOperation(HueComm &aHueComm, HttpMethods aMethod, const char* aUrl, JsonObjectPtr aData, HueApiResultCB aResultHandler);
    virtual ~HueApiOperation();

    virtual bool initiate();
    virtual bool hasCompleted();
    virtual OperationPtr finalize();
    virtual void abortOperation(ErrorPtr aError);

  };
  typedef boost::intrusive_ptr<HueApiOperation> HueApiOperationPtr;


  class BridgeFinder;

  class HueComm : public OperationQueue
  {
    typedef OperationQueue inherited;
    friend class BridgeFinder;

    bool findInProgress;
    bool apiReady;
    MLMicroSeconds lastApiAction;

  public:

    HueComm();
    virtual ~HueComm();

    // HTTP communication object
    JsonWebClient bridgeAPIComm;

    // volatile vars
    string baseURL; ///< base URL for API calls

    /// @name settings
    /// @{

    string fixedBaseURL; ///< fixed hue API base URL, bypasses any SSDP searches
    string uuid; ///< the UUID for searching the hue bridge via SSDP
    string userName; ///< the user name
    bool useNUPnP; ///< if set, N-UPnP is used as a fallback to find bridges

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
    void apiAction(HttpMethods aMethod, const char* aUrlSuffix, JsonObjectPtr aData, HueApiResultCB aResultHandler, bool aNoAutoURL = false);

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

    typedef std::list<std::string> NupnpResult;
    typedef boost::function<void (NupnpResult)> HueBridgeNupnpFindCB;
    void findBridgesNupnp(HueBridgeNupnpFindCB aFindHandler);

  private:
    void gotBridgeNupnpResponse(JsonObjectPtr aResult, ErrorPtr aError, HueBridgeNupnpFindCB aFindHandler);
    /// @}

  };
  
} // namespace p44

#endif // ENABLE_HUE
#endif // __p44vdc__huecomm__
