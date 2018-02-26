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

#include "enoceanvld.hpp"

#if ENABLE_ENOCEAN

#include "enoceanvdc.hpp"

#include "buttonbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "binaryinputbehaviour.hpp"


using namespace p44;


// MARK: ===== special extraction functions


// strange irregular fan speed scale as used in A5-10-01,02,04,07,08 and 09
static void currentClampHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  // extract 8-bit value
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    double value = EnoceanSensors::bitsExtractor(aSensorDescriptor, aDataP, aDataSize);
    if (aDataP[0] & 0x40) {
      // divisor by 10 is active
      value = value / 10;
    }
    sb->updateSensorValue(value);
  }
}


// MARK: ===== sensor mapping table for generic EnoceanSensorHandler

using namespace EnoceanSensors;

const p44::EnoceanSensorDescriptor enoceanVLDdescriptors[] = {
  // variant,func,type, SD,primarygroup,  channelGroup,                  behaviourType,         behaviourParam,         usage,              min,  max, MSB,     LSB,   updateIv,aliveSignIv, handler,              typeText

  // D2-32 AC current clamps
  // D2-32-00: single phase current clamp
  { 0, 0x32, 0x00, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_current,     usage_undefined,    0,  409.6, DB(1,7), DB(0,4),     30,          0, &currentClampHandler, "Current" },
  // D2-32-01: two phase current clamp
  // - separate devices
  { 0, 0x32, 0x01, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_current,     usage_undefined,    0,  409.6, DB(2,7), DB(1,4),     30,          0, &currentClampHandler, "Current1" },
  { 0, 0x32, 0x01, 1, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_current,     usage_undefined,    0,  409.6, DB(1,3), DB(0,0),     30,          0, &currentClampHandler, "Current2" },
  // - both in one device
  { 1, 0x32, 0x01, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_current,     usage_undefined,    0,  409.6, DB(2,7), DB(1,4),     30,          0, &currentClampHandler, "Current1" },
  { 1, 0x32, 0x01, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_current,     usage_undefined,    0,  409.6, DB(1,3), DB(0,0),     30,          0, &currentClampHandler, "Current2" },
  // D2-32-02: three phase current clamp
  // - separate devices
  { 0, 0x32, 0x02, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_current,     usage_undefined,    0,  409.6, DB(4,7), DB(3,4),     30,          0, &currentClampHandler, "Current1" },
  { 0, 0x32, 0x02, 1, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_current,     usage_undefined,    0,  409.6, DB(3,3), DB(2,0),     30,          0, &currentClampHandler, "Current2" },
  { 0, 0x32, 0x02, 2, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_current,     usage_undefined,    0,  409.6, DB(1,7), DB(0,4),     30,          0, &currentClampHandler, "Current3" },
  // - all three in one device
  { 1, 0x32, 0x02, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_current,     usage_undefined,    0,  409.6, DB(4,7), DB(3,4),     30,          0, &currentClampHandler, "Current1" },
  { 1, 0x32, 0x02, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_current,     usage_undefined,    0,  409.6, DB(3,3), DB(2,0),     30,          0, &currentClampHandler, "Current2" },
  { 1, 0x32, 0x02, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_current,     usage_undefined,    0,  409.6, DB(1,7), DB(0,4),     30,          0, &currentClampHandler, "Current3" },

  // terminator
  { 0, 0,    0,    0, class_black_joker,  group_black_variable,          behaviour_undefined, 0, usage_undefined, 0, 0, 0, 0, 0, 0, NULL /* NULL for extractor function terminates list */, NULL },
};


// MARK: ===== VLD profile variants


static const ProfileVariantEntry profileVariantsVLD[] = {
  // current clamp alternatives
  {  1, 0x00D23201, 0, "two separate current sensor devices", NULL },
  {  1, 0x01D23201, 0, "single device with two current sensors", NULL },
  {  2, 0x00D23202, 0, "three separate current sensor devices", NULL },
  {  2, 0x01D23202, 0, "single device with three current sensors", NULL },
  { 0, 0, 0, NULL, NULL } // terminator
};


// MARK: ===== EnoceanVLDDevice

EnoceanVLDDevice::EnoceanVLDDevice(EnoceanVdc *aVdcP) :
  inherited(aVdcP)
{
}


const ProfileVariantEntry *EnoceanVLDDevice::profileVariantsTable()
{
  return profileVariantsVLD;
}


// static device creator function
EnoceanDevicePtr createVLDDeviceFunc(EnoceanVdc *aVdcP)
{
  return EnoceanDevicePtr(new EnoceanVLDDevice(aVdcP));
}


