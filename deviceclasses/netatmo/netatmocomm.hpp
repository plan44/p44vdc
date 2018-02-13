//
//  Copyright (c) 2017 digitalSTROM.org, Zurich, Switzerland
//
//  Author: Krystian Heberlein <krystian.heberlein@digitalstrom.com>
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

#ifndef __p44vdc__netatmocomm__
#define __p44vdc__netatmocomm__

#include "p44vdc_common.hpp"

#if ENABLE_NETATMO_V2

#include "boost/optional.hpp"
#include "boost/signals2.hpp"
#include "jsonobject.hpp"
#include "jsonwebclient.hpp"
#include "serialqueue.hpp"
#include "web.hpp"
#include "persistentstorage.hpp"


using namespace std;

namespace p44 {

  using UpdateDataCB = boost::function<void(JsonObjectPtr)>;

  class INetatmoComm
  {
    public:
      virtual ~INetatmoComm() {}
      virtual boost::signals2::connection registerCallback(UpdateDataCB aCallback)=0;
  };


 class NetatmoComm : public INetatmoComm
 {
   public:

   private:
     HttpClient httpClient;
     string accessToken;
     string refreshToken;
     string userEmail;

     AccountStatus accountStatus;
     ErrorPtr error;

     int refreshTokenRetries;

     boost::signals2::signal<void(JsonObjectPtr)> dataPollCBs;
     PersistentStorageWithRowId<PersistentParams, string, string, string, string, string> storage;

     static const string AUTHENTICATE_URL;
     string clientId;
     string clientSecret;
     // basing on api description: 
     // "Do not try to pull data every minute. 
     // Netatmo Weather Station sends its measures to the server every ten minutes"
     static const MLMicroSeconds POLLING_INTERVAL = (10*Minute);
     static const int REFRESH_TOKEN_RETRY_MAX = 3;

     static const int API_ERROR_INVALID_TOKEN = 2;
     static const int API_ERROR_TOKEN_EXPIRED = 3;


   public:

     NetatmoComm(ParamStore &aParamStore,  const string& aRowId);
     virtual ~NetatmoComm() {}

     void loadConfigFile(JsonObjectPtr aConfigJson);
     void setAccessToken(const string& aAccessToken);
     string getAccessToken() const { return accessToken; }
     void setRefreshToken(const string& aRefreshToken);
     string getRefreshToken() const { return refreshToken; }
     void setUserEmail(const string& aUserEmail);
     string getUserEmail() const { return userEmail; }
     AccountStatus getAccountStatus() const { return accountStatus; }

     virtual boost::signals2::connection registerCallback(UpdateDataCB aCallback) P44_OVERRIDE;

     enum class Query {
         getStationsData,
         getHomeCoachsData,
         _num
     };

     void apiQuery(Query aQuery, HttpCommCB aResponseCB);

     void pollCycle();
     void pollStationsData();
     void pollHomeCoachsData();

     void authorizeByEmail(const string& aEmail, const string& aPassword, StatusCB aCompletedCB);
     void refreshAccessToken(StatusCB aCompletedCB);
     void gotAccessData(const string& aResponse, ErrorPtr aError, StatusCB aCompletedCB={});
     void disconnect();
     bool isConfigured() { return !accessToken.empty(); }

 };

 class NetatmoOperation : public HttpOperation<> {

     using inherited = HttpOperation<>;

     static const MLMicroSeconds OP_TIMEOUT = (10*Second);
     static const string BASE_URL;
     static const string GET_STATIONS_DATA_URL;
     static const string GET_HOME_COACHS_URL;
     
     const string& accessToken;
     const NetatmoComm::Query query;

     virtual void sendRequest() P44_OVERRIDE;
//      virtual void processAnswer(const string& aResponse, ErrorPtr aError) P44_OVERRIDE;

   public:
     NetatmoOperation(
         NetatmoComm::Query aQuery,
         HttpClient &aHttpClient,
         const string& aAccessToken,
         HttpCommCB aResultHandler,
         AuthCallback aAuthCallback={});
     virtual ~NetatmoOperation(){}

     boost::optional<string> buildQuery(NetatmoComm::Query aQuery);
     virtual bool isAuthError(ErrorPtr aError) P44_OVERRIDE;
     virtual OperationPtr finalize() P44_OVERRIDE;
 };
 using NetatmoOperationPtr = boost::intrusive_ptr<NetatmoOperation>;

} // namespace p44

#endif // ENABLE_NETATMO_V2
#endif // __p44vdc__netatmocomm__
