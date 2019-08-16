//
//  Copyright (c) 1-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__ledchaindevice__
#define __p44vdc__ledchaindevice__

#include "device.hpp"

#if ENABLE_LEDCHAIN

#include "ledchainvdc.hpp"

using namespace std;

namespace p44 {

  class LedChainVdc;

  class LedChainDevice : public Device
  {
    typedef Device inherited;
    friend class LedChainVdc;

    P44ViewPtr lightView; ///< the view representing the light

    typedef enum {
      lighttype_unknown,
      lighttype_simplearea
    } LightType;

    LightType lightType;

    long long ledChainDeviceRowID; ///< the ROWID this device was created from (0=none)

    MLTicket transitionTicket;

  public:

    LedChainDevice(LedChainVdc *aVdcP, int aX, int aDx, int aY, int aDy, const string &aDeviceConfig);

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// device type identifier
    /// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "ledchain"; };

    /// @name interaction with subclasses, actually representing physical I/O
    /// @{

    /// apply all pending channel value updates to the device's hardware
    /// @note this is the only routine that should trigger actual changes in output values. It must consult all of the device's
    ///   ChannelBehaviours and check isChannelUpdatePending(), and send new values to the device hardware. After successfully
    ///   updating the device hardware, channelValueApplied() must be called on the channels that had isChannelUpdatePending().
    /// @param aCompletedCB if not NULL, must be called when values are applied
    /// @param aForDimming hint for implementations to optimize dimming, indicating that change is only an increment/decrement
    ///   in a single channel (and not switching between color modes etc.)
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming) P44_OVERRIDE;

    /// Get color and opacity of light for a specific LED position
    /// @param aLedNumber LED position
    /// @param aRed will receive red intensity
    /// @param aGreen will receive green intensity
    /// @param aBlue will receive blue intensity
    /// @param aWhite will receive white intensity for LED chains that have a separate white component
    /// @return opacity of light (0=no light, 1=only this light source, between: weight in mix with other sources)
    double getLEDColor(int aLedNumber, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite);

    /// @}

    LedChainVdc &getLedChainVdc();

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// Get extra info (plan44 specific) to describe the addressable in more detail
    /// @return string, single line extra info describing aspects of the device not visible elsewhere
    virtual string getExtraInfo() P44_OVERRIDE;

    /// check if device can be disconnected by software (i.e. Web-UI)
    /// @return true if device might be disconnectable by the user via software (i.e. web UI)
    /// @note devices returning true here might still refuse disconnection on a case by case basis when
    ///   operational state does not allow disconnection.
    /// @note devices returning false here might still be disconnectable using disconnect() triggered
    ///   by vDC API "remove" method.
    virtual bool isSoftwareDisconnectable() P44_OVERRIDE;

    /// disconnect device. For static device, this means removing the config from the container's DB. Note that command line
    /// static devices cannot be disconnected.
    /// @param aForgetParams if set, not only the connection to the device is removed, but also all parameters related to it
    ///   such that in case the same device is re-connected later, it will not use previous configuration settings, but defaults.
    /// @param aDisconnectResultHandler will be called to report true if device could be disconnected,
    ///   false in case it is certain that the device is still connected to this and only this vDC
    virtual void disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler) P44_OVERRIDE;

    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// @}

  private:

    virtual void applyChannelValueSteps(bool aForDimming, double aStepSize);

  };
  typedef boost::intrusive_ptr<LedChainDevice> LedChainDevicePtr;


} // namespace p44

#endif // ENABLE_LEDCHAIN
#endif // __p44vdc__ledchaindevice__
