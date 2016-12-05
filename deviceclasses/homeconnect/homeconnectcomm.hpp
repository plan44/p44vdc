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
  ///   delivered by the hue brigde itself.
  typedef boost::function<void (JsonObjectPtr aResult, ErrorPtr aError)> HomeConnectApiResultCB;


  class HomeConnectApiOperation : public Operation
  {
    typedef Operation inherited;

    HomeConnectComm &homeConnectComm;
    string method;
    string url;
    JsonObjectPtr data;
    bool completed;
    ErrorPtr error;
    HomeConnectApiResultCB resultHandler;

    void processAnswer(JsonObjectPtr aJsonResponse, ErrorPtr aError);

  public:

    HomeConnectApiOperation(HomeConnectComm &aHomeConnectComm, const string aMethod, const string aUrl, JsonObjectPtr aData, HomeConnectApiResultCB aResultHandler);
    virtual ~HomeConnectApiOperation();

    virtual bool initiate();
    virtual bool hasCompleted();
    virtual OperationPtr finalize();
    virtual void abortOperation(ErrorPtr aError);

  };
  typedef boost::intrusive_ptr<HomeConnectApiOperation> HomeConnectApiOperationPtr;


  class HomeConnectComm : public OperationQueue
  {
    typedef OperationQueue inherited;
    friend class HomeConnectApiOperation;

    bool findInProgress;
    bool apiReady;

  public:

    HomeConnectComm();
    virtual ~HomeConnectComm();

    // HTTP communication object
    JsonWebClient httpAPIComm;

    /// @name settings
    /// @{

    string accessToken; ///< the access token for issuing requests (obtained via OAuth)

    /// @}

    /// @name executing regular API calls
    /// @{

    /// Query information from the API
    /// @param aUrlSuffix the suffix to append to the baseURL+userName (including leading slash)
    /// @param aResultHandler will be called with the result
    void apiQuery(const char* aUrlSuffix, HomeConnectApiResultCB aResultHandler);

    /// Send information to the API
    /// @param aMethod the HTTP method to use
    /// @param aUrlPath the path to append to the API base URL (including leading slash if not empty)
    /// @param aData the data for the action to perform (JSON body of the request)
    /// @param aResultHandler will be called with the result
    /// @param aNoAutoURL if set, aUrlSuffix must be the complete URL (baseURL and userName will not be used automatically)
    void apiAction(const string aMethod, const string aUrlPath, JsonObjectPtr aData, HomeConnectApiResultCB aResultHandler, bool aNoAutoURL = false);

    /// @}

  };
  
} // namespace p44

#endif // ENABLE_HOMECONNECT
#endif // __p44vdc__homeconnectcomm__
