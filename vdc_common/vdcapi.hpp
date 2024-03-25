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

#ifndef __p44vdc__apiconnection__
#define __p44vdc__apiconnection__

#include "p44utils_common.hpp"

#include "apivalue.hpp"
#include "socketcomm.hpp"


using namespace std;

namespace p44 {

  class VdcApiError;
  typedef boost::intrusive_ptr<VdcApiError> VdcApiErrorPtr;

  typedef uint8_t VdcErrorType;

  class VdcApiError : public Error
  {
    VdcErrorType errorType; ///< the error type (see Vdcapi__ErrorType protobuf enum) as a hint for user faced error processing
    string userFacingMessage; ///< a user facing message, which might also be the key into a translation database

  public:
    typedef ErrorCode ErrorCodes;

    static const char *domain() { return "VdcApi"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return VdcApiError::domain(); };
    VdcApiError(ErrorCode aError) : Error(aError), errorType(0) {};
    VdcApiError(ErrorCode aError, const string &aMsg, VdcErrorType aErrorType, const string &aUsrMsg) :
      Error(aError, aMsg), errorType(aErrorType), userFacingMessage(aUsrMsg) {};

    VdcErrorType getErrorType() const { return errorType; };
    string getUserFacingMessage() const { return userFacingMessage; };

    /// set userFacingMessage
    /// @param aFmt error message format string
    /// @param aArgs argument list for formatting
    void setFormattedUserFacingMessage(const char *aFmt, va_list aArgs);

    /// set user faced error info
    /// @param aErrorType the type of user facing error, see Vdcapi__ErrorType enum
    /// @param aFmt formatting string for the user facing error
    void setUserFacingInfo(VdcErrorType aErrorType, const char *aFmt, ...) __printflike(3,4);

    /// factory function to create a user facing VdcApiError
    /// @param aErrorCode the technical error code (usually a http-like error code)
    /// @param aErrorType the type of user facing error, see Vdcapi__ErrorType enum
    /// @param aFmt formatting string for the user facing error
    static VdcApiErrorPtr userFacingErr(ErrorCode aErrorCode, VdcErrorType aErrorType, const char *aFmt, ...) __printflike(3,4);

    /// get the description of the error
    /// @return a description string. If an error message was not set, a standard string with the error domain and number will be shown
    virtual string description() const P44_OVERRIDE;

  };


  class VdcApiConnection;
  class VdcApiServer;
  class VdcApiRequest;

  typedef boost::intrusive_ptr<VdcApiConnection> VdcApiConnectionPtr;
  typedef boost::intrusive_ptr<VdcApiServer> VdcApiServerPtr;
  typedef boost::intrusive_ptr<VdcApiRequest> VdcApiRequestPtr;

  /// callback for delivering a API request (needs answer) or notification (does not need answer)
  /// @param aApiConnection the VdcApiConnection calling this handler
  /// @param aRequest the request. The handler must pass this object when calling sendResult(). If this is a notification, aRequest is NULL.
  /// @param aMethod If this is a method call, this is the JSON-RPC (2.0) method or notification requested by the peer.
  /// @param aParams the parameters of the request as ApiValue
  typedef boost::function<void (VdcApiConnectionPtr aApiConnection, VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)> VdcApiRequestCB;

  /// callback for delivering the result for a previously sent request
  /// @param aApiConnection the VdcApiConnection calling this handler
  /// @param aRequest the request that caused this answer
  /// @param aError the referenced ErrorPtr will be set when an error occurred.
  ///   If the error returned is an VdcApiError, aError.getErrorCode() will return the "code" member from the API error object,
  ///   and aError.description() will return the "message" member from the API error object.
  ///   aResultOrErrorData will contain the "data" member from the JSON-RPC error object, if any.
  /// @param aResultOrErrorData the result object in case of success, or the "data" member from the JSON-RPC error object
  ///   in case of an error returned via JSON-RPC from the remote peer.
  typedef boost::function<void (VdcApiConnectionPtr aApiConnection, VdcApiRequestPtr aRequest, ErrorPtr &aError, ApiValuePtr aResultOrErrorData)> VdcApiResponseCB;


  /// callback for announcing new API connection (which may or may not lead to a session) or termination of a connection
  /// @param aApiConnection the VdcApiConnection calling this handler
  /// @param aError set if an error occurred on the connection (including remote having closed the connection)
  typedef boost::function<void (VdcApiConnectionPtr aApiConnection, ErrorPtr &aError)> VdcApiConnectionCB;




  /// a single API connection
  class VdcApiConnection : public P44Obj
  {
    typedef P44Obj inherited;

  protected:

    VdcApiRequestCB apiRequestHandler;
    int apiVersion;

  public:

    VdcApiConnection() : apiVersion(0) {};

    /// install callback for received API requests
    /// @param aApiRequestHandler will be called when a API request has been received
    void setRequestHandler(VdcApiRequestCB aApiRequestHandler);

    /// end connection
    void closeConnection();

    /// The underlying socket connection
    /// @return socket connection
    virtual SocketCommPtr socketConnection() = 0;

