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

#include "enoceanvld.hpp"

#if ENABLE_ENOCEAN

#include "enoceanvdc.hpp"

#include "buttonbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "lightbehaviour.hpp"


using namespace p44;


// MARK: - special extraction functions


/// strange irregular fan speed scale as used in A5-10-01,02,04,07,08 and 09
static void currentClampHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  // extract 8-bit value
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    double value = EnoceanInputs::bitsExtractor(aInputDescriptor, aDataP, aDataSize);
    if (aDataP[0] & 0x40) {
      // divisor by 10 is active
      value = value / 10;
    }
    sb->updateSensorValue(value);
  }
}


/// standard button input handler
static void D2030AButtonHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  if (ButtonBehaviourPtr bb = boost::dynamic_pointer_cast<ButtonBehaviour>(aBehaviour)) {
    uint8_t action = (uint8_t)EnoceanInputs::bitsExtractor(aInputDescriptor, aDataP, aDataSize);
    // special coding
    // - 1 : single click
    // - 2 : double click
    // - 3 : pressed longer
    // - 4 : released
    switch (action) {
      case 1 : bb->injectClick(ct_click_1x); break;
      case 2 : bb->injectClick(ct_click_2x); break;
      case 3 : bb->injectClick(ct_hold_start); break;
      case 4 : bb->injectClick(ct_hold_end); break;
    }
  }
}





// MARK: - mapping table for generic EnoceanInputHandler

using namespace EnoceanInputs;

