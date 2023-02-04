//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2015-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__enoceanremotecontrol__
#define __p44vdc__enoceanremotecontrol__

#include "p44vdc_common.hpp"

#if ENABLE_ENOCEAN

#include "enoceandevice.hpp"
#include "channelbehaviour.hpp"

#ifndef ENABLE_ENOCEAN_SHADOW
  #define ENABLE_ENOCEAN_SHADOW 1
#endif


using namespace std;

namespace p44 {

  // pseudo-RORG used in this implementation to identify "remote control" devices, i.e. those that use local baseID to send out actions
  #define PSEUDO_RORG_REMOTECONTROL 0xFF
  // - switch controls
  #define PSEUDO_FUNC_SWITCHCONTROL 0xF6
  #define PSEUDO_TYPE_SIMPLEBLIND 0xFF // simplistic Fully-Up/Fully-Down blind controller
  #define PSEUDO_TYPE_BLIND 0xFE // time controlled blind with angle support
  #define PSEUDO_TYPE_ON_OFF 0xFD // simple relay switched on by key up and switched off by key down
  #define PSEUDO_TYPE_SWITCHED_LIGHT 0xFC // switched light (with full light behaviour)
  // - proprietary
  #define PSEUDO_FUNC_SYSTEMELECTRONIC 0x50 // SystemElectronic.de specific profiles
  #define PSEUDO_TYPE_SE_HEATTUBE 0x01 // Heat tube protocol



  class EnoceanRemoteControlDevice : public EnoceanDevice
  {
    typedef EnoceanDevice inherited;

    MLTicket teachInTimer;

  public:

    /// constructor
    /// @param aDsuidIndexStep step between dSUID subdevice indices (default is 1, historically 2 for dual 2-way rocker switches)
    EnoceanRemoteControlDevice(EnoceanVdc *aVdcP, uint8_t aDsuidIndexStep = 1);

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "enocean_remotecontrol"; };

    /// @param aVariant -1 to just get number of available teach-in variants. 0..n to send teach-in signal;
    ///   some devices may have different teach-in signals (like: one for ON, one for OFF).
    /// @return number of teach-in signal variants the device can send
    /// @note will be called via UI for devices that need to be learned into remote actors
    virtual uint8_t teachInSignal(int8_t aVariant) P44_OVERRIDE;

    /// mark base offsets in use by this device
    /// @param aUsedOffsetsMap must be passed a string with 128 chars of '0' or '1'.
    virtual void markUsedBaseOffsets(string &aUsedOffsetsMap) P44_OVERRIDE;

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

  protected:

    /// utility function to send button action telegrams
    /// @param aRight: right (B) button instead of left (A) button
    /// @param aUp: up instead of down button
    /// @param aPress: pressing button instead of releasing it
    void buttonAction(bool aRight, bool aUp, bool aPress);

  private:

    void sendSwitchBeaconRelease(bool aRight, bool aUp);

  };


  class EnoceanRelayControlDevice : public EnoceanRemoteControlDevice
  {
    typedef EnoceanRemoteControlDevice inherited;

    MLTicket buttonTimer;

  public:

    /// constructor
    /// @param aDsuidIndexStep step between dSUID subdevice indices (default is 1, historically 2 for dual 2-way rocker switches)
    EnoceanRelayControlDevice(EnoceanVdc *aVdcP, uint8_t aDsuidIndexStep = 1);

    /// device type identifier
    /// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "enocean_relay"; };

    /// apply channel values
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming) P44_OVERRIDE;

  private:

    void sendReleaseTelegram(SimpleCB aDoneCB, bool aUp);

  };


  #if ENABLE_ENOCEAN_SHADOW

  class EnoceanBlindControlDevice : public EnoceanRemoteControlDevice
  {
    typedef EnoceanRemoteControlDevice inherited;

    int movingDirection; ///< currently moving direction 0=stopped, -1=moving down, +1=moving up
    MLTicket commandTicket;
    MLTicket sequenceTicket;

  public:

    /// constructor
    /// @param aDsuidIndexStep step between dSUID subdevice indices (default is 1, historically 2 for dual 2-way rocker switches)
    EnoceanBlindControlDevice(EnoceanVdc *aVdcP, uint8_t aDsuidIndexStep = 1);

    /// device type identifier
    /// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "enocean_blind"; };

    /// sync channel values (with time-derived estimates of current blind position
    virtual void syncChannelValues(SimpleCB aDoneCB) P44_OVERRIDE;

    /// apply channel values
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming) P44_OVERRIDE;

    /// start or stop dimming (optimized blind controller version)
    virtual void dimChannel(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode, bool aDoApply) P44_OVERRIDE;

  private:

    void changeMovement(SimpleCB aDoneCB, int aNewDirection);
    void sendReleaseTelegram(SimpleCB aDoneCB);

  };

  #endif // ENABLE_ENOCEAN_SHADOW



  class EnoceanSEHeatTubeDevice : public EnoceanRemoteControlDevice
  {
    typedef EnoceanRemoteControlDevice inherited;

    MLTicket applyRepeatTicket;

  public:

    /// constructor
    /// @param aDsuidIndexStep step between dSUID subdevice indices (default is 1, historically 2 for dual 2-way rocker switches)
    EnoceanSEHeatTubeDevice(EnoceanVdc *aVdcP, uint8_t aDsuidIndexStep = 1);

    /// device type identifier
    /// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "enocean_se_heattube"; };

    /// @param aVariant -1 to just get number of available teach-in variants. 0..n to send teach-in signal;
    ///   some devices may have different teach-in signals (like: one for ON, one for OFF).
    /// @return number of teach-in signal variants the device can send
    /// @note will be called via UI for devices that need to be learned into remote actors
    virtual uint8_t teachInSignal(int8_t aVariant) P44_OVERRIDE;

    /// apply channel values
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming) P44_OVERRIDE;

  private:

    void setPowerState(int aLevel, bool aInitial);

  };


}

#endif // ENABLE_ENOCEAN
#endif // __p44vdc__enoceanremotecontrol__

