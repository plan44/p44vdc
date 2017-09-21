//
//  Copyright (c) 2015-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__enoceanvld__
#define __p44vdc__enoceanvld__

#include "p44vdc_common.hpp"

#if ENABLE_ENOCEAN

#include "enoceandevice.hpp"
#include "enoceansensorhandler.hpp"

using namespace std;

namespace p44 {


  class EnoceanVLDDevice : public EnoceanDevice
  {
    typedef EnoceanDevice inherited;

  public:

    /// constructor
    EnoceanVLDDevice(EnoceanVdc *aVdcP);

    /// device type identifier
    /// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "enocean_vld"; };

    /// get table of profile variants
    /// @return NULL or pointer to a list of profile variants
    virtual const ProfileVariantEntry *profileVariantsTable() P44_OVERRIDE;

    /// factory: (re-)create logical device from address|channel|profile|manufacturer tuple
    /// @param aVdcP the class container
    /// @param aSubDeviceIndex subdevice number (multiple logical EnoceanDevices might exists for the same EnoceanAddress)
    ///   upon exit, this will be incremented by the number of subdevice indices the device occupies in the index space
    ///   (usually 1, but some profiles might reserve extra space, such as up/down buttons)
    /// @param aEEProfile RORG/FUNC/TYPE EEP profile number
    /// @param aEEManufacturer manufacturer number (or manufacturer_unknown)
    /// @param aSendTeachInResponse enable sending teach-in response for this device
    /// @return returns NULL if no device can be created for the given aSubDeviceIndex, new device otherwise
    static EnoceanDevicePtr newDevice(
      EnoceanVdc *aVdcP,
      EnoceanAddress aAddress,
      EnoceanSubDevice &aSubDeviceIndex,
      EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
      bool aSendTeachInResponse
    );

  };


  /// SODA window handle handler
  class EnoceanD20601Handler : public EnoceanChannelHandler
  {
    typedef EnoceanChannelHandler inherited;

    // behaviours for extra sensors
    // Note: using base class' behaviour pointer for first sensor = window handle input
    DsBehaviourPtr temperatureSensor;
    DsBehaviourPtr humiditySensor;
    DsBehaviourPtr illuminationSensor;
    DsBehaviourPtr batterySensor;
    DsBehaviourPtr burglaryAlarmInput;
    DsBehaviourPtr protectionAlarmInput;
    DsBehaviourPtr motionInput;
    DsBehaviourPtr tiltInput;

    /// private constructor, friend class' Enocean4bsHandler::newDevice is the place to call it from
    EnoceanD20601Handler(EnoceanDevice &aDevice);

  public:

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
      bool aSendTeachInResponse
    );

    /// handle radio packet related to this channel
    /// @param aEsp3PacketPtr the radio packet to analyze and extract channel related information
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr);

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc();
  };
  typedef boost::intrusive_ptr<EnoceanD20601Handler> EnoceanD20601HandlerPtr;


  /// single EnOcean button channel
  class EnoceanD20601ButtonHandler : public EnoceanChannelHandler
  {
    typedef EnoceanChannelHandler inherited;
    friend class EnoceanD20601Handler;

    /// private constructor, create new channels using factory static method
    EnoceanD20601ButtonHandler(EnoceanDevice &aDevice, int aSwitchIndex);

    bool pressed; ///< true if currently pressed, false if released, index: 0=on/down button, 1=off/up button
    int switchIndex; ///< which switch within the device (0..1)

    /// handle radio packet related to this channel
    /// @param aEsp3PacketPtr the radio packet to analyze and extract channel related information
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr);

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc();
  };



} // namespace p44

#endif // ENABLE_ENOCEAN
#endif // __p44vdc__enoceanvld__