const p44::EnoceanInputDescriptor enoceanVLDdescriptors[] = {
  // variant,func,type, SD,primarygroup,  channelGroup,                  behaviourType,         behaviourParam,         usage,              min,  max, MSB,     LSB,   updateIv,aliveSignIv, handler,              typeText

  // D2-03-0A Single button with battery indicator
  { 0, 0x03, 0x0A, 0, class_black_joker,  group_yellow_light,            behaviour_button,      buttonElement_center,   usage_room,         0,      1, DB(0,7), DB(0,0),      0,          0, &D2030AButtonHandler,   "button" },
  { 0, 0x03, 0x0A, 0, class_black_joker,  group_yellow_light,            behaviour_sensor,      sensorType_none,        usage_room,         0,    255, DB(1,7), DB(1,0),      0,          0, &batPercSensorHandler,  supplyText },
  // D2-14-30 Multi-Function Smoke, Air quality, Temperature, Humidity sensor
  { 0, 0x14, 0x30, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,         0,     51, DB(3,0), DB(2,1),    100,      40*60, &stdSensorHandler,   tempText },
  { 0, 0x14, 0x30, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_room,         0,  127.5, DB(2,0), DB(1,1),    100,      40*60, &stdSensorHandler,   humText },
  { 0, 0x14, 0x30, 0, class_blue_climate, group_red_security,            behaviour_binaryinput, binInpType_smoke,       usage_room,         0,      1, DB(5,7), DB(5,7),    100,      40*60, &stdInputHandler,    "Smoke Alarm" },
  { 0, 0x14, 0x30, 0, class_blue_climate, group_roomtemperature_control, behaviour_binaryinput, binInpType_lowBattery,  usage_room,         0,      1, DB(4,2), DB(4,2),    100,      40*60, &lowBatInputHandler, lowBatText }, // MSB of 2-bit battery status -> low+Critical report low bat
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


// MARK: - VLD profile variants


static const ProfileVariantEntry profileVariantsVLD[] = {
  // current clamp alternatives
  {  1, 0x00D23201, 0, "two separate current sensor devices", NULL },
  {  1, 0x01D23201, 0, "single device with two current sensors", NULL },
  {  2, 0x00D23202, 0, "three separate current sensor devices", NULL },
  {  2, 0x01D23202, 0, "single device with three current sensors", NULL },
  {  3, 0x00D201FF, 1, "input does not locally control output", "nolocalcontrol" },
  {  3, 0x01D201FF, 1, "input locally controls output", "localcontrol" },
  { 0, 0, 0, NULL, NULL } // terminator
};


// MARK: - EnoceanVLDDevice

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
  if (EEP_PURE(EEP_UNTYPED(aEEProfile))==0xD20100) {
    // D2-01 family of switches and dimmers
    newDev = EnoceanD201XXHandler::newDevice(aVdcP, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
  }
//  else if (EEP_UNTYPED(aEEProfile)==0xD20300) {
//    // D2-03-xx buttons and window handles
//    newDev = EnoceanD203xxHandler::newDevice(aVdcP, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
//  }
  else if (EEP_PURE(aEEProfile)==0xD20601) {
    // Note: Profile has variants (with and without temperature sensor)
    // use specialized handler for output functions of heating valve (valve value, summer/winter, prophylaxis)
    newDev = EnoceanD20601Handler::newDevice(aVdcP, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
  }
  else
  {
    // check table based sensors, might create more than one device
    newDev = EnoceanInputHandler::newDevice(aVdcP, createVLDDeviceFunc, enoceanVLDdescriptors, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
  }
  return newDev;
}


// MARK: - EnoceanD201XXHandler - Electronic Switches and Dimmers with local control

enum {
  switching = (1<<0),
  dimming = (1<<1),
  dimmingConfigurable = (1<<2),
  pilotWire = (1<<3),
  localControl = (1<<4),
  localControlDisable = (1<<5),
  externalControl = (1<<6),
  externalControlType = (1<<7),
  autoOffTimer = (1<<8),
  delayOffTimer = (1<<9),
  taughtInDisable = (1<<10),
  dayNightUI = (1<<11),
  overCurrentReporting = (1<<12),
  overCurrentConfigurable = (1<<13),
  energyMeasurement = (1<<14),
  powerMeasurement = (1<<15),
  measurementRollover = (1<<16),
  measurementAutoscaling = (1<<17),
  measurementConfigurable = (1<<18),
  measurementReportOnQuery = (1<<19),
  measurementAutoReport = (1<<20),
  defaultStateConfigurable = (1<<21),
  errorLevelReporting = (1<<22),
  powerFailureDetection = (1<<23),
  powerFailureDetectionDisable = (1<<24),
  maxDimValue = (1<<25),
  minDimValue = (1<<26),
};
typedef uint32_t D201Features;


// D2-01-xx number of channels and feature matrix

typedef struct {
  int numChannels;
  D201Features features;
} D201Descriptor;

static const int numD201Descriptors = 0x17;
static const D201Descriptor D201descriptors[numD201Descriptors] = {
  { 1, 0x00094011 }, // D2-01-00
  { 1, 0x00000011 }, // D2-01-01
  { 1, 0x00094913 }, // D2-01-02
  { 1, 0x00000013 }, // D2-01-03
  { 1, 0x001AF437 }, // D2-01-04
  { 1, 0x007EFC37 }, // D2-01-05
  { 1, 0x00094001 }, // D2-01-06
  { 1, 0x00000001 }, // D2-01-07
  { 1, 0x007EFC31 }, // D2-01-08
  { 1, 0x007ED417 }, // D2-01-09
  { 1, 0x01A00C31 }, // D2-01-0A
  { 1, 0x01BDCC31 }, // D2-01-0B
  { 1, 0x007EFC39 }, // D2-01-0C
  { 1, 0x00200C31 }, // D2-01-0D
  { 1, 0x003DCC31 }, // D2-01-0E
  { 1, 0x00200FF1 }, // D2-01-0F
  { 2, 0x00094011 }, // D2-01-10
  { 2, 0x00000011 }, // D2-01-11
  { 2, 0x00200FF1 }, // D2-01-12
  { 4, 0x00000011 }, // D2-01-13
  { 8, 0x00000011 }, // D2-01-14
  { 4, 0x00680BF1 }, // D2-01-15
  { 2, 0x06000BF7 }, // D2-01-16
};



EnoceanD201XXHandler::EnoceanD201XXHandler(EnoceanDevice &aDevice) :
  inherited(aDevice),
  overCurrent(false),
  powerFailure(false),
  errorLevel(ok)
{
}


// static factory method
EnoceanDevicePtr EnoceanD201XXHandler::newDevice(
  EnoceanVdc *aVdcP,
  EnoceanAddress aAddress,
  EnoceanSubDevice &aSubDeviceIndex, // current subdeviceindex, factory returns NULL when no device can be created for this subdevice index
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aSendTeachInResponse
) {
  // D2-01-xx - Electronic Switches and Dimmers with local control
  EnoceanDevicePtr newDev; // none so far
  EepType d201type = EEP_TYPE(aEEProfile);
  if (d201type<numD201Descriptors) {
    // a type we know of, get the descriptor
    const D201Descriptor &d201desc = D201descriptors[d201type];
    // each channel corresponds to a subdevice
    if (aSubDeviceIndex<d201desc.numChannels) {
      // create EnoceanVLDDevice
      newDev = EnoceanDevicePtr(new EnoceanD201XXDevice(aVdcP));
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      // is always updateable (no need to wait for incoming data)
      newDev->setAlwaysUpdateable();
      // assign EPP information
      newDev->setEEPInfo(aEEProfile, aEEManufacturer);
      // treat all as generic (black) devices...
      newDev->setColorClass(class_black_joker);
      // ...but always use light behaviour
      newDev->installSettings(DeviceSettingsPtr(new LightDeviceSettings(*newDev)));
      LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*newDev.get()));
      l->setGroupMembership(group_yellow_light, true); // put into light group by default
      // determine features
      if (d201desc.features & dimming) {
        // dimmer
        newDev->setFunctionDesc("dimmer");
        // - configure dimmer behaviour
        l->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, false, -1);
      }
      else {
        // switch only
        newDev->setFunctionDesc("on/off switch");
        // - configure switch behaviour
        l->setHardwareOutputConfig(outputFunction_switch, outputmode_binary, usage_undefined, false, -1);
      }
      // add a channel handler with output behaviour
      EnoceanChannelHandlerPtr d201handler = EnoceanChannelHandlerPtr(new EnoceanD201XXHandler(*newDev.get()));
      d201handler->behaviour = l;
      newDev->addChannelHandler(d201handler);
      // count it
      aSubDeviceIndex++;
    }
  }
  // return device (or empty if none created)
  return newDev;
}



