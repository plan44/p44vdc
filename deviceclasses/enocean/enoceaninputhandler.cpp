//
//  Copyright (c) 2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "enoceaninputhandler.hpp"

#if ENABLE_ENOCEAN

#include "sensorbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "buttonbehaviour.hpp"

using namespace p44;
using namespace EnoceanInputs;


// MARK: - bit field handlers for EnoceanInputHandler

/// standard bitfield extractor function for sensor behaviours (read only)
uint64_t p44::EnoceanInputs::bitsExtractor(const struct EnoceanInputDescriptor &aInputDescriptor, uint8_t *aDataP, int aDataSize)
{
  // in aDataP, MSB come first
  int msByteIndex = aDataSize-1-(aInputDescriptor.msBit>>3);
  int lsByteIndex = aDataSize-1-(aInputDescriptor.lsBit>>3);
  if (msByteIndex>lsByteIndex || lsByteIndex>=aDataSize) return 0; // bit field specified is not within data
  // collect data
  uint64_t value = 0;
  uint8_t firstBitNo = aInputDescriptor.msBit & 0x07;
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
  uint8_t lastBitNo = aInputDescriptor.lsBit & 0x07;
  uint8_t numBits = firstBitNo+1-lastBitNo;
  uint8_t mask = ((uint16_t)1<<(numBits))-1;
  value = (value<<numBits) | ((aDataP[lsByteIndex]>>lastBitNo) & mask);
  return value;
}


/// standard bitfield extractor function for sensor behaviours
void p44::EnoceanInputs::stdSensorHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  uint64_t value = bitsExtractor(aInputDescriptor, aDataP, aDataSize);
  // now pass to behaviour
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    sb->updateEngineeringValue(value);
  }
}

/// inverted bitfield extractor function (for various temperature sensors)
void p44::EnoceanInputs::invSensorHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  uint64_t value = bitsExtractor(aInputDescriptor, aDataP, aDataSize);
  value ^= (1ll<<(aInputDescriptor.msBit-aInputDescriptor.lsBit+1))-1; // invert all bits
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    sb->updateEngineeringValue(value);
  }
}


/// standard binary input handler
void p44::EnoceanInputs::stdInputHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  bool newState = bitsExtractor(aInputDescriptor, aDataP, aDataSize)>0 ? aInputDescriptor.max : aInputDescriptor.min; // max for set bits, min for cleared bits
  // now pass to behaviour
  BinaryInputBehaviourPtr ib = boost::dynamic_pointer_cast<BinaryInputBehaviour>(aBehaviour);
  if (ib) {
    ib->updateInputState(newState);
  }
}


/// standard button input handler
void p44::EnoceanInputs::stdButtonHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  uint64_t value = bitsExtractor(aInputDescriptor, aDataP, aDataSize);
  // max is the expected value for pressed state
  // min is the expected value for released state
  if (ButtonBehaviourPtr bb = boost::dynamic_pointer_cast<ButtonBehaviour>(aBehaviour)) {
    if (value==aInputDescriptor.max)
      bb->updateButtonState(true);
    else if (value==aInputDescriptor.min)
      bb->updateButtonState(false);
  }
}


/// battery indicator binary input handler
void p44::EnoceanInputs::lowBatInputHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  bool lowBat = bitsExtractor(aInputDescriptor, aDataP, aDataSize)>0 ? aInputDescriptor.max : aInputDescriptor.min; // max for set bits, min for cleared bits
  aChannelP->batPercentage = lowBat ? LOW_BAT_PERCENTAGE : 100; // just bad or fully ok
  // now pass to behaviour
  BinaryInputBehaviourPtr ib = boost::dynamic_pointer_cast<BinaryInputBehaviour>(aBehaviour);
  if (ib) {
    ib->updateInputState(lowBat);
  }
}


void p44::EnoceanInputs::batPercSensorHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  uint64_t value = bitsExtractor(aInputDescriptor, aDataP, aDataSize);
  // now pass to behaviour
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    sb->updateEngineeringValue(value);
    aChannelP->batPercentage = sb->getCurrentValue(); // also update battery percentage
  }
}


void p44::EnoceanInputs::batVoltSensorHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  uint64_t value = bitsExtractor(aInputDescriptor, aDataP, aDataSize);
  // now pass to behaviour
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    sb->updateEngineeringValue(value);
    aChannelP->batPercentage = (sb->getCurrentValue()-LOW_BAT_VOLTAGE_LEVEL)*100/(FULL_BAT_VOLTAGE_LEVEL-LOW_BAT_VOLTAGE_LEVEL)+LOW_BAT_PERCENTAGE;
  }
}



// helper to make sure handler and its parameter always match
void p44::EnoceanInputs::handleBitField(const EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  if (aInputDescriptor.bitFieldHandler) {
    aInputDescriptor.bitFieldHandler(aInputDescriptor, aBehaviour, aDataP, aDataSize, aChannelP);
  }
}


// MARK: - Texts

namespace p44 { namespace EnoceanInputs {

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

  const char *lowBatText = "Low battery";


} }


// MARK: - EnoceanInputHandler

EnoceanInputHandler::EnoceanInputHandler(EnoceanDevice &aDevice) :
  inherited(aDevice),
  sensorChannelDescriptorP(NULL)
{
}



