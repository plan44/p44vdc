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

#ifndef __p44vdc__jsonvdcapi__
#define __p44vdc__jsonvdcapi__

#include "p44utils_common.hpp"

#include "vdcapi.hpp"

#include "jsonrpccomm.hpp"

using namespace std;

namespace p44 {

  class VdcJsonApiConnection;
  class VdcJsonApiServer;
  class VdcJsonApiRequest;

  typedef boost::intrusive_ptr<VdcJsonApiConnection> VdcJsonApiConnectionPtr;
  typedef boost::intrusive_ptr<VdcJsonApiServer> VdcJsonApiServerPtr;
  typedef boost::intrusive_ptr<VdcJsonApiRequest> VdcJsonApiRequestPtr;



  class JsonApiValue;

  typedef boost::intrusive_ptr<JsonApiValue> JsonApiValuePtr;

  /// JSON specific implementation of ApiValue
  class JsonApiValue : public ApiValue
  {
    typedef ApiValue inherited;

    // using an embedded Json Object
    JsonObjectPtr mJsonObj;

    // set value from a JsonObject
    void setJsonObject(JsonObjectPtr aJsonObject);

  public:

    JsonApiValue();

    virtual ApiValuePtr newValue(ApiValueType aObjectType) P44_OVERRIDE;

    static ApiValuePtr newValueFromJson(JsonObjectPtr aJsonObject);

    /// utility to get a JSON version from a untyped API value (which might or might not be JSON already)
    static JsonObjectPtr getAsJson(ApiValuePtr aApiValue);
    /// utility to set an API value directly from a Json Object
    static void setAsJson(ApiValuePtr aApiValue, JsonObjectPtr aJson);

    virtual void clear() P44_OVERRIDE;
    virtual void operator=(ApiValue &aApiValue) P44_OVERRIDE;

    virtual void add(const string &aKey, ApiValuePtr aObj) P44_OVERRIDE { JsonApiValuePtr o = boost::dynamic_pointer_cast<JsonApiValue>(aObj); if (mJsonObj && o) mJsonObj->add(aKey.c_str(), o->jsonObject()); };
    virtual ApiValuePtr get(const string &aKey) P44_OVERRIDE { JsonObjectPtr o; if (mJsonObj && mJsonObj->get(aKey.c_str(), o, false)) return newValueFromJson(o); else return ApiValuePtr(); };
    virtual void del(const string &aKey) P44_OVERRIDE { if (mJsonObj) mJsonObj->del(aKey.c_str()); };
    virtual int arrayLength() P44_OVERRIDE { return mJsonObj ? mJsonObj->arrayLength() : 0; };
    virtual void arrayAppend(ApiValuePtr aObj) P44_OVERRIDE { JsonApiValuePtr o = boost::dynamic_pointer_cast<JsonApiValue>(aObj); if (mJsonObj && o) mJsonObj->arrayAppend(o->jsonObject()); };
    virtual ApiValuePtr arrayGet(int aAtIndex) P44_OVERRIDE { if (mJsonObj) { JsonObjectPtr o = mJsonObj->arrayGet(aAtIndex); return newValueFromJson(o); } else return ApiValuePtr(); };
    virtual void arrayPut(int aAtIndex, ApiValuePtr aObj) P44_OVERRIDE { JsonApiValuePtr o = boost::dynamic_pointer_cast<JsonApiValue>(aObj); if (mJsonObj && o) mJsonObj->arrayPut(aAtIndex, o->jsonObject()); };
    virtual bool resetKeyIteration() P44_OVERRIDE { if (mJsonObj) return mJsonObj->resetKeyIteration(); else return false; };
    virtual bool nextKeyValue(string &aKey, ApiValuePtr &aValue) P44_OVERRIDE { if (mJsonObj) { JsonObjectPtr o; bool gotone = mJsonObj->nextKeyValue(aKey, o); aValue = newValueFromJson(o); return gotone; } else return false; };

    virtual uint64_t uint64Value() P44_OVERRIDE { return mJsonObj ? (uint64_t)mJsonObj->int64Value() : 0; };
    virtual int64_t int64Value() P44_OVERRIDE { return mJsonObj ? mJsonObj->int64Value() : 0; };
    virtual double doubleValue() P44_OVERRIDE { return mJsonObj ? mJsonObj->doubleValue() : 0; };
    virtual bool boolValue() P44_OVERRIDE { return mJsonObj ? mJsonObj->boolValue() : false; };
    virtual string binaryValue() P44_OVERRIDE;
    virtual string stringValue() P44_OVERRIDE { if (getType()==apivalue_string) { return mJsonObj ? mJsonObj->stringValue() : ""; } else return inherited::stringValue(); };