string EnoceanD201XXHandler::shortDesc()
{
  const D201Descriptor &d201desc = D201descriptors[EEP_TYPE(device.getEEProfile())];
  return string_format("%d channel %s", d201desc.numChannels, d201desc.features & dimming ? "dimmer" : "switch");
}


// handle incoming data from device and extract data for this channel
void EnoceanD201XXHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  if (!aEsp3PacketPtr->radioHasTeachInfo()) {
    // only look at non-teach-in packets
    uint8_t *dataP = aEsp3PacketPtr->radioUserData();
    int datasize = (int)aEsp3PacketPtr->radioUserDataLength();
    if (datasize<3) return; // wrong data size
    uint8_t cmd = dataP[0] & 0x0F;
    // check message type
    if (cmd==0x4 && datasize==3) {
      // Actuator Status Response
      // - channel must match
      if ((dataP[1] & 0x1F)!=device.getSubDevice()) return; // not this channel handler
      resendTicket.cancel();
      // - sync current output state
      uint8_t outVal = dataP[2] & 0x7F;
      LightBehaviourPtr l = device.getOutput<LightBehaviour>();
      if (l) {
        l->syncBrightnessFromHardware(outVal);
      }
      else {
        ChannelBehaviourPtr ch = device.getOutput()->getChannelByType(channeltype_default);
        ch->syncChannelValue(outVal);
      }
      // - update error info
      powerFailure = (dataP[0] & 0x40)!=0;
      overCurrent = (dataP[1] & 0x80)!=0;
      errorLevel = (ErrorLevel)((dataP[1]>>5) & 0x03);
    }
    if (syncChannelCB) {
      SimpleCB cb = syncChannelCB;
      syncChannelCB = NULL;
      cb();
    }
  }
}


int EnoceanD201XXHandler::opStateLevel()
{
  if (errorLevel==failure || powerFailure) return 0; // complete failure
  if (errorLevel==warning || overCurrent) return 20; // warning
  return inherited::opStateLevel();
}

string EnoceanD201XXHandler::getOpStateText()
{
  if (powerFailure) return "power failure";
  if (overCurrent) return "overcurrent";
  if (errorLevel==failure) return "failure";
  if (errorLevel==warning) return "warning";
  return inherited::getOpStateText();
}



EnoceanD201XXDevice::EnoceanD201XXDevice(EnoceanVdc *aVdcP) :
  inherited(aVdcP)
{
}


void EnoceanD201XXDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // send a little later to not interfere with teach-ins
  cfgTicket.executeOnce(boost::bind(&EnoceanD201XXDevice::configureD201XX, this), 1*Second);
  // let inherited complete initialisation
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


