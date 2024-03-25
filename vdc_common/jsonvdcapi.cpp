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

#include "jsonvdcapi.hpp"

using namespace p44;



// MARK: - JsonApiValue

JsonApiValue::JsonApiValue()
{
}


ApiValuePtr JsonApiValue::newValue(ApiValueType aObjectType)
{
  ApiValuePtr newVal = ApiValuePtr(new JsonApiValue);
  newVal->setType(aObjectType);
  return newVal;
}



void JsonApiValue::clear()
{
  switch (getType()) {
    case apivalue_object:
      // just assign new object and forget old one
      mJsonObj = JsonObject::newObj();
      break;
    case apivalue_array:
      mJsonObj = JsonObject::newArray();
      break;
    // for unstuctured values, the json obj will be created on assign, so clear it now
    default:
      mJsonObj.reset();
      break;
  }
}


bool JsonApiValue::setStringValue(const string &aString)
{
  if (getType()==apivalue_string || getType()==apivalue_binary) {
    mJsonObj = JsonObject::newString(aString, false);
    return true;
  }
  else
    return inherited::setStringValue(aString);
};


void JsonApiValue::setBinaryValue(const string &aBinary)
{
  // represent as hex string in JSON
  setStringValue(binaryToHexString(aBinary));
}


string JsonApiValue::binaryValue()
{
  // parse binary string as hex
  return hexToBinaryString(stringValue().c_str());
}





void JsonApiValue::setJsonObject(JsonObjectPtr aJsonObject)
{
  mJsonObj = aJsonObject;
  // derive type
  if (!mJsonObj) {
    setType(apivalue_null);
  }
  else {
    switch (mJsonObj->type()) {
      case json_type_boolean: mObjectType = apivalue_bool; break;
      case json_type_double: mObjectType = apivalue_double; break;
      case json_type_int: mObjectType = apivalue_int64; break;
      case json_type_object: mObjectType = apivalue_object; break;
      case json_type_array: mObjectType = apivalue_array; break;
      case json_type_string: mObjectType = apivalue_string; break;
      case json_type_null:
      default:
        setType(apivalue_null);
        break;
    }
  }
}


void JsonApiValue::operator=(ApiValue &aApiValue)
{
  JsonApiValue *javP = dynamic_cast<JsonApiValue *>(&aApiValue);
  if (javP)
    setJsonObject(javP->mJsonObj);
  else
    inherited::operator=(aApiValue); // cross-type assignment, needs more expensive generic assignment
}



ApiValuePtr JsonApiValue::newValueFromJson(JsonObjectPtr aJsonObject)
{
  JsonApiValue *javP = new JsonApiValue;
  javP->setJsonObject(aJsonObject);
  return ApiValuePtr(javP);
}



// MARK: - VdcJsonApiServer


VdcApiConnectionPtr VdcJsonApiServer::newConnection()
{
  // create the right kind of API connection
  return VdcApiConnectionPtr(static_cast<VdcApiConnection *>(new VdcJsonApiConnection()));
}


ApiValuePtr VdcJsonApiServer::newApiValue()
{
  return ApiValuePtr(new JsonApiValue);
}




// MARK: - VdcJsonApiRequest


VdcJsonApiRequest::VdcJsonApiRequest(VdcJsonApiConnectionPtr aConnection, const JsonObjectPtr aJsonRpcId) :
  mJsonConnection(aConnection),
  mJsonRpcId(aJsonRpcId)
{
}


VdcApiConnectionPtr VdcJsonApiRequest::connection()
{
  return mJsonConnection;
}



ErrorPtr VdcJsonApiRequest::sendResult(ApiValuePtr aResult)
{
  LOG(LOG_INFO, "%s <- vDC, id=%s: result=%s", JsonObject::text(requestId()), apiName(), aResult ? aResult->description().c_str() : "<none>");
  JsonApiValuePtr result = boost::dynamic_pointer_cast<JsonApiValue>(aResult);
  return mJsonConnection->mJsonRpcComm->sendResult(requestId(), result ? result->jsonObject() : NULL);
}