    virtual void setUint64Value(uint64_t aUint64) P44_OVERRIDE { mJsonObj = JsonObject::newInt64(aUint64); }
    virtual void setInt64Value(int64_t aInt64) P44_OVERRIDE { mJsonObj = JsonObject::newInt64(aInt64); };
    virtual void setDoubleValue(double aDouble) P44_OVERRIDE { mJsonObj = JsonObject::newDouble(aDouble); };
    virtual void setBoolValue(bool aBool) P44_OVERRIDE { mJsonObj = JsonObject::newBool(aBool); };
    virtual void setBinaryValue(const string &aBinary) P44_OVERRIDE;
    virtual bool setStringValue(const string &aString) P44_OVERRIDE;

    JsonObjectPtr jsonObject() { return mJsonObj; };

  protected:
    
    
  };


  /// a JSON API server
  class VdcJsonApiServer : public VdcApiServer
  {
    typedef VdcApiServer inherited;

  protected:

    /// create API connection of correct type for this API server
    /// @return API connection
    virtual VdcApiConnectionPtr newConnection();

    /// get a new API value suitable for this connection
    /// @return new API value of suitable internal implementation to be used on this API connection
    virtual ApiValuePtr newApiValue();

  };



  /// a JSON API request
  class VdcJsonApiRequest : public VdcApiRequest
  {
    typedef VdcApiRequest inherited;

    string mJsonRpcId;
    VdcJsonApiConnectionPtr mJsonConnection;

  public:

    /// constructor
    VdcJsonApiRequest(VdcJsonApiConnectionPtr aConnection, const string aJsonRpcId);

    /// return the request ID as a string
    /// @return request ID as string
    virtual string requestId() P44_OVERRIDE { return mJsonRpcId; }

    /// get the API connection this request originates from
    /// @return API connection
    virtual VdcApiConnectionPtr connection() P44_OVERRIDE;

    /// send a vDC API result (answer for successful method call)
    /// @param aResult the result as a ApiValue. Can be NULL for procedure calls without return value
    /// @result empty or Error object in case of error sending result response
    virtual ErrorPtr sendResult(ApiValuePtr aResult) P44_OVERRIDE;

    /// send a error to the vDC API (answer for unsuccesful method call)
    /// @param aError the error object
    /// @note depending on the Error object's subclass and the vDC API kind (protobuf, json...),
    ///   different information is transmitted. ErrorCode and ErrorMessage are always sent,
    ///   Errors based on class VdcApiError will also include errorType, errorData and userFacingMessage
    /// @note if aError is NULL, a generic "OK" error condition is sent
    /// @result empty or Error object in case of error sending error response
    virtual ErrorPtr sendError(ErrorPtr aError) P44_OVERRIDE;

  };



  /// a JSON API connection
  class VdcJsonApiConnection : public VdcApiConnection
  {
    typedef VdcApiConnection inherited;

    friend class VdcJsonApiRequest;

    JsonRpcCommPtr mJsonRpcComm;

  public:

    VdcJsonApiConnection();

    /// The underlying socket connection
    /// @return socket connection
    virtual SocketCommPtr socketConnection() P44_OVERRIDE { return mJsonRpcComm; };

    /// request closing connection after last message has been sent
    virtual void closeAfterSend() P44_OVERRIDE;

    /// the name of the API or the API's peer for logging
    virtual const char* apiName() P44_OVERRIDE { return "vdSM (JSON)"; };

    /// send a API request
    /// @param aMethod the vDC API method or notification name to be sent
    /// @param aParams the parameters for the method or notification request. Can be NULL.
    /// @param aResponseHandler if the request is a method call, this handler will be called when the method result arrives
    ///   Note that the aResponseHandler might not be called at all in case of lost messages etc. So do not rely on
    ///   this callback for chaining a execution thread.
    /// @return empty or Error object in case of error
    virtual ErrorPtr sendRequest(const string &aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler = VdcApiResponseCB()) P44_OVERRIDE;

  private:

    void jsonRequestHandler(const char *aMethod, const JsonObjectPtr aJsonRpcId, JsonObjectPtr aParams);
    void jsonResponseHandler(VdcApiResponseCB aResponseHandler, int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData);

  };


}




#endif /* defined(__p44vdc__jsonvdcapi__) */