void EnoceanD201XXDevice::configureD201XX()
{
  ALOG(LOG_INFO, "D2-01-xx: configuring using Actuator Set Local command");
  Esp3PacketPtr packet = Esp3PacketPtr(new Esp3Packet());
  packet->initForRorg(rorg_VLD, 4);
  packet->setRadioDestination(getAddress());
  packet->radioUserData()[0] = 0x02; // CMD 0x1 - Actuator Set Local
  packet->radioUserData()[1] =
  (getSubDevice() & 0x1F) | // Bits 0..4: output channel number (1E & 1F reserved)
  (1<<7) | // Bit7: OC: Overcurrent shut down automatically restarts
  (0<<6) | // Bit6: RO: no explicit overcurrent reset now
  ((EEP_VARIANT(getEEProfile())==1 ? 1 : 0)<<5); // Bit5: LC: local control
  packet->radioUserData()[2] =
  (10<<4) | // Bits 7..4: dim timer 2, medium, use 5sec, 0..15 = 0sec..7.5sec (0.5 sec/digit)
  (15<<0); // Bits 3..0: dim timer 3, slow, use max = 7.5sec, 0..15 = 0sec..7.5sec (0.5 sec/digit)
  packet->radioUserData()[3] =
  (0<<7) | // Bit 7: d/n: day/night, always use "day" for now
  (0<<6) | // Bit 6: PF: disable power failure detection for now
  (2<<4) | // Bits 5..4: default state: 0=off, 1=100% on, 2=previous state, 3=not used
  (1<<0); // Bits 3..0: dim timer 0, fast, use min = 0.5sec, 0..15 = 0sec..7.5sec (0.5 sec/digit)
  sendCommand(packet, NULL);
}




void EnoceanD201XXDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  // standard output behaviour
  if (getOutput()) {
    const D201Descriptor &d201desc = D201descriptors[EEP_TYPE(getEEProfile())];
    bool doApply = false;
    uint8_t percentOn;
    uint8_t dimValue = 0; // 0=immediate change, 1,2,3 = use timer 1,2,3
    LightBehaviourPtr l = getOutput<LightBehaviour>();
    if (l) {
      // light output
      if (l->brightnessNeedsApplying()) {
        percentOn = l->brightnessForHardware();
        if (d201desc.features & dimming) {
          MLMicroSeconds tt = l->transitionTimeToNewBrightness();
  //        if (d201desc.features & dimmingConfigurable) {
  //          // we could set the dimming times
  //          // TODO: implement
  //        }
  //        else
          {
            // just assume fixed dimming times
            if (tt>=1*Minute) dimValue = 3; // use the "slow" timer (dS: 1 Minute)
            else if (tt>=5*Second) dimValue = 2; // use the "medium" timer (dS: 5 Seconds)
            else if (tt>0) dimValue = 1; // use the "fast" timer (dS: 100mS)
          }
        }
        doApply = true;
        l->brightnessApplied();
      }
    }
    else {
      // generic output
      ChannelBehaviourPtr ch = getOutput()->getChannelByType(channeltype_default);
      if (ch->needsApplying()) {
        percentOn = ch->getChannelValueBool() ? 100 : 0;
        doApply = true;
        ch->channelValueApplied();
      }
    }
    if (doApply) {
      updateOutput(percentOn, dimValue);
      // re-send later again when we get no response (ticket gets cancelled when receiving confirmation)
      EnoceanD201XXHandlerPtr c = boost::dynamic_pointer_cast<EnoceanD201XXHandler>(channelForBehaviour(getOutput().get()));
      if (c) {
        c->resendTicket.executeOnce(boost::bind(&EnoceanD201XXDevice::updateOutput, this, percentOn, dimValue), 1*Second);
      }
    }
  }
  inherited::applyChannelValues(aDoneCB, aForDimming);
}


void EnoceanD201XXDevice::updateOutput(uint8_t aPercentOn, uint8_t aDimTimeSelector)
{
  ALOG(LOG_INFO, "D2-01-xx: sending Actuator Set Output command: new value = %d%%", aPercentOn);
  Esp3PacketPtr packet = Esp3PacketPtr(new Esp3Packet());
  packet->initForRorg(rorg_VLD, 3);
  packet->setRadioDestination(getAddress());
  packet->radioUserData()[0] = 0x01; // CMD 0x1 - Actuator Set Output
  packet->radioUserData()[1] =
  (getSubDevice() & 0x1F) | // Bits 0..4: output channel number (1E & 1F reserved)
  ((aDimTimeSelector & 0x07)<<5);
  packet->radioUserData()[2] = aPercentOn; // 0=off, 1..100 = 1..100% on
  sendCommand(packet, NULL);
}


