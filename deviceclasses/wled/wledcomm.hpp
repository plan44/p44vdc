//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Michael Troß <digitalstrom@tross.org>
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

#ifndef __p44vdc__wledcomm__
#define __p44vdc__wledcomm__

#include "p44vdc_common.hpp"

#if ENABLE_WLED

#include "jsonwebclient.hpp"
#include "operationqueue.hpp"
#include "macaddress.hpp"

#if ENABLE_JSON_WEBSOCKET
#include "jsonwebsocketclient.hpp"
#endif

// Enable DNS-SD discovery for WLED devices
#if !defined(WLED_DNSSD_DISCOVERY) && !DISABLE_DISCOVERY
  #define WLED_DNSSD_DISCOVERY 1
#endif

#if WLED_DNSSD_DISCOVERY
  #include "dnssd.hpp"
#endif

using namespace std;

namespace p44 {


  class WledCommError : public Error
  {
  public:
    enum {
      OK,
      DeviceNotFound,        ///< WLED device not reachable
      InvalidResponse,       ///< Invalid JSON response from device
      ApiError,              ///< WLED API returned an error
      NoCapabilities,        ///< Device doesn't support required capabilities
      InvalidParameter,      ///< Invalid parameter value
      Timeout,               ///< Request timeout
      NetworkError,          ///< Network connectivity issue
      InvalidDeviceState,    ///< Device in invalid state
      NoDeviceInfo,          ///< Could not retrieve device info
    };
    typedef int ErrorCodes;

    static const char *domain() { return "WledComm"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return WledCommError::domain(); };
    explicit WledCommError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE;
    #endif
  };


  class WledComm;
  typedef boost::intrusive_ptr<WledComm> WledCommPtr;

  /// Callback for API result
  /// @param aResult the result object in case of success
  /// @param aError error in case of failure
  typedef boost::function<void (JsonObjectPtr aResult, ErrorPtr aError)> WledApiResultCB;

  /// Callback for device info retrieval
  typedef boost::function<void (string aDeviceName, string aVersion, ErrorPtr aError)> WledInfoResultCB;

  /// Callback for device discovery
  /// @param aDeviceURL the URL of the discovered device
  /// @param aDeviceInfo device information including name, IP, MAC, etc.
  /// @param aError error if any
  /// @return true to continue browsing for more devices, false to stop
  typedef boost::function<bool (string aDeviceURL, JsonObjectPtr aDeviceInfo, ErrorPtr aError)> WledDiscoveryResultCB;

  /// Callback for WebSocket state updates
  /// @param aStateUpdate the state object from WLED device
  /// @param aError error if any
  typedef boost::function<void (JsonObjectPtr aStateUpdate, ErrorPtr aError)> WledWebsocketUpdateCB;

  /// Callback for WebSocket connection status
  /// @param aConnected true if connected, false if disconnected
  /// @param aError error if any
  typedef boost::function<void (bool aConnected, ErrorPtr aError)> WledWebsocketStatusCB;


  /// WLED API operation
  class WledApiOperation : public Operation
  {
    typedef Operation inherited;

  public:

    typedef enum {
      GET,
      POST,
      PUT,
      DELETE
    } HttpMethods;

    WledApiOperation(WledComm &aWledComm, HttpMethods aMethod, const string &aUrl, JsonObjectPtr aData, WledApiResultCB aResultHandler);
    virtual ~WledApiOperation();

    virtual bool initiate();
    virtual bool hasCompleted();
    virtual OperationPtr finalize();
    virtual void abortOperation(ErrorPtr aError);

  private:

    WledComm &mWledComm;
    HttpMethods mMethod;
    string mUrl;
    JsonObjectPtr mData;
    bool mCompleted;
    ErrorPtr mError;
    WledApiResultCB mResultHandler;

    void processAnswer(JsonObjectPtr aJsonResponse, ErrorPtr aError);
  };
  typedef boost::intrusive_ptr<WledApiOperation> WledApiOperationPtr;


