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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL IFNOTREDUCEDFOOTPRINT(7)

#include "enoceanrps.hpp"

#if ENABLE_ENOCEAN

#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"


using namespace p44;

// MARK: - EnoceanRPSDevice

EnoceanRPSDevice::EnoceanRPSDevice(EnoceanVdc *aVdcP) :
  inherited(aVdcP)
{
}


static const ProfileVariantEntry RPSprofileVariants[] = {
  // dual rocker RPS button alternatives
  { 1, 0x00F602FF, 2, "dual rocker switch (as 2-way rockers)", DeviceConfigurations::buttonTwoWay }, // rocker switches affect 2 indices (of which odd one does not exist in 2-way mode)
  { 1, 0x02F602FF, 2, "dual rocker switch (2-way, reversed)", DeviceConfigurations::buttonTwoWayReversed }, // rocker switches affect 2 indices (of which odd one does not exist in 2-way mode)
  { 1, 0x01F602FF, 2, "dual rocker switch (up and down as separate buttons)", DeviceConfigurations::buttonSingle },
  { 1, 0x00F60401, 0, "key card activated switch", NULL },
  { 1, 0x00F604C0, 0, "key card activated switch FKC/FKF", NULL },
  { 1, 0x00F605C0, 0, "Smoke detector FRW/GUARD", NULL },
  { 1, 0x00F60502, 0, "Smoke detector", NULL },
  { 1, 0x00F60500, 0, "Wind speed detector", NULL },
  { 1, 0x00F60501, 0, "Liquid Leakage detector", NULL },
  // quad rocker RPS button alternatives
  { 2, 0x00F603FF, 2, "quad rocker switch (as 2-way rockers)", DeviceConfigurations::buttonTwoWay }, // rocker switches affect 2 indices (of which odd one does not exist in 2-way mode)
  { 2, 0x02F603FF, 2, "quad rocker switch (2-way, reversed)", DeviceConfigurations::buttonTwoWayReversed }, // rocker switches affect 2 indices (of which odd one does not exist in 2-way mode)
  { 2, 0x01F603FF, 2, "quad rocker switch (up and down as separate buttons)", DeviceConfigurations::buttonSingle },
  // single RPS button alternatives
  { 3, 0x00F60101, 2, "single button", DeviceConfigurations::buttonSingle }, // use as single button (oneWay)
  { 3, 0x01F60101, 2, "single contact (closed = 1)", NULL }, // use as generic contact input
  { 3, 0x02F60101, 2, "single contact, inverted (open = 1)", NULL }, // use as generic contact input with inverted polarity
  { 0, 0, 0, NULL, NULL } // terminator
};


const ProfileVariantEntry *EnoceanRPSDevice::profileVariantsTable()
{
  return RPSprofileVariants;
}


#define CONTACT_UPDATE_INTERVAL (15*Minute)