// static factory method
EnoceanDevicePtr EnoceanVLDDevice::newDevice(
  EnoceanVdc *aVdcP,
  EnoceanAddress aAddress,
  EnoceanSubDevice &aSubDeviceIndex,
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aSendTeachInResponse
) {
  EnoceanDevicePtr newDev; // none so far
  // check for specialized handlers for certain profiles first
  if (EEP_PURE(aEEProfile)==0xD20601) {
    // Note: Profile has variants (with and without temperature sensor)
    // use specialized handler for output functions of heating valve (valve value, summer/winter, prophylaxis)
    newDev = EnoceanD20601Handler::newDevice(aVdcP, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
  }
  else
  {
    // check table based sensors, might create more than one device
    newDev = EnoceanSensorHandler::newDevice(aVdcP, createVLDDeviceFunc, enoceanVLDdescriptors, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
  }
  return newDev;
}


// MARK: ===== EnoceanD20601Handler

/// sensor bitfield extractor function and check for validity for D2-06-01 profile
static void D20601SensorHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  uint64_t value = bitsExtractor(aSensorDescriptor, aDataP, aDataSize);
  // now pass to behaviour
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    // D20601 values all have the last two values in the bitrange reserved for invalid/notsupported value
    int fieldsize = aSensorDescriptor.msBit-aSensorDescriptor.lsBit+1;
    uint64_t maxval = (1<<fieldsize)-1;
    if (value==maxval || value==maxval-1)
      sb->invalidateSensorValue(); // not a valid value
    else
      sb->updateEngineeringValue(value); // update the value
  }
}


/// binary input bitfield extractor function and check for validity for D2-06-01 profile
static void D20601InputHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  uint64_t value = bitsExtractor(aSensorDescriptor, aDataP, aDataSize);
  // now pass to behaviour
  if (BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(aBehaviour)) {
    // D20601 binary values use 4-bit fields, with bit 0=signal and 0xE and 0xF indicating invalid/notsupported
    if (value==0xF || value==0xE)
      bb->invalidateInputState(); // not a valid value
    else
      bb->updateInputState(value & 1);
  }
}


/// binary input bitfield extractor function and check for validity for window tilted input in D2-06-01 profile
static void D20601TiltedHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  uint64_t value = bitsExtractor(aSensorDescriptor, aDataP, aDataSize);
  // now pass to behaviour
  if (BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(aBehaviour)) {
    // D20601 binary values use 4-bit fields, with 0=undefined, 1..0xD=state and 0xE and 0xF indicating invalid/notsupported
    if (value==0 || value==0xF || value==0xE)
      bb->invalidateInputState(); // not a valid value
    else
      bb->updateInputState(value==2);
  }
}


/// binary input bitfield extractor function and check for validity for window handle position input in D2-06-01 profile
static void D20601HandlePosHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  uint64_t value = bitsExtractor(aSensorDescriptor, aDataP, aDataSize);
  // now pass to behaviour
  if (BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(aBehaviour)) {
    // D20601 binary values use 4-bit fields, with 0=undefined, 1..0xD=state and 0xE and 0xF indicating invalid/notsupported
    if (value==0 || value==0xF || value==0xE)
      bb->invalidateInputState(); // not a valid value
    else
      bb->updateInputState(value==2 ? 0 : (value==1 ? 2 : 1)); // handle down->closed, handle up->tilted, everything else -> window open
  }
}


// configuration for D2-06-01 sensor channels

// - D2-06-01 sensor telegram
static const p44::EnoceanSensorDescriptor D20601handleposition =
  { 0, 0x06, 0x01, 0, class_black_joker, group_blue_windows, behaviour_binaryinput, binInpType_windowHandle, usage_undefined, 0, 2, DB(7,7), DB(7,4), 100, 40*60, &D20601HandlePosHandler, "Window Handle State" };
static const p44::EnoceanSensorDescriptor D20601temperature =
  { 0, 0x06, 0x01, 0, class_black_joker, group_roomtemperature_control, behaviour_sensor, sensorType_temperature, usage_room, -20, 61.6, DB(4,7), DB(4,0), 100, 40*60, &D20601SensorHandler, tempText };
static const p44::EnoceanSensorDescriptor D20601humidity =
  { 0, 0x06, 0x01, 0, class_black_joker, group_roomtemperature_control, behaviour_sensor, sensorType_humidity, usage_room, 0, 127.5, DB(3,7), DB(3,0), 100, 40*60, &D20601SensorHandler, humText };
static const p44::EnoceanSensorDescriptor D20601illumination =
  { 0, 0x06, 0x01, 0, class_black_joker, group_yellow_light, behaviour_sensor, sensorType_illumination, usage_undefined, 0, 65535, DB(2,7), DB(1,0), 100, 40*60, &D20601SensorHandler, illumText };