  /// WLED Device Info structure
  struct WledDeviceInfo {
    string name;
    string ipAddress;
    string hostname;
    string macAddress;
    string swVersion;
    uint32_t ledCount;
    bool hasRgb;
    bool hasRgbw;
    bool hasCct;
  };


  /// WLED Communication handler
  class WledComm : public OperationQueue
  {
    typedef OperationQueue inherited;
    friend class WledApiOperation;
    friend class WledVdc;

  public:

    WledComm();
    virtual ~WledComm();

    /// set the log level offset on this logging object
    virtual void setLogLevelOffset(int aLogLevelOffset) P44_OVERRIDE;

    /// Get icon data or name
    bool getIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix);

    /// set device URL
    void setDeviceURL(const string &aBaseURL);

    /// get device URL
    string getDeviceURL() const { return mBaseURL; }

    /// get current state
    void getState(WledApiResultCB aResultCallback);

    /// set device state (brightness, color, etc.)
    void setState(JsonObjectPtr aStateUpdate, WledApiResultCB aResultCallback);

    /// get device information
    void getInfo(WledApiResultCB aResultCallback);

    /// get list of effects
    void getEffects(WledApiResultCB aResultCallback);

    /// get list of palettes
    void getPalettes(WledApiResultCB aResultCallback);

    /// get network settings
    void getNetwork(WledApiResultCB aResultCallback);

    /// queue an API call
    void queueApiCall(const string &aPath, const string &aMethod,
                      JsonObjectPtr aData, WledApiResultCB aResultCallback);

    /// @brief discover and identify a WLED device (deprecated, kept for compatibility)
    /// @param aResultCallback 
    void discoverDevices(WledDiscoveryResultCB aResultCallback);

    #if ENABLE_JSON_WEBSOCKET
    /// Enable/disable WebSocket support for this device
    /// @param aEnable true to enable, false to disable
    void enableWebsocket(bool aEnable);

    /// Check if WebSocket is enabled
    /// @return true if WebSocket is enabled
    bool isWebsocketEnabled() const { return mWebsocketEnabled; }

    /// Check if WebSocket is currently connected
    /// @return true if WebSocket connection is active
    bool isWebsocketConnected() const;

    /// Connect to WLED device via WebSocket
    /// @param aStatusCallback optional callback for connection status changes
    void websocketConnect(WledWebsocketStatusCB aStatusCallback = WledWebsocketStatusCB());

    /// Disconnect WebSocket from WLED device
    void websocketDisconnect();

    /// Set callback for WebSocket state updates
    /// @param aUpdateCallback callback invoked when device state changes via WebSocket
    void setWebsocketUpdateCallback(WledWebsocketUpdateCB aUpdateCallback);
    #endif // ENABLE_JSON_WEBSOCKET

  private:

    string mBaseURL;                  ///< http://device-ip/json
    JsonWebClient mJsonClient;        ///< JSON web client for API calls
    JsonObjectPtr mCachedState;       ///< Last known state
    JsonObjectPtr mCachedInfo;        ///< Device info cache

    #if ENABLE_JSON_WEBSOCKET
    JsonWebsocketClientPtr mWebsocketClient;    ///< WebSocket connection
    bool mWebsocketEnabled;                     ///< WebSocket enabled for this device
    WledWebsocketUpdateCB mWebsocketUpdateCB;   ///< Callback for state updates
    WledWebsocketStatusCB mWebsocketStatusCB;   ///< Callback for connection status
    
    // WebSocket message handlers
    void onWebsocketMessage(JsonObjectPtr aMessage, ErrorPtr aError);
    void onWebsocketConnected(bool aConnected, ErrorPtr aError);
    #endif // ENABLE_JSON_WEBSOCKET
  };


} // namespace p44

#endif // ENABLE_WLED

#endif // __p44vdc__wledcomm__