EnoceanDevicePtr EnoceanRPSDevice::newDevice(
  EnoceanVdc *aVdcP,
  EnoceanAddress aAddress,
  EnoceanSubDevice &aSubDeviceIndex,
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aNeedsTeachInResponse
) {
  EnoceanDevicePtr newDev; // none so far
  EnoceanProfile functionProfile = EEP_UNTYPED(aEEProfile);
  if (aEEProfile==0x00F60101) {
    // F6-01-01 single button
    if (aSubDeviceIndex<1) {
      // create EnoceanRPSDevice device
      newDev = EnoceanDevicePtr(new EnoceanRPSDevice(aVdcP));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      // assign EPP information
      newDev->setEEPInfo(aEEProfile, aEEManufacturer);
      newDev->setFunctionDesc("button");
      // set icon name: generic button icon
      newDev->setIconInfo("button", true);
      // RPS switches can be used for anything
      newDev->setColorClass(class_black_joker);
      // Create single button handler
      EnoceanRpsButtonHandlerPtr buttonHandler = EnoceanRpsButtonHandlerPtr(new EnoceanRpsButtonHandler(*newDev.get(), undefined));
      ButtonBehaviourPtr buttonBhvr = ButtonBehaviourPtr(new ButtonBehaviour(*newDev.get(),"")); // automatic id
      buttonBhvr->setHardwareButtonConfig(0, buttonType_single, buttonElement_center, false, 0, 0); // not combinable
      buttonBhvr->setGroup(group_yellow_light); // pre-configure for light
      buttonBhvr->setHardwareName("button");
      buttonHandler->behaviour = buttonBhvr;
      newDev->addChannelHandler(buttonHandler);
      // count it
      aSubDeviceIndex++;
    }
  }
  else if (aEEProfile==0x01F60101 || aEEProfile==0x02F60101) {
    // F6-01-01 used as contact input, eg. Eltako FPE-1 (normal) and FPE-2 (inverted)
    if (aSubDeviceIndex<1) {
      // create device
      newDev = EnoceanDevicePtr(new EnoceanRPSDevice(aVdcP));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      // assign EPP information
      newDev->setEEPInfo(aEEProfile, aEEManufacturer);
      newDev->setFunctionDesc("single contact");
      // joker by default, we don't know what kind of contact this is
      newDev->setColorClass(class_black_joker);
      // create channel handler, EEP variant 2 means inverted state interpretation
      EnoceanRpsButtonHandlerPtr newHandler = EnoceanRpsButtonHandlerPtr(new EnoceanRpsButtonHandler(*newDev.get(), aEEProfile==0x02F60101 ? no : yes));
      // create the behaviour
      BinaryInputBehaviourPtr bb = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get(),"contact"));
      bb->setHardwareInputConfig(binInpType_none, usage_undefined, true, CONTACT_UPDATE_INTERVAL, CONTACT_UPDATE_INTERVAL*3);
      bb->setGroup(group_black_variable); // joker by default
      bb->setHardwareName(newHandler->shortDesc());
      newHandler->behaviour = bb;
      // add channel to device
      newDev->addChannelHandler(newHandler);
      // count it
      aSubDeviceIndex++;
    }
  }
  else if (EEP_PURE(functionProfile)==0xF60200 || EEP_PURE(functionProfile)==0xF60300) {
    // F6-02-xx or F6-03-xx: 2 or 4 rocker switch
    // - we have the standard rocker variant (0), the separate buttons variant (1) or the reversed rocker variant (2)
    // - subdevice index range is always 4 (or 8 for 4-rocker), but for 2-way only every other subdevice index is used
    EnoceanSubDevice numSubDevices = functionProfile==0xF60300 ? 8 : 4;
    if (EEP_VARIANT(aEEProfile)==1) {
      // Custom variant: up and down are treated as separate buttons -> max 4 or 8 dsDevices
      if (aSubDeviceIndex<numSubDevices) {
        // create EnoceanRPSDevice device
        newDev = EnoceanDevicePtr(new EnoceanRPSDevice(aVdcP));
        // standard device settings without scene table
        newDev->installSettings();
        // assign channel and address
        newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
        // assign EPP information
        newDev->setEEPInfo(aEEProfile, aEEManufacturer);
        newDev->setFunctionDesc("button");
        // set icon name: 4-rocker have a generic icon, 2-rocker have the 4btn icon in separated mode
        newDev->setIconInfo(functionProfile==0xF60300 ? "enocean_4rkr" : "enocean_4btn", true);
        // RPS switches can be used for anything
        newDev->setColorClass(class_black_joker);
        // Create single handler, up button for even aSubDevice, down button for odd aSubDevice
        bool isUp = (aSubDeviceIndex & 0x01)==0;
        EnoceanRpsRockerHandlerPtr buttonHandler = EnoceanRpsRockerHandlerPtr(new EnoceanRpsRockerHandler(*newDev.get()));
        buttonHandler->switchIndex = aSubDeviceIndex/2; // subdevices are half-switches, so switch index == subDeviceIndex/2
        buttonHandler->isRockerUp = isUp;
        ButtonBehaviourPtr buttonBhvr = ButtonBehaviourPtr(new ButtonBehaviour(*newDev.get(),"")); // automatic id
        buttonBhvr->setHardwareButtonConfig(0, buttonType_single, buttonElement_center, false, 0, 2); // combinable in pairs
        buttonBhvr->setGroup(group_yellow_light); // pre-configure for light
        buttonBhvr->setHardwareName(isUp ? "upper key" : "lower key");
        buttonHandler->behaviour = buttonBhvr;
        newDev->addChannelHandler(buttonHandler);
        // count it
        // - separate buttons use all indices 0,1,2,3...
        aSubDeviceIndex++;
      }
    }
    else {
      // Up+Down together form a  2-way rocker
      // - but variant 2 allows up/down to be reversed
      bool reversed = EEP_VARIANT(aEEProfile)==2;
      if (aSubDeviceIndex<numSubDevices) {
        // create EnoceanRPSDevice device
        newDev = EnoceanDevicePtr(new EnoceanRPSDevice(aVdcP));
        // standard device settings without scene table
        newDev->installSettings();
        // assign channel and address
        newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
        // assign EPP information
        newDev->setEEPInfo(aEEProfile, aEEManufacturer);
        newDev->setFunctionDesc("rocker switch");
        // set icon name: generic 4-rocker or for 2-rocker: even-numbered subdevice is left, odd is right
        newDev->setIconInfo(functionProfile==0xF60300 ? "enocean_4rkr" : (aSubDeviceIndex & 0x02 ? "enocean_br" : "enocean_bl"), true);
        // RPS switches can be used for anything
        newDev->setColorClass(class_black_joker);
        // Create two handlers, one for the up button, one for the down button
        // - create button input for what dS will handle as "down key" (actual button depends on "reversed")
        EnoceanRpsRockerHandlerPtr downHandler = EnoceanRpsRockerHandlerPtr(new EnoceanRpsRockerHandler(*newDev.get()));
        downHandler->switchIndex = aSubDeviceIndex/2; // subdevices are half-switches, so switch index == subDeviceIndex/2
        downHandler->isRockerUp = reversed; // normal: first button is the hardware-down-button, reversed: hardware-up-button
        ButtonBehaviourPtr downBhvr = ButtonBehaviourPtr(new ButtonBehaviour(*newDev.get(),""));  // automatic id
        downBhvr->setHardwareButtonConfig(0, buttonType_2way, buttonElement_down, false, 1, 0); // counterpart up-button has buttonIndex 1, fixed mode
        downBhvr->setGroup(group_yellow_light); // pre-configure for light
        downBhvr->setHardwareName("down key");
        downHandler->behaviour = downBhvr;
        newDev->addChannelHandler(downHandler);
        // - create button input for what dS will handle as "up key" (actual button depends on "reversed")
        EnoceanRpsRockerHandlerPtr upHandler = EnoceanRpsRockerHandlerPtr(new EnoceanRpsRockerHandler(*newDev.get()));
        upHandler->switchIndex = aSubDeviceIndex/2; // subdevices are half-switches, so switch index == subDeviceIndex/2
        upHandler->isRockerUp = !reversed; // normal: second button is the hardware-up-button, reversed: hardware-down-button
        ButtonBehaviourPtr upBhvr = ButtonBehaviourPtr(new ButtonBehaviour(*newDev.get(),"")); // automatic id
        upBhvr->setGroup(group_yellow_light); // pre-configure for light
        upBhvr->setHardwareButtonConfig(0, buttonType_2way, buttonElement_up, false, 0, 0); // counterpart down-button has buttonIndex 0, fixed mode
        upBhvr->setHardwareName("up key");
        upHandler->behaviour = upBhvr;
        newDev->addChannelHandler(upHandler);
        // count it
        // - 2-way rocker switches use indices 0,2,4,6,... to leave room for separate button mode without shifting indices
        aSubDeviceIndex+=2;
      }
    }
  }
  else if (functionProfile==0xF61000) {
    // F6-10-00 : Window handle = single device
    if (aSubDeviceIndex<1) {
      // create EnoceanRPSDevice device
      newDev = EnoceanDevicePtr(new EnoceanRPSDevice(aVdcP));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      // assign EPP information
      newDev->setEEPInfo(aEEProfile, aEEManufacturer);
      newDev->setFunctionDesc("window handle");
      // Window handle switches can be used for anything
      newDev->setColorClass(class_black_joker);
      // Single input with tri-state (extendedValue)
      // - Input0: 0: window closed (Handle down position), 1: window fully open, 2: window tilted
      EnoceanRpsWindowHandleHandlerPtr newHandler = EnoceanRpsWindowHandleHandlerPtr(new EnoceanRpsWindowHandleHandler(*newDev.get()));
      BinaryInputBehaviourPtr bb = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get(),"")); // automatic id
      bb->setHardwareInputConfig(binInpType_windowHandle, usage_undefined, true, Never, Never);
      bb->setGroup(group_black_variable); // joker by default
      bb->setHardwareName("Window open/tilted");
      newHandler->behaviour = bb;
      newDev->addChannelHandler(newHandler);
      // count it
      aSubDeviceIndex++;
    }
  }
  else if (functionProfile==0xF60400) {
    // F6-04-01, F6-04-02, F6-04-C0 : key card activated switch = single device
    // Note: F6-04-C0 is custom pseudo-EEP for not officially defined Eltako FKC/FKF card switches
    if (aSubDeviceIndex<1) {
      // create EnoceanRPSDevice device
      newDev = EnoceanDevicePtr(new EnoceanRPSDevice(aVdcP));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      // assign EPP information
      newDev->setEEPInfo(aEEProfile, aEEManufacturer);
      newDev->setFunctionDesc("Key card switch");
      // key card switches can be used for anything
      newDev->setColorClass(class_black_joker);
      // Current simple dS mapping: one binary input
      // - Input0: 1: card inserted, 0: card extracted
      EnoceanRpsCardKeyHandlerPtr newHandler = EnoceanRpsCardKeyHandlerPtr(new EnoceanRpsCardKeyHandler(*newDev.get()));
      BinaryInputBehaviourPtr bb = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get(),"card"));
      bb->setHardwareInputConfig(binInpType_none, usage_undefined, true, Never, Never);
      bb->setGroup(group_black_variable); // joker by default
      bb->setHardwareName("Card inserted");
      newHandler->isServiceCardDetector = false;
      newHandler->behaviour = bb;
      newDev->addChannelHandler(newHandler);
      // FKC/FKF can distinguish guest and service cards and have a second input
      if (aEEProfile==0xF604C0) {
        // - Input1: 1: card is service card, 0: card is guest card
        EnoceanRpsCardKeyHandlerPtr newHandler = EnoceanRpsCardKeyHandlerPtr(new EnoceanRpsCardKeyHandler(*newDev.get()));
        BinaryInputBehaviourPtr bb = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get(),"service"));
        bb->setHardwareInputConfig(binInpType_none, usage_undefined, true, Never, Never);
        bb->setGroup(group_black_variable); // joker by default
        bb->setHardwareName("Service card");
        newHandler->isServiceCardDetector = true;
        newHandler->behaviour = bb;
        newDev->addChannelHandler(newHandler);
      }
      // count it
      aSubDeviceIndex++;
    }
  }
  else if (aEEProfile==0xF60501) {
    // F6-05-01 - Liquid Leakage Detector
    if (aSubDeviceIndex<1) {
      // create EnoceanRPSDevice device
      newDev = EnoceanDevicePtr(new EnoceanRPSDevice(aVdcP));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      // assign EPP information
      newDev->setEEPInfo(aEEProfile, aEEManufacturer);
      newDev->setFunctionDesc("Leakage detector");
      // leakage detectors can be used for anything
      newDev->setColorClass(class_black_joker);
      // Current simple dS mapping: one binary input for leakage status
      EnoceanRpsLeakageDetectorHandlerPtr newHandler;
      BinaryInputBehaviourPtr bb;
      // - 1: Leakage: 0: no leakage
      newHandler = EnoceanRpsLeakageDetectorHandlerPtr(new EnoceanRpsLeakageDetectorHandler(*newDev.get()));
      bb = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get(),"leakage"));
      bb->setHardwareInputConfig(binInpType_none, usage_undefined, true, Never, Never); // generic because dS does not have a binary sensor function for leakage yet
      bb->setGroup(group_black_variable); // joker by default
      bb->setHardwareName("Leakage detector");
      newHandler->behaviour = bb;
      newDev->addChannelHandler(newHandler);
      // count it
      aSubDeviceIndex++;
    }
  }
  else if (aEEProfile==0xF60500 || aEEProfile==0xF60502 || aEEProfile==0xF605C0) {
    // F6-05-00 - official EEP for "wind speed threshold detector"
    // F6-05-02 - official EEP for "smoke detector"
    // F6-05-C0 - P44 pseudo-EEP for smoke detector used when F6-05-02 was not yet defined (but Eltako FRW and alphaEOS GUARD already used it)
    if (aEEProfile==0xF605C0) aEEProfile = 0xF60502;
    if (aSubDeviceIndex<1) {
      // create EnoceanRPSDevice device
      newDev = EnoceanDevicePtr(new EnoceanRPSDevice(aVdcP));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      // assign EPP information - Note: hard-coding now-defined F6-05-02 (not showing internal F6-05-C0 any more)
      newDev->setEEPInfo(aEEProfile, aEEManufacturer);
      newDev->setFunctionDesc(aEEProfile==0xF60500 ? "Wind speed threshold detector" : "Smoke detector");
      // detectors can be used for anything
      newDev->setColorClass(class_black_joker);
      // Current simple dS mapping: one binary input for alarm status, one for low bat status
      EnoceanRpsWindSmokeDetectorHandlerPtr newHandler;
      BinaryInputBehaviourPtr bb;
      // - Alarm: 1: Alarm, 0: no Alarm
      newHandler = EnoceanRpsWindSmokeDetectorHandlerPtr(new EnoceanRpsWindSmokeDetectorHandler(*newDev.get()));
      bb = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get(),"")); // automatic id
      bb->setHardwareInputConfig(aEEProfile==0xF60500 ? binInpType_wind : binInpType_smoke, usage_room, true, Never, Never);
      bb->setGroup(group_black_variable); // joker by default
      bb->setHardwareName(aEEProfile==0xF60500 ? "Wind alarm" : "Smoke alarm");
      newHandler->behaviour = bb;
      newHandler->isBatteryStatus = false;
      newDev->addChannelHandler(newHandler);
      // - Low Battery: 1: battery low, 0: battery OK
      newHandler = EnoceanRpsWindSmokeDetectorHandlerPtr(new EnoceanRpsWindSmokeDetectorHandler(*newDev.get()));
      bb = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get(),"")); // automatic id
      bb->setHardwareInputConfig(binInpType_lowBattery, usage_room, true, Never, Never);
      bb->setGroup(group_black_variable); // joker by default
      bb->setHardwareName("Low battery");
      newHandler->behaviour = bb;
      newHandler->isBatteryStatus = true;
      newDev->addChannelHandler(newHandler);
      // count it
      aSubDeviceIndex++;
    }
  }
  // RPS never needs a teach-in response
  // return device (or empty if none created)
  return newDev;
}


