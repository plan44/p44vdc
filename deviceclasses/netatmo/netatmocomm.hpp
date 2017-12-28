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
#include "httpclient.hpp"


using namespace std;

namespace p44 {


  class NetatmoOperation : public HttpOperation {

      using inherited = HttpOperation;

      static const MLMicroSeconds OP_TIMEOUT = (10*Second);

      string contentType;
      string streamBuffer;

      virtual void sendRequest() P44_OVERRIDE;
      virtual void processAnswer(const string& aResponse, ErrorPtr aError) P44_OVERRIDE;

    public:
      NetatmoOperation(
          HttpClient &aHttpClient,
          const string& aMethod,
          const string& aUrl,
          const string& aRequestBody,
          HttpCommCB aResultHandler,
          const string& aContentType="application/json");
      virtual ~NetatmoOperation(){}
  };
  using NetatmoOperationPtr = boost::intrusive_ptr<NetatmoOperation>;

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
      enum class AccountStatus{
          disconnected,
          connected,
          offline
      };

   private:
     HttpClient httpClient;
     string accessToken;
     string refreshToken;
     string userEmail;

     AccountStatus accountStatus;
     ErrorPtr error;

     boost::signals2::signal<void(JsonObjectPtr)> dataPollCBs;

     static const string BASE_URL;
     static const string GET_STATIONS_DATA_URL;
     static const string GET_HOME_COACHS_URL;
     static const string AUTHENTICATE_URL;
     string clientId;
     string clientSecret;
     // basing on api description: 
     // "Do not try to pull data every minute. 
     // Netatmo Weather Station sends its measures to the server every ten minutes"
     static const MLMicroSeconds NETATMO_POLLING_INTERVAL = (10*Minute);


   public:

     NetatmoComm();
     virtual ~NetatmoComm() {}

     void loadConfigFile(JsonObjectPtr aConfigJson);
     void setAccessToken(const string& aAccessToken) { accessToken = aAccessToken; }
     string getAccessToken() const { return accessToken; }
     void setRefreshToken(const string& aRefreshToken) { refreshToken = aRefreshToken; }
     string getRefreshToken() const { return refreshToken; }
     void setUserEmail(const string& aUserEmail) { userEmail = aUserEmail; }
     string getUserEmail() const { return userEmail; }
     AccountStatus getAccountStatus() const { return accountStatus; }

     virtual boost::signals2::connection registerCallback(UpdateDataCB aCallback) P44_OVERRIDE;

     enum class Query {
         getStationsData,
         getHomeCoachsData,
         _num
     };

     void apiQuery(Query aQuery, HttpCommCB aResponseCB);

     void pollCycle(bool aEnqueueNextPoll=true);

     void authorizeByEmail(const string& aEmail, const string& aPassword, StatusCB aCompletedCB);
     bool checkIfAccessTokenExpired(JsonObjectPtr aJsonResponse);
     void refreshAccessToken();
     void gotAccessData(const string& aResponse, ErrorPtr aError, StatusCB aCompletedCB={});
     string getAccountStatusString();
     void updateAccountStatus(ErrorPtr aError);
     void disconnect();

   private:
     boost::optional<string> buildQuery(Query aQuery);
 };


} // namespace p44

#endif // ENABLE_NETATMO_V2
#endif // __p44vdc__netatmocomm__
