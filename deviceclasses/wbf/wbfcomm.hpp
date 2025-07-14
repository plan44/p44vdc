//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__wbfcomm__
#define __p44vdc__wbfcomm__

#include "p44vdc_common.hpp"

#if ENABLE_WBF

#if !DISABLE_DISCOVERY
  #include "dnssd.hpp"
#endif

#include "jsonwebclient.hpp"
#include "operationqueue.hpp"
#include "websocket.hpp"

using namespace std;

namespace p44 {


  class WbfCommError : public Error
  {
  public:
    // Errors
    enum {
      OK,
      Failure,
      ApiNotReady,
      PairingTimeout,
      NotPaired,
      FindTimeout,
      ResponseErr,
      ApiError,
    };
    typedef int ErrorCodes;

    static const char *domain() { return "WbfComm"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE{ return WbfCommError::domain(); };
    explicit WbfCommError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE;
    #endif // ENABLE_NAMED_ERRORS
  };


  class WbfComm;
  typedef boost::intrusive_ptr<WbfComm> WbfCommPtr;


  /// will be called to deliver api result
  /// @param aResult the result in case of success.
  /// - In case of PUT, POST and DELETE requests, it is the entire response object, but only if it is a success. Otherwise, aError will return an error.
  /// - In case of GET requests, it is the entire answer object
  /// @param aError error in case of failure, error code is either a HueCommErrors enum or the error code as
  ///   delivered by the hue brigde itself.
  typedef boost::function<void (JsonObjectPtr aResult, ErrorPtr aError)> WbfApiResultCB;


  class WbfApiOperation : public Operation
  {
    typedef Operation inherited;

  public:

    typedef enum {
      GET,
      POST,
      PUT,
      PATCH,
      DELETE
    } HttpMethods;


    WbfApiOperation(WbfComm &aWbfComm, HttpMethods aMethod, const char* aUrl, JsonObjectPtr aData, WbfApiResultCB aResultHandler);
    virtual ~WbfApiOperation();

    virtual bool initiate();
    virtual bool hasCompleted();
    virtual OperationPtr finalize();
    virtual void abortOperation(ErrorPtr aError);

  private:

    WbfComm &mWbfComm;
    HttpMethods mMethod;
    string mUrl;
    JsonObjectPtr mData;
    bool mCompleted;
    ErrorPtr mError;
    WbfApiResultCB mResultHandler;

    void processAnswer(JsonObjectPtr aJsonResponse, ErrorPtr aError);

  };
  typedef boost::intrusive_ptr<WbfApiOperation> WbfApiOperationPtr;



  class WbfComm : public OperationQueue
  {
    typedef OperationQueue inherited;

    bool mApiReady;

  public:

    WbfComm();
    virtual ~WbfComm();

    /// @return type (such as: device, element, vdc, trigger) of the context object
    virtual string contextType() const P44_OVERRIDE { return "wbf"; }

    // HTTP communication object
    JsonWebClient mGatewayAPIComm;

    // Websocket for state change monitoring
    WebSocketClient mGatewayWebsocket;
    WebSocketMessageCB mWebSocketCB;

    // to be persisted state
    string mFixedHostName; ///< if empty, DNSSD will be used to find potential gateways. Otherwise, fixed hostname or IP
    string mDNSSDHostName; ///< DNSSD host name (must be set for re-finding paired gateway without fixed host name, will be set at pairing)
    string mApiSecret; ///< the API secret (must be set for re-finding paired gateway, will be set at pairing)

    // volatile state
    string mResolvedHost; ///< host name for REST API and websocket, IP address resolved or regular DNS name
    MLTicket mSearchTicket; ///< timeout for search
    MLTicket mWebsocketTicket; ///< websocket restart


    /// @name executing regular API calls
    /// @{

    /// Query information from the API
    /// @param aUrlSuffix the suffix to append to the baseURL+userName (including leading slash)
    /// @param aResultHandler will be called with the result
    void apiQuery(const char* aUrlSuffix, WbfApiResultCB aResultHandler);

    /// Send information to the API
    /// @param aMethod the HTTP method to use
    /// @param aUrlSuffix the suffix to append to the baseURL
    /// @param aData the data for the action to perform (JSON body of the request)
    /// @param aResultHandler will be called with the result
    /// @param aNoAutoURL if set, aUrlSuffix must be the complete URL (mResolvedBaseURL and mAPISecret will not be used automatically)
    void apiAction(WbfApiOperation::HttpMethods aMethod, const char* aUrlSuffix, JsonObjectPtr aData, WbfApiResultCB aResultHandler, bool aNoAutoURL = false);

    /// @}


    /// @name discovery and pairing
    /// @{

    /// pair a new gateway
    /// @param aFixedHostName if empty, DNSSD will be used to find potential gateways,
    ///   put all in pair mode and wait for user pressing button. Otherwise, the fixed host
    ///   name will be used to address the gateway to pair.
    void pairGateway(StatusCB aPairingResultCB);

    /// abort pairing
    void stopPairing();

    /// re-find a gateway
    void refindGateway(StatusCB aFindingResultCB);

    /// prepare API for normal calls (automatic base URL and auth header), start websocket
    /// @param aOnMessageCB called when websocket messages arrive
    /// @param aStartupCB called when API startup done or failed
    void startupApi(WebSocketMessageCB aOnMessageCB, StatusCB aStartupCB);

    /// stop API, stop websocket
    void stopApi(StatusCB aStopCB);

    ErrorPtr sendWebSocketTextMsg(const string aTextMessage);
    ErrorPtr sendWebSocketJsonMsg(JsonObjectPtr aJsonMessage);

    /// @}

  private:

    void pairingTimeout(StatusCB aPairingResultCB);

    void claimAccount(StatusCB aPairingResultCB, const string aResolvedHost, const string aHostName);
    void claimResultHander(StatusCB aPairingResultCB, const string aResolvedHost, const string aHostName, JsonObjectPtr aResult, ErrorPtr aError);

    #if !DISABLE_DISCOVERY
    bool dnsSdPairingResultHandler(ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo, StatusCB aPairingResultCB);
    bool dnsSdRefindResultHandler(ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo, StatusCB aFindingResultCB);
    #endif

    void refindTimeout(StatusCB aFindingResultCB);
    void foundGateway(StatusCB aFindingResultCB);

    void webSocketStart(StatusCB aStartupCB);
    void webStocketStatus(StatusCB aStartupCB, ErrorPtr aError);

  };
  
} // namespace p44

#endif // ENABLE_WBF
#endif // !__p44vdc__wbfcomm__