// MARK: - single button

EnoceanRpsButtonHandler::EnoceanRpsButtonHandler(EnoceanDevice &aDevice, Tristate aBinContactClosedValue) :
  inherited(aDevice),
  mBinContactClosedValue(aBinContactClosedValue)
{
}


// device specific radio packet handling
void EnoceanRpsButtonHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  // extract payload data
  uint8_t data = aEsp3PacketPtr->radioUserData()[0];
  uint8_t rpsStatus = aEsp3PacketPtr->radioStatus() & status_rps_mask;
  // decode
  if (rpsStatus==status_T21) {
    bool pressed = data==0x10;
    if (mBinContactClosedValue==undefined) {
      // handle as button
      ButtonBehaviourPtr b = boost::dynamic_pointer_cast<ButtonBehaviour>(behaviour);
      if (b) {
        LOG(LOG_INFO, "Enocean Button %s - %08X: reports state %s", b->getHardwareName().c_str(), device.getAddress(), pressed ? "PRESSED" : "RELEASED");
        b->updateButtonState(pressed);
      }
    }
    else {
      // handle as contact
      BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(behaviour);
      if (bb) {
        bb->updateInputState(pressed==(mBinContactClosedValue==yes));
      }
    }
  }
}


string EnoceanRpsButtonHandler::shortDesc()
{
  return "Button";
}


