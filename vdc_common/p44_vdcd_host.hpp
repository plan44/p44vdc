//
//  Copyright (c) 1-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

    /// send a vDC API error (answer for unsuccesful method call)
    /// @param aErrorCode the error code
    /// @param aErrorMessage the error message or NULL to generate a standard text
    /// @param aErrorData the optional "data" member for the vDC API error object (in JSON only)
    /// @param aErrorType the optional "errorType"
    /// @param aUserFacingMessage the optional user facing message
    /// @result empty or Error object in case of error sending error response
    virtual ErrorPtr sendError(uint32_t aErrorCode, string aErrorMessage = "", ApiValuePtr aErrorData = ApiValuePtr(), uint8_t aErrorType = 0, string aUserFacingMessage = "") P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<P44JsonApiRequest> P44JsonApiRequestPtr;



  /// plan44 specific implementation of a vdc host, with a separate API used by WebUI components.
  class P44VdcHost : public VdcHost
  {
    typedef VdcHost inherited;
    friend class P44JsonApiRequest;

    MLTicket learnIdentifyTicket;
    JsonCommPtr learnIdentifyRequest;

    SocketCommPtr configApiServer; ///< JSON API for web interface

  public:

    int webUiPort; ///< port number of the web-UI (on the same host). 0 if no Web-UI present
    string webUiPath; ///< path to be used in the webuiURLString

    P44VdcHost(bool aWithLocalController = false);

    /// enable config API
    /// @param aServiceOrPort port number or service string
    /// @param aNonLocalAllowed if set, non-local clients are allowed to connect to the config API
    /// @note API server will be started only at initialize()
    void enableConfigApi(const char *aServiceOrPort, bool aNonLocalAllowed);

		/// perform self testing
    /// @param aCompletedCB will be called when the entire self test is done
    /// @param aButton button for interacting with tests
    /// @param aRedLED red LED output
    /// @param aGreenLED green LED output
    void selfTest(StatusCB aCompletedCB, ButtonInputPtr aButton, IndicatorOutputPtr aRedLED, IndicatorOutputPtr aGreenLED);

    /// @return URL for Web-UI (for access from local LAN)
    virtual string webuiURLString();

    /// initialize
    /// @param aCompletedCB will be called when the entire container is initialized or has been aborted with a fatal error
    /// @param aFactoryReset if set, database will be reset
    virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset);

  private:

    SocketCommPtr configApiConnectionHandler(SocketCommPtr aServerSocketComm);
    void configApiRequestHandler(JsonCommPtr aJsonComm, ErrorPtr aError, JsonObjectPtr aJsonObject);
    void learnHandler(JsonCommPtr aJsonComm, bool aLearnIn, ErrorPtr aError);
    void identifyHandler(JsonCommPtr aJsonComm, DevicePtr aDevice);
    void endIdentify();

    ErrorPtr processVdcRequest(JsonCommPtr aJsonComm, JsonObjectPtr aRequest);
    ErrorPtr processP44Request(JsonCommPtr aJsonComm, JsonObjectPtr aRequest);

    static void sendCfgApiResponse(JsonCommPtr aJsonComm, JsonObjectPtr aResult, ErrorPtr aError);

  };
  typedef boost::intrusive_ptr<P44VdcHost> P44VdcHostPtr;



}

#endif /* defined(__vdcd__p44_vdcd_host__) */
