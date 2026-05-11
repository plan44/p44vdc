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

#ifndef __p44vdc__wleddevice__
#define __p44vdc__wleddevice__

#include "device.hpp"

#if ENABLE_WLED

#include "colorlightbehaviour.hpp"
#include "jsonobject.hpp"
#include "wledcomm.hpp"

using namespace std;

namespace p44 {

  class WledVdc;
  class WledDevice;

  typedef boost::intrusive_ptr<WledDevice> WledDevicePtr;

  /// WLED color light device.
  ///
  /// A single WledDevice represents either:
  ///   - an entire WLED physical device (mSegmentId == -1), or
  ///   - one segment of a multi-segment WLED device (mSegmentId >= 0).
  ///
  /// When representing a segment, mComm is shared with the other segment devices
  /// belonging to the same physical WLED unit.
  class WledDevice : public Device
  {
    typedef Device inherited;
    friend class WledVdc;

  public:

    /// Constructor for a whole-device (single-segment) WledDevice.
    /// Creates a new WledComm internally.
    /// @param aVdcP  parent VDC
    /// @param aHostname  hostname or IP address of the WLED device
    /// @param aDeviceInfo  /json/info response from the device
    WledDevice(WledVdc *aVdcP, const string &aHostname, JsonObjectPtr aDeviceInfo);

    /// Constructor for a per-segment WledDevice that shares a WledComm.
    /// @param aVdcP  parent VDC
    /// @param aComm  shared communication layer for the physical device
    /// @param aSegmentId  WLED segment index (0..N-1)
    /// @param aDeviceInfo  /json/info response from the physical device
    WledDevice(WledVdc *aVdcP, WledCommPtr aComm, int aSegmentId, JsonObjectPtr aDeviceInfo);

    virtual ~WledDevice();

    /// set the log level offset on this logging object
    virtual void setLogLevelOffset(int aLogLevelOffset) P44_OVERRIDE;

    /// device type identifier
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "wled"; };

    /// description of the device
    virtual string description() P44_OVERRIDE;

    /// update device info from WLED response
    void updateInfo(JsonObjectPtr aDeviceInfo);

    /// identify the device (e.g., blink or light up)
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// initialize device, query current state from hardware
    virtual void initializeDevice(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    /// apply channel values
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming) P44_OVERRIDE;

    /// sync channel values from hardware (called before scene save)
    virtual void syncChannelValues(SimpleCB aDoneCB) P44_OVERRIDE;

    /// let device indicate if it can be part of an optimized (native) notification set
    virtual bool prepareForOptimizedSet(NotificationDeliveryStatePtr aDeliveryState) P44_OVERRIDE;

    /// handle vdc-level method calls
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// presence check: reachable iff /json/info responds without error
    virtual void checkPresence(PresenceCB aPresenceResultHandler) P44_OVERRIDE;

    /// disconnect guard: only allow removal when device is not reachable
    virtual void disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler) P44_OVERRIDE;

    /// propagate name changes to the WLED device firmware
    virtual void setName(const string &aName) P44_OVERRIDE;

  protected:

    WledVdc &mVdc;                    ///< reference to parent VDC
    WledCommPtr mComm;                ///< communication layer (may be shared with sibling segments)
    int mSegmentId;                   ///< segment index, or -1 for whole-device (single segment)

    /// device information
    string mDeviceName;               ///< user friendly device name
    string mUniqueId;                 ///< unique device identifier (MAC address)
    string mSwVersion;                ///< WLED software version
    string mHwVersion;                ///< WLED hardware version

    /// capabilities
    bool mHasRgb;                     ///< device supports RGB color
    bool mHasRgbw;                    ///< device supports RGBW (with white channel)
    bool mHasCct;                     ///< device supports color temperature
    uint32_t mLedCount;               ///< number of LEDs

    /// light behavior
    ColorLightBehaviourPtr mColorLightBehaviour;
    LightBehaviourPtr mDimmerLightBehaviour;

    /// state
    JsonObjectPtr mLastState;         ///< last known device state
    MLTicket mUpdateTicket;           ///< ticket for state update
    bool mSettingState;               ///< flag to prevent feedback loops

    // internal state
    Tristate mCurrentlyOn;            ///< current "on" status
    uint8_t mLastSentBri;             ///< last sent "bri", 0=undefined

    #if ENABLE_JSON_WEBSOCKET
    bool mWebsocketUpdatePending;     ///< flag to prevent duplicate state processing
    #endif

  private:

    /// shared constructor body (called from both public constructors)
    void construct(JsonObjectPtr aDeviceInfo);

    /// initialize behaviors based on device capabilities
    void initializeBehaviors(JsonObjectPtr aDeviceInfo);

    /// update local state from device response
    void updateState(JsonObjectPtr aStateResponse);

    /// query device for current state
    void queryState();

    /// handle state response from device
    void handleStateResponse(JsonObjectPtr aState, ErrorPtr aError);

    /// handler for initializeDevice() state response
    void deviceStateReceived(StatusCB aCompletedCB, bool aFactoryReset, JsonObjectPtr aState, ErrorPtr aError);

    /// handler for syncChannelValues() state response
    void channelValuesReceived(SimpleCB aDoneCB, JsonObjectPtr aState, ErrorPtr aError);

    /// handler for checkPresence() network query
    void presenceStateReceived(PresenceCB aPresenceResultHandler, JsonObjectPtr aDeviceInfo, ErrorPtr aError);

    /// handler for disconnect() presence check
    void disconnectableHandler(bool aForgetParams, DisconnectCB aDisconnectResultHandler, bool aPresent);

    /// convert WLED RGB color to HSV
    static void rgbToHsv(uint8_t aRed, uint8_t aGreen, uint8_t aBlue,
                         double &aHue, double &aSaturation, double &aValue);

    /// convert HSV color to WLED RGB
    static void hsvToRgb(double aHue, double aSaturation, double aValue,
                         uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue);

    /// extract RGB from WLED state
    void extractRgbFromState(JsonObjectPtr aState, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue);

    #if ENABLE_JSON_WEBSOCKET

    /// WebSocket state update callback - receives state changes from WebSocket
    void onWebsocketUpdate(JsonObjectPtr aState, ErrorPtr aError);

    /// WebSocket connection status callback
    void onWebsocketStatus(bool aConnected, ErrorPtr aError);

    /// Enable/disable WebSocket for this device
    void enableWebsocket(bool aEnable);

    /// Get WebSocket enabled state
    bool isWebsocketEnabled() const { return mWebsocketEnabled; }

    /// Get WebSocket connection state
    bool isWebsocketConnected() const;

    /// Request WebSocket connection
    void websocketConnect();

    /// Request WebSocket disconnection
    void websocketDisconnect();

    /// Update polling frequency based on WebSocket status
    void updatePollingFrequency();

    // WebSocket members
  private:

    bool mWebsocketEnabled;           ///< WebSocket feature enabled for this device
    MLMicroSeconds mNormalPollInterval;    ///< Normal polling interval (when WebSocket not active)
    MLMicroSeconds mReducedPollInterval;   ///< Reduced polling interval (when WebSocket active)

    #endif // ENABLE_JSON_WEBSOCKET

  };

} // namespace p44

#endif // ENABLE_WLED

#endif // __p44vdc__wleddevice__