// MARK: - rocker buttons

EnoceanRpsRockerHandler::EnoceanRpsRockerHandler(EnoceanDevice &aDevice) :
  inherited(aDevice),
  pressed(false)
{
  switchIndex = 0; // default to first
}


// device specific radio packet handling
void EnoceanRpsRockerHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  // extract payload data
  uint8_t data = aEsp3PacketPtr->radioUserData()[0];
  uint8_t status = aEsp3PacketPtr->radioStatus();
  FOCUSLOG("RPS message processing: data=0x%02X, status=0x%02X (switchIndex=%d, isRockerUp=%d)", data, status, switchIndex, isRockerUp);
  // decode
  if (status & status_NU) {
    // N-Message
    FOCUSLOG("- N-message");
    // collect action(s)
    for (int ai=1; ai>=0; ai--) {
      // first action is in DB7..5, second action is in DB3..1 (if DB0==1)
      uint8_t a = (data >> (4*ai+1)) & 0x07;
      if (ai==0 && (data&0x01)==0)
        break; // no second action
      FOCUSLOG("- action #%d = %d", 2-ai, a);
      if (((a>>1) & 0x03)==switchIndex) {
        // querying this subdevice/rocker
        FOCUSLOG("- is my switchIndex == %d", switchIndex);
        if (((a & 0x01)!=0) == isRockerUp) {
          FOCUSLOG("- is my side (%s) of the switch, isRockerUp == %d", isRockerUp ? "Up" : "Down", isRockerUp);
          // my half of the rocker, DB4 is button state (1=pressed, 0=released)
          setButtonState((data & 0x10)!=0);
        }
      }
    }
  }
  else {
    // U-Message
    FOCUSLOG("- U-message");
    uint8_t b = (data>>5) & 0x07;
    bool pressed = (data & 0x10);
    FOCUSLOG("- number of buttons still pressed code = %d, action (energy bow) = %s", b, pressed ? "PRESSED" : "RELEASED");
    if (!pressed) {
      // report release if all buttons are released now
      if (b==0) {
        // all buttons released, this includes this button
        FOCUSLOG("- released multiple buttons, report RELEASED for all");
        setButtonState(false);
      }
    }
    // ignore everything else (more that 2 press actions simultaneously)
  }
}


