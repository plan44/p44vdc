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

#ifndef __p44vdc__pbufvdcapi__
#define __p44vdc__pbufvdcapi__

#include "p44utils_common.hpp"

#include "vdcapi.hpp"
#include "jsonobject.hpp" // required for requestId()

#include "vdcapi.pb-c.h"
#include "messages.pb-c.h"

using namespace std;

namespace p44 {

  class VdcPbufApiConnection;
  class VdcPbufApiServer;
  class VdcPbufApiRequest;

  typedef boost::intrusive_ptr<VdcPbufApiConnection> VdcPbufApiConnectionPtr;
  typedef boost::intrusive_ptr<VdcPbufApiServer> VdcPbufApiServerPtr;
  typedef boost::intrusive_ptr<VdcPbufApiRequest> VdcPbufApiRequestPtr;



  class PbufApiValue;

  typedef boost::intrusive_ptr<PbufApiValue> PbufApiValuePtr;

  typedef map<string, PbufApiValuePtr> ApiValueFieldMap;
  typedef vector<PbufApiValuePtr> ApiValueArray;

  /// Protocol buffer specific implementation of ApiValue
  class PbufApiValue : public ApiValue
  {
    typedef ApiValue inherited;
    friend class VdcPbufApiConnection;

    // the actual storage
    ApiValueType allocatedType;
    union {
      bool boolVal;
      uint64_t uint64Val;
      int64_t int64Val;
      double doubleVal;
      string *stringP; // for strings and binary values
      ApiValueFieldMap *objectMapP;
      ApiValueArray *arrayVectorP;
    } objectValue;

    ApiValueFieldMap::iterator keyIterator;

  public:

    PbufApiValue();
    virtual ~PbufApiValue();

    virtual ApiValuePtr newValue(ApiValueType aObjectType) P44_OVERRIDE;

    virtual void clear() P44_OVERRIDE;
    virtual void operator=(ApiValue &aApiValue) P44_OVERRIDE;

    virtual void add(const string &aKey, ApiValuePtr aObj) P44_OVERRIDE;
    virtual ApiValuePtr get(const string &aKey) P44_OVERRIDE;
    virtual void del(const string &aKey) P44_OVERRIDE;
    virtual int arrayLength() P44_OVERRIDE;
    virtual void arrayAppend(const ApiValuePtr aObj) P44_OVERRIDE;
    virtual ApiValuePtr arrayGet(int aAtIndex) P44_OVERRIDE;
    virtual void arrayPut(int aAtIndex, ApiValuePtr aObj) P44_OVERRIDE;
    virtual bool resetKeyIteration() P44_OVERRIDE;
    virtual bool nextKeyValue(string &aKey, ApiValuePtr &aValue) P44_OVERRIDE;

    virtual uint64_t uint64Value() P44_OVERRIDE;
    virtual int64_t int64Value() P44_OVERRIDE;
    virtual double doubleValue() P44_OVERRIDE;
    virtual bool boolValue() P44_OVERRIDE;
    virtual string binaryValue() P44_OVERRIDE;
    virtual string stringValue() P44_OVERRIDE;

    virtual void setUint64Value(uint64_t aUint64) P44_OVERRIDE;
    virtual void setInt64Value(int64_t aInt64) P44_OVERRIDE;
    virtual void setDoubleValue(double aDouble) P44_OVERRIDE;
    virtual void setBoolValue(bool aBool) P44_OVERRIDE;
    virtual void setBinaryValue(const string &aString) P44_OVERRIDE;
    virtual bool setStringValue(const string &aString) P44_OVERRIDE;

    /// @name protobuf-c interfacing
    /// @{

    /// extract all fields of a message into this ApiValue as apivalue_object
    /// @param aMessage the protobuf-c message to extract the fields from
    void getObjectFromMessageFields(const ProtobufCMessage &aMessage);

    /// add specified field of the protobuf message as a field into this ApiValue (which will be made type object if not already so)
    /// @param aMessage the protobuf-c message to extract the field from
    /// @param aFieldName the name of the protobuf-c message field
    void addObjectFieldFromMessage(const ProtobufCMessage &aMessage, const char* aFieldName);

    /// put all values in this ApiValue into name-matching fields of the passed protobuf message
    /// @param aMessage the protobuf-c message to put the fields into
    void putObjectIntoMessageFields(ProtobufCMessage &aMessage);

    /// put specified field of this ApiValue (must be of type object) into the protobuf message as a field
    /// @param aMessage the protobuf-c message to put the the field into
    /// @param aFieldName the name of the protobuf-c message field
    void putObjectFieldIntoMessage(ProtobufCMessage &aMessage, const char* aFieldName);


