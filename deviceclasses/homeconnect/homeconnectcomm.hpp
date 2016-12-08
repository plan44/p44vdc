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

#ifndef __p44vdc__homeconnectcomm__
#define __p44vdc__homeconnectcomm__

#include "p44vdc_common.hpp"

#if ENABLE_HOMECONNECT

#include "jsonwebclient.hpp"
#include "operationqueue.hpp"

using namespace std;

namespace p44 {


  class HomeConnectCommError : public Error
  {
  public:
    // Errors
    enum {
      OK,
      ReservedForHttp = 1, ///< 1..999 are native http error codes
      ApiNotReady = 1000, ///< API not ready (bridge not yet found, no bridge paired)
    };
    typedef int ErrorCodes;

    static const char *domain() { return "HomeConnectComm"; }
    virtual const char *getErrorDomain() const { return HomeConnectCommError::domain(); };
    HomeConnectCommError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
  };


  class HomeConnectComm;
  typedef boost::intrusive_ptr<HomeConnectComm> HomeConnectCommPtr;


  /// will be called to deliver api result
  /// @param aResult the result in case of success.
  /// - In case of PUT, POST and DELETE requests, it is the entire response object, but only if it is a success. Otherwise, aError will return an error.
  /// - In case of GET requests, it is the entire answer object
  /// @param aError error in case of failure, error code is either a HomeConnectCommErrors enum or the error code as
  ///   delivered by the API itself.
  typedef boost::function<void (JsonObjectPtr aResult, ErrorPtr aError)> HomeConnectApiResultCB;


  class HomeConnectApiOperation : public Operation
  {
    typedef Operation inherited;

    HomeConnectComm &homeConnectComm;
    string method;
    string urlPath;
    JsonObjectPtr data;
    bool completed;
    ErrorPtr error;
    HomeConnectApiResultCB resultHandler;

    void sendRequest();
    void processAnswer(JsonObjectPtr aJsonResponse, ErrorPtr aError);
    void refreshAccessToken();
    void processRefreshAnswer(JsonObjectPtr aJsonResponse, ErrorPtr aError);

  public:

    HomeConnectApiOperation(HomeConnectComm &aHomeConnectComm, const string aMethod, const string aUrlPath, JsonObjectPtr aData, HomeConnectApiResultCB aResultHandler);
    virtual ~HomeConnectApiOperation();

    virtual bool initiate();
    virtual bool hasCompleted();
    virtual OperationPtr finalize();
    virtual void abortOperation(ErrorPtr aError);

  };
  typedef boost::intrusive_ptr<HomeConnectApiOperation> HomeConnectApiOperationPtr;


  /// will be called to deliver events
  /// @param aEventType the type of event
  /// @param aEventData the event data.
  /// @param aError error in case of failure, error code is either a HomeConnectCommErrors enum or the error code as
  ///   delivered by the API itself.
  typedef boost::function<void (string aEventType, JsonObjectPtr aEventData, ErrorPtr aError)> HomeConnectEventResultCB;


  class HomeConnectEventMonitor: public JsonWebClient
  {
    typedef JsonWebClient inherited;

    HomeConnectComm &homeConnectComm;
    string urlPath;
    HomeConnectEventResultCB eventCB;

    string eventBuffer; ///< accumulating event data
    string eventType;
    string eventData;

  public:

    /// Create event monitor
    /// @param aHomeConnectComm a authorized homeconnect API communication object
    /// @param aUrlPath the path to append to the baseURL (including leading slash)
    /// @param aEventCB will be called when a event is received
    HomeConnectEventMonitor(HomeConnectComm &aHomeConnectComm, const char *aUrlPath, HomeConnectEventResultCB aEventCB);

    virtual ~HomeConnectEventMonitor();

  private:

    void sendGetEventRequest();
    void processEventData(const string &aResponse, ErrorPtr aError);

  };
  typedef boost::intrusive_ptr<HomeConnectEventMonitor> HomeConnectEventMonitorPtr;


  class HomeConnectComm : public OperationQueue
  {
    typedef OperationQueue inherited;
    friend class HomeConnectApiOperation;
    friend class HomeConnectEventMonitor;

    bool findInProgress;
    bool apiReady;

    string accessToken; ///< the access token for issuing requests (obtained via OAuth)

    string refreshToken; ///< the Oauth refresh token that can be used to obtain access tokens
    bool developerApi; ///< if set, developer (simulator) API is used

  public:

    HomeConnectComm();
    virtual ~HomeConnectComm();

    // HTTP communication object for serialized requests
    JsonWebClient httpAPIComm;

    // set the account to use
    // @param aRefreshToken the (long-lived) OAuth refresh token that can be used to obtain access tokens
    // @param aDeveloperApi if set, the developer (simulator) API is used
    void setAccount(string aRefreshToken, bool aDeveloperApi);

    /// @return true if API is configured
    bool isConfigured() { return !refreshToken.empty(); };

    /// the API base URL (depends on developerApi setting)
    string baseUrl();

    /// @name executing regular API calls
    /// @{

    /// Query information from the API
    /// @param aUrlPath the path to append to the baseURL (including leading slash)
    /// @param aResultHandler will be called with the result
    void apiQuery(const char* aUrlPath, HomeConnectApiResultCB aResultHandler);

    /// Send information to the API
    /// @param aMethod the HTTP method to use
    /// @param aUrlPath the path to append to the API base URL (including leading slash if not empty)
    /// @param aData the data for the action to perform (JSON body of the request)
    /// @param aResultHandler will be called with the result
    void apiAction(const string aMethod, const string aUrlPath, JsonObjectPtr aData, HomeConnectApiResultCB aResultHandler);


    /// @}

  };
  
} // namespace p44

#endif // ENABLE_HOMECONNECT
#endif // __p44vdc__homeconnectcomm__
