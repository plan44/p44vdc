//
//  Copyright (c) 2014-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __vdcd__p44_vdcd_host__
#define __vdcd__p44_vdcd_host__

#include "vdchost.hpp"

#include "jsoncomm.hpp"

#ifndef ENABLE_JSONCFGAPI
  // by default, we have _either_ ubus _or_ json config API, depending on ENABLE_UBUS
  #if ENABLE_UBUS
    #define ENABLE_JSONCFGAPI 0
  #else
    #define ENABLE_JSONCFGAPI 1
  #endif
#endif

#if ENABLE_UBUS
  #include "ubus.hpp"
#endif


using namespace std;

namespace p44 {

  class P44VdcError : public Error
  {
  public:
    typedef ErrorCode ErrorCodes;

    static const char *domain() { return "p44vdc"; }
    virtual const char *getErrorDomain() const { return P44VdcError::domain(); };
    P44VdcError(ErrorCode aError) : Error(aError) {};
  };


  #if ENABLE_JSONCFGAPI

  /// Dummy config API "connection" object
  class P44JsonApiConnection : public VdcApiConnection
  {
    typedef VdcApiConnection inherited;

  public:

    P44JsonApiConnection();

    /// install callback for received API requests
    /// @param aApiRequestHandler will be called when a API request has been received
    void setRequestHandler(VdcApiRequestCB aApiRequestHandler);

    /// The underlying socket connection
    /// @return socket connection
    virtual SocketCommPtr socketConnection() P44_OVERRIDE { return SocketCommPtr(); };

    /// Cannot send a API request
    /// @return empty or Error object in case of error
    virtual ErrorPtr sendRequest(const string &aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler = VdcApiResponseCB()) P44_OVERRIDE
      { return TextError::err("cant send request to config API"); };

    /// request closing connection after last message has been sent
    virtual void closeAfterSend() P44_OVERRIDE {};

    /// get a new API value suitable for this connection
    /// @return new API value of suitable internal implementation to be used on this API connection
    virtual ApiValuePtr newApiValue() P44_OVERRIDE;
  };


  /// plan44 specific config API JSON request
  class P44JsonApiRequest : public VdcApiRequest
  {
    typedef VdcApiRequest inherited;
    JsonCommPtr jsonComm;

  public:

    /// constructor
    P44JsonApiRequest(JsonCommPtr aJsonComm);

    /// return the request ID as a string
    /// @return request ID as string
    virtual string requestId()  P44_OVERRIDE { return ""; }

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
  typedef boost::intrusive_ptr<P44JsonApiRequest> P44JsonApiRequestPtr;

  #endif // ENABLE_JSONCFGAPI


  #if P44SCRIPT_FULL_SUPPORT || EXPRESSION_SCRIPT_SUPPORT

  /// Dummy api call from script "connection" object
  class ScriptCallConnection : public VdcApiConnection
  {
    typedef VdcApiConnection inherited;

  public:

    ScriptCallConnection();

    /// install callback for received API requests
    /// @param aApiRequestHandler will be called when a API request has been received
    void setRequestHandler(VdcApiRequestCB aApiRequestHandler);

    /// The underlying socket connection
    /// @return socket connection
    virtual SocketCommPtr socketConnection() P44_OVERRIDE { return SocketCommPtr(); };

    /// Cannot send a API request
    /// @return empty or Error object in case of error
    virtual ErrorPtr sendRequest(const string &aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler = VdcApiResponseCB()) P44_OVERRIDE
      { return TextError::err("can't send request to script API"); };

    /// request closing connection after last message has been sent
    virtual void closeAfterSend() P44_OVERRIDE {};

    /// get a new API value suitable for this connection
    /// @return new API value of suitable internal implementation to be used on this API connection
    virtual ApiValuePtr newApiValue() P44_OVERRIDE;
  };


  /// API JSON request from script
  class ScriptApiRequest : public VdcApiRequest
  {
    typedef VdcApiRequest inherited;

    #if ENABLE_P44SCRIPT
    BuiltinFunctionContextPtr mBuiltinFunctionContext;
    #else
    ScriptExecutionContextPtr scriptContext;
    #endif

  public:

    /// constructor
    #if ENABLE_P44SCRIPT
    ScriptApiRequest(BuiltinFunctionContextPtr aBuiltinFunctionContext) : mBuiltinFunctionContext(aBuiltinFunctionContext) {};
    #else
    ScriptApiRequest(ScriptExecutionContextPtr aScriptContext) : scriptContext(aScriptContext) {};
    #endif

    /// return the request ID as a string
    /// @return request ID as string
    virtual string requestId()  P44_OVERRIDE { return ""; }

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
  typedef boost::intrusive_ptr<ScriptApiRequest> ScriptApiRequestPtr;

  #endif // EXPRESSION_SCRIPT_SUPPORT



  #if ENABLE_UBUS

  /// Dummy ubus API "connection" object
  class UbusApiConnection : public VdcApiConnection
  {
    typedef VdcApiConnection inherited;

  public:

    UbusApiConnection();

    /// install callback for received API requests
    /// @param aApiRequestHandler will be called when a API request has been received
    void setRequestHandler(VdcApiRequestCB aApiRequestHandler);

    /// The underlying socket connection (dummy for ubus)
    /// @return socket connection
    virtual SocketCommPtr socketConnection() P44_OVERRIDE { return SocketCommPtr(); };

    /// Cannot send a API request
    /// @return empty or Error object in case of error
    virtual ErrorPtr sendRequest(const string &aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler = VdcApiResponseCB()) P44_OVERRIDE
      { return TextError::err("cant send request to ubus API"); };

