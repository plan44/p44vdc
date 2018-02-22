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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 6

#include "netatmocomm.hpp"

#if ENABLE_NETATMO_V2

#include "sstream"
#include "boost/algorithm/string.hpp"
#include "netatmodeviceenumerator.hpp"

using namespace p44;


// MARK: ===== NetatmoOperation


NetatmoOperation::NetatmoOperation(
    NetatmoComm::Query aQuery,
    HttpClient &aHttpClient,
    const string& aAccessToken,
    HttpCommCB aResultHandler,
    AuthCallback aAuthCallback
) :
    inherited(aHttpClient, HttpMethod::GET, {}, {}, aResultHandler, aAuthCallback),
    query(aQuery),
    accessToken(aAccessToken)
{
  this->setTimeout(OP_TIMEOUT);
}


bool NetatmoOperation::isAuthError(ErrorPtr aError)
{
  if (aError &&
      (aError->getErrorCode()==HTTP_FORBIDDEN_ERR_CODE
      || aError->getErrorCode()==HTTP_UNAUTHORIZED_ERR_CODE)
  ) {
    LOG(LOG_WARNING, "Auth error: '%s'", aError->description().c_str());
    return true;
  }
  return false;
}


void NetatmoOperation::sendRequest()
{
  if (auto path = buildQuery(query)) {
    urlPath = *path;
    httpClient.getApi().clearRequestHeaders();
    httpClient.getApi().addRequestHeader("Connection", "close");

    issueRequest("application/json", true, true);
  } else {
    abortOperation(ErrorPtr(new Error(HTTP_FORBIDDEN_ERR_CODE)));
  }
}


OperationPtr NetatmoOperation::finalize()
{
  /*if chunked data has been read out, terminate http request*/
  httpClient.getApi().cancelRequest();
  return inherited::finalize();
}


const string NetatmoOperation::BASE_URL = "https://api.netatmo.com";
const string NetatmoOperation::GET_STATIONS_DATA_URL = "/api/getstationsdata";
const string NetatmoOperation::GET_HOME_COACHS_URL = "/api/gethomecoachsdata";


boost::optional<string> NetatmoOperation::buildQuery(NetatmoComm::Query aQuery)
{
    if (accessToken.empty()) {
      return boost::none;
    }

  stringstream retUrl;
  retUrl << BASE_URL;

  switch(aQuery)
  {
    case NetatmoComm::Query::getStationsData:     retUrl << GET_STATIONS_DATA_URL; break;
    case NetatmoComm::Query::getHomeCoachsData:  retUrl << GET_HOME_COACHS_URL; break;
    default: return boost::none;
   }

  retUrl << "?access_token=" << accessToken;

  return retUrl.str();
}


// MARK: ===== NetatmoComm


const string NetatmoComm::AUTHENTICATE_URL = "https://api.netatmo.com/oauth2/token";


NetatmoComm::NetatmoComm(ParamStore &aParamStore,  const string& aRowId) :
    accountStatus(AccountStatus::disconnected),
    storage(aRowId, "CommSettings", {"accessToken",   accessData.token},
                                    {"refreshToken",  accessData.refreshToken},
                                    {"userEmail",     accessData.userEmail},
                                    {"clientId",      accessData.clientId},
                                    {"clientSecret",  accessData.clientSecret},
                                    aParamStore),
    refreshTokenRetries(0)
{
  httpClient.isMemberVariable();
  storage.load();
}

void NetatmoComm::setAccessData(const NetatmoAccessData& aAccessData)
{
  accessData = aAccessData;
  storage.save();
}


void NetatmoComm::setUserEmail(const string& aUserEmail)
{
  accessData.userEmail = aUserEmail;
  storage.save();
}


