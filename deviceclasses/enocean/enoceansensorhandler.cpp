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

#include "enoceansensorhandler.hpp"

#if ENABLE_ENOCEAN

#include "sensorbehaviour.hpp"
#include "binaryinputbehaviour.hpp"

using namespace p44;
using namespace EnoceanSensors;


// MARK: - bit field handlers for EnoceanSensorHandler

/// standard bitfield extractor function for sensor behaviours (read only)
uint64_t p44::EnoceanSensors::bitsExtractor(const struct EnoceanSensorDescriptor &aSensorDescriptor, uint8_t *aDataP, int aDataSize)
{
  // in aDataP, MSB come first
  int msByteIndex = aDataSize-1-(aSensorDescriptor.msBit>>3);
  int lsByteIndex = aDataSize-1-(aSensorDescriptor.lsBit>>3);
  if (msByteIndex>lsByteIndex || lsByteIndex>=aDataSize) return 0; // bit field specified is not within data
  // collect data
  uint64_t value = 0;
  uint8_t firstBitNo = aSensorDescriptor.msBit & 0x07;
  // - bits from first byte
  if (msByteIndex<lsByteIndex) {
    // more than one byte
    uint8_t mask = ((uint16_t)1<<(firstBitNo+1))-1;
    value = aDataP[msByteIndex] & mask;
    firstBitNo = 7; // first bit to take from any further byte is bit 7
  }
  // - bits from middle bytes
  for (int i=msByteIndex+1; i<lsByteIndex; i++) {
    value = (value<<8) | aDataP[i];
  }
  // - bits from last byte
  uint8_t lastBitNo = aSensorDescriptor.lsBit & 0x07;
  uint8_t numBits = firstBitNo+1-lastBitNo;
  uint8_t mask = ((uint16_t)1<<(numBits))-1;
  value = (value<<numBits) | ((aDataP[lsByteIndex]>>lastBitNo) & mask);
  return value;
}


/// standard bitfield extractor function for sensor behaviours
void p44::EnoceanSensors::stdSensorHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  uint64_t value = bitsExtractor(aSensorDescriptor, aDataP, aDataSize);
  // now pass to behaviour
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    sb->updateEngineeringValue(value);
  }
}

/// inverted bitfield extractor function (for various temperature sensors)
void p44::EnoceanSensors::invSensorHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  uint64_t value = bitsExtractor(aSensorDescriptor, aDataP, aDataSize);
  value ^= (1ll<<(aSensorDescriptor.msBit-aSensorDescriptor.lsBit+1))-1; // invert all bits
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    sb->updateEngineeringValue(value);
  }
}


/// standard binary input handler
void p44::EnoceanSensors::stdInputHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  int byteIndex = aDataSize-1-(aSensorDescriptor.lsBit>>3);
  int bitIndex = aSensorDescriptor.lsBit & 0x07;
  bool newRawState = (aDataP[byteIndex]>>bitIndex) & 0x01;
  bool newState;
  if (newRawState)
    newState = (bool)aSensorDescriptor.max; // true: report value for max
  else
    newState = (bool)aSensorDescriptor.min; // false: report value for min
  // now pass to behaviour
  BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(aBehaviour);
  if (bb) {
    bb->updateInputState(newState);
  }
}


// helper to make sure handler and its parameter always match
void p44::EnoceanSensors::handleBitField(const EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  if (aSensorDescriptor.bitFieldHandler) {
    aSensorDescriptor.bitFieldHandler(aSensorDescriptor, aBehaviour, aDataP, aDataSize);
  }
}


// MARK: - Texts

namespace p44 { namespace EnoceanSensors {

  const char *tempText = "Temperature";

  const char *humText = "Humidity";

  const char *coText = "CO";
  const char *co2Text = "CO2";

  const char *illumText = "Illumination";

  const char *occupText = "Occupancy";

  const char *motionText = "Motion";

  const char *setPointText = "Set Point";
  const char *fanSpeedText = "Fan Speed";
  const char *dayNightText = "Day/Night";

  const char *contactText = "Contact";

  const char *supplyText = "Power supply";

} }


