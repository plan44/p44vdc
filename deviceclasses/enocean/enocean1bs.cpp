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

#include "enocean1bs.hpp"

#if ENABLE_ENOCEAN

#include "enoceanvdc.hpp"
#include "binaryinputbehaviour.hpp"

using namespace p44;


// MARK: - Enocean1BSDevice

Enocean1BSDevice::Enocean1BSDevice(EnoceanVdc *aVdcP) :
  inherited(aVdcP)
{
}


#define CONTACT_UPDATE_INTERVAL (15*Minute)

EnoceanDevicePtr Enocean1BSDevice::newDevice(
  EnoceanVdc *aVdcP,
  EnoceanAddress aAddress,
  EnoceanSubDevice &aSubDeviceIndex,
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aSendTeachInResponse
) {
  EepFunc func = EEP_FUNC(aEEProfile);
  EepType type = EEP_TYPE(aEEProfile);
  EnoceanDevicePtr newDev; // none so far
  // At this time, only the "single input contact" profile is defined in EEP: D5-00-01
  // Note: two variants exist, one with inverted contact signal (reporting 1 for open contact)
  if (func==0x00 && type==0x01) {
    // single input contact, always consists of a single device
    if (aSubDeviceIndex<1) {
      // create device
      newDev = EnoceanDevicePtr(new Enocean1BSDevice(aVdcP));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      // assign EPP information
      newDev->setEEPInfo(aEEProfile, aEEManufacturer);
      newDev->setFunctionDesc("single contact");
      // joker by default, we don't know what kind of contact this is
      newDev->setColorClass(class_black_joker);
      // create channel handler, EEP variant 1 means inverted state interpretation
      SingleContactHandlerPtr newHandler = SingleContactHandlerPtr(new SingleContactHandler(*newDev.get(), !(EEP_VARIANT(aEEProfile)==1)));
      // create the behaviour
      BinaryInputBehaviourPtr bb = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get(),"contact"));
      bb->setHardwareInputConfig(binInpType_none, usage_undefined, true, CONTACT_UPDATE_INTERVAL, CONTACT_UPDATE_INTERVAL*3);
      bb->setGroup(group_black_variable); // joker by default
      bb->setHardwareName(newHandler->shortDesc());
      newHandler->mBehaviour = bb;
      // add channel to device
      newDev->addChannelHandler(newHandler);
      // count it
      aSubDeviceIndex++;
    }
  }
  // return device (or empty if none created)
  return newDev;
}


static const ProfileVariantEntry E1BSprofileVariants[] = {
  // single contact alternatives
  { 1, 0x00D50001, 0, "single contact (closed = 1)", NULL },
  { 1, 0x01D50001, 0, "single contact, inverted (open = 1)", NULL },
  { 0, 0, 0, NULL, NULL } // terminator
};


const ProfileVariantEntry *Enocean1BSDevice::profileVariantsTable()
{
  return E1BSprofileVariants;
}



// MARK: - SingleContactHandler


SingleContactHandler::SingleContactHandler(EnoceanDevice &aDevice, bool aActiveState) :
  EnoceanChannelHandler(aDevice),
  mActiveState(aActiveState)
{
}


// handle incoming data from device and extract data for this channel
void SingleContactHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  if (!aEsp3PacketPtr->radioHasTeachInfo()) {
    // only look at non-teach-in packets
    if (aEsp3PacketPtr->eepRorg()==rorg_1BS && aEsp3PacketPtr->radioUserDataLength()==1) {
      // only look at 1BS packets of correct length
      uint8_t data = aEsp3PacketPtr->radioUserData()[0];
      // report contact state to binaryInputBehaviour
      BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(mBehaviour);
      if (bb) {
        bb->updateInputState(((data & 0x01)!=0) == mActiveState); // Bit 0 is the contact, report straight or inverted depending on activeState
      }
    }
  }
}


bool SingleContactHandler::isAlive()
{
  // check if gotten no message for longer than aliveSignInterval*factor
  if (MainLoop::now()-mDevice.getLastPacketTime() < CONTACT_UPDATE_INTERVAL*TIMEOUT_FACTOR_FOR_INACTIVE)
    return true;
  // timed out
  return false;
}



string SingleContactHandler::shortDesc()
{
  return "Single Contact";
}


#endif // ENABLE_ENOCEAN