void EnoceanRpsRockerHandler::setButtonState(bool aPressed)
{
  // only propagate real changes
  // (because we also get called here for release of other buttons. updateButtonState()
  // works with reporting release multiple times, but we'd produce confusing log entries, so
  // we suppress these EnOcean technology specific extra updates)
  if (aPressed!=pressed) {
    // real change, propagate to behaviour
    ButtonBehaviourPtr b = boost::dynamic_pointer_cast<ButtonBehaviour>(behaviour);
    if (b) {
      OLOG(LOG_INFO, "Enocean Button %s - %08X, subDevice %d: changed state to %s", b->getHardwareName().c_str(), device.getAddress(), device.getSubDevice(), aPressed ? "PRESSED" : "RELEASED");
      b->updateButtonState(aPressed);
    }
    // update cached status
    pressed = aPressed;
  }
}



string EnoceanRpsRockerHandler::shortDesc()
{
  return "Rocker";
}


// MARK: - window handle


EnoceanRpsWindowHandleHandler::EnoceanRpsWindowHandleHandler(EnoceanDevice &aDevice) :
  inherited(aDevice)
{
}


// device specific radio packet handling
void EnoceanRpsWindowHandleHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  // extract payload data
  uint8_t data = aEsp3PacketPtr->radioUserData()[0];
  uint8_t rpsStatus = aEsp3PacketPtr->radioStatus() & status_rps_mask;
  // decode
  bool tilted = false;
  bool closed = false;
  if (rpsStatus==status_T21) {
    // Valid ERP1 window handle status change message
    // extract status (in bits 4..7)
    tilted = (data & 0xF0)==0xD0; // turned up from sideways
    closed = (data & 0xF0)==0xF0; // turned down from sideways
  }
  else {
    return; // unknown data, don't update binary inputs at all
  }
  // report data for this binary input
  BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(behaviour);
  if (bb) {
    LOG(LOG_INFO, "Enocean Window Handle %08X reports state: %s", device.getAddress(), closed ? "closed" : (tilted ? "tilted open" : "fully open"));
    bb->updateInputState(closed ? 0 : (tilted ? 2 : 1)); // report the extendedValue state: 0=closed, 1=fully open, 2=tilted open
  }
}