    /// request closing connection after last message has been sent
    virtual void closeAfterSend() P44_OVERRIDE {};

    /// get a new API value suitable for this connection
    /// @return new API value of suitable internal implementation to be used on this API connection
    virtual ApiValuePtr newApiValue() P44_OVERRIDE;

  };


  /// ubus api request
  class UbusApiRequest : public VdcApiRequest
  {
    typedef VdcApiRequest inherited;
    UbusRequestPtr ubusRequest;

  public:

    /// constructor
    UbusApiRequest(UbusRequestPtr aUbusRequest);

    /// return the request ID as a string
    /// @return request ID as string
    virtual string requestId() P44_OVERRIDE { return ""; }

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

    /// helper to send response or error
    /// @param aResult the result object to send
    /// @param aError the error object to send (if set, it overrides aResult)
    void sendResponse(JsonObjectPtr aResult, ErrorPtr aError);

  };
  typedef boost::intrusive_ptr<UbusApiRequest> UbusApiRequestPtr;

  #endif // ENABLE_UBUS



  /// plan44 specific implementation of a vdc host, with a separate API used by WebUI components.
  class P44VdcHost : public VdcHost
  {
    typedef VdcHost inherited;

    MLTicket learnIdentifyTicket;
    VdcApiRequestPtr learnIdentifyRequest;

    #if ENABLE_JSONCFGAPI
    SocketCommPtr configApiServer; ///< JSON API for legacy web interface
    #endif

    #if ENABLE_UBUS
    UbusServerPtr ubusApiServer; ///< ubus API for openwrt web interface
    #endif

    #if P44SCRIPT_FULL_SUPPORT
    ScriptMainContextPtr vdcHostScriptContext; ///< context for global vdc scripts
    #endif
    #if EXPRESSION_SCRIPT_SUPPORT
    ScriptQueue globalScripts;
    #endif

  public:

    int webUiPort; ///< port number of the web-UI (on the same host). 0 if no Web-UI present
    string webUiPath; ///< path to be used in the webuiURLString

    P44VdcHost(bool aWithLocalController = false, bool aWithPersistentChannels = false);

    #if ENABLE_JSONCFGAPI
    /// enable config API
    /// @param aServiceOrPort port number or service string
    /// @param aNonLocalAllowed if set, non-local clients are allowed to connect to the config API
    /// @note API server will be started only at initialize()
    void enableConfigApi(const char *aServiceOrPort, bool aNonLocalAllowed);
    #endif

    #if ENABLE_UBUS
    /// enable ubus API
    /// @note ubus server will be started only at initialize()
    void enableUbusApi();
    #endif

		/// perform self testing
    /// @param aCompletedCB will be called when the entire self test is done
    /// @param aButton button for interacting with tests
    /// @param aRedLED red LED output
    /// @param aGreenLED green LED output
    void selfTest(StatusCB aCompletedCB, ButtonInputPtr aButton, IndicatorOutputPtr aRedLED, IndicatorOutputPtr aGreenLED);

    /// @return URL for Web-UI (for access from local LAN)
    virtual string webuiURLString() P44_OVERRIDE;

    /// initialize
    /// @param aCompletedCB will be called when the entire container is initialized or has been aborted with a fatal error
    /// @param aFactoryReset if set, database will be reset
    virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    /// handle global events
    /// @param aEvent the event to handle
    virtual void handleGlobalEvent(VdchostEvent aEvent) P44_OVERRIDE;

    #if ENABLE_JSONCFGAPI
    static void sendCfgApiResponse(JsonCommPtr aJsonComm, JsonObjectPtr aResult, ErrorPtr aError);
    #endif

  protected:

    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest,  const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

  private:

    #if ENABLE_JSONCFGAPI
    SocketCommPtr configApiConnectionHandler(SocketCommPtr aServerSocketComm);
    void configApiRequestHandler(JsonCommPtr aJsonComm, ErrorPtr aError, JsonObjectPtr aJsonObject);
    ErrorPtr processVdcRequest(JsonCommPtr aJsonComm, JsonObjectPtr aRequest);
    #if ENABLE_LEGACY_P44CFGAPI
    ErrorPtr processP44Request(JsonCommPtr aJsonComm, JsonObjectPtr aRequest);
    #endif
    #endif

    #if ENABLE_UBUS
    void ubusApiRequestHandler(UbusRequestPtr aUbusRequest, const string aMethod, JsonObjectPtr aJsonRequest);
    #endif

    #if P44SCRIPT_FULL_SUPPORT
    void runInitScripts();
    void scriptExecHandler(VdcApiRequestPtr aRequest, ScriptObjPtr aResult);
    #endif
    #if EXPRESSION_SCRIPT_SUPPORT
    void runInitScripts();
    #endif

    void learnHandler(VdcApiRequestPtr aRequest, bool aLearnIn, ErrorPtr aError);
    void identifyHandler(VdcApiRequestPtr aRequest, DevicePtr aDevice);

  };
  typedef boost::intrusive_ptr<P44VdcHost> P44VdcHostPtr;


  #if P44SCRIPT_FULL_SUPPORT
  namespace P44Script {

    class P44VdcHostLookup : public BuiltInMemberLookup {
      typedef BuiltInMemberLookup inherited;
      P44VdcHostLookup();
    public:
      static MemberLookupPtr sharedLookup();
    };

  }
  #endif


}

#endif /* defined(__vdcd__p44_vdcd_host__) */