static const p44::EnoceanSensorDescriptor D20601battery =
  { 0, 0x06, 0x01, 0, class_black_joker, group_black_variable, behaviour_sensor, sensorType_none, usage_undefined, 0, 155, DB(0,7), DB(0,3), 100, 40*60, &D20601SensorHandler, supplyText };
static const p44::EnoceanSensorDescriptor D20601burglaryAlarm =
  { 0, 0x06, 0x01, 0, class_red_security, group_red_security, behaviour_binaryinput, binInpType_none, usage_undefined, 0, 1, DB(8,7), DB(8,4), 100, 40*60, &D20601InputHandler, "Burglary alarm" };
static const p44::EnoceanSensorDescriptor D20601protectionAlarm =
  { 0, 0x06, 0x01, 0, class_red_security, group_red_security, behaviour_binaryinput, binInpType_none, usage_undefined, 0, 1, DB(8,3), DB(8,0), 100, 40*60, &D20601InputHandler, "Protection alarm" };
static const p44::EnoceanSensorDescriptor D20601motion =
  { 0, 0x06, 0x01, 0, class_black_joker, group_black_variable, behaviour_binaryinput, binInpType_motion, usage_undefined, 0, 1, DB(5,7), DB(5,4), 100, 40*60, &D20601InputHandler, motionText };
static const p44::EnoceanSensorDescriptor D20601tilt =
  { 0, 0x06, 0x01, 0, class_black_joker, group_blue_windows, behaviour_binaryinput, binInpType_none, usage_undefined, 0, 1, DB(7,3), DB(7,0), 100, 40*60, &D20601TiltedHandler, "Window tilted" };


EnoceanD20601Handler::EnoceanD20601Handler(EnoceanDevice &aDevice) :
  inherited(aDevice)
{
}

// static factory method
EnoceanDevicePtr EnoceanD20601Handler::newDevice(
  EnoceanVdc *aVdcP,
  EnoceanAddress aAddress,
  EnoceanSubDevice &aSubDeviceIndex, // current subdeviceindex, factory returns NULL when no device can be created for this subdevice index
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aSendTeachInResponse
) {
  // D2-06-01 - Multisensor Window Handle
  // - e.g. SODA S8
  // create devices
  EnoceanDevicePtr newDev; // none so far
  int numdevices = 3; // subindex 0,1 = buttons, 2 = sensors
  if (aSubDeviceIndex<numdevices) {
    // create EnoceanRPSDevice device
    newDev = EnoceanDevicePtr(new EnoceanVLDDevice(aVdcP));
    // standard device settings without scene table
    newDev->installSettings();
    // assign channel and address
    newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
    // assign EPP information
    newDev->setEEPInfo(aEEProfile, aEEManufacturer);
    if (aSubDeviceIndex<2) {
      // buttons
      newDev->setFunctionDesc("button");
      // set icon name: generic button
      newDev->setIconInfo("button", true);
      // buttons can be used for anything
      newDev->setColorClass(class_black_joker);
      // Create single handler, up button for even aSubDevice, down button for odd aSubDevice
      int bidx = aSubDeviceIndex & 0x01; // 0 or 1
      EnoceanChannelHandlerPtr buttonHandler = EnoceanChannelHandlerPtr(new EnoceanD20601ButtonHandler(*newDev.get(), bidx));
      ButtonBehaviourPtr buttonBhvr = ButtonBehaviourPtr(new ButtonBehaviour(*newDev.get(),"")); // automatic id
      buttonBhvr->setHardwareButtonConfig(0, buttonType_2way, bidx==0 ? buttonElement_down : buttonElement_up, false, 1-bidx, 2); // combined by default, combinable in pairs
      buttonBhvr->setStateMachineMode(ButtonBehaviour::statemachine_simple); // SODA buttons are too slow in reporting button release -> always detected as hold by standard state machine, so we use the simple one instead
      buttonBhvr->setGroup(group_grey_shadow); // pre-configure for shadow
      buttonBhvr->setHardwareName(bidx==0 ? "down key" : "up key");
      buttonHandler->behaviour = buttonBhvr;
      newDev->addChannelHandler(buttonHandler);
      // count it
      // - separate buttons use all indices 0,1,2,3...
      aSubDeviceIndex++;
    }
    else if (aSubDeviceIndex==2) {
      // this sensor carrying device
      newDev->setFunctionDesc("multisensor window handle");
      // set icon name: enocean device
      //newDev->setIconInfo("enocean", true);
      // sensors are not specifically targeted
      newDev->setColorClass(class_black_joker);
      // - create D2-06-01 specific handler (which handles all sensors and inputs, but not buttons)
      EnoceanD20601HandlerPtr newHandler = EnoceanD20601HandlerPtr(new EnoceanD20601Handler(*newDev.get()));
      // - channel-built-in behaviour is main function = window position
      newHandler->behaviour = EnoceanSensorHandler::newSensorBehaviour(D20601handleposition, newDev, NULL); // automatic id;
      newDev->addChannelHandler(newHandler);
      // - add extra sensors
      newHandler->temperatureSensor = EnoceanSensorHandler::newSensorBehaviour(D20601temperature, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->temperatureSensor);
      newHandler->humiditySensor = EnoceanSensorHandler::newSensorBehaviour(D20601humidity, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->humiditySensor);
      newHandler->illuminationSensor = EnoceanSensorHandler::newSensorBehaviour(D20601illumination, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->illuminationSensor);
      newHandler->batterySensor = EnoceanSensorHandler::newSensorBehaviour(D20601battery, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->batterySensor);
      // - and input behaviours
      newHandler->burglaryAlarmInput = EnoceanSensorHandler::newSensorBehaviour(D20601burglaryAlarm, newDev, "burglary"); // specific id
      newDev->addBehaviour(newHandler->burglaryAlarmInput);
      newHandler->protectionAlarmInput = EnoceanSensorHandler::newSensorBehaviour(D20601protectionAlarm, newDev, "protection"); // specific id
      newDev->addBehaviour(newHandler->protectionAlarmInput);
      newHandler->motionInput = EnoceanSensorHandler::newSensorBehaviour(D20601motion, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->motionInput);
      newHandler->tiltInput = EnoceanSensorHandler::newSensorBehaviour(D20601tilt, newDev, "tilted"); // specific id
      newDev->addBehaviour(newHandler->tiltInput);
      // count it
      aSubDeviceIndex++;
    }
  }
  // return device (or empty if none created)
  return newDev;
}