// MARK: - EnoceanSensorHandler

EnoceanSensorHandler::EnoceanSensorHandler(EnoceanDevice &aDevice) :
  inherited(aDevice),
  sensorChannelDescriptorP(NULL)
{
}



bool EnoceanSensorHandler::isAlive()
{
  if (sensorChannelDescriptorP->aliveSignInterval<=0)
    return true; // no alive sign interval to check, assume alive
  // check if gotten no message for longer than aliveSignInterval*factor
  if (MainLoop::now()-device.getLastPacketTime() < sensorChannelDescriptorP->aliveSignInterval*Second*TIMEOUT_FACTOR_FOR_INACTIVE)
    return true;
  // timed out
  return false;
}


// static factory method
EnoceanDevicePtr EnoceanSensorHandler::newDevice(
  EnoceanVdc *aVdcP,
  CreateDeviceFunc aCreateDeviceFunc,
  const EnoceanSensorDescriptor *aDescriptorTable,
  EnoceanAddress aAddress,
  EnoceanSubDevice &aSubDeviceIndex, // current subdeviceindex, factory returns NULL when no device can be created for this subdevice index
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aSendTeachInResponse
) {
  uint8_t variant = EEP_VARIANT(aEEProfile);
  EepFunc func = EEP_FUNC(aEEProfile);
  EepType type = EEP_TYPE(aEEProfile);
  EnoceanDevicePtr newDev; // none so far

  // create device from matching EEP with sensor table
  int numDescriptors = 0; // number of descriptors
  // Search descriptors for this EEP and for the start of channels for this aSubDeviceIndex (in case sensors in one physical devices are split into multiple vdSDs)
  const EnoceanSensorDescriptor *subdeviceDescP = NULL;
  const EnoceanSensorDescriptor *descP = aDescriptorTable;
  while (descP->bitFieldHandler!=NULL) {
    if (descP->variant==variant && descP->func==func && descP->type==type) {
      // remember if this is the subdevice we are looking for
      if (descP->subDevice==aSubDeviceIndex) {
        if (!subdeviceDescP) subdeviceDescP = descP; // remember the first descriptor of this subdevice as starting point for creating handlers below
        numDescriptors++; // count descriptors for this subdevice as a limit for creating handlers below
      }
    }
    descP++;
  }
  // Create device and channels
  bool needsTeachInResponse = false;
  bool firstDescriptorForDevice = true;
  while (numDescriptors>0) {
    // more channels to create for this subdevice number
    if (!newDev) {
      // device not yet created, create it now
      newDev = aCreateDeviceFunc(aVdcP);
      // sensor devices don't need scenes
      newDev->installSettings(); // no scenes
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      // assign EPP information
      newDev->setEEPInfo(aEEProfile, aEEManufacturer);
      // first descriptor defines device primary color
      newDev->setColorClass(subdeviceDescP->colorClass);
      // count it
      aSubDeviceIndex++;
    }
    // now add the channel
    addSensorChannel(newDev, *subdeviceDescP, firstDescriptorForDevice, NULL); // automatic id
    // next descriptor
    firstDescriptorForDevice = false;
    subdeviceDescP++;
    numDescriptors--;
  }
  // create the teach-in response if one is required
  if (newDev && aSendTeachInResponse && needsTeachInResponse) {
    newDev->sendTeachInResponse();
  }
  // return device (or empty if none created)
  return newDev;
}


// static factory method
void EnoceanSensorHandler::addSensorChannel(
  EnoceanDevicePtr aDevice,
  const EnoceanSensorDescriptor &aSensorDescriptor,
  bool aSetDeviceDescription,
  const char *aId
) {
  // create channel handler
  EnoceanSensorHandlerPtr newHandler = EnoceanSensorHandlerPtr(new EnoceanSensorHandler(*aDevice.get()));
  // assign descriptor
  newHandler->sensorChannelDescriptorP = &aSensorDescriptor;
  // create the behaviour
  newHandler->behaviour = EnoceanSensorHandler::newSensorBehaviour(aSensorDescriptor, aDevice, aId);
  switch (aSensorDescriptor.behaviourType) {
    case behaviour_sensor: {
      if (aSetDeviceDescription) {
        aDevice->setFunctionDesc(string(aSensorDescriptor.typeText) + " sensor");
        aDevice->setIconInfo("enocean_sensor", true);
      }
      break;
    }
    case behaviour_binaryinput: {
      if (aSetDeviceDescription) {
        aDevice->setFunctionDesc(string(aSensorDescriptor.typeText) + " input");
      }
      break;
    }
    default: {
      break;
    }
  }
  // add channel to device
  aDevice->addChannelHandler(newHandler);
}



