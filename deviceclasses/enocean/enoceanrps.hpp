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

#ifndef __p44vdc__enoceanrps__
#define __p44vdc__enoceanrps__

#include "p44vdc_common.hpp"

#if ENABLE_ENOCEAN

#include "enoceandevice.hpp"


using namespace std;

namespace p44 {


  class EnoceanRPSDevice : public EnoceanDevice
  {
    typedef EnoceanDevice inherited;

  public:

    /// constructor
    EnoceanRPSDevice(EnoceanVdc *aVdcP);

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "enocean_rps"; };

    /// get table of profile variants
    /// @return NULL or pointer to a list of profile variants
    virtual const ProfileVariantEntry *profileVariantsTable() P44_OVERRIDE;


    /// factory: (re-)create logical device from address|channel|profile|manufacturer tuple
    /// @param aVdcP the class container
    /// @param aSubDeviceIndex subdevice number (multiple logical EnoceanDevices might exists for the same EnoceanAddress)
    ///   upon exit, this will be incremented by the number of subdevice indices the device occupies in the index space
    ///   (usually 1, but some profiles might reserve extra space, such as up/down buttons)
    /// @param aEEProfile VARIANT/RORG/FUNC/TYPE EEP profile number
    /// @param aEEManufacturer manufacturer number (or manufacturer_unknown)
    /// @param aSendTeachInResponse enable sending teach-in response for this device
    /// @return returns NULL if no device can be created for the given aSubDeviceIndex, new device otherwise
    static EnoceanDevicePtr newDevice(
      EnoceanVdc *aVdcP,
      EnoceanAddress aAddress,
      EnoceanSubDevice &aSubDeviceIndex,
      EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
      bool aNeedsTeachInResponse
    );

  };



  /// single EnOcean button channel
  class EnoceanRpsButtonHandler : public EnoceanChannelHandler
  {
    typedef EnoceanChannelHandler inherited;
    friend class EnoceanRPSDevice;

    /// private constructor, create new channels using factory static method
    EnoceanRpsButtonHandler(EnoceanDevice &aDevice, Tristate aBinContactClosedValue);

    Tristate mBinContactClosedValue; // if YES or NO the button is handled as binary input, with 1 or 0, resp as "closed/pressed" contact value

    /// handle radio packet related to this channel
    /// @param aEsp3PacketPtr the radio packet to analyze and extract channel related information
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr) P44_OVERRIDE;

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc() P44_OVERRIDE;
  };
  typedef boost::intrusive_ptr<EnoceanRpsButtonHandler> EnoceanRpsButtonHandlerPtr;


  /// single EnOcean rocker button channel
  class EnoceanRpsRockerHandler : public EnoceanChannelHandler
  {
    typedef EnoceanChannelHandler inherited;
    friend class EnoceanRPSDevice;

    /// private constructor, create new channels using factory static method
    EnoceanRpsRockerHandler(EnoceanDevice &aDevice);

    bool pressed; ///< true if currently pressed, false if released, index: 0=on/down button, 1=off/up button
    int switchIndex; ///< which switch within the device (A..D)
    bool isRockerUp; ///< set if rocker up side of switch


    /// handle radio packet related to this channel
    /// @param aEsp3PacketPtr the radio packet to analyze and extract channel related information
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr) P44_OVERRIDE;

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc() P44_OVERRIDE;
    
    void setButtonState(bool aPressed);
  };
  typedef boost::intrusive_ptr<EnoceanRpsRockerHandler> EnoceanRpsRockerHandlerPtr;


  /// single EnOcean window handle channel
  class EnoceanRpsWindowHandleHandler : public EnoceanChannelHandler
  {
    typedef EnoceanChannelHandler inherited;
    friend class EnoceanRPSDevice;

    /// private constructor, create new channels using factory static method
    EnoceanRpsWindowHandleHandler(EnoceanDevice &aDevice);

    /// handle radio packet related to this channel
    /// @param aEsp3PacketPtr the radio packet to analyze and extract channel related information
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr) P44_OVERRIDE;

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc() P44_OVERRIDE;
  };
  typedef boost::intrusive_ptr<EnoceanRpsWindowHandleHandler> EnoceanRpsWindowHandleHandlerPtr;



  /// single EnOcean key card switch handler
  class EnoceanRpsCardKeyHandler : public EnoceanChannelHandler
  {
    typedef EnoceanChannelHandler inherited;
    friend class EnoceanRPSDevice;

    bool isServiceCardDetector; ///< set if this represents the service card detector (otherwise, it's the card inserted status)

    /// private constructor, create new channels using factory static method
    EnoceanRpsCardKeyHandler(EnoceanDevice &aDevice);

    /// handle radio packet related to this channel
    /// @param aEsp3PacketPtr the radio packet to analyze and extract channel related information
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr) P44_OVERRIDE;

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc() P44_OVERRIDE;
  };
  typedef boost::intrusive_ptr<EnoceanRpsCardKeyHandler> EnoceanRpsCardKeyHandlerPtr;



  /// single EnOcean wind and smoke detector handler
  class EnoceanRpsWindSmokeDetectorHandler : public EnoceanChannelHandler
  {
    typedef EnoceanChannelHandler inherited;
    friend class EnoceanRPSDevice;

    bool isBatteryStatus; ///< set if this represents the battery status (otherwise, it's the alarm status)

    /// private constructor, create new channels using factory static method
    EnoceanRpsWindSmokeDetectorHandler(EnoceanDevice &aDevice);

    /// handle radio packet related to this channel
    /// @param aEsp3PacketPtr the radio packet to analyze and extract channel related information
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr) P44_OVERRIDE;

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc() P44_OVERRIDE;
  };
  typedef boost::intrusive_ptr<EnoceanRpsWindSmokeDetectorHandler> EnoceanRpsWindSmokeDetectorHandlerPtr;


  /// single EnOcean liquid leakage detector handler
  class EnoceanRpsLeakageDetectorHandler : public EnoceanChannelHandler
  {
    typedef EnoceanChannelHandler inherited;
    friend class EnoceanRPSDevice;

    /// private constructor, create new channels using factory static method
    EnoceanRpsLeakageDetectorHandler(EnoceanDevice &aDevice);

    /// handle radio packet related to this channel
    /// @param aEsp3PacketPtr the radio packet to analyze and extract channel related information
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr) P44_OVERRIDE;

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc() P44_OVERRIDE;
  };
  typedef boost::intrusive_ptr<EnoceanRpsLeakageDetectorHandler> EnoceanRpsLeakageDetectorHandlerPtr;

}

#endif // ENABLE_ENOCEAN
#endif // __p44vdc__enoceanrps__