/// synchronize channel values by reading them back from the device's hardware (if possible)
void EnoceanD201XXDevice::syncChannelValues(SimpleCB aDoneCB)
{
  EnoceanD201XXHandlerPtr c = boost::dynamic_pointer_cast<EnoceanD201XXHandler>(channelForBehaviour(getOutput().get()));
  if (c) {
    c->syncChannelCB = aDoneCB;
    // trigger device report
    ALOG(LOG_INFO, "D2-01-xx: sending Actuator Status Query");
    Esp3PacketPtr packet = Esp3PacketPtr(new Esp3Packet());
    packet->initForRorg(rorg_VLD, 2);
    packet->setRadioDestination(getAddress());
    packet->radioUserData()[0] = 0x03; // CMD 0x3 - Actuator Status Query
    packet->radioUserData()[1] = (getSubDevice() & 0x1F); // Bits 0..4: output channel number (1E & 1F reserved)
    sendCommand(packet, NULL);
    return;
  }
  inherited::syncChannelValues(aDoneCB);
}



// MARK: - EnoceanD20601Handler - SODA Window Handle

/// sensor bitfield extractor function and check for validity for D2-06-01 profile
static void D20601SensorHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  uint64_t value = bitsExtractor(aInputDescriptor, aDataP, aDataSize);
  // now pass to behaviour
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    // D20601 values all have the last two values in the bitrange reserved for invalid/notsupported value
    int fieldsize = aInputDescriptor.msBit-aInputDescriptor.lsBit+1;
    uint64_t maxval = (1<<fieldsize)-1;
    if (value==maxval || value==maxval-1)
      sb->invalidateSensorValue(); // not a valid value
    else
      sb->updateEngineeringValue(value); // update the value
  }
}


