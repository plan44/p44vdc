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
    HttpClient &aHttpClient,
    const string& aMethod,
    const string& aUrl,
    const string& aRequestBody,
    HttpCommCB aResultHandler,
    const string& aContentType
) :
    inherited(aHttpClient, aMethod, aUrl, aRequestBody, aResultHandler),
    contentType(aContentType)
{
  /*set timeout for every request, look at the explanation at NetatmoOperation::sendRequest*/
  this->setTimeout(OP_TIMEOUT);
}

void NetatmoOperation::sendRequest()
{
  auto httpCallback = [=](const string& aResponse, ErrorPtr aError){

    if (Error::isOK(aError)){
      string decoded;
      streamBuffer += aResponse;

      bool chunked = any_of(
          httpClient.getHttpApi().responseHeaders->begin(),
          httpClient.getHttpApi().responseHeaders->end(),
          [=](auto aElem){ return ((aElem.first == "Transfer-Encoding") && (aElem.second == "chunked")); }
      );

      if (chunked) {
        decoded = httputils::decodeChunkData(streamBuffer);
      } else {
        decoded = aResponse;
      }

      /* check if string is valid as json data */
      if (auto jsonResponse = JsonObject::objFromText(decoded.c_str())){
        this->processAnswer(decoded, aError);
      }
    } else {
      LOG(LOG_ERR, "NetatmoOperation Response Error: '%s'", aError->description().c_str());
      inherited::abortOperation(aError);
    }

  };

  httpClient.getHttpApi().clearRequestHeaders();
  httpClient.getHttpApi().addRequestHeader("Connection", "close");

  httpClient.getHttpApi().httpRequest(
      url.c_str(),
      httpCallback,
      method.c_str(),
      requestBody.c_str(),
      contentType.c_str(),
      -1,
      true,
      true
  );
}

void NetatmoOperation::processAnswer(const string& aResponse, ErrorPtr aError)
{
  /*if chunked data has been read out, terminate http request*/
  httpClient.getHttpApi().cancelRequest();
  inherited::processAnswer(aResponse, aError);
}



// MARK: ===== NetatmoComm

const string NetatmoComm::BASE_URL = "https://api.netatmo.com";
const string NetatmoComm::GET_STATIONS_DATA_URL = "/api/getstationsdata";
const string NetatmoComm::GET_HOME_COACHS_URL = "/api/gethomecoachsdata";
const string NetatmoComm::AUTHENTICATE_URL = "https://api.netatmo.com/oauth2/token";


NetatmoComm::NetatmoComm() :
    accountStatus(AccountStatus::disconnected)
{
  httpClient.isMemberVariable();
}

void NetatmoComm::loadConfigFile(JsonObjectPtr aConfigJson)
{
  if (aConfigJson) {
    if (auto clientIdJson = aConfigJson->get("client_id")){
      clientId = clientIdJson->stringValue();
      LOG(LOG_INFO, "CLIENT ID: '%s'", clientId.c_str());
    }

    if (auto clientSecretJson = aConfigJson->get("client_secret")){
      clientSecret = clientSecretJson->stringValue();
      LOG(LOG_INFO, "CLIENT SECRET: '%s'", clientSecret.c_str());
    }
  } else {
    LOG(LOG_ERR, "NetatmoComm error: cannot load configuration");
  }
}


boost::optional<string> NetatmoComm::buildQuery(Query aQuery)
{
  if (accessToken.empty()) {
    accountStatus = AccountStatus::disconnected;
    return boost::none;
  }

  stringstream retUrl;
  retUrl << BASE_URL;

  switch(aQuery)
  {
    case Query::getStationsData:     retUrl << GET_STATIONS_DATA_URL; break;
    case Query::getHomeCoachsData:  retUrl << GET_HOME_COACHS_URL; break;
    default: return boost::none;
   }

  retUrl << "?access_token=" << accessToken;

  return retUrl.str();
}


void NetatmoComm::apiQuery(Query aQuery, HttpCommCB aResponseCB)
{
  if (auto command = buildQuery(aQuery)) {

    auto op = NetatmoOperationPtr(
        new NetatmoOperation(
            httpClient,
            "GET",
            command.value().c_str(),
            {},
            [=](const string& aResponse, ErrorPtr aError){
                // save an error and set account status
                updateAccountStatus(aError);
                if (aResponseCB) aResponseCB(aResponse, aError);
              }
        )
    );

    httpClient.queueOperation(op);
    httpClient.processOperations();
  } else {
    if (aResponseCB) aResponseCB({}, TextError::err("NetatmoComm::apiQuery: Cannot build query"));
  }
}


boost::signals2::connection NetatmoComm::registerCallback(UpdateDataCB aCallback)
{
  return dataPollCBs.connect(aCallback);
}