    /// send a API request (method call or notification)
    /// @param aMethod the vDC API method or notification name to be sent
    /// @param aParams the parameters for the method or notification request. Can be NULL.
    /// @param aResponseHandler if the request is a method call, this handler will be called when the method result arrives
    ///   Note that the aResponseHandler might not be called at all in case of lost messages etc. So do not rely on
    ///   this callback for chaining a execution thread.
    /// @return empty or Error object in case of error
    /// @note it depends on the API what exactly are the targets. Some APIs might send the request (which then
    ///   should be a notification) to all connected clients.
    virtual ErrorPtr sendRequest(const string &aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler = VdcApiResponseCB())
      { return TextError::err("can't initiate request/notification to %s API", apiName()); }; // by default API cannot initiate requests itself

    /// request closing connection after last message has been sent
    virtual void closeAfterSend() = 0;

    /// get a new API value suitable for this connection
    /// @return new API value of suitable internal implementation to be used on this API connection
    virtual ApiValuePtr newApiValue();

    /// the API domain code for this API
    virtual int domain() { return VDC_API_DOMAIN; }

    /// the name of the API or the API's peer for logging
    virtual const char* apiName() { return "(JSON)"; }; // it is json, but we don't know the peer here

    /// get API version
    /// @return API version for this connection
    /// @note 0 is returned as long as API version is not yet determined
    int getApiVersion() { return apiVersion; }

    /// set API version for this connection
    /// @param aApiVersion numeric API version to set
    /// @note is effective only when current API version is not defined (==0)
    void setApiVersion(int aApiVersion) { if (apiVersion==0) apiVersion = aApiVersion; };

  };



  /// a API server
  class VdcApiServer : public SocketComm
  {
    typedef SocketComm inherited;

    VdcApiConnectionCB apiConnectionStatusHandler; ///< connection status handler

  public:

    VdcApiServer();

    /// set connection status handler
    /// @param aConnectionCB will be called when connections opens, ends or has error
    void setConnectionStatusHandler(VdcApiConnectionCB aConnectionCB);

    /// start API server
    void start();

    /// stop API server, close all connections
    void stop();

    /// clear all callbacks
    /// @note this is important because handlers might cause retain cycles when they have smart ptr arguments
    virtual void clearCallbacks() { apiConnectionStatusHandler = NoOP; inherited::clearCallbacks(); }

    /// get a new API value suitable for connections on this server
    /// @return new API value of suitable internal implementation to be with connections on this server
    virtual ApiValuePtr newApiValue() = 0;

  protected:

    /// create API connection of correct type for this API server
    /// @return API connection
    virtual VdcApiConnectionPtr newConnection() = 0;

  private:

    SocketCommPtr serverConnectionHandler(SocketCommPtr aServerSocketComm);
    void connectionStatusHandler(SocketCommPtr aSocketComm, ErrorPtr aError);

  };



  /// a single request which needs to be answered
  class VdcApiRequest : public P44Obj
  {
    typedef P44Obj inherited;

  public:

    /// return the request ID as a JSON object
    /// @return request ID as JSON object (usually string or integer)
    virtual JsonObjectPtr requestId() = 0;

    /// get the API connection this request originates from
    /// @return API connection
    virtual VdcApiConnectionPtr connection() = 0;

    /// send a vDC API result (answer for successful method call)
    /// @param aResult the result as a ApiValue. Can be NULL for procedure calls without return value
    /// @result empty or object in case of error sending result response
    virtual ErrorPtr sendResult(ApiValuePtr aResult) = 0;

    /// send a error to the vDC API (answer for unsuccesful method call)
    /// @param aError the error object
    /// @note depending on the Error object's subclass and the vDC API kind (protobuf, json...),
    ///   different information is transmitted. ErrorCode and ErrorMessage are always sent,
    ///   Errors based on class VdcApiError will also include errorType, errorData and userFacingMessage
    /// @note if aError is NULL, a generic "OK" error condition is sent
    /// @result empty or Error object in case of error sending error response
    virtual ErrorPtr sendError(ErrorPtr aError) = 0;

    /// send p44utils::Error object as vDC API OK or error status
    /// @param aStatusToSend if Error::isOK(), a OK status (NULL result) will be returned, otherwise error will be returned
    /// @result empty or Error object in case of error sending error response
    ErrorPtr sendStatus(ErrorPtr aStatusToSend);

    /// get a new API value suitable for answering this request connection
    /// @return new API value of suitable internal implementation to be used on this API connection
    /// @note default is asking the connection, but for special cases (e.g. vDC API requests via config API)
    ///   this might be overridden.
    virtual ApiValuePtr newApiValue() { return connection()->newApiValue(); }; // default is asking connection (but p44 web api will override this)

    /// get API version
    /// @return API version for this request
    virtual int getApiVersion() { return connection()->getApiVersion(); }; // default is asking connection (but p44 web api will override this)

    /// get API / peer name
    /// @return API name for logging
    const char* apiName() { return connection()->apiName(); };

  };

}


#endif /* defined(__p44vdc__apiconnection__) */