    /// extract a single field from a protobuf message into this value
    /// @param aFieldDescriptor the protobuf-c field descriptor for this field
    /// @param aMessage the protobuf-c message to extract the field value from
    void getValueFromMessageField(const ProtobufCFieldDescriptor &aFieldDescriptor, const ProtobufCMessage &aMessage);

    /// extract a single field from a protobuf message into this value
    /// @param aFieldDescriptor the protobuf-c field descriptor for this field
    /// @param aMessage the protobuf-c message to put the field value into
    void putValueIntoMessageField(const ProtobufCFieldDescriptor &aFieldDescriptor, const ProtobufCMessage &aMessage);

    /// @}

  private:

    void allocate();
    bool allocateIf(ApiValueType aIsType);

    void setValueFromField(const ProtobufCFieldDescriptor &aFieldDescriptor, const void *aData, size_t aIndex, ssize_t aArraySize);
    void putValueIntoField(const ProtobufCFieldDescriptor &aFieldDescriptor, void *aData, size_t aIndex, ssize_t aArraySize);

    void addKeyValFromPropertyElementField(const Vdcapi__PropertyElement *aPropertyElementP);
    void storeKeyValIntoPropertyElementField(string aKey, Vdcapi__PropertyElement *&aPropertyElementP);


    void getValueFromPropVal(Vdcapi__PropertyValue &aPropVal);
    void putValueIntoPropVal(Vdcapi__PropertyValue &aPropVal);

    size_t numObjectFields();

  };


  /// a JSON API server
  class VdcPbufApiServer : public VdcApiServer
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


  /// Protocol buffer specific implementation of VdcApiRequest
  class VdcPbufApiRequest : public VdcApiRequest
  {
    typedef VdcApiRequest inherited;

    friend class VdcPbufApiConnection;

    uint32_t reqId;
    VdcPbufApiConnectionPtr pbufConnection;
    Vdcapi__Type responseType; ///< which response message to send back

  public:

    /// constructor
    VdcPbufApiRequest(VdcPbufApiConnectionPtr aConnection, uint32_t aRequestId);

    /// return the request ID as a string
    /// @return request ID as string
    virtual JsonObjectPtr requestId() P44_OVERRIDE { return JsonObject::newInt32(reqId); }

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



  /// Protocol buffer specific implementation of VdcApiConnection
  class VdcPbufApiConnection : public VdcApiConnection
  {
    typedef VdcApiConnection inherited;

    friend class VdcPbufApiRequest;

    SocketCommPtr socketComm;

    // receiving
    uint32_t expectedMsgBytes; ///< number of bytes expected of next message
    string receivedMessage; ///< accumulated message bytes, including 4-byte length header

    // sending
    string transmitBuffer; ///< binary buffer for data to be sent
    bool closeWhenSent;

    // pending requests
    int32_t requestIdCounter;
    typedef map<int32_t, VdcApiResponseCB> PendingAnswerMap;
    PendingAnswerMap pendingAnswers;

  public:

    VdcPbufApiConnection();

    /// The underlying socket connection
    /// @return socket connection
    virtual SocketCommPtr socketConnection() P44_OVERRIDE { return socketComm; };

    /// request closing connection after last message has been sent
    virtual void closeAfterSend() P44_OVERRIDE;

    /// the name of the API or the API's peer for logging
    virtual const char* apiName() P44_OVERRIDE { return "vdSM (pbuf)"; };

    /// send a API request
    /// @param aMethod the vDC API method or notification name to be sent
    /// @param aParams the parameters for the method or notification request. Can be NULL.
    /// @param aResponseHandler if the request is a method call, this handler will be called when the method result arrives
    ///   Note that the aResponseHandler might not be called at all in case of lost messages etc. So do not rely on
    ///   this callback for chaining a execution thread.
    /// @return empty or Error object in case of error
    virtual ErrorPtr sendRequest(const string &aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler = VdcApiResponseCB()) P44_OVERRIDE;

  private:

    void gotData(ErrorPtr aError);
    void canSendData(ErrorPtr aError);

    ErrorPtr processMessage(const uint8_t *aPackedMessageP, size_t aPackedMessageSize);
    ErrorPtr sendMessage(const Vdcapi__Message *aVdcApiMessage);

    static ErrorCode pbufToInternalError(Vdcapi__ResultCode aVdcApiResultCode);
    static Vdcapi__ResultCode internalToPbufError(ErrorCode aErrorCode);


  };

}


#endif /* defined(__p44vdc__pbufvdcapi__) */
