//
//  Copyright (c) 2013 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of vdcd.
//
//  vdcd is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  vdcd is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with vdcd. If not, see <http://www.gnu.org/licenses/>.
//

#ifndef __vdcd__pbufvdcapi__
#define __vdcd__pbufvdcapi__

#include "p44_common.hpp"

#include "vdcapi.hpp"

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
      string *stringP;
      ApiValueFieldMap *objectMapP;
      ApiValueArray *arrayVectorP;
    } objectValue;

    ApiValueFieldMap::iterator keyIterator;

  public:

    PbufApiValue();
    virtual ~PbufApiValue();

    virtual ApiValuePtr newValue(ApiValueType aObjectType);

    virtual void clear();

    virtual void add(const string &aKey, ApiValuePtr aObj);
    virtual ApiValuePtr get(const string &aKey);
    virtual void del(const string &aKey);
    virtual int arrayLength();
    virtual void arrayAppend(const ApiValuePtr aObj);
    virtual ApiValuePtr arrayGet(int aAtIndex);
    virtual void arrayPut(int aAtIndex, ApiValuePtr aObj);
    virtual bool resetKeyIteration();
    virtual bool nextKeyValue(string &aKey, ApiValuePtr &aValue);

    virtual uint64_t uint64Value();
    virtual int64_t int64Value();
    virtual double doubleValue();
    virtual bool boolValue();
    virtual string stringValue();

    virtual void setUint64Value(uint64_t aUint64);
    virtual void setInt64Value(int64_t aInt64);
    virtual void setDoubleValue(double aDouble);
    virtual void setBoolValue(bool aBool);
    virtual bool setStringValue(const string &aString);
    virtual void setNull();

    /// @name protobuf-c interfacing
    /// @{

    /// extract all fields of a message into this ApiValue as apivalue_object
    /// @param aMessage the protobuf-c message to extract the fields from
    void getObjectFromMessageFields(const ProtobufCMessage &aMessage);

    /// add specified field of the protobuf message as a field into this ApiValue (which will be made type object if not already so)
    /// @param aMessage the protobuf-c message to extract the field from
    /// @param aMessageFieldName the name of the protobuf-c message field
    /// @param aObjectFieldName The name of the field in the ApiValue object. if NULL, aMessageFieldName will be used.
    void addObjectFieldFromMessage(const ProtobufCMessage &aMessage, const char* aMessageFieldName, const char* aObjectFieldName = NULL);

    /// put all values in this ApiValue into name-matching fields of the passed protobuf message
    /// @param aMessage the protobuf-c message to put the fields into
    void putObjectIntoMessageFields(ProtobufCMessage &aMessage);

    /// put specified field of this ApiValue (must be of type object) into the protobuf message as a field
    /// @param aMessage the protobuf-c message to put the the field into
    /// @param aMessageFieldName the name of the protobuf-c message field
    /// @param aObjectFieldName The name of the field in the ApiValue object. if NULL, aMessageFieldName will be used.
    void putObjectFieldIntoMessage(ProtobufCMessage &aMessage, const char* aMessageFieldName, const char* aObjectFieldName = NULL);


    /// extract a single field from a protobuf message into this value
    /// @param aFieldDescriptor the protobuf-c field descriptor for this field
    /// @param aMessage the protobuf-c message to extract the field value from
    void getValueFromMessageField(const ProtobufCFieldDescriptor &aFieldDescriptor, const ProtobufCMessage &aMessage);

    /// extract a single field from a protobuf message into this value
    /// @param aFieldDescriptor the protobuf-c field descriptor for this field
    /// @param aMessage the protobuf-c message to put the field value into
    void putValueIntoMessageField(const ProtobufCFieldDescriptor &aFieldDescriptor, const ProtobufCMessage &aMessage, const char *aBaseName);

    /// @}

  private:

    void allocate();
    bool allocateIf(ApiValueType aIsType);

    void setValueFromField(const ProtobufCFieldDescriptor &aFieldDescriptor, const void *aData, size_t aIndex, ssize_t aArraySize);
    void putValueIntoField(const ProtobufCFieldDescriptor &aFieldDescriptor, void *aData, size_t aIndex, ssize_t aArraySize, const char *aBaseName);

    void getValueFromPropVal(Vdcapi__PropertyValue &aPropVal);
    void putValueIntoPropVal(Vdcapi__PropertyValue &aPropVal);

    void getValueFromProp(Vdcapi__Property &aProp, const char *&aBaseName);
    Vdcapi__PropertyElement *propElementFromValue(const char *aName);
    void putValueIntoProp(Vdcapi__Property &aProp, const char *aBaseName);


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

  };



  class VdcPbufApiRequest : public VdcApiRequest
  {
    typedef VdcApiRequest inherited;

    friend class VdcPbufApiConnection;

    uint32_t reqId;
    VdcPbufApiConnectionPtr pbufConnection;
    Vdcapi__Type responseType; ///< which response message to send back
    string requestedPropertyName; ///< which name the property we requested had, because this needs to be in the reply (ugh)

  public:

    /// constructor
    VdcPbufApiRequest(VdcPbufApiConnectionPtr aConnection, uint32_t aRequestId);

    /// return the request ID as a string
    /// @return request ID as string
    virtual string requestId() { return string_format("%d", reqId); }

    /// get the API connection this request originates from
    /// @return API connection
    virtual VdcApiConnectionPtr connection();

    /// send a vDC API result (answer for successful method call)
    /// @param aResult the result as a ApiValue. Can be NULL for procedure calls without return value
    /// @result empty or Error object in case of error sending result response
    virtual ErrorPtr sendResult(ApiValuePtr aResult);

    /// send a vDC API error (answer for unsuccesful method call)
    /// @param aErrorCode the error code
    /// @param aErrorMessage the error message or NULL to generate a standard text
    /// @param aErrorData the optional "data" member for the vDC API error object
    /// @result empty or Error object in case of error sending error response
    virtual ErrorPtr sendError(uint32_t aErrorCode, string aErrorMessage = "", ApiValuePtr aErrorData = ApiValuePtr());

  };



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
    virtual SocketCommPtr socketConnection() { return socketComm; };

    /// request closing connection after last message has been sent
    virtual void closeAfterSend();

    /// get a new API value suitable for this connection
    /// @return new API value of suitable internal implementation to be used on this API connection
    virtual ApiValuePtr newApiValue();

    /// send a API request
    /// @param aMethod the vDC API method or notification name to be sent
    /// @param aParams the parameters for the method or notification request. Can be NULL.
    /// @param aResponseHandler if the request is a method call, this handler will be called when the method result arrives
    ///   Note that the aResponseHandler might not be called at all in case of lost messages etc. So do not rely on
    ///   this callback for chaining a execution thread.
    /// @return empty or Error object in case of error
    virtual ErrorPtr sendRequest(const string &aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler = VdcApiResponseCB());

  private:

    void gotData(ErrorPtr aError);
    void canSendData(ErrorPtr aError);

    ErrorPtr processMessage(const uint8_t *aPackedMessageP, size_t aPackedMessageSize);
    ErrorPtr sendMessage(const Vdcapi__Message *aVdcApiMessage);

    static ErrorCode pbufToInternalError(Vdcapi__ResultCode aVdcApiResultCode);
    static Vdcapi__ResultCode internalToPbufError(ErrorCode aErrorCode);


  };

}

// C helper functions to print protobuf
void protobufFieldPrint(const ProtobufCFieldDescriptor *aFieldDescriptorP, const void *aData, size_t aIndex);
void protobufMessagePrint(const ProtobufCMessage *aMessageP);


#endif /* defined(__vdcd__pbufvdcapi__) */