// handle incoming data from device and extract data for this channel
void EnoceanD20601Handler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  if (!aEsp3PacketPtr->radioHasTeachInfo()) {
    // only look at non-teach-in packets
    uint8_t *dataP = aEsp3PacketPtr->radioUserData();
    int datasize = (int)aEsp3PacketPtr->radioUserDataLength();
    if (datasize!=10) return; // wrong data size
    // check message type
    if (dataP[0]==0x00) {
      // Sensor Values message
      if (behaviour) handleBitField(D20601handleposition, behaviour, dataP, datasize);
      if (temperatureSensor) handleBitField(D20601temperature, temperatureSensor, dataP, datasize);
      if (humiditySensor) handleBitField(D20601humidity, humiditySensor, dataP, datasize);
      if (illuminationSensor) handleBitField(D20601illumination, illuminationSensor, dataP, datasize);
      if (batterySensor) handleBitField(D20601battery, batterySensor, dataP, datasize);
      if (burglaryAlarmInput) handleBitField(D20601burglaryAlarm, burglaryAlarmInput, dataP, datasize);
      if (protectionAlarmInput) handleBitField(D20601protectionAlarm, protectionAlarmInput, dataP, datasize);
      if (motionInput) handleBitField(D20601motion, motionInput, dataP, datasize);
      if (tiltInput) handleBitField(D20601tilt, tiltInput, dataP, datasize);
    }
  }
}


string EnoceanD20601Handler::shortDesc()
{
  return "Multisensor Window Handle";
}



// MARK: ===== EnoceanD20601ButtonHandler

EnoceanD20601ButtonHandler::EnoceanD20601ButtonHandler(EnoceanDevice &aDevice, int aSwitchIndex) :
  inherited(aDevice)
{
  switchIndex = aSwitchIndex;
  pressed = false;
}


// device specific radio packet handling
void EnoceanD20601ButtonHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  // BR = down = index 0 = DB(6,7..4)
  // BL = up = index 1 = DB(6,3..0)
  uint8_t buttonActivity = (aEsp3PacketPtr->radioUserData()[3]>>(switchIndex ? 0 : 4)) & 0x0F;
  ButtonBehaviourPtr bb = boost::dynamic_pointer_cast<ButtonBehaviour>(behaviour);
  if (bb) {
    if (buttonActivity==1)
      bb->buttonAction(true); // pressed
    else if (buttonActivity==2)
      bb->buttonAction(false); // released
  }
}


string EnoceanD20601ButtonHandler::shortDesc()
{
  return "Window Handle Button";
}


#endif // ENABLE_ENOCEAN