void NetatmoComm::apiQuery(Query aQuery, HttpCommCB aResponseCB)
{
  if (isConfigured()) {
    auto apiQueryCB = [=](const string& aResponse, ErrorPtr aError){
      this->accountStatus = updateAccountStatus(aError);
      if (aResponseCB) aResponseCB(aResponse, aError);
    };

    auto op = NetatmoOperationPtr(
        new NetatmoOperation(
            aQuery,
            httpClient,
            accessData.token,
            apiQueryCB,
            [=](StatusCB aCB){ this->refreshAccessToken(aCB); })
    );

    httpClient.enqueueAndProcessOperation(op);
  } else {
    if (aResponseCB) aResponseCB({}, TextError::err("apiQuery: No Access Token"));
  }
}


boost::signals2::connection NetatmoComm::registerCallback(UpdateDataCB aCallback)
{
  return dataPollCBs.connect(aCallback);
}


void NetatmoComm::pollCycle()
{
  // get weather stations state
  pollStationsData();
  MainLoop::currentMainLoop().executeOnce([&](auto...){ this->pollCycle(); }, POLLING_INTERVAL);
}


void NetatmoComm::pollStationsData()
{
  apiQuery(Query::getStationsData, [&](const string& aResponse, ErrorPtr aError){

    if (Error::isOK(aError)) {
      if (auto jsonResponse = JsonObject::objFromText(aResponse.c_str())) {
          dataPollCBs(NetatmoDeviceEnumerator::getDevicesJson(jsonResponse));
      }
      // now get home coach devices state
      pollHomeCoachsData();
    }

  });
}


void NetatmoComm::pollHomeCoachsData()
{
  apiQuery(Query::getHomeCoachsData, [&](const string& aResponse, ErrorPtr aError){

    if (Error::isOK(aError)) {
      if (auto jsonResponse = JsonObject::objFromText(aResponse.c_str())) {
        dataPollCBs(NetatmoDeviceEnumerator::getDevicesJson(jsonResponse));
      }
    }

  });
}


void NetatmoComm::refreshAccessToken(StatusCB aCompletedCB)
{
  if (refreshTokenRetries++ >= REFRESH_TOKEN_RETRY_MAX) {
    LOG(LOG_ERR, "Refresh Access Token not succeded. Account '%s' is going to be disconnected.", accessData.userEmail.c_str());
    disconnect();
    refreshTokenRetries = 0;
    if (aCompletedCB) aCompletedCB(TextError::err("Max retries exceeded for refresh token"));
    return;
  }

  if (!accessData.refreshToken.empty()) {
    stringstream requestBody;

    requestBody<<"grant_type=refresh_token"
        <<"&refresh_token="<<accessData.refreshToken
        <<"&client_id="<<accessData.clientId
        <<"&client_secret="<<accessData.clientSecret;

    auto refreshAccessTokenCB = [=](const string& aResponse, ErrorPtr aError){
      this->gotAccessData(aResponse, aError, aCompletedCB);
    };
    
    httpClient.getApi().httpRequest(
        AUTHENTICATE_URL.c_str(),
        refreshAccessTokenCB,
        "POST",
        requestBody.str().c_str(),
        "application/x-www-form-urlencoded;charset=UTF-8");

  } else {
    LOG(LOG_ERR, "NetatmoComm::refreshAccessToken no refresh token available");
    if (aCompletedCB) aCompletedCB(TextError::err("No refresh token is available"));
  }

}


void NetatmoComm::gotAccessData(const string& aResponse, ErrorPtr aError, StatusCB aCompletedCB)
{
  if(auto jsonResponse = JsonObject::objFromText(aResponse.c_str())) {
    if (auto accessTokenJson = jsonResponse->get("access_token")) {
      accessData.token = accessTokenJson->stringValue();
      if (auto refreshTokenJson = jsonResponse->get("refresh_token")) {
        accessData.refreshToken = refreshTokenJson->stringValue();
        refreshTokenRetries = 0;
        storage.save();
      }
      if (aCompletedCB) aCompletedCB(Error::ok());
      return;
    }
  } else {
    if (aCompletedCB) aCompletedCB(TextError::err("Authentication failure: Data Received '%s'", aResponse.c_str()));
  }
}


void NetatmoComm::disconnect()
{
  accessData = {};
  storage.save();
  accountStatus = AccountStatus::disconnected;
}


#endif // ENABLE_NETATMO_V2