string EnoceanRpsWindowHandleHandler::shortDesc()
{
  return "Window Handle";
}


// MARK: - key card switch


EnoceanRpsCardKeyHandler::EnoceanRpsCardKeyHandler(EnoceanDevice &aDevice) :
  inherited(aDevice)
{
}


// EEP F6-04-01
//   inserted = status_NU and data = 0x70
//   extracted = !status_NU and data = 0x00

// Eltako FKC and FKF (not documented in EEP):
// - FKF just detects cards
// - FKC can detect Guest (KCG) and service (KCS) cards
//   data 0x10, status 0x30 = inserted KCS (Service Card)
//   data 0x00, status 0x20 = extracted
//   data 0x10, status 0x20 = inserted KCG (Guest Card)
//   means:
//   - state of card is in bit 4 (1=inserted)
//   - type of card is status_NU (N=Service, U=Guest)

// device specific radio packet handling
void EnoceanRpsCardKeyHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  bool isInserted = false;
  bool isServiceCard = false;
  // extract payload data
  uint8_t data = aEsp3PacketPtr->radioUserData()[0];
  uint8_t rpsStatus = aEsp3PacketPtr->radioStatus() & status_rps_mask;
  if (device.getEEProfile()==0xF604C0) {
    // FKC or FKF style switch (no official EEP for this)
    isInserted = data & 0x10; // Bit4
    if (isInserted && ((rpsStatus & status_NU)!=0)) {
      // Insertion with N-message (status=0x30) means service card
      isServiceCard = true;
    }
  }
  else {
    // Asssume Standard F6-04-01 Key Card Activated Switch
    isInserted = (rpsStatus & status_NU)!=0 && data==0x70;
  }
  // report data for this binary input
  BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(behaviour);
  if (bb) {
    if (isServiceCardDetector) {
      LOG(LOG_INFO, "Enocean Key Card Switch %08X reports: %s", device.getAddress(), isServiceCard ? "Service Card" : "Guest Card");
      bb->updateInputState(isServiceCard); // report the card type
    }
    else {
      LOG(LOG_INFO, "Enocean Key Card Switch %08X reports state: %s", device.getAddress(), isInserted ? "inserted" : "extracted");
      bb->updateInputState(isInserted); // report the status
    }
  }
}