void NetatmoComm::pollCycle(bool aEnqueueNextPoll)
{
  apiQuery(Query::getStationsData, [&](const string& aResponse, ErrorPtr aError){

    if (Error::isOK(aError)) {
      if (auto jsonResponse = JsonObject::objFromText(aResponse.c_str())) {
        // even when api error occured, http code is 200, thus error check in json response is needed
        if (checkIfAccessTokenExpired(jsonResponse)){
          accountStatus = AccountStatus::disconnected;
          refreshAccessToken();
        } else {
          dataPollCBs(NetatmoDeviceEnumerator::getDevicesJson(jsonResponse));
        }
      }
    }
  });

  apiQuery(Query::getHomeCoachsData, [&](const string& aResponse, ErrorPtr aError){

    if (Error::isOK(aError)) {
      if (auto jsonResponse = JsonObject::objFromText(aResponse.c_str())) {
        dataPollCBs(NetatmoDeviceEnumerator::getDevicesJson(jsonResponse));
      }
    }
  });

  if (aEnqueueNextPoll)
    MainLoop::currentMainLoop().executeOnce([&](auto...){ this->pollCycle(); }, NETATMO_POLLING_INTERVAL);
}

void NetatmoComm::authorizeByEmail(const string& aEmail, const string& aPassword, StatusCB aCompletedCB)
{
  stringstream requestBody;

  requestBody<<"grant_type=password"
      <<"&username="<<HttpComm::urlEncode(aEmail, false)
      <<"&password="<<HttpComm::urlEncode(aPassword, false)
      <<"&client_id="<<clientId
      <<"&client_secret="<<clientSecret
      <<"&scope="<<HttpComm::urlEncode("read_station read_homecoach", false);


  auto op = NetatmoOperationPtr(
          new NetatmoOperation(
              httpClient,
              "POST",
              AUTHENTICATE_URL,
              requestBody.str(),
              [=](auto...params){ this->gotAccessData(params..., aCompletedCB); },
              "application/x-www-form-urlencoded;charset=UTF-8"
          )
      );

      httpClient.queueOperation(op);
      httpClient.processOperations();
}

bool NetatmoComm::checkIfAccessTokenExpired(JsonObjectPtr aJsonResponse)
{
  if (aJsonResponse){
    if (auto errorJson = aJsonResponse->get("error")){
      if (auto errMsgJson = errorJson->get("message")){
        LOG(LOG_WARNING, "Response Error: '%s'", errMsgJson->stringValue().c_str());
        return errMsgJson->stringValue() == "Access token expired";
      }
    }
  }
  return false;
}


void NetatmoComm::refreshAccessToken()
{
  if (!refreshToken.empty()) {
    stringstream requestBody;

    requestBody<<"grant_type=refresh_token"
        <<"&refresh_token="<<refreshToken
        <<"&client_id="<<clientId
        <<"&client_secret="<<clientSecret;

    auto refreshAccessTokenCB = [=](const string& aResponse, ErrorPtr aError){
      this->gotAccessData(aResponse, aError, [=](ErrorPtr aError){
        if (Error::isOK(aError)){
          // poll data when access token has been renewed
          MainLoop::currentMainLoop().executeOnce([=](auto...){ this->pollCycle(false); }, 30*Second);
        }
      });
    };

    auto op = NetatmoOperationPtr(
        new NetatmoOperation(
            httpClient,
            "POST",
            AUTHENTICATE_URL,
            requestBody.str(),
            refreshAccessTokenCB,
            "application/x-www-form-urlencoded;charset=UTF-8"
        )
    );

    httpClient.queueOperation(op);
    httpClient.processOperations();
    
  } else {
    LOG(LOG_ERR, "NetatmoComm::refreshAccessToken no refresh token available");
  }

}

void NetatmoComm::gotAccessData(const string& aResponse, ErrorPtr aError, StatusCB aCompletedCB)
{
  if(auto jsonResponse = JsonObject::objFromText(aResponse.c_str())) {
    if (auto accessTokenJson = jsonResponse->get("access_token")) {
      accessToken = accessTokenJson->stringValue();
      if (auto refreshTokenJson = jsonResponse->get("refresh_token")) {
        refreshToken = refreshTokenJson->stringValue();
        // note: save tokens to database
      }
      if (aCompletedCB) aCompletedCB(Error::ok());
      return;
    }
  }
  if (aCompletedCB) aCompletedCB(TextError::err("Authentication failure: Data Received '%s'", aResponse.c_str()));
}


void NetatmoComm::updateAccountStatus(ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    accountStatus = AccountStatus::connected;
  } else {
    if(aError->isDomain(HttpCommError::domain())) {
      accountStatus = AccountStatus::offline;
      LOG(LOG_ERR, "HttpCommError %s ", aError->description().c_str());
    } else if (aError->getErrorCode() == 401 || aError->getErrorCode() == 403) {
      accountStatus = AccountStatus::disconnected;
      refreshAccessToken();
      LOG(LOG_ERR, "Authorization Error %s %d", aError->description().c_str());
    } else {
      LOG(LOG_ERR, "Communication Error %s %d", aError->description().c_str());
    }
  }
}

void NetatmoComm::disconnect()
{
  accessToken.clear();
  refreshToken.clear();
  userEmail.clear();

  accountStatus = AccountStatus::disconnected;
}


string NetatmoComm::getAccountStatusString()
{
  switch(accountStatus) {
    case AccountStatus::connected:        return "connected";
    case AccountStatus::disconnected:     return "disconnected";
    case AccountStatus::offline:          return "offline";
    default:                              return "unknown";
  }
}

#endif // ENABLE_NETATMO_V2