ErrorPtr VdcJsonApiRequest::sendError(ErrorPtr aError)
{
  LOG(LOG_INFO, "%s <- vDC, id=%s: error='%s'", apiName(), JsonObject::text(requestId()), Error::text(aError));
  if (!aError) {
    aError = Error::ok();
  }
  JsonApiValuePtr errorData;
  VdcApiErrorPtr vdcApiErr = boost::dynamic_pointer_cast<VdcApiError>(aError);
  if (vdcApiErr) {
    // extra fields possible
    if (vdcApiErr->getErrorType()!=0 || !vdcApiErr->getUserFacingMessage().empty()) {
      errorData = JsonApiValuePtr(new JsonApiValue);
      errorData->setType(apivalue_object);
      errorData->add("errorType", errorData->newUint64(vdcApiErr->getErrorType()));
      errorData->add("userFacingMessage", errorData->newString(vdcApiErr->getUserFacingMessage()));
    }
  }
  return mJsonConnection->mJsonRpcComm->sendError(requestId(), (uint32_t)aError->getErrorCode(), *aError->getErrorMessage() ? aError->getErrorMessage() : NULL, errorData ? errorData->jsonObject() : JsonObjectPtr());
}



// MARK: - VdcJsonApiConnection


VdcJsonApiConnection::VdcJsonApiConnection()
{
  mJsonRpcComm = JsonRpcCommPtr(new JsonRpcComm(MainLoop::currentMainLoop()));
  // install JSON request handler locally
  mJsonRpcComm->setRequestHandler(boost::bind(&VdcJsonApiConnection::jsonRequestHandler, this, _1, _2, _3));
}



void VdcJsonApiConnection::jsonRequestHandler(const char *aMethod, const JsonObjectPtr aJsonRpcId, JsonObjectPtr aParams)
{
  ErrorPtr respErr;
  if (mApiRequestHandler) {
    // create params API value
    ApiValuePtr params = JsonApiValue::newValueFromJson(aParams);
    VdcApiRequestPtr request;
    // create request object in case this request expects an answer
    if (aJsonRpcId) {
      // Method
      request = VdcJsonApiRequestPtr(new VdcJsonApiRequest(VdcJsonApiConnectionPtr(this), aJsonRpcId));
      LOG(LOG_INFO, "%s -> vDC, id=%s: called method '%s', params=%s", apiName(), JsonObject::text(request->requestId()), aMethod, params ? params->description().c_str() : "<none>");
    }
    else {
      // Notification
      LOG(LOG_INFO, "%s -> vDC: sent notification '%s', params=%s", apiName(), aMethod, params ? params->description().c_str() : "<none>");
    }
    // call handler
    mApiRequestHandler(VdcJsonApiConnectionPtr(this), request, aMethod, params);
  }
}


void VdcJsonApiConnection::closeAfterSend()
{
  mJsonRpcComm->closeAfterSend();
}


ErrorPtr VdcJsonApiConnection::sendRequest(const string &aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler)
{
  JsonApiValuePtr params = boost::dynamic_pointer_cast<JsonApiValue>(aParams);
  ErrorPtr err;
  if (aResponseHandler) {
    // method call expecting response
    LOG(LOG_INFO, "%s <- vDC, id=%d: calling method '%s', params=%s", apiName(), mJsonRpcComm->lastRequestId(), aMethod.c_str(), aParams ? aParams->description().c_str() : "<none>");
    err = mJsonRpcComm->sendRequest(aMethod.c_str(), params->jsonObject(), boost::bind(&VdcJsonApiConnection::jsonResponseHandler, this, aResponseHandler, _1, _2, _3));
  }
  else {
    // notification
    LOG(LOG_INFO, "%s <- vDC: sending notification '%s', params=%s", apiName(), aMethod.c_str(), aParams ? aParams->description().c_str() : "<none>");
    err = mJsonRpcComm->sendRequest(aMethod.c_str(), params->jsonObject(), NoOP);
  }
  return err;
}


void VdcJsonApiConnection::jsonResponseHandler(VdcApiResponseCB aResponseHandler, int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData)
{
  if (aResponseHandler) {
    // create request object just to hold the response ID
    JsonObjectPtr respId = JsonObject::newInt32(aResponseId);
    ApiValuePtr resultOrErrorData = JsonApiValue::newValueFromJson(aResultOrErrorData);
    VdcApiRequestPtr request = VdcJsonApiRequestPtr(new VdcJsonApiRequest(VdcJsonApiConnectionPtr(this), respId));
    if (Error::isOK(aError)) {
      LOG(LOG_INFO, "%s -> vDC, id='%s', result=%s", apiName(), JsonObject::text(request->requestId()), resultOrErrorData ? resultOrErrorData->description().c_str() : "<none>");
    }
    else {
      LOG(LOG_INFO, "%s -> vDC, id='%s', error=%s, errordata=%s", apiName(), JsonObject::text(request->requestId()), aError->text(), resultOrErrorData ? resultOrErrorData->description().c_str() : "<none>");
    }
    aResponseHandler(VdcApiConnectionPtr(this), request, aError, resultOrErrorData);
  }
}