string EnoceanRpsCardKeyHandler::shortDesc()
{
  return "Key Card Switch";
}


// MARK: - Wind and Smoke Detector

EnoceanRpsWindSmokeDetectorHandler::EnoceanRpsWindSmokeDetectorHandler(EnoceanDevice &aDevice) :
  inherited(aDevice)
{
}


// device specific radio packet handling
void EnoceanRpsWindSmokeDetectorHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  // extract payload data
  uint8_t data = aEsp3PacketPtr->radioUserData()[0];
  BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(behaviour);
  if (isBatteryStatus) {
    // battery status channel (member field of EnoceanChannelHandler, influences opStateLevel())
    bool lowBat = (data & 0x30)==0x30;
    batPercentage = lowBat ? LOW_BAT_PERCENTAGE : 100;
    LOG(LOG_INFO, "Enocean Detector %08X reports state: Battery %s", device.getAddress(), lowBat ? "LOW" : "ok");
    bb->updateInputState(lowBat);
  }
  else {
    // smoke alarm status
    bool alarm = (data & 0x30)==0x10;
    LOG(LOG_INFO, "Enocean Detector %08X reports state: %s", device.getAddress(), alarm ? "ALARM" : "no alarm");
    bb->updateInputState(alarm);
  }
}


string EnoceanRpsWindSmokeDetectorHandler::shortDesc()
{
  return "Detector";
}


// MARK: - Liquid Leakage Detector

EnoceanRpsLeakageDetectorHandler::EnoceanRpsLeakageDetectorHandler(EnoceanDevice &aDevice) :
  inherited(aDevice)
{
}



// F6-05-01
//                          DATA 	STATUS
//  Water detected          11		30 (NU + T21 both set)


// device specific radio packet handling
void EnoceanRpsLeakageDetectorHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  // extract payload data
  uint8_t data = aEsp3PacketPtr->radioUserData()[0];
  BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(behaviour);
  // smoke alarm status
  bool leakage =
    (data==0x11) && // data must be 0x11
    ((aEsp3PacketPtr->radioStatus() & status_rps_mask)==status_T21+status_NU); // AND NU and T21 must be set
  LOG(LOG_INFO, "Enocean Liquid Leakage Detector %08X reports state: %s", device.getAddress(), leakage ? "LEAKAGE" : "no leakage");
  bb->updateInputState(leakage);
}


string EnoceanRpsLeakageDetectorHandler::shortDesc()
{
  return "Leakage Detector";
}


#endif // ENABLE_ENOCEAN