// static factory method
DsBehaviourPtr EnoceanSensorHandler::newSensorBehaviour(const EnoceanSensorDescriptor &aSensorDescriptor, DevicePtr aDevice, const char *aId)
{
  // create the behaviour
  switch (aSensorDescriptor.behaviourType) {
    case behaviour_sensor: {
      SensorBehaviourPtr sb = SensorBehaviourPtr(new SensorBehaviour(*aDevice.get(), nonNullCStr(aId)));
      int numBits = (aSensorDescriptor.msBit-aSensorDescriptor.lsBit)+1; // number of bits
      double resolution = (aSensorDescriptor.max-aSensorDescriptor.min) / ((1<<numBits)-1); // units per LSB
      sb->setHardwareSensorConfig((VdcSensorType)aSensorDescriptor.behaviourParam, aSensorDescriptor.usage, aSensorDescriptor.min, aSensorDescriptor.max, resolution, aSensorDescriptor.updateInterval*Second, aSensorDescriptor.aliveSignInterval*Second);
      sb->setGroup(aSensorDescriptor.channelGroup);
      sb->setSensorNameWithRange(aSensorDescriptor.typeText);
      return sb;
    }
    case behaviour_binaryinput: {
      BinaryInputBehaviourPtr bb = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*aDevice.get(),nonNullCStr(aId)));
      bb->setHardwareInputConfig((DsBinaryInputType)aSensorDescriptor.behaviourParam, aSensorDescriptor.usage, true, aSensorDescriptor.updateInterval*Second, aSensorDescriptor.aliveSignInterval*Second);
      bb->setGroup(aSensorDescriptor.channelGroup);
      bb->setHardwareName(aSensorDescriptor.typeText);
      return bb;
    }
    default: {
      break;
    }
  }
  // none
  return DsBehaviourPtr();
}



// handle incoming data from device and extract data for this channel
void EnoceanSensorHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  if (!aEsp3PacketPtr->radioHasTeachInfo()) {
    // only look at non-teach-in packets
    if (sensorChannelDescriptorP && sensorChannelDescriptorP->bitFieldHandler) {
      // call bit field handler, will pass result to behaviour
      handleBitField(*sensorChannelDescriptorP, behaviour, aEsp3PacketPtr->radioUserData(), (int)aEsp3PacketPtr->radioUserDataLength());
      if (sensorChannelDescriptorP->behaviourType==behaviour_binaryinput && sensorChannelDescriptorP->behaviourParam==binInpType_lowBattery) {
        // auto-update global lowBat flag from lowBattery type inputs
        BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(behaviour);
        if (bb && bb->hasDefinedState()) lowBat = bb->getCurrentState()!=0;
      }
      else if (sensorChannelDescriptorP->behaviourType==behaviour_sensor && sensorChannelDescriptorP->behaviourParam==sensorType_supplyVoltage) {
        SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(behaviour);
        if (sb && sb->hasDefinedState()) lowBat = sb->getCurrentValue()<2.6; // assume CR2032 type battery, which goes down to 2V but 2.6 is already indicating "low"
      }
    }
  }
}



string EnoceanSensorHandler::shortDesc()
{
  return EnoceanSensorHandler::sensorDesc(*sensorChannelDescriptorP);
}


string EnoceanSensorHandler::sensorDesc(const EnoceanSensorDescriptor &aSensorDescriptor)
{
  if (aSensorDescriptor.typeText)
    return aSensorDescriptor.typeText;
  return "";
}


#endif // ENABLE_ENOCEAN