/// binary input bitfield extractor function and check for validity for D2-06-01 profile
static void D20601InputHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  uint64_t value = bitsExtractor(aInputDescriptor, aDataP, aDataSize);
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
static void D20601TiltedHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  uint64_t value = bitsExtractor(aInputDescriptor, aDataP, aDataSize);
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
static void D20601HandlePosHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP)
{
  uint64_t value = bitsExtractor(aInputDescriptor, aDataP, aDataSize);
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
static const p44::EnoceanInputDescriptor D20601handleposition =
  { 0, 0x06, 0x01, 0, class_black_joker, group_blue_windows, behaviour_binaryinput, binInpType_windowHandle, usage_undefined, 0, 2, DB(7,7), DB(7,4), 100, 40*60, &D20601HandlePosHandler, "Window Handle State" };
static const p44::EnoceanInputDescriptor D20601temperature =
  { 0, 0x06, 0x01, 0, class_black_joker, group_roomtemperature_control, behaviour_sensor, sensorType_temperature, usage_room, -20, 61.6, DB(4,7), DB(4,0), 100, 0, &D20601SensorHandler, tempText };
static const p44::EnoceanInputDescriptor D20601humidity =
  { 0, 0x06, 0x01, 0, class_black_joker, group_roomtemperature_control, behaviour_sensor, sensorType_humidity, usage_room, 0, 127.5, DB(3,7), DB(3,0), 100, 0, &D20601SensorHandler, humText };
static const p44::EnoceanInputDescriptor D20601illumination =
  { 0, 0x06, 0x01, 0, class_black_joker, group_yellow_light, behaviour_sensor, sensorType_illumination, usage_undefined, 0, 65535, DB(2,7), DB(1,0), 100, 0, &D20601SensorHandler, illumText };
static const p44::EnoceanInputDescriptor D20601battery =
  { 0, 0x06, 0x01, 0, class_black_joker, group_black_variable, behaviour_sensor, sensorType_none, usage_undefined, 0, 155, DB(0,7), DB(0,3), 100, 40*60, &D20601SensorHandler, supplyText };
static const p44::EnoceanInputDescriptor D20601burglaryAlarm =
  { 0, 0x06, 0x01, 0, class_red_security, group_red_security, behaviour_binaryinput, binInpType_none, usage_undefined, 0, 1, DB(8,7), DB(8,4), 100, 40*60, &D20601InputHandler, "Burglary alarm" };
static const p44::EnoceanInputDescriptor D20601protectionAlarm =
  { 0, 0x06, 0x01, 0, class_red_security, group_red_security, behaviour_binaryinput, binInpType_none, usage_undefined, 0, 1, DB(8,3), DB(8,0), 100, 0, &D20601InputHandler, "Protection alarm" };
static const p44::EnoceanInputDescriptor D20601motion =
  { 0, 0x06, 0x01, 0, class_black_joker, group_black_variable, behaviour_binaryinput, binInpType_motion, usage_undefined, 0, 1, DB(5,7), DB(5,4), 100, 0, &D20601InputHandler, motionText };
static const p44::EnoceanInputDescriptor D20601tilt =
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
    // create EnoceanVLDDevice
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
      //buttonBhvr->setStateMachineMode(ButtonBehaviour::statemachine_simple); // SODA buttons are too slow in reporting button release -> always detected as hold by standard state machine, so we use the simple one instead
      buttonBhvr->setLongFunctionDelay(800*MilliSecond); // SODA buttons report release not sooner than 500mS -> extend long function delay to 800mS to allow proper click detection
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
      newHandler->behaviour = EnoceanInputHandler::newInputChannelBehaviour(D20601handleposition, newDev, NULL); // automatic id;
      newDev->addChannelHandler(newHandler);
      // - add extra sensors
      newHandler->temperatureSensor = EnoceanInputHandler::newInputChannelBehaviour(D20601temperature, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->temperatureSensor);
      newHandler->humiditySensor = EnoceanInputHandler::newInputChannelBehaviour(D20601humidity, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->humiditySensor);
      newHandler->illuminationSensor = EnoceanInputHandler::newInputChannelBehaviour(D20601illumination, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->illuminationSensor);
      newHandler->batterySensor = EnoceanInputHandler::newInputChannelBehaviour(D20601battery, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->batterySensor);
      // - and input behaviours
      newHandler->burglaryAlarmInput = EnoceanInputHandler::newInputChannelBehaviour(D20601burglaryAlarm, newDev, "burglary"); // specific id
      newDev->addBehaviour(newHandler->burglaryAlarmInput);
      newHandler->protectionAlarmInput = EnoceanInputHandler::newInputChannelBehaviour(D20601protectionAlarm, newDev, "protection"); // specific id
      newDev->addBehaviour(newHandler->protectionAlarmInput);
      newHandler->motionInput = EnoceanInputHandler::newInputChannelBehaviour(D20601motion, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->motionInput);
      newHandler->tiltInput = EnoceanInputHandler::newInputChannelBehaviour(D20601tilt, newDev, "tilted"); // specific id
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
      if (behaviour) handleBitField(D20601handleposition, behaviour, dataP, datasize, this);
      if (temperatureSensor) handleBitField(D20601temperature, temperatureSensor, dataP, datasize, this);
      if (humiditySensor) handleBitField(D20601humidity, humiditySensor, dataP, datasize, this);
      if (illuminationSensor) handleBitField(D20601illumination, illuminationSensor, dataP, datasize, this);
      if (batterySensor) {
        handleBitField(D20601battery, batterySensor, dataP, datasize, this);
        SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(batterySensor);
        if (sb && sb->hasDefinedState()) {
          batPercentage = sb->getCurrentValue();
        }
      }
      if (burglaryAlarmInput) handleBitField(D20601burglaryAlarm, burglaryAlarmInput, dataP, datasize, this);
      if (protectionAlarmInput) handleBitField(D20601protectionAlarm, protectionAlarmInput, dataP, datasize, this);
      if (motionInput) handleBitField(D20601motion, motionInput, dataP, datasize, this);
      if (tiltInput) handleBitField(D20601tilt, tiltInput, dataP, datasize, this);
    }
  }
}


string EnoceanD20601Handler::shortDesc()
{
  return "Multisensor Window Handle";
}



// MARK: - EnoceanD20601ButtonHandler

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
      bb->updateButtonState(true); // pressed
    else if (buttonActivity==2)
      bb->updateButtonState(false); // released
  }
}


string EnoceanD20601ButtonHandler::shortDesc()
{
  return "Window Handle Button";
}


#endif // ENABLE_ENOCEAN
