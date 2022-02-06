//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
      jsonObj = JsonObject::newObj();
      break;
    case apivalue_array:
      jsonObj = JsonObject::newArray();
      break;
    // for unstuctured values, the json obj will be created on assign, so clear it now
    default:
      jsonObj.reset();
      break;
  }
}


bool JsonApiValue::setStringValue(const string &aString)
{
  if (getType()==apivalue_string || getType()==apivalue_binary) {
    jsonObj = JsonObject::newString(aString, false);
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
  jsonObj = aJsonObject;
  // derive type
  if (!jsonObj) {
    setType(apivalue_null);
  }
  else {
    switch (jsonObj->type()) {
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
    setJsonObject(javP->jsonObj);
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


VdcJsonApiRequest::VdcJsonApiRequest(VdcJsonApiConnectionPtr aConnection, const char *aJsonRpcId)
{
  jsonConnection = aConnection;
  jsonRpcId = aJsonRpcId ? aJsonRpcId : ""; // empty if none passed
}


VdcApiConnectionPtr VdcJsonApiRequest::connection()
{
  return jsonConnection;
}



ErrorPtr VdcJsonApiRequest::sendResult(ApiValuePtr aResult)
{
  LOG(LOG_INFO, "vdSM <- vDC (JSON), id=%s: result=%s", requestId().c_str(), aResult ? aResult->description().c_str() : "<none>");
  JsonApiValuePtr result = boost::dynamic_pointer_cast<JsonApiValue>(aResult);
  return jsonConnection->jsonRpcComm->sendResult(requestId().c_str(), result ? result->jsonObject() : NULL);
}


ErrorPtr VdcJsonApiRequest::sendError(ErrorPtr aError)
{
  LOG(LOG_INFO, "vdSM <- vDC (JSON), id=%s: error='%s'", requestId().c_str(), Error::text(aError));
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
  return jsonConnection->jsonRpcComm->sendError(requestId().c_str(), (uint32_t)aError->getErrorCode(), *aError->getErrorMessage() ? aError->getErrorMessage() : NULL, errorData ? errorData->jsonObject() : JsonObjectPtr());
}



// MARK: - VdcJsonApiConnection


VdcJsonApiConnection::VdcJsonApiConnection()
{
  jsonRpcComm = JsonRpcCommPtr(new JsonRpcComm(MainLoop::currentMainLoop()));
  // install JSON request handler locally
  jsonRpcComm->setRequestHandler(boost::bind(&VdcJsonApiConnection::jsonRequestHandler, this, _1, _2, _3));
}



void VdcJsonApiConnection::jsonRequestHandler(const char *aMethod, const char *aJsonRpcId, JsonObjectPtr aParams)
{
  ErrorPtr respErr;
  if (apiRequestHandler) {
    // create params API value
    ApiValuePtr params = JsonApiValue::newValueFromJson(aParams);
    VdcApiRequestPtr request;
    // create request object in case this request expects an answer
    if (aJsonRpcId) {
      // Method
      request = VdcJsonApiRequestPtr(new VdcJsonApiRequest(VdcJsonApiConnectionPtr(this), aJsonRpcId));
      LOG(LOG_INFO, "vdSM -> vDC (JSON), id=%s: called method '%s', params=%s", request->requestId().c_str(), aMethod, params ? params->description().c_str() : "<none>");
    }
    else {
      // Notification
      LOG(LOG_INFO, "vdSM -> vDC (JSON): sent notification '%s', params=%s", aMethod, params ? params->description().c_str() : "<none>");
    }
    // call handler
    apiRequestHandler(VdcJsonApiConnectionPtr(this), request, aMethod, params);
  }
}


void VdcJsonApiConnection::closeAfterSend()
{
  jsonRpcComm->closeAfterSend();
}


ErrorPtr VdcJsonApiConnection::sendRequest(const string &aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler)
{
  JsonApiValuePtr params = boost::dynamic_pointer_cast<JsonApiValue>(aParams);
  ErrorPtr err;
  if (aResponseHandler) {
    // method call expecting response
    LOG(LOG_INFO, "vdSM <- vDC (JSON), id=%d: calling method '%s', params=%s", jsonRpcComm->lastRequestId(), aMethod.c_str(), aParams ? aParams->description().c_str() : "<none>");
    err = jsonRpcComm->sendRequest(aMethod.c_str(), params->jsonObject(), boost::bind(&VdcJsonApiConnection::jsonResponseHandler, this, aResponseHandler, _1, _2, _3));
  }
  else {
    // notification
    LOG(LOG_INFO, "vdSM <- vDC (JSON): sending notification '%s', params=%s", aMethod.c_str(), aParams ? aParams->description().c_str() : "<none>");
    err = jsonRpcComm->sendRequest(aMethod.c_str(), params->jsonObject(), NULL);
  }
  return err;
}


void VdcJsonApiConnection::jsonResponseHandler(VdcApiResponseCB aResponseHandler, int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData)
{
  if (aResponseHandler) {
    // create request object just to hold the response ID
    string respId = string_format("%d", aResponseId);
    ApiValuePtr resultOrErrorData = JsonApiValue::newValueFromJson(aResultOrErrorData);
    VdcApiRequestPtr request = VdcJsonApiRequestPtr(new VdcJsonApiRequest(VdcJsonApiConnectionPtr(this), respId.c_str()));
    if (Error::isOK(aError)) {
      LOG(LOG_INFO, "vdSM -> vDC (JSON), id='%s', result=%s", request->requestId().c_str(), resultOrErrorData ? resultOrErrorData->description().c_str() : "<none>");
    }
    else {
      LOG(LOG_INFO, "vdSM -> vDC (JSON), id='%s', error=%s, errordata=%s", request->requestId().c_str(), aError->text(), resultOrErrorData ? resultOrErrorData->description().c_str() : "<none>");
    }
    aResponseHandler(VdcApiConnectionPtr(this), request, aError, resultOrErrorData);
  }
}
