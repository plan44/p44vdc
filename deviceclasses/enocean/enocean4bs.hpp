//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__enocean4bs__
#define __p44vdc__enocean4bs__

#include "p44vdc_common.hpp"

#if ENABLE_ENOCEAN

#include "enoceandevice.hpp"
#include "enoceansensorhandler.hpp"

using namespace std;

namespace p44 {


  class Enocean4BSDevice : public EnoceanDevice
  {
    typedef EnoceanDevice inherited;

  public:

    /// constructor
    Enocean4BSDevice(EnoceanVdc *aVdcP);

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "enocean_4bs"; };

    /// device specific teach in response
    /// @note will be called from newDevice() when created device needs a teach-in response
    virtual void sendTeachInResponse() P44_OVERRIDE;

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

    /// prepare aOutgoingPacket for sending 4BS data.
    /// creates new packet if none passed in, and returns already collected data
    /// @param aOutgoingPacket existing packet will be used, if NULL, new packet will be created
    /// @param a4BSdata will be set to already collected 4BS data (from already consulted channels or device global bits like LRN)
    static void prepare4BSpacket(Esp3PacketPtr &aOutgoingPacket, uint32_t &a4BSdata);

  };



  /// heating valve handler
  class EnoceanA52001Handler : public EnoceanChannelHandler
  {
    typedef EnoceanChannelHandler inherited;

    enum {
      service_idle,
      service_openvalve,
      service_openandclosevalve,
      service_closevalve,
      service_finish
    } serviceState;

    int8_t lastRequestedValvePos; ///< last valve position requested (used to calculate output for binary valves like MD10-FTL)
    int8_t lastActualValvePos; ///< last actually applied valve position (first derivative of lastRequestedValvePos)

    /// private constructor, friend class' Enocean4bsHandler::newDevice is the place to call it from
    EnoceanA52001Handler(EnoceanDevice &aDevice);

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
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr) P44_OVERRIDE;

    /// collect data for outgoing message from this channel
    /// @param aEsp3PacketPtr must be set to a suitable packet if it is empty, or packet data must be augmented with
    ///   channel's data when packet already exists
    virtual void collectOutgoingMessageData(Esp3PacketPtr &aEsp3PacketPtr) P44_OVERRIDE;

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc() P44_OVERRIDE;
  };
  typedef boost::intrusive_ptr<EnoceanA52001Handler> EnoceanA52001HandlerPtr;



  /// heating valve handler
  class EnoceanA52004Handler : public EnoceanChannelHandler
  {
    typedef EnoceanChannelHandler inherited;

    enum {
      service_idle,
      service_openvalve,
      service_openandclosevalve,
      service_closevalve,
      service_finish
    } serviceState;

    int8_t lastRequestedValvePos; ///< last valve position requested (used to calculate output for binary valves like MD10-FTL)
    int8_t lastActualValvePos; ///< last actually applied valve position (first derivative of lastRequestedValvePos)

    /// behaviours of extra sensors
    DsBehaviourPtr roomTemp;
    DsBehaviourPtr feedTemp;
    DsBehaviourPtr setpointTemp;
    DsBehaviourPtr lowBatInput;

    /// private constructor, friend class' Enocean4bsHandler::newDevice is the place to call it from
    EnoceanA52004Handler(EnoceanDevice &aDevice);

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
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr) P44_OVERRIDE;

    /// collect data for outgoing message from this channel
    /// @param aEsp3PacketPtr must be set to a suitable packet if it is empty, or packet data must be augmented with
    ///   channel's data when packet already exists
    virtual void collectOutgoingMessageData(Esp3PacketPtr &aEsp3PacketPtr) P44_OVERRIDE;

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc() P44_OVERRIDE;
  };
  typedef boost::intrusive_ptr<EnoceanA52004Handler> EnoceanA52004HandlerPtr;




  /// weather station handler
  class EnoceanA5130XHandler : public EnoceanChannelHandler
  {
    typedef EnoceanChannelHandler inherited;

    // behaviours for extra sensors
    // Note: using base class' behaviour pointer for first sensor = dawn sensor
    DsBehaviourPtr outdoorTemp;
    DsBehaviourPtr windSpeed;
    DsBehaviourPtr gustSpeed;
    DsBehaviourPtr dayIndicator;
    DsBehaviourPtr rainIndicator;
    DsBehaviourPtr sunWest;
    DsBehaviourPtr sunSouth;
    DsBehaviourPtr sunEast;

    /// private constructor, friend class' Enocean4bsHandler::newDevice is the place to call it from
    EnoceanA5130XHandler(EnoceanDevice &aDevice);

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
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr) P44_OVERRIDE;

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc() P44_OVERRIDE;
  };
  typedef boost::intrusive_ptr<EnoceanA5130XHandler> EnoceanA5130XHandlerPtr;



} // namespace p44

#endif // ENABLE_ENOCEAN
#endif // __p44vdc__enocean4bs__