bool EnoceanInputHandler::isAlive()
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
EnoceanDevicePtr EnoceanInputHandler::newDevice(
  EnoceanVdc *aVdcP,
  CreateDeviceFunc aCreateDeviceFunc,
  const EnoceanInputDescriptor *aDescriptorTable,
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
  const EnoceanInputDescriptor *subdeviceDescP = NULL;
  const EnoceanInputDescriptor *descP = aDescriptorTable;
  while (descP->typeText!=NULL) {
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
    addInputChannel(newDev, *subdeviceDescP, firstDescriptorForDevice, NULL); // automatic id
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
void EnoceanInputHandler::addInputChannel(
  EnoceanDevicePtr aDevice,
  const EnoceanInputDescriptor &aInputDescriptor,
  bool aSetDeviceDescription,
  const char *aId
) {
  // create channel handler
  EnoceanInputHandlerPtr newHandler = EnoceanInputHandlerPtr(new EnoceanInputHandler(*aDevice.get()));
  // assign descriptor
  newHandler->sensorChannelDescriptorP = &aInputDescriptor;
  // create the behaviour
  newHandler->behaviour = EnoceanInputHandler::newInputChannelBehaviour(aInputDescriptor, aDevice, aId);
  switch (aInputDescriptor.behaviourType) {
    case behaviour_sensor: {
      if (aSetDeviceDescription) {
        aDevice->setFunctionDesc(string(aInputDescriptor.typeText) + " sensor");
        aDevice->setIconInfo("enocean_sensor", true);
      }
      break;
    }
    case behaviour_binaryinput: {
      if (aSetDeviceDescription) {
        aDevice->setFunctionDesc(string(aInputDescriptor.typeText) + " input");
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
DsBehaviourPtr EnoceanInputHandler::newInputChannelBehaviour(const EnoceanInputDescriptor &aInputDescriptor, DevicePtr aDevice, const char *aId)
{
  // create the behaviour
  switch (aInputDescriptor.behaviourType) {
    case behaviour_sensor: {
      // behaviourParam is VdcSensorType
      SensorBehaviourPtr sb = SensorBehaviourPtr(new SensorBehaviour(*aDevice.get(), nonNullCStr(aId)));
      int numBits = (aInputDescriptor.msBit-aInputDescriptor.lsBit)+1; // number of bits
      double resolution = (aInputDescriptor.max-aInputDescriptor.min) / ((1<<numBits)-1); // units per LSB
      sb->setHardwareSensorConfig(
        (VdcSensorType)aInputDescriptor.behaviourParam, aInputDescriptor.usage,
        aInputDescriptor.min, aInputDescriptor.max, resolution,
        aInputDescriptor.updateInterval*Second,
        aInputDescriptor.aliveSignInterval*Second,
        fmin(aInputDescriptor.updateInterval*18,3600)*Second // limit unchanged value reporting to 1/18 frequency by default (gives 30min for usual 100sec updateinterval)
      );
      sb->setGroup(aInputDescriptor.channelGroup);
      sb->setSensorNameWithRange(aInputDescriptor.typeText);
      return sb;
    }
    case behaviour_binaryinput: {
      // behaviourParam is DsBinaryInputType
      BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*aDevice.get(),nonNullCStr(aId)));
      ib->setHardwareInputConfig(
        (DsBinaryInputType)aInputDescriptor.behaviourParam,
        aInputDescriptor.usage,
        true,
        fabs(aInputDescriptor.updateInterval*Second), // can be negative in descriptor to enable auto-reset
        aInputDescriptor.aliveSignInterval*Second, // can be Never
        aInputDescriptor.updateInterval<0 ? aInputDescriptor.min : -1 // negative updateInterval means autoreset to "min" after update interval
      );
      ib->setGroup(aInputDescriptor.channelGroup);
      ib->setHardwareName(aInputDescriptor.typeText);
      return ib;
    }
    case behaviour_button: {
      ButtonBehaviourPtr bb = ButtonBehaviourPtr(new ButtonBehaviour(*aDevice.get(),nonNullCStr(aId)));
      // behaviourparam is VdcButtonElement. Center denotes single button, up/down denotes pairable rockers
      bb->setHardwareButtonConfig(0,
        aInputDescriptor.behaviourParam==buttonElement_center ? buttonType_single : buttonType_2way, // single if element is "center", otherwise: rocker
        (VdcButtonElement)aInputDescriptor.behaviourParam, // button element
        false, // no local button
        aInputDescriptor.behaviourParam==buttonElement_down ? 1 : 0, // assuming down is the first button with index==0, up second with index==1
        0 // not combinable
      );
      bb->setGroup(aInputDescriptor.channelGroup);
      bb->setHardwareName(aInputDescriptor.typeText);
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
void EnoceanInputHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  if (!aEsp3PacketPtr->radioHasTeachInfo()) {
    // only look at non-teach-in packets
    if (sensorChannelDescriptorP && sensorChannelDescriptorP->bitFieldHandler) {
      // call bit field handler, will pass result to behaviour
      handleBitField(*sensorChannelDescriptorP, behaviour, aEsp3PacketPtr->radioUserData(), (int)aEsp3PacketPtr->radioUserDataLength(), this);
    }
  }
}



string EnoceanInputHandler::shortDesc()
{
  return EnoceanInputHandler::inputDesc(*sensorChannelDescriptorP);
}


string EnoceanInputHandler::inputDesc(const EnoceanInputDescriptor &aInputDescriptor)
{
  if (aInputDescriptor.typeText)
    return aInputDescriptor.typeText;
  return "";
}


#endif // ENABLE_ENOCEAN



