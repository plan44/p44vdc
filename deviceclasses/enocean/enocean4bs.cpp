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

#include "enocean4bs.hpp"

#if ENABLE_ENOCEAN

#include "enoceanvdc.hpp"

#include "sensorbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "outputbehaviour.hpp"
#include "climatecontrolbehaviour.hpp"

using namespace p44;


// MARK: - special extraction functions

// two-range illumination handler, as used in A5-06-01 and A5-06-02
static void illumHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  double value;
  // actual data comes in:
  //  DB(0,0)==0 -> in DB(1), full range / lower resolution
  //  DB(0,0)==1 -> in DB(2), half range / higher resolution
  if (aDataSize<4) return;
  if (aDataP[3-0] & 0x01) {
    // DB(0,0)==1: DB 2 contains low range / higher resolution
    double res = (aSensorDescriptor.max/2 - aSensorDescriptor.min) / 255.0; // units per LSB, half scale (half max)
    value = aSensorDescriptor.min + (double)aDataP[3-2]*res;
  }
  else {
    // DB(0,0)==0: DB 1 contains high range / lower resolution
    double res = (aSensorDescriptor.max - aSensorDescriptor.min*2) / 255.0; // units per LSB, full scale
    value = aSensorDescriptor.min*2 + (double)aDataP[3-1]*res; // starting point is double min!
  }
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    sb->updateSensorValue(value);
  }
}


// three-range illumination handler, as used in A5-06-01 in Eltako FAH60
static void illumHandlerFAH60(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  // DB2==0 -> in DB(3), 0..100lx = 0..255
  if (aDataP[3-2]==0) {
    double value = ((double)aDataP[3-3])*100.0/255.0;
    if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
      sb->updateSensorValue(value);
    }
  }
  else {
    // same as standard A5-06-01
    illumHandler(aSensorDescriptor, aBehaviour, aDataP, aDataSize);
  }
}




// power meter data extraction handler
static void powerMeterHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  // raw value is in DB3.7..DB1.0 (upper 24 bits)
  uint32_t value =
  (aDataP[0]<<16) +
  (aDataP[1]<<8) +
  aDataP[2];
  // scaling is in bits DB0.1 and DB0.0 : 00=scale1, 01=scale10, 10=scale100, 11=scale1000
  int divisor = 1;
  switch (aDataP[3] & 0x03) {
    case 1: divisor = 10; break; // value scale is 0.1kWh or 0.1W per LSB
    case 2: divisor = 100; break; // value scale is 0.01kWh or 0.01W per LSB
    case 3: divisor = 1000; break; // value scale is 0.001kWh (1Wh) or 0.001W (1mW) per LSB
  }
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    // DB0.2 signals which value it is: 0=cumulative (energy), 1=current value (power)
    if (aDataP[3] & 0x04) {
      // power
      if (sb->getSensorType()==sensorType_power) {
        // we're being called for power, and data is power -> update
        sb->updateSensorValue((double)value/divisor);
      }
    }
    else {
      // energy
      if (sb->getSensorType()==sensorType_energy) {
        // we're being called for energy, and data is energy -> update
        sb->updateSensorValue((double)value/divisor);
      }
    }
  }
}


// strange irregular fan speed scale as used in A5-10-01,02,04,07,08 and 09
static void fanSpeedHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  // extract 8-bit value
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    uint8_t value = (uint8_t)EnoceanSensors::bitsExtractor(aSensorDescriptor, aDataP, aDataSize);
    // 255..210 = Auto
    // 209..190 = Speed 0 / OFF
    // 189..165 = Speed 1
    // 164..145 = Speed 2
    // 144..0 = Speed 3 = full speed
    double fanSpeed;
    if (value>=210) fanSpeed = -1; // auto (at full speed, i.e. not limited to lower stage)
    else {
      // get stage
      if (value>=190) fanSpeed = 0; // off
      else if (value>=165) fanSpeed = 1;
      else if (value>=145) fanSpeed = 2;
      else fanSpeed = 3;
      // scale to 0..1
      fanSpeed = fanSpeed/3;
    }
    sb->updateSensorValue(fanSpeed);
  }
}


// window closed (0), open (1), tilted (2) tri-state binary input in A5-14-09/0A
static void windowStateHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  // A5-14-09/0A have 0=closed, 1=tilted, 2=reserved/invalid, 3=open
  uint8_t status = (uint8_t)EnoceanSensors::bitsExtractor(aSensorDescriptor, aDataP, aDataSize);
  if (BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(aBehaviour)) {
    // 00->0 (closed), 01->2 (tilted), 10/11->1 (open)
    bb->updateInputState(status==0 ? 0 : (status==1 ? 2 : 1));
  }
}
// window closed (0), open (1), tilted (2) tri-state binary input in A5-14-09/0A
static void reversedWindowStateHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  // A5-14-09/0A have 0=closed, 1=tilted, 2=reserved/invalid, 3=open
  uint8_t status = (uint8_t)EnoceanSensors::bitsExtractor(aSensorDescriptor, aDataP, aDataSize);
  if (BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(aBehaviour)) {
    // 00->2 (tilted), 01->0 (closed), 10/11->1 (open)
    bb->updateInputState(status==0 ? 2 : (status==1 ? 0 : 1));
  }
}





// two-range illumination sensor in A5-06-05
static void illumA50605Handler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  bool lowrange = aDataP[3-0] & 0x01; // DB0.0 selects range: 0=high range data in DB1, 1=low range data in DB2
  uint16_t raw = lowrange ? aDataP[3-2] : (uint16_t)aDataP[3-1]*2; // raw value in low range scaling = 0..510 = 0..10200 lx
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    // 10200/510 = 20
    sb->updateSensorValue(raw*20);
  }
}


// 0..360 angle handler (makes sure results is always 0..<360
static void angleHandler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  uint32_t value = (uint32_t)EnoceanSensors::bitsExtractor(aSensorDescriptor, aDataP, aDataSize);
  // convert range to degrees
  if (SensorBehaviourPtr sb = boost::dynamic_pointer_cast<SensorBehaviour>(aBehaviour)) {
    double degrees = sb->getMin()+(sb->getResolution()*value);
    // limit to 0..360
    while (degrees<0) degrees+=360;
    while (degrees>=360) degrees-=360;
    sb->updateSensorValue(degrees);
  }
}


// only update sensor when DB0 Bit1 is set
static void condDB0Bit1Handler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  // DB0.1 must be set, otherwise this sensor is not available and value must not be updated
  if (aDataP[3-0] & 0x02) {
    EnoceanSensors::stdSensorHandler(aSensorDescriptor, aBehaviour, aDataP, aDataSize);
  }
}


// only update sensor when DB0 Bit2 is set
static void condDB0Bit2Handler(const struct EnoceanSensorDescriptor &aSensorDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize)
{
  // DB0.2 must be set, otherwise this sensor is not available and value must not be updated
  if (aDataP[3-0] & 0x04) {
    EnoceanSensors::stdSensorHandler(aSensorDescriptor, aBehaviour, aDataP, aDataSize);
  }
}



// MARK: - sensor mapping table for generic EnoceanSensorHandler

using namespace EnoceanSensors;

static const char *vibrationText = "Vibration";
static const char *lockText = "Lock";
static const char *doorText = "Door";
static const char *windowText = "Window open/tilted";

const p44::EnoceanSensorDescriptor enocean4BSdescriptors[] = {
  // variant,func,type, SD,primarygroup,  channelGroup,                  behaviourType,         behaviourParam,         usage,              min,  max,MSB,     LSB,  updateIv,aliveSignIv, handler,     typeText
  // A5-02-xx: Temperature sensors
  // - 40 degree range
  //   -40..0
  { 0, 0x02, 0x01, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -40,    0, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x01, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -40,    0, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   -30..10
  { 0, 0x02, 0x02, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -30,   10, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x02, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -30,   10, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   -20..20
  { 0, 0x02, 0x03, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -20,   20, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x03, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -20,   20, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   -10..30
  { 0, 0x02, 0x04, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -10,   30, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x04, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -10,   30, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   0..40
  { 0, 0x02, 0x05, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x05, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,      0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   10..50
  { 0, 0x02, 0x06, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,         10,   50, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x06, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,     10,   50, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   20..60
  { 0, 0x02, 0x07, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,         20,   60, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x07, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,     20,   60, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   30..70
  { 0, 0x02, 0x08, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,         30,   70, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x08, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,     30,   70, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   40..80
  { 0, 0x02, 0x09, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,         40,   80, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x09, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,     40,   80, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   50..90
  { 0, 0x02, 0x0A, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,         50,   90, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x0A, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,     50,   90, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   60..100
  { 0, 0x02, 0x0B, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,         60,  100, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x0B, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,     60,  100, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  // - 80 degree range
  //   -60..20
  { 0, 0x02, 0x10, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -60,   20, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x10, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -60,   20, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   -50..30
  { 0, 0x02, 0x11, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -50,   30, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x11, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -50,   30, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   -40..40
  { 0, 0x02, 0x12, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -40,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x12, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -40,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   -30..50
  { 0, 0x02, 0x13, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -30,   50, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x13, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -30,   50, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   -20..60
  { 0, 0x02, 0x14, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -20,   60, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x14, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -20,   60, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   -10..70
  { 0, 0x02, 0x15, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -10,   70, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x15, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -10,   70, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   0..80
  { 0, 0x02, 0x16, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   80, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x16, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,      0,   80, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   10..90
  { 0, 0x02, 0x17, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,         10,   90, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x17, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,     10,   90, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   20..100
  { 0, 0x02, 0x18, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,         20,  100, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x18, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,     20,  100, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   30..110
  { 0, 0x02, 0x19, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,         30,  110, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x19, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,     30,  110, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   40..120
  { 0, 0x02, 0x1A, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,         40,  120, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x1A, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,     40,  120, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   50..130
  { 0, 0x02, 0x1B, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,         50,  130, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x1B, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,     50,  130, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  // - 10 bit
  //   -10..40
  { 0, 0x02, 0x20, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -10, 41.2, DB(2,1), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x20, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -10, 41.2, DB(2,1), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  //   -40..60
  { 0, 0x02, 0x30, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -40, 62.3, DB(2,1), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  { 1, 0x02, 0x30, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -40, 62.3, DB(2,1), DB(1,0), 100, 40*60, &invSensorHandler,  tempText },
  // A5-04-xx: Temperature and Humidity
  // - 0..40 degree, e.g. Alpha Sense
  //   - Default profile is indoor
  { 0, 0x04, 0x01, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0, 40.8, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler,  tempText },
  { 0, 0x04, 0x01, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_room,          0,  102, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler,  humText, },
  //   - Alternate profile is outdoor
  { 1, 0x04, 0x01, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,      0, 40.8, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler,  tempText },
  { 1, 0x04, 0x01, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_outdoors,      0,  102, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler,  humText, },
  // - -20..60 degree, e.g. Alpha Sense or Eltako FFT65B
  //   - Default profile is outdoor
  { 0, 0x04, 0x02, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -20, 61.6, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler,  tempText },
  { 0, 0x04, 0x02, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_outdoors,      0,  102, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler,  humText },
  //   - Alternate profile is indoor
  { 1, 0x04, 0x02, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -20, 61.6, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler,  tempText },
  { 1, 0x04, 0x02, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_room,          0,  102, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler,  humText },
  // - -20..60 degree with 10 bit resolution
  //   - Default profile is outdoor
  { 0, 0x04, 0x03, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -20,   60, DB(2,1), DB(1,0), 100, 40*60, &stdSensorHandler,  tempText },
  { 0, 0x04, 0x03, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_outdoors,      0,  100, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler,  humText },
  //   - Alternate profile is indoor
  { 1, 0x04, 0x03, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -20,   60, DB(2,1), DB(1,0), 100, 40*60, &stdSensorHandler,  tempText },
  { 1, 0x04, 0x03, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_room,          0,  100, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler,  humText },
  // A5-06-xx: Light Sensors
  // - A5-06-01 outdoor
  { 0, 0x06, 0x01, 0, class_black_joker,  group_yellow_light,            behaviour_sensor,      sensorType_illumination,usage_outdoors,    300,60000, DB(2,7), DB(1,0), 100, 40*60, &illumHandler,      illumText },
  { 0, 0x06, 0x01, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // - A5-06-01 Eltako FAH60 with low light sensor in DB3, but no supply voltage
  { 1, 0x06, 0x01, 0, class_black_joker,  group_yellow_light,            behaviour_sensor,      sensorType_illumination,usage_outdoors,    300,60000, DB(2,7), DB(1,0), 100, 40*60, &illumHandlerFAH60, illumText },
  // - A5-06-02 indoor
  { 0, 0x06, 0x02, 0, class_black_joker,  group_yellow_light,            behaviour_sensor,      sensorType_illumination,usage_room,          0, 1020, DB(2,7), DB(1,0), 100, 40*60, &illumHandler,      illumText },
  { 0, 0x06, 0x02, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // - A5-06-03 10-bit indoor
  { 0, 0x06, 0x03, 0, class_black_joker,  group_yellow_light,            behaviour_sensor,      sensorType_illumination,usage_room,          0, 1024, DB(2,7), DB(1,6), 100, 40*60, &stdSensorHandler,  illumText },
  { 0, 0x06, 0x03, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // - A5-06-04 courtain wall sensor + temperature
  { 0, 0x06, 0x04, 0, class_black_joker,  group_yellow_light,            behaviour_sensor,      sensorType_illumination,usage_outdoors,      0,65535, DB(2,7), DB(1,0), 100, 40*60, &stdSensorHandler,  illumText },
  { 0, 0x06, 0x04, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_outdoors,    -20,   60, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler,  tempText },
  // - A5-06-05 two range light
  { 0, 0x06, 0x05, 0, class_black_joker,  group_yellow_light,            behaviour_sensor,      sensorType_illumination,usage_room,          0,10200, DB(1,7), DB(1,0), 100, 40*60, &illumA50605Handler,illumText },
  { 0, 0x06, 0x05, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },

  // A5-07-xx: Occupancy Sensor
  // - occupancy sensor
  { 0, 0x07, 0x01, 0, class_black_joker,  group_black_variable,          behaviour_binaryinput, binInpType_motion,      usage_room,          0,    1, DB(1,7), DB(1,7), 100, 40*60, &stdInputHandler,  motionText },
  { 0, 0x07, 0x01, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // - slightly different occupancy sensor
  { 0, 0x07, 0x02, 0, class_black_joker,  group_black_variable,          behaviour_binaryinput, binInpType_motion,      usage_room,          0,    1, DB(0,7), DB(0,7), 100, 40*60, &stdInputHandler,  motionText },
  { 0, 0x07, 0x02, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // - occupancy sensor with illumination sensor
  { 0, 0x07, 0x03, 0, class_black_joker,  group_black_variable,          behaviour_binaryinput, binInpType_motion,      usage_room,          0,    1, DB(0,7), DB(0,7), 100, 40*60, &stdInputHandler,  motionText },
  { 0, 0x07, 0x03, 0, class_black_joker,  group_yellow_light,            behaviour_sensor,      sensorType_illumination,usage_room,          0, 1024, DB(2,7), DB(1,6), 100, 40*60, &stdSensorHandler, illumText },
  { 0, 0x07, 0x03, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },

  // A5-08-01: Light, Temperature and Occupancy sensor
  // - generic EEP
  { 0, 0x08, 0x01, 0, class_black_joker,  group_yellow_light,            behaviour_sensor,      sensorType_illumination,usage_room,          0,  510, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, illumText },
  { 0, 0x08, 0x01, 0, class_black_joker,  group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   51, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler, tempText },
  { 0, 0x08, 0x01, 0, class_black_joker,  group_black_variable,          behaviour_binaryinput, binInpType_motion,      usage_room,          1,    0, DB(0,1), DB(0,1), 100, 40*60, &stdInputHandler,  motionText },
  { 0, 0x08, 0x01, 0, class_black_joker,  group_black_variable,          behaviour_binaryinput, binInpType_presence,    usage_user,          1,    0, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  occupText },
  { 0, 0x08, 0x01, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // - Eltako FABH65S+FBH65B+FBH65S+FBH65TFB (no temperature and presence, extended illumination range)
  { 1, 0x08, 0x01, 0, class_black_joker,  group_yellow_light,            behaviour_sensor,      sensorType_illumination,usage_room,          0, 2048, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, illumText },
  { 1, 0x08, 0x01, 0, class_black_joker,  group_black_variable,          behaviour_binaryinput, binInpType_motion,      usage_room,          1,    0, DB(0,1), DB(0,1), 100, 40*60, &stdInputHandler,  motionText },

  // A5-09-02: CO concentration, Temperature
  // - e.g. enoluz.com
  { 0, 0x09, 0x02, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_gas_CO,      usage_room,          0, 1020, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler,   coText },
  { 0, 0x09, 0x02, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   51, DB(1,7), DB(1,0), 100, 40*60, &condDB0Bit1Handler, tempText },
  { 0, 0x09, 0x02, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },

  // A5-09-04: Humidity, CO2 concentration, Temperature
  // - e.g. enoluz.com
  { 0, 0x09, 0x04, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_room,          0,127.5, DB(3,7), DB(3,0), 100, 40*60, &condDB0Bit2Handler, humText },
  { 0, 0x09, 0x04, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_gas_CO2,     usage_room,          0, 2550, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler,   co2Text },
  { 0, 0x09, 0x04, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   51, DB(1,7), DB(1,0), 100, 40*60, &condDB0Bit1Handler, tempText },

  // A5-10-01: Room Control Panel with Temperature Sensor, Set Point, Fan Speed and Occupancy button
  // Note: fan speed negative range denotes "automatic" (210..255 -> -0.215311..-0)
  // - e.g. Siemens QAX95.4..98.4, Thermokon SR06 LCD 4T type 2
  { 0, 0x10, 0x01, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x01, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, setPointText },
  { 0, 0x10, 0x01, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_fan_speed,   usage_room,         -1,    1, DB(3,7), DB(3,0), 100, 40*60, &fanSpeedHandler,  fanSpeedText },
  { 0, 0x10, 0x01, 0, class_blue_climate, group_black_variable,          behaviour_binaryinput, binInpType_presence,    usage_user,          1,    0, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  occupText },

  // A5-10-02: Room Control Panel with Temperature Sensor, Set Point, Fan Speed and Day/Night Control
  // - e.g. Thermokon Thanos
  { 0, 0x10, 0x02, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x02, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, setPointText },
  { 0, 0x10, 0x02, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_fan_speed,   usage_room,         -1,    1, DB(3,7), DB(3,0), 100, 40*60, &fanSpeedHandler, fanSpeedText },
  { 0, 0x10, 0x02, 0, class_blue_climate, group_roomtemperature_control, behaviour_binaryinput, binInpType_none,        usage_user,          0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  dayNightText },

  // A5-10-03: Room Control Panel with Temperature Sensor and Set Point Control
  // - e.g. Eltako FTR78S, Thermokon SR06 LCD 2T, SR07 P
  { 0, 0x10, 0x03, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x03, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, setPointText },

  // A5-10-04: Room Control Panel with Temperature Sensor, Set Point, Fan Speed
  // - e.g. Thermokon SR06 LCD 4T type 1
  { 0, 0x10, 0x04, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x04, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, setPointText },
  { 0, 0x10, 0x04, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_fan_speed,   usage_room,         -1,    1, DB(3,7), DB(3,0), 100, 40*60, &fanSpeedHandler,  fanSpeedText },

  // A5-10-05: Room Control Panel with Temperature Sensor, Set Point and Occupancy button
  // - e.g. Siemens QAX95.4..98.4, Thermokon SR06 LCD 4T type 3
  { 0, 0x10, 0x05, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x05, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, setPointText },
  { 0, 0x10, 0x05, 0, class_blue_climate, group_black_variable,          behaviour_binaryinput, binInpType_presence,    usage_user,          1,    0, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  occupText },

  // A5-10-06: Room Panel with Temperature Sensor, Set Point Control, Day/Night Control
  { 0, 0x10, 0x06, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x06, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, setPointText },
  { 0, 0x10, 0x06, 0, class_blue_climate, group_roomtemperature_control, behaviour_binaryinput, binInpType_none,        usage_user,          0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  dayNightText },
  // A5-10-06: Variant with Set Point Control as temperature scaled 0..40 degrees
  // - e.g. Eltako FTR55D
  { 1, 0x10, 0x06, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 1, 0x10, 0x06, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_user,          0,   40, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, setPointText },
  { 1, 0x10, 0x06, 0, class_blue_climate, group_roomtemperature_control, behaviour_binaryinput, binInpType_none,        usage_user,          0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  dayNightText },

  // A5-10-07: Room Control Panel with Temperature Sensor, Fan Speed
  { 0, 0x10, 0x07, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x07, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_fan_speed,   usage_room,         -1,    1, DB(3,7), DB(3,0), 100, 40*60, &fanSpeedHandler,  fanSpeedText },

  // A5-10-08: Room Control Panel with Temperature Sensor, Fan Speed and Occupancy button
  { 0, 0x10, 0x08, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x08, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_fan_speed,   usage_room,         -1,    1, DB(3,7), DB(3,0), 100, 40*60, &fanSpeedHandler,  fanSpeedText },
  { 0, 0x10, 0x08, 0, class_blue_climate, group_black_variable,          behaviour_binaryinput, binInpType_presence,    usage_user,          1,    0, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  occupText },

  // A5-10-09: Room Control Panel with Temperature Sensor, Fan Speed and day/night control
  { 0, 0x10, 0x09, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x09, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_fan_speed,   usage_room,         -1,    1, DB(3,7), DB(3,0), 100, 40*60, &fanSpeedHandler,  fanSpeedText },
  { 0, 0x10, 0x09, 0, class_blue_climate, group_roomtemperature_control, behaviour_binaryinput, binInpType_none,        usage_user,          0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  dayNightText },

  // A5-10-0A: Room Control Panel with Temperature Sensor, Set Point and single contact
  { 0, 0x10, 0x0A, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x0A, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, setPointText },
  { 0, 0x10, 0x0A, 0, class_blue_climate, group_black_variable,          behaviour_binaryinput, binInpType_none,        usage_user,          1,    0, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  contactText },

  // A5-10-0B: Temperature Sensor and single contact
  { 0, 0x10, 0x0B, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x0B, 0, class_blue_climate, group_black_variable,          behaviour_binaryinput, binInpType_none,        usage_user,          1,    0, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  contactText },

  // A5-10-0C: Temperature Sensor and Occupancy button
  { 0, 0x10, 0x0C, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x0C, 0, class_blue_climate, group_black_variable,          behaviour_binaryinput, binInpType_presence,    usage_user,          1,    0, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  occupText },

  // A5-10-0D: Temperature Sensor and day/night control
  { 0, 0x10, 0x0D, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x0D, 0, class_blue_climate, group_roomtemperature_control, behaviour_binaryinput, binInpType_none,        usage_user,          0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  dayNightText },


  // A5-10-10: Room Control Panel with Temperature Sensor, Set Point, Humidity and Occupancy button
  // - e.g. Thermokon SR06 LCD 4T rh type 3
  { 0, 0x10, 0x10, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, setPointText },
  { 0, 0x10, 0x10, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_room,          0,  102, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, humText },
  { 0, 0x10, 0x10, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0, 40.8, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler, tempText },
  { 0, 0x10, 0x10, 0, class_blue_climate, group_black_variable,          behaviour_binaryinput, binInpType_presence,    usage_user,          1,    0, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  occupText },

  // A5-10-11: Room Panel with Temperature Sensor, Set Point Control, Humidity and day/night control
  // - e.g. Thermokon Thanos
  { 0, 0x10, 0x11, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, setPointText },
  { 0, 0x10, 0x11, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_room,          0,  102, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, humText },
  { 0, 0x10, 0x11, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0, 40.8, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler, tempText },
  { 0, 0x10, 0x11, 0, class_blue_climate, group_roomtemperature_control, behaviour_binaryinput, binInpType_none,        usage_user,          0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  dayNightText },

  // A5-10-12: Room Panel with Temperature Sensor, Set Point Control, Humidity
  // - e.g. Thermokon SR06 LCD 2T rh
  { 0, 0x10, 0x12, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, setPointText },
  { 0, 0x10, 0x12, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_room,          0,  102, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, humText },
  { 0, 0x10, 0x12, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0, 40.8, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler, tempText },

  // A5-10-13: Room Panel with Temperature Sensor, Humidity and day/night control
  { 0, 0x10, 0x13, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_room,          0,  102, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, humText },
  { 0, 0x10, 0x13, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0, 40.8, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler, tempText },
  { 0, 0x10, 0x13, 0, class_blue_climate, group_black_variable,          behaviour_binaryinput, binInpType_presence,    usage_user,          1,    0, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  occupText },

  // A5-10-14: Room Panel with Temperature Sensor, Humidity and day/night control
  { 0, 0x10, 0x14, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_room,          0,  102, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, humText },
  { 0, 0x10, 0x14, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0, 40.8, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler, tempText },
  { 0, 0x10, 0x14, 0, class_blue_climate, group_roomtemperature_control, behaviour_binaryinput, binInpType_none,        usage_user,          0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  dayNightText },

  // A5-10-15: Room Panel with 10 bit Temperature Sensor, 6 bit set point
  { 0, 0x10, 0x15, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -10, 41.2, DB(2,1), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x15, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(2,7), DB(2,2), 100, 40*60, &stdSensorHandler, setPointText },

  // A5-10-16: Room Panel with 10 bit Temperature Sensor, 6 bit set point and Occupancy button
  { 0, 0x10, 0x16, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -10, 41.2, DB(2,1), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x16, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(2,7), DB(2,2), 100, 40*60, &stdSensorHandler, setPointText },
  { 0, 0x10, 0x16, 0, class_blue_climate, group_black_variable,          behaviour_binaryinput, binInpType_presence,    usage_user,          1,    0, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  occupText },

  // A5-10-17: Room Panel with 10 bit Temperature Sensor and Occupancy button
  { 0, 0x10, 0x17, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,        -10, 41.2, DB(2,1), DB(1,0), 100, 40*60, &invSensorHandler, tempText },
  { 0, 0x10, 0x17, 0, class_blue_climate, group_black_variable,          behaviour_binaryinput, binInpType_presence,    usage_user,          1,    0, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  occupText },

  // A5-10-18..1F seem quite exotic, and Occupancy enable/button bits are curiously swapped in A5-10-19 compared to all other similar profiles (typo or real?)
  //  // INCOMPLETE: A5-10-18: Room Panel with Temperature Sensor, Temperature set point, fan speed and Occupancy button and disable
  //  { 0, 0x10, 0x18, 0, class_blue_climate, group_yellow_light,            behaviour_sensor,      sensorType_illumination,usage_room,          0, 1020, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, illumText },
  //  { 0, 0x10, 0x18, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_user,          0,   40, DB(2,7), DB(2,0), 100, 40*60, &invSensorHandler, tempText },
  //  { 0, 0x10, 0x18, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0,   40, DB(1,7), DB(1,0), 100, 40*60, &invSensorHandler, setPointText },

  // A5-10-20 and A5-10-21 (by MSR/Viessmann) are currently too exotic as well, so left off for now

  // A5-10-22: Room Panel with Temperature Sensor, Humitity, Set Point and Fan control
  // - e.g. Thermokon SR06 LCD 4T rh type 1
  { 0, 0x10, 0x22, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, setPointText },
  { 0, 0x10, 0x22, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_room,          0,  102, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, humText },
  { 0, 0x10, 0x22, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0, 40.8, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler, tempText },
  { 0, 0x10, 0x22, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_fan_speed,   usage_room,  -0.333333,    2, DB(0,7), DB(0,5), 100, 40*60, &stdSensorHandler, fanSpeedText },

  // A5-10-23: Room Panel with Temperature Sensor, Humitity, Set Point, Fan control and Occupancy button
  // - e.g. Thermokon SR06 LCD 4T rh type 2
  { 0, 0x10, 0x23, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_set_point,   usage_user,          0,    1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, setPointText },
  { 0, 0x10, 0x23, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_humidity,    usage_room,          0,  102, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, humText },
  { 0, 0x10, 0x23, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room,          0, 40.8, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler, tempText },
  { 0, 0x10, 0x23, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_fan_speed,   usage_room,  -0.333333,    2, DB(0,7), DB(0,5), 100, 40*60, &stdSensorHandler, fanSpeedText },
  { 0, 0x10, 0x23, 0, class_blue_climate, group_black_variable,          behaviour_binaryinput, binInpType_presence,    usage_user,          0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  occupText },


  // A5-12-01: Energy meter
  // - e.g. Eltako FWZ12-16A
  { 0, 0x12, 0x01, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_power,       usage_room,          0, 2500, DB(3,7), DB(1,0), 600, 40*60, &powerMeterHandler, "Power" },
  { 0, 0x12, 0x01, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_energy,      usage_room,          0, 16e9, DB(3,7), DB(1,0), 600, 40*60, &powerMeterHandler, "Energy" },


  // A5-13-07: Wind Sensor
  { 0, 0x13, 0x07, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_wind_direction, usage_outdoors,22.5,  360, DB(3,3), DB(3,0), 100, 40*60, &angleHandler, "wind direction" },
  { 0, 0x13, 0x07, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_wind_speed,     usage_outdoors,0.45,89.36, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, "wind speed" }, // 1..199.9 mph = 0.45..89.36 m/S
  { 0, 0x13, 0x07, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_gust_speed,     usage_outdoors,0.45,89.36, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler, "max wind (gust) speed" }, // 1..199.9 mph = 0.45..89.36 m/S
  { 0, 0x13, 0x07, 0, class_black_joker,  group_black_variable,          behaviour_binaryinput, binInpType_lowBattery,     usage_outdoors,   0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  "Low Battery" },

  // A5-14: Multi-Function Sensors
  // A5-14-01: Single door/window contact, 0=contact (and window/door) closed, 1=contact (and window/door) open
  { 0, 0x14, 0x01, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_undefined,  0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  contactText },
  { 0, 0x14, 0x01, 0, class_red_security, group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // A5-14-02: Single door/window contact with illumination, 0=contact (and window/door) closed, 1=contact (and window/door) open
  { 0, 0x14, 0x02, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_undefined,  0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  contactText },
  { 0, 0x14, 0x02, 0, class_red_security, group_yellow_light,            behaviour_sensor,      sensorType_illumination,   usage_room,       0, 1020, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, illumText },
  { 0, 0x14, 0x02, 0, class_red_security, group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // A5-14-03: Single door/window contact with vibration, 0=contact (and window/door) closed, 1=contact (and window/door) open
  { 0, 0x14, 0x03, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_undefined,  0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  contactText },
  { 0, 0x14, 0x03, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_undefined,  0,    1, DB(0,1), DB(0,1), 100, 40*60, &stdInputHandler,  vibrationText },
  { 0, 0x14, 0x03, 0, class_red_security, group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // A5-14-04: Single door/window contact with illumination and vibration, 0=contact (and window/door) closed, 1=contact (and window/door) open
  { 0, 0x14, 0x04, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_undefined,  0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  contactText },
  { 0, 0x14, 0x04, 0, class_red_security, group_yellow_light,            behaviour_sensor,      sensorType_illumination,   usage_room,       0, 1020, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, illumText },
  { 0, 0x14, 0x04, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_undefined,  0,    1, DB(0,1), DB(0,1), 100, 40*60, &stdInputHandler,  vibrationText },
  { 0, 0x14, 0x04, 0, class_red_security, group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // A5-14-05: Vibration detector
  { 0, 0x14, 0x05, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_undefined,  0,    1, DB(0,1), DB(0,1), 100, 40*60, &stdInputHandler,  vibrationText },
  { 0, 0x14, 0x05, 0, class_red_security, group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // A5-14-06: Single door/window contact with illumination and vibration, 0=contact (and window/door) closed, 1=contact (and window/door) open
  { 0, 0x14, 0x06, 0, class_red_security, group_yellow_light,            behaviour_sensor,      sensorType_illumination,   usage_room,       0, 1020, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, illumText },
  { 0, 0x14, 0x06, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_undefined,  0,    1, DB(0,1), DB(0,1), 100, 40*60, &stdInputHandler,  vibrationText },
  { 0, 0x14, 0x06, 0, class_red_security, group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // A5-14-07: Dual door contact for door and lock, 0=door closed/locked, 1=door open/unlocked
  { 0, 0x14, 0x07, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_doorOpen,       usage_undefined,  0,    1, DB(0,2), DB(0,2), 100, 40*60, &stdInputHandler,  doorText },
  { 0, 0x14, 0x07, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_undefined,  0,    1, DB(0,1), DB(0,1), 100, 40*60, &stdInputHandler,  lockText },
  { 0, 0x14, 0x07, 0, class_red_security, group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // A5-14-08: Dual door contact for door and lock plus vibration, 0=door closed/locked, 1=door open/unlocked
  { 0, 0x14, 0x08, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_doorOpen,       usage_undefined,  0,    1, DB(0,2), DB(0,2), 100, 40*60, &stdInputHandler,  doorText },
  { 0, 0x14, 0x08, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_undefined,  0,    1, DB(0,1), DB(0,1), 100, 40*60, &stdInputHandler,  lockText },
  { 0, 0x14, 0x08, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_undefined,  0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  vibrationText },
  { 0, 0x14, 0x08, 0, class_red_security, group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // A5-14-09: Window state, 0=closed, 1=open, 2=tilted
  // - standard mount
  { 0, 0x14, 0x09, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_windowHandle,   usage_undefined,  0,    1, DB(0,2), DB(0,1), 100, 40*60, &windowStateHandler, windowText },
  { 0, 0x14, 0x09, 0, class_red_security, group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // - reverse mount (value 2 and 0 swapped)
  { 1, 0x14, 0x09, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_windowHandle,   usage_undefined,  0,    1, DB(0,2), DB(0,1), 100, 40*60, &reversedWindowStateHandler, windowText },
  { 1, 0x14, 0x09, 0, class_red_security, group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // A5-14-0A: Window state + vibration, 0=closed, 1=open, 2=tilted
  // - standard mount
  { 0, 0x14, 0x0A, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_windowHandle,   usage_undefined,  0,    1, DB(0,2), DB(0,1), 100, 40*60, &windowStateHandler, windowText },
  { 0, 0x14, 0x0A, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_undefined,  0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  vibrationText },
  { 0, 0x14, 0x0A, 0, class_red_security, group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },
  // - reverse mount (value 2 and 0 swapped)
  { 1, 0x14, 0x0A, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_windowHandle,   usage_undefined,  0,    1, DB(0,2), DB(0,1), 100, 40*60, &reversedWindowStateHandler, windowText },
  { 1, 0x14, 0x0A, 0, class_red_security, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_undefined,  0,    1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  vibrationText },
  { 1, 0x14, 0x0A, 0, class_red_security, group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,  5.1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler, supplyText },

  // A5-30-03: generic temperature + 4 digital inputs
  // - variant for Afriso water sensor with Wake==0 -> water detected
  { 0, 0x30, 0x03, 0, class_blue_climate, group_black_variable,          behaviour_binaryinput, binInpType_none,           usage_user,       1,    0, DB(1,4), DB(1,4), 100, 40*60, &stdInputHandler,  "Water detected" },
  { 0, 0x30, 0x03, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature,    usage_room,       0,   40, DB(2,7), DB(2,0), 100, 40*60, &invSensorHandler,  tempText },

  // A5-3F-7F: manufacturer specific
  { 0, 0x3F, 0x7F, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_none,           usage_undefined,  0,    1, DB(3,7), DB(3,0), 100, 40*60, &stdSensorHandler,  "undefined" }, // just shows the first byte
  // - Thermokon SR65 3AI - 3 analog inputs 0..10V
  { 1, 0x3F, 0x7F, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,   10, DB(3,7), DB(3,0), 100,  1000, &stdSensorHandler,  "V3" },
  { 1, 0x3F, 0x7F, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,   10, DB(2,7), DB(2,0), 100,  1000, &stdSensorHandler,  "V2" },
  { 1, 0x3F, 0x7F, 0, class_black_joker,  group_black_variable,          behaviour_sensor,      sensorType_supplyVoltage,  usage_undefined,  0,   10, DB(1,7), DB(1,0), 100,  1000, &stdSensorHandler,  "V1" },

  // terminator
  { 0, 0,    0,    0, class_black_joker,  group_black_variable,          behaviour_undefined, 0, usage_undefined, 0, 0, 0, 0, 0, 0, NULL /* NULL for extractor function terminates list */, NULL },
};


// MARK: - 4BS profile variants

static const char *indoorText = "indoor sensor";
static const char *outdoorText = "outdoor sensor";


static const ProfileVariantEntry profileVariants4BS[] = {
  // heating valve alternatives
  {  1, 0x00A52001, 0, "heating valve", NULL },
  {  1, 0x01A52001, 0, "heating valve (with temperature sensor)", NULL },
  {  1, 0x02A52001, 0, "heating valve with binary output adjustment (e.g. MD10-FTL)", NULL },
  {  1, 0x03A52001, 0, "heating valve in self-regulation mode", NULL },
  // room panel alternatives for set point
  {  2, 0x00A51006, 0, "standard profile", NULL },
  {  2, 0x01A51006, 0, "set point interpreted as 0..40°C (e.g. FTR55D)", NULL },
  // weather station alternatives for separated sun sensors
  {  3, 0x01A51301, 0, "weather station device + 3 separate sun sensor devices", NULL },
  {  3, 0x00A51301, 0, "weather station with all sensors in single device", NULL },
  // illumination sensor variants
  {  4, 0x00A50601, 0, "outdoor illumination sensor", NULL },
  {  4, 0x01A50601, 0, "outdoor illumination with low light (e.g. FAH60)", NULL },
  // all temperature sensors have indoor and outdoor variant
  // - 40 degree ranges
  {  5, 0x00A50201, 0, indoorText, NULL },
  {  5, 0x01A50201, 0, outdoorText, NULL },
  {  6, 0x00A50202, 0, indoorText, NULL },
  {  6, 0x01A50202, 0, outdoorText, NULL },
  {  7, 0x00A50203, 0, indoorText, NULL },
  {  7, 0x01A50203, 0, outdoorText, NULL },
  {  8, 0x00A50204, 0, indoorText, NULL },
  {  8, 0x01A50204, 0, outdoorText, NULL },
  {  9, 0x00A50205, 0, indoorText, NULL },
  {  9, 0x01A50205, 0, outdoorText, NULL },
  { 10, 0x00A50206, 0, indoorText, NULL },
  { 10, 0x01A50206, 0, outdoorText, NULL },
  { 11, 0x00A50207, 0, indoorText, NULL },
  { 11, 0x01A50207, 0, outdoorText, NULL },
  { 12, 0x00A50208, 0, indoorText, NULL },
  { 12, 0x01A50208, 0, outdoorText, NULL },
  { 13, 0x00A50209, 0, indoorText, NULL },
  { 13, 0x01A50209, 0, outdoorText, NULL },
  { 14, 0x00A5020A, 0, indoorText, NULL },
  { 14, 0x01A5020A, 0, outdoorText, NULL },
  { 15, 0x00A5020B, 0, indoorText, NULL },
  { 15, 0x01A5020B, 0, outdoorText, NULL },
  // - 80 degree ranges
  { 16, 0x00A50211, 0, indoorText, NULL },
  { 16, 0x01A50211, 0, outdoorText, NULL },
  { 17, 0x00A50212, 0, indoorText, NULL },
  { 17, 0x01A50212, 0, outdoorText, NULL },
  { 18, 0x00A50213, 0, indoorText, NULL },
  { 18, 0x01A50213, 0, outdoorText, NULL },
  { 19, 0x00A50214, 0, indoorText, NULL },
  { 19, 0x01A50214, 0, outdoorText, NULL },
  { 20, 0x00A50215, 0, indoorText, NULL },
  { 20, 0x01A50215, 0, outdoorText, NULL },
  { 21, 0x00A50216, 0, indoorText, NULL },
  { 21, 0x01A50216, 0, outdoorText, NULL },
  { 22, 0x00A50217, 0, indoorText, NULL },
  { 22, 0x01A50217, 0, outdoorText, NULL },
  { 23, 0x00A50218, 0, indoorText, NULL },
  { 23, 0x01A50218, 0, outdoorText, NULL },
  { 24, 0x00A50219, 0, indoorText, NULL },
  { 24, 0x01A50219, 0, outdoorText, NULL },
  { 25, 0x00A5021A, 0, indoorText, NULL },
  { 25, 0x01A5021A, 0, outdoorText, NULL },
  { 26, 0x00A5021B, 0, indoorText, NULL },
  { 26, 0x01A5021B, 0, outdoorText, NULL },
  // - 10 bit
  { 27, 0x00A50220, 0, indoorText, NULL },
  { 27, 0x01A50220, 0, outdoorText, NULL },
  { 28, 0x00A50230, 0, indoorText, NULL },
  { 28, 0x01A50230, 0, outdoorText, NULL },
  // - with humidity
  { 29, 0x00A50401, 0, indoorText, NULL },
  { 29, 0x01A50401, 0, outdoorText, NULL },
  { 29, 0x00A50402, 0, outdoorText, NULL }, // outdoor is default!
  { 29, 0x01A50402, 0, indoorText, NULL },
  { 30, 0x00A50403, 0, outdoorText, NULL }, // outdoor is default!
  { 30, 0x01A50403, 0, indoorText, NULL },
  // heating valve alternatives
  { 31, 0x00A52004, 0, "heating valve", NULL },
  { 31, 0x01A52004, 0, "heating valve (with sensors and setpoint)", NULL },
  // A5-14-09 reverse mount alternative
  { 32, 0x00A51409, 0, "window state - regular mounting position", NULL },
  { 32, 0x01A51409, 0, "window state - upside down mounting position", NULL },
  // A5-14-0A reverse mount alternative
  { 33, 0x00A5140A, 0, "window state - regular mounting position", NULL },
  { 33, 0x01A5140A, 0, "window state - upside down mounting position", NULL },
  // A5-08-01 generic and Eltako versions
  { 34, 0x00A50801, 0, "standard EEP", NULL },
  { 34, 0x01A50801, 0, "Eltako modified version (no temp/presence, extended lux range)", NULL },
  // A5-3F-7F manufacturer specific
  { 35, 0x00A53F7F, 0, "undefined", NULL },
  { 35, 0x01A53F7F, 0, "Thermokon SR65 3AI - 3*0..10V analog inputs", NULL },
  { 0, 0, 0, NULL, NULL } // terminator
};



// MARK: - Enocean4BSDevice


Enocean4BSDevice::Enocean4BSDevice(EnoceanVdc *aVdcP) :
  inherited(aVdcP)
{
}


const ProfileVariantEntry *Enocean4BSDevice::profileVariantsTable()
{
  return profileVariants4BS;
}



void Enocean4BSDevice::sendTeachInResponse()
{
  Esp3PacketPtr responsePacket = Esp3PacketPtr(new Esp3Packet);
  responsePacket->initForRorg(rorg_4BS);
  // TODO: implement other 4BS teach-in variants
  if (EEP_FUNC(getEEProfile())==0x20) {
    // A5-20-xx, just mirror back the learn request's EEP
    responsePacket->set4BSTeachInEEP(getEEProfile());
    // Note: manufacturer not set for now (is 0)
    // Set learn response flags
    //               D[3]
    //   7   6   5   4   3   2   1   0
    //
    //  LRN EEP LRN LRN LRN  x   x   x
    //  typ res res sta bit
    responsePacket->radioUserData()[3] =
      (1<<7) | // LRN type = 1=with EEP
      (1<<6) | // 1=EEP is supported
      (1<<5) | // 1=sender ID stored
      (1<<4) | // 1=is LRN response
      (0<<3); // 0=is LRN packet
    // set destination
    responsePacket->setRadioDestination(getAddress());
    // now send
    LOG(LOG_INFO, "Sending 4BS teach-in response for EEP %06X", EEP_PURE(getEEProfile()));
    sendCommand(responsePacket, NULL);
  }
}



// static device creator function
EnoceanDevicePtr create4BSDeviceFunc(EnoceanVdc *aVdcP)
{
  return EnoceanDevicePtr(new Enocean4BSDevice(aVdcP));
}


// static factory method
EnoceanDevicePtr Enocean4BSDevice::newDevice(
  EnoceanVdc *aVdcP,
  EnoceanAddress aAddress,
  EnoceanSubDevice &aSubDeviceIndex,
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aSendTeachInResponse
) {
  EnoceanDevicePtr newDev; // none so far
  // check for specialized handlers for certain profiles first
  if (EEP_PURE(aEEProfile)==0xA52001) {
    // Note: Profile has variants (with and without temperature sensor)
    newDev = EnoceanA52001Handler::newDevice(aVdcP, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
  }
  else if (EEP_PURE(aEEProfile)==0xA52004) {
    // Note: Profile has variants (with and without sensors)
    newDev = EnoceanA52004Handler::newDevice(aVdcP, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
  }
  else if (EEP_PURE(aEEProfile)==0xA51301) {
    // Note: Profile has variants (single device or with separate light sensors for sun directions)
    newDev = EnoceanA5130XHandler::newDevice(aVdcP, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
  }
  else {
    // check table based sensors, might create more than one device
    newDev = EnoceanSensorHandler::newDevice(aVdcP, create4BSDeviceFunc, enocean4BSdescriptors, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
  }
  return newDev;
}


void Enocean4BSDevice::prepare4BSpacket(Esp3PacketPtr &aOutgoingPacket, uint32_t &a4BSdata)
{
  if (!aOutgoingPacket) {
    aOutgoingPacket = Esp3PacketPtr(new Esp3Packet());
    aOutgoingPacket->initForRorg(rorg_4BS);
    // new packet, start with zero data except for LRN bit (D0.3) which must be set for ALL non-learn data
    a4BSdata = LRN_BIT_MASK;
  }
  else {
    // packet exists, get already collected data to modify
    a4BSdata = aOutgoingPacket->get4BSdata();
  }
}






// MARK: - EnoceanA52001Handler


EnoceanA52001Handler::EnoceanA52001Handler(EnoceanDevice &aDevice) :
  inherited(aDevice),
  serviceState(service_idle),
  lastActualValvePos(50), // assume centered
  lastRequestedValvePos(50) // assume centered
{
}


// static factory method
EnoceanDevicePtr EnoceanA52001Handler::newDevice(
  EnoceanVdc *aVdcP,
  EnoceanAddress aAddress,
  EnoceanSubDevice &aSubDeviceIndex, // current subdeviceindex, factory returns NULL when no device can be created for this subdevice index
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aSendTeachInResponse
) {
  // A5-20-01: heating valve actuator
  // - e.g. thermokon SAB 02 or Kieback+Peter MD15-FTL, MD10-FTL
  // configuration for included sensor channels
  static const p44::EnoceanSensorDescriptor tempSensor =
    { 0, 0x20, 0x01, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room, 0, 40, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler, tempText };
  static const p44::EnoceanSensorDescriptor lowBatInput =
    { 0, 0x20, 0x01, 0, class_blue_climate, group_roomtemperature_control, behaviour_binaryinput, binInpType_lowBattery,  usage_room, 1,  0, DB(2,4), DB(2,4), 100, 40*60, &stdInputHandler,  "Low Battery" };
  // create device
  EnoceanDevicePtr newDev; // none so far
  if (aSubDeviceIndex<1) {
    // only one device
    newDev = EnoceanDevicePtr(new Enocean4BSDevice(aVdcP));
    // valve needs climate control scene table (ClimateControlScene)
    newDev->installSettings(DeviceSettingsPtr(new ClimateDeviceSettings(*newDev)));
    // assign channel and address
    newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
    // assign EPP information
    newDev->setEEPInfo(aEEProfile, aEEManufacturer);
    // is heating
    newDev->setColorClass(class_blue_climate);
    // function
    newDev->setFunctionDesc("heating valve actuator");
    // climate control output (assume possible use for heating and cooling (even if only applying absolute heating level value to valve)
    ClimateControlBehaviourPtr cb = ClimateControlBehaviourPtr(new ClimateControlBehaviour(*newDev.get(), climatedevice_simple, hscapability_heatingAndCooling));
    cb->setGroupMembership(group_roomtemperature_control, true);
    cb->setHardwareOutputConfig(outputFunction_positional, outputmode_gradual, usage_room, false, 0);
    cb->setHardwareName("valve");
    // - create A5-20-01 specific handler for output
    EnoceanA52001HandlerPtr newHandler = EnoceanA52001HandlerPtr(new EnoceanA52001Handler(*newDev.get()));
    newHandler->behaviour = cb;
    newDev->addChannelHandler(newHandler);
    if (EEP_VARIANT(aEEProfile)!=0) {
      // all non-default profiles have the valve sensor enabled -> add built-in temp sensor
      EnoceanSensorHandler::addSensorChannel(newDev, tempSensor, false, NULL); // automatic id
    }
    // report low bat status as a binary input
    EnoceanSensorHandler::addSensorChannel(newDev, lowBatInput, false, NULL); // automatic id
    // A5-20-01 need teach-in response if requested (i.e. if this device creation is caused by learn-in, not reinstantiation from DB)
    if (aSendTeachInResponse) {
      newDev->sendTeachInResponse();
    }
    newDev->setUpdateAtEveryReceive();
    // count it
    aSubDeviceIndex++;
  }
  // return device (or empty if none created)
  return newDev;
}



// handle incoming data from device and extract data for this channel
void EnoceanA52001Handler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  if (!aEsp3PacketPtr->radioHasTeachInfo()) {
    // only look at non-teach-in packets
    if (aEsp3PacketPtr->eepRorg()==rorg_4BS && aEsp3PacketPtr->radioUserDataLength()==4) {
      // only look at 4BS packets of correct length
      // sensor inputs will be checked by separate handlers, check error bits only, most fatal first
      // - check actuator obstructed
      uint32_t data = aEsp3PacketPtr->get4BSdata();
      if ((data & DBMASK(2,0))!=0) {
        HLOG(LOG_ERR, "EnOcean valve error: actuator obstructed");
        behaviour->setHardwareError(hardwareError_overload);
      }
      else if ((data & DBMASK(2,4))==0 && (data & DBMASK(2,5))==0) {
        HLOG(LOG_ERR, "EnOcean valve error: energy storage AND battery are low");
        behaviour->setHardwareError(hardwareError_lowBattery);
      }
      // show general status
      HLOG(LOG_NOTICE,
        "EnOcean valve actual set point: %d%% open\n"
        "- Service %s, Energy input %s, Energy storage %scharged, Battery %s, Cover %s, Sensor %s, Detected window %s, Actuator %s",
        (data>>DB(3,0)) & 0xFF, // get data from DB(3,0..7), range is 0..100% (NOT 0..255!)
        data & DBMASK(2,7) ? "ON" : "off",
        data & DBMASK(2,6) ? "enabled" : "disabled",
        data & DBMASK(2,5) ? "" : "NOT ",
        data & DBMASK(2,4) ? "ok" : "LOW",
        data & DBMASK(2,3) ? "OPEN" : "closed",
        data & DBMASK(2,2) ? "FAILURE" : "ok",
        data & DBMASK(2,1) ? "open" : "closed",
        data & DBMASK(2,0) ? "OBSTRUCTED" : "ok"
      );
    }
  }
}



void EnoceanA52001Handler::collectOutgoingMessageData(Esp3PacketPtr &aEsp3PacketPtr)
{
  ClimateControlBehaviourPtr cb = boost::dynamic_pointer_cast<ClimateControlBehaviour>(behaviour);
  if (cb) {
    // get the right channel
    ChannelBehaviourPtr ch = cb->getChannelByIndex(dsChannelIndex);
    // prepare 4BS packet (create packet if none created already)
    uint32_t data;
    Enocean4BSDevice::prepare4BSpacket(aEsp3PacketPtr, data);
    // check for pending service operation
    ClimateControlBehaviour::ValveService vs = cb->pendingServiceOperation();
    if (vs!=ClimateControlBehaviour::vs_none && serviceState==service_idle) {
      // needs to initiate a prophylaxis cycle (only if not already one running)
      switch(vs) {
        case ClimateControlBehaviour::vs_prophylaxis : serviceState = service_openandclosevalve; break; // open and then close
        case ClimateControlBehaviour::vs_fullyopen : serviceState = service_openvalve; break; // only open
        case ClimateControlBehaviour::vs_fullyclose : serviceState = service_closevalve; break; // only close, like end of open/close
        default: break;
      }
    }
    if (serviceState!=service_idle) {
      // process pending service steps
      // - DB(1,0) set to 1 = service operation
      data |= DBMASK(1,0); // service on
      if (serviceState==service_openandclosevalve || serviceState==service_openvalve) {
        // trigger force full open
        LOG(LOG_NOTICE, "- valve prophylaxis operation: fully opening valve");
        data |= DBMASK(1,5); // service: open
        if (serviceState==service_openandclosevalve) {
          // next is closing
          serviceState = service_closevalve;
          device.needOutgoingUpdate();
        }
        else {
          // already done
          serviceState = service_idle;
          device.needOutgoingUpdate();
        }
      }
      else if (serviceState==service_closevalve) {
        // trigger force fully closed
        LOG(LOG_NOTICE, "- valve prophylaxis operation: fully closing valve");
        data |= DBMASK(1,4); // service: close
        // next is normal operation again
        serviceState = service_idle;
        device.needOutgoingUpdate();
      }
    }
    else {
      // Normal operation
      // - DB(1,0) left 0 = normal operation (not service)
      // - DB(1,1) left 0 = no inverted set value
      // - DB(1,2) leave 0 to send send valve position, set 1 to send set point/current temperature and use internal regulator
      // - DB(3,7)..DB(3,0) is
      //   - if DB(1,2)==0: valve position 0..100% (0..255 is only for temperature set point mode!)
      //   - if DB(1,2)==1: set point 0..40 degree Celsius mapped to 0..255
      // - DB(2,7)..DB(2,0) is current temperature when using built-in regulator (inverse mapping 0..40 -> 255..0)
      if (EEP_VARIANT(device.getEEProfile())==3) {
        // use valve's own regulation
        double currentTemp, setPoint;
        if (cb->getZoneTemperatures(currentTemp, setPoint)) {
          data |= DBMASK(1,2); // SPS, set point for DB3
          // add the set point
          uint8_t b = setPoint/40*255;
          data |= b<<DB(3,0);
          // add the current temperature
          b = 255-currentTemp/40*255; // inverse mapping
          data |= b<<DB(2,0);
          LOG(LOG_NOTICE, "- self regulating mode, current temp = %.1f °C, set point = %.1f °C", currentTemp, setPoint);
        }
        else {
          // no control values available, use last actual valve position (which is initially 50%)
          LOG(LOG_NOTICE, "- In self regulating mode, but control values not (yet) available -> use previous valve position=%d%% open", lastActualValvePos);
          data |= (lastActualValvePos<<DB(3,0)); // insert data into DB(3,0..7)
        }
      }
      else {
        // Note: value is always positive even for cooling, because climateControlBehaviour checks outputfunction and sees this is a unipolar valve
        int8_t newValue = cb->outputValueAccordingToMode(ch->getChannelValue(), ch->getChannelIndex());
        // Still: limit to 0..100 to make sure
        if (newValue<0) newValue = 0;
        else if (newValue>100) newValue=100;
        // Special transformation in case valve is binary
        if (EEP_VARIANT(device.getEEProfile())==2) {
          // this valve can only adjust output by about 4k around the mechanically preset set point
          if (newValue>lastRequestedValvePos) {
            // increase -> open to at least 51%
            LOG(LOG_NOTICE, "- Binary valve: requested set point has increased from %d%% to %d%% -> open to 51%% or more", lastRequestedValvePos, newValue);
            lastRequestedValvePos = newValue;
            if (newValue<=50) newValue = 51;
          }
          else if (newValue<lastRequestedValvePos) {
            // decrease -> close to at least 49%
            LOG(LOG_NOTICE, "- Binary valve: requested set point has decreased from %d%% to %d%% -> close to 49%% or less", lastRequestedValvePos, newValue);
            lastRequestedValvePos = newValue;
            if (newValue>=50) newValue = 49;
          }
          else {
            // no change, just repeat last valve position
            LOG(LOG_NOTICE, "- Binary valve: requested set point has not changed (%d%%) -> send last actual value (%d%%) again", lastRequestedValvePos, lastActualValvePos);
            newValue = lastActualValvePos;
          }
        }
        // remember last actually transmitted value
        lastActualValvePos = newValue;
        // - DB3 is set point with range 0..100 (0..255 is only for temperature set point)
        data |= (newValue<<DB(3,0)); // insert data into DB(3,0..7)
        // - DB(1,3) is summer mode
        LOG(LOG_NOTICE, "- requesting new valve position: %d%% open", newValue);
      }
      if (cb->isClimateControlIdle()) {
        data |= DBMASK(1,3);
        LOG(LOG_NOTICE, "- valve is in IDLE mode (slow updates)");
      }
    }
    // save data
    aEsp3PacketPtr->set4BSdata(data);
    // value from this channel is applied to the outgoing telegram
    ch->channelValueApplied(true); // applied even if channel did not have needsApplying() status before
  }
}



string EnoceanA52001Handler::shortDesc()
{
  return string_format("valve output, 0..100 %%");
}


// MARK: - EnoceanA52004Handler


EnoceanA52004Handler::EnoceanA52004Handler(EnoceanDevice &aDevice) :
  inherited(aDevice),
  serviceState(service_idle),
  lastActualValvePos(50), // assume centered
  lastRequestedValvePos(50) // assume centered
{
}

// configuration for included sensor channels
static const p44::EnoceanSensorDescriptor A52004roomTemp =
  { 0, 0x20, 0x04, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_room, 10, 30, DB(1,7), DB(1,0), 100, 40*60, &stdSensorHandler, tempText };
static const p44::EnoceanSensorDescriptor A52004feedTemp =
  { 0, 0x20, 0x04, 0, class_blue_climate, group_blue_heating,            behaviour_sensor,      sensorType_temperature, usage_undefined, 20, 80, DB(2,7), DB(2,0), 100, 40*60, &stdSensorHandler, "feed temperature" };
static const p44::EnoceanSensorDescriptor A52004setpointTemp =
  { 0, 0x20, 0x04, 0, class_blue_climate, group_roomtemperature_control, behaviour_sensor,      sensorType_temperature, usage_user, 10, 30, DB(2,7), DB(2,0), 5, Never, &stdSensorHandler, setPointText }; // user action quickly forwarded, but not regularily transmitted
static const p44::EnoceanSensorDescriptor A52004lowBatInput =
  { 0, 0x20, 0x04, 0, class_blue_climate, group_roomtemperature_control, behaviour_binaryinput, binInpType_lowBattery,  usage_room, 0,  1, DB(0,0), DB(0,0), 100, 40*60, &stdInputHandler,  "Low Battery" };


// static factory method
EnoceanDevicePtr EnoceanA52004Handler::newDevice(
  EnoceanVdc *aVdcP,
  EnoceanAddress aAddress,
  EnoceanSubDevice &aSubDeviceIndex, // current subdeviceindex, factory returns NULL when no device can be created for this subdevice index
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aSendTeachInResponse
) {
  // A5-20-04: heating valve actuator
  // - e.g. Hora SmartDrive MX aka Eltako TF-FKS
  // create device
  EnoceanDevicePtr newDev; // none so far
  if (aSubDeviceIndex<1) {
    // only one device
    newDev = EnoceanDevicePtr(new Enocean4BSDevice(aVdcP));
    // valve needs climate control scene table (ClimateControlScene)
    newDev->installSettings(DeviceSettingsPtr(new ClimateDeviceSettings(*newDev)));
    // assign channel and address
    newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
    // assign EPP information
    newDev->setEEPInfo(aEEProfile, aEEManufacturer);
    // is heating
    newDev->setColorClass(class_blue_climate);
    // function
    newDev->setFunctionDesc("heating valve actuator");
    // climate control output (assume possible use for heating and cooling (even if only applying absolute heating level value to valve)
    ClimateControlBehaviourPtr cb = ClimateControlBehaviourPtr(new ClimateControlBehaviour(*newDev.get(), climatedevice_simple, hscapability_heatingAndCooling));
    cb->setGroupMembership(group_roomtemperature_control, true);
    cb->setHardwareOutputConfig(outputFunction_positional, outputmode_gradual, usage_room, false, 0);
    cb->setHardwareName("valve");
    // - create A5-20-04 specific handler for output
    EnoceanA52004HandlerPtr newHandler = EnoceanA52004HandlerPtr(new EnoceanA52004Handler(*newDev.get()));
    newHandler->behaviour = cb;
    newDev->addChannelHandler(newHandler);
    if (EEP_VARIANT(aEEProfile)!=0) {
      // all non-default profiles have the sensors enabled
      // - built-in room temperature
      newHandler->roomTemp = EnoceanSensorHandler::newSensorBehaviour(A52004roomTemp, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->roomTemp);
      // - built-in feed temperature
      newHandler->feedTemp = EnoceanSensorHandler::newSensorBehaviour(A52004feedTemp, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->feedTemp);
      // - set point temperature
      newHandler->setpointTemp = EnoceanSensorHandler::newSensorBehaviour(A52004setpointTemp, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->setpointTemp);
    }
    // report low bat status as a binary input
    newHandler->lowBatInput = EnoceanSensorHandler::newSensorBehaviour(A52004lowBatInput, newDev, NULL); // automatic id
    newDev->addBehaviour(newHandler->lowBatInput);
    // A5-20-04 need teach-in response if requested (i.e. if this device creation is caused by learn-in, not reinstantiation from DB)
    if (aSendTeachInResponse) {
      newDev->sendTeachInResponse();
    }
    newDev->setUpdateAtEveryReceive();
    // count it
    aSubDeviceIndex++;
  }
  // return device (or empty if none created)
  return newDev;
}



void EnoceanA52004Handler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  if (!aEsp3PacketPtr->radioHasTeachInfo()) {
    // only look at non-teach-in packets
    uint8_t *dataP = aEsp3PacketPtr->radioUserData();
    int datasize = (int)aEsp3PacketPtr->radioUserDataLength();
    if (aEsp3PacketPtr->eepRorg()==rorg_4BS && datasize==4) {
      // only look at 4BS packets of correct length
      // All sensors need to be checked here, we don't have separate handlers for them
      // because sensor value meaning depends on additional status bits in A5-20-04
      bool lowBat = false;
      bool measurementOn = !ENOBIT(0, 7, dataP, datasize);
      // - check failure
      if (ENOBIT(0, 0, dataP, datasize)) {
        // DB1 transmits failure code
        uint8_t fc = ENOBYTE(1, dataP, datasize) & 0xFF;
        HLOG(LOG_NOTICE, "EnOcean valve A5-20-04 failure code: %d", fc);
        switch (fc) {
          case 18:
            // battery empty
            HLOG(LOG_ERR, "EnOcean valve error: battery is low");
            behaviour->setHardwareError(hardwareError_lowBattery);
            lowBat = true;
            break;
          case 33:
            HLOG(LOG_ERR, "EnOcean valve error: actuator obstructed");
            goto valveErr;
          case 36:
            HLOG(LOG_ERR, "EnOcean valve error: end point detection error");
          valveErr:
            behaviour->setHardwareError(hardwareError_overload);
            break;
        }
      }
      else {
        // DB1 transmits room temperature
        if (roomTemp && measurementOn) EnoceanSensors::handleBitField(A52004roomTemp, roomTemp, dataP, datasize);
      }
      // - update low bat state
      boost::dynamic_pointer_cast<BinaryInputBehaviour>(lowBatInput)->updateInputState(lowBat);
      if (ENOBIT(0, 1, dataP, datasize)) {
        // set point transmitted
        if (setpointTemp) EnoceanSensors::handleBitField(A52004setpointTemp, setpointTemp, dataP, datasize);
      }
      else {
        // feed temperature transmitted
        if (feedTemp && measurementOn) EnoceanSensors::handleBitField(A52004feedTemp, feedTemp, dataP, datasize);
      }
      // show general status
      HLOG(LOG_NOTICE,
        "EnOcean valve actual set point: %d%% open\n"
        "- Buttons %s, Status %s",
        ENOBYTE(3, dataP, datasize), // DB3 = valve position, range is 0..100% (NOT 0..255!)
        ENOBIT(0, 2, dataP, datasize) ? "locked" : "unlocked",
        ENOBIT(0, 0, dataP, datasize) ? "FAILURE" : "ok"
      );
    }
  }
}



void EnoceanA52004Handler::collectOutgoingMessageData(Esp3PacketPtr &aEsp3PacketPtr)
{
  ClimateControlBehaviourPtr cb = boost::dynamic_pointer_cast<ClimateControlBehaviour>(behaviour);
  if (cb) {
    // get the right channel
    ChannelBehaviourPtr ch = cb->getChannelByIndex(dsChannelIndex);
    // prepare 4BS packet (create packet if none created already)
    uint32_t data;
    Enocean4BSDevice::prepare4BSpacket(aEsp3PacketPtr, data);
    // check for pending service operation
    ClimateControlBehaviour::ValveService vs = cb->pendingServiceOperation();
    if (vs!=ClimateControlBehaviour::vs_none && serviceState==service_idle) {
      // needs to initiate a prophylaxis cycle (only if not already one running)
      switch(vs) {
        case ClimateControlBehaviour::vs_prophylaxis : serviceState = service_openandclosevalve; break; // open and then close
        case ClimateControlBehaviour::vs_fullyopen : serviceState = service_openvalve; break; // only open
        case ClimateControlBehaviour::vs_fullyclose : serviceState = service_closevalve; break; // only close, like end of open/close
        default: break;
      }
    }
    if (serviceState!=service_idle) {
      // process pending service steps
      // - DB0..1 = service command 0=no change, 1=open valve, 2=run init, 3=close valve
      if (serviceState==service_openandclosevalve || serviceState==service_openvalve) {
        // trigger force full open
        LOG(LOG_NOTICE, "- valve prophylaxis operation: fully opening valve for 2 min");
        data |= 100<<DB(3,0); // do not use service, just open to 100%
        data |= 3<<DB(1,0); // 2 min
        if (serviceState==service_openandclosevalve) {
          // next is closing
          serviceState = service_closevalve;
          device.needOutgoingUpdate();
        }
        else {
          // already done
          serviceState = service_idle;
          device.needOutgoingUpdate();
        }
      }
      else if (serviceState==service_closevalve) {
        // trigger force fully closed
        LOG(LOG_NOTICE, "- valve prophylaxis operation: fully closing valve for 2 min");
        data |= 0<<DB(3,0); // do not use service, just close to 0%
        data |= 3<<DB(1,0); // 2 min
        // next is normal operation again
        serviceState = service_idle;
        device.needOutgoingUpdate();
      }
    }
    else {
      // Normal operation
      // - wake up cycle: fast in winter, slow in summer
      if (cb->isClimateControlIdle()) {
        data |= 54<<DB(1,0); // Summer: 12 hours
        data |= DBMASK(1,6); // measurement disabled
        LOG(LOG_NOTICE, "- valve is in IDLE mode (12hr wake cycle)");
      }
      else {
        data |= 39<<DB(1,0); // Winter: 20 min
        if (!roomTemp && !feedTemp) {
          // nobody interested in measurements, don't waste battery on performing them
          data |= DBMASK(1,6); // measurement disabled
        }
      }
      // - valve position
      //   Note: value is always positive even for cooling, because climateControlBehaviour checks outputfunction and sees this is a unipolar valve
      int8_t newValue = cb->outputValueAccordingToMode(ch->getChannelValue(), ch->getChannelIndex());
      // Still: limit to 0..100 to make sure
      if (newValue<0) newValue = 0;
      else if (newValue>100) newValue=100;
      data |= newValue<<DB(3,0);
      // - set point (only for displaying it)
      double currentTemp, setPoint;
      if (cb->getZoneTemperatures(currentTemp, setPoint)) {
        if (setPoint<10) setPoint=10;
        else if (setPoint>30) setPoint=30;
        uint8_t sp = (uint8_t)((setPoint-10)/20*255);
        data |= sp<<DB(2,0);
      }
      // display orientation == 0 == standard
      // button lock == 0 == not locked
      LOG(LOG_NOTICE, "- requesting new valve position: %d%% open", newValue);
    }
    // save data
    aEsp3PacketPtr->set4BSdata(data);
    // value from this channel is applied to the outgoing telegram
    ch->channelValueApplied(true); // applied even if channel did not have needsApplying() status before
  }
}



string EnoceanA52004Handler::shortDesc()
{
  return string_format("valve output, 0..100 %%");
}




// MARK: - EnoceanA5130XHandler

// configuration for A5-13-0X sensor channels
// - A5-13-01 telegram
static const p44::EnoceanSensorDescriptor A513lowLightSensor =
  { 0, 0x13, 0x01, 0, class_black_joker, group_black_variable, behaviour_sensor, sensorType_illumination, usage_outdoors, 0, 999, DB(3,7), DB(3,0), 10, 40*60, &stdSensorHandler, illumText };
static const p44::EnoceanSensorDescriptor A513outdoorTemp =
  { 0, 0x13, 0x01, 0, class_black_joker, group_black_variable, behaviour_sensor, sensorType_temperature, usage_outdoors, -40, 80, DB(2,7), DB(2,0), 10*60, 40*60, &stdSensorHandler, tempText };
static const p44::EnoceanSensorDescriptor A513windSpeed =
  { 0, 0x13, 0x01, 0, class_black_joker, group_black_variable, behaviour_sensor, sensorType_wind_speed, usage_outdoors, 0, 70, DB(1,7), DB(1,0), 20, 40*60, &stdSensorHandler, "Wind Speed" };
static const p44::EnoceanSensorDescriptor A513gustSpeed =
  { 0, 0x13, 0x01, 0, class_black_joker, group_black_variable, behaviour_sensor, sensorType_gust_speed, usage_outdoors, 0, 70, DB(1,7), DB(1,0), 3, 40*60, &stdSensorHandler, "Gust Speed" };
static const p44::EnoceanSensorDescriptor A513twilightIndicator =
  { 0, 0x13, 0x01, 0, class_black_joker, group_black_variable, behaviour_binaryinput, binInpType_twilight,  usage_outdoors, 0,  1, DB(0,2), DB(0,2), 30, 40*60, &stdInputHandler,  "Twilight Indicator" };
static const p44::EnoceanSensorDescriptor A513rainIndicator =
  { 0, 0x13, 0x01, 0, class_black_joker, group_black_variable, behaviour_binaryinput, binInpType_rain,  usage_outdoors, 0,  1, DB(0,1), DB(0,1), 30, 40*60, &stdInputHandler,  "Rain indicator" };
// - A5-13-02 telegram
static const p44::EnoceanSensorDescriptor A513sunWest =
  { 0, 0x13, 0x02, 0, class_black_joker, group_black_variable, behaviour_sensor, sensorType_illumination, usage_outdoors, 0, 150000, DB(3,7), DB(3,0), 30, 40*60, &stdSensorHandler, "Sun West" };
static const p44::EnoceanSensorDescriptor A513sunSouth =
  { 0, 0x13, 0x02, 0, class_black_joker, group_black_variable, behaviour_sensor, sensorType_illumination, usage_outdoors, 0, 150000, DB(2,7), DB(2,0), 30, 40*60, &stdSensorHandler, "Sun South" };
static const p44::EnoceanSensorDescriptor A513sunEast =
  { 0, 0x13, 0x02, 0, class_black_joker, group_black_variable, behaviour_sensor, sensorType_illumination, usage_outdoors, 0, 150000, DB(1,7), DB(1,0), 30, 40*60, &stdSensorHandler, "Sun East" };

EnoceanA5130XHandler::EnoceanA5130XHandler(EnoceanDevice &aDevice) :
  inherited(aDevice),
  broken(false)
{
}


// static factory method
EnoceanDevicePtr EnoceanA5130XHandler::newDevice(
  EnoceanVdc *aVdcP,
  EnoceanAddress aAddress,
  EnoceanSubDevice &aSubDeviceIndex, // current subdeviceindex, factory returns NULL when no device can be created for this subdevice index
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aSendTeachInResponse
) {
  // A5-13-01..06 (actually used 01,02): environmental sensor
  // - e.g. Eltako Multisensor MS with FWS61
  // create device
  EnoceanDevicePtr newDev; // none so far
  bool separateSunSensors = EEP_VARIANT(aEEProfile)==1;
  int numdevices = separateSunSensors ? 4 : 1;
  if (aSubDeviceIndex<numdevices) {
    // only one device
    newDev = EnoceanDevicePtr(new Enocean4BSDevice(aVdcP));
    // sensor only, standard settings without scene table
    newDev->installSettings();
    // assign channel and address
    newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
    // assign EPP information
    newDev->setEEPInfo(aEEProfile, aEEManufacturer);
    // is joker (AKM type)
    newDev->setColorClass(class_black_joker);
    // - create A5-13-0X specific handler (which handles all sensors)
    EnoceanA5130XHandlerPtr newHandler = EnoceanA5130XHandlerPtr(new EnoceanA5130XHandler(*newDev.get()));
    // Now add functionality depending on subdevice index
    if (aSubDeviceIndex==0) {
      // this is the main device
      newDev->setFunctionDesc("environmental multisensor");
      // - Add channel-built-in behaviour: low light measurement at dawn and dusk (below 1000lx)
      newHandler->behaviour = EnoceanSensorHandler::newSensorBehaviour(A513lowLightSensor, newDev, NULL); // automatic id
      // - register the handler and the default behaviour
      newDev->addChannelHandler(newHandler);
      // - Add extra behaviours for A5-13-01
      newHandler->outdoorTemp = EnoceanSensorHandler::newSensorBehaviour(A513outdoorTemp, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->outdoorTemp);
      newHandler->windSpeed = EnoceanSensorHandler::newSensorBehaviour(A513windSpeed, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->windSpeed);
      newHandler->gustSpeed = EnoceanSensorHandler::newSensorBehaviour(A513gustSpeed, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->gustSpeed);
      newHandler->twilightIndicator = EnoceanSensorHandler::newSensorBehaviour(A513twilightIndicator, newDev, "twilight"); // is low light (dawn, dusk) below 1000lx
      newDev->addBehaviour(newHandler->twilightIndicator);
      newHandler->rainIndicator = EnoceanSensorHandler::newSensorBehaviour(A513rainIndicator, newDev, NULL); // automatic id
      newDev->addBehaviour(newHandler->rainIndicator);
      // sub sensors in same device?
      if (!separateSunSensors) {
        // - Add extra behaviours for A5-13-02
        newHandler->sunWest = EnoceanSensorHandler::newSensorBehaviour(A513sunWest, newDev, "sun_west");
        newDev->addBehaviour(newHandler->sunWest);
        newHandler->sunSouth = EnoceanSensorHandler::newSensorBehaviour(A513sunSouth, newDev, "sun_south");
        newDev->addBehaviour(newHandler->sunSouth);
        newHandler->sunEast = EnoceanSensorHandler::newSensorBehaviour(A513sunEast, newDev, "sun_east");
        newDev->addBehaviour(newHandler->sunEast);
      }
    }
    else if (aSubDeviceIndex==1) {
      // this is a sun direction sensor
      newDev->setFunctionDesc("sun west sensor");
      newHandler->sunWest = EnoceanSensorHandler::newSensorBehaviour(A513sunWest, newDev, "sun_west");
      newDev->addChannelHandler(newHandler);
      newDev->addBehaviour(newHandler->sunWest);
    }
    else if (aSubDeviceIndex==2) {
      // this is a sun direction sensor
      newDev->setFunctionDesc("sun south sensor");
      newHandler->sunSouth = EnoceanSensorHandler::newSensorBehaviour(A513sunSouth, newDev, "sun_south");
      newDev->addChannelHandler(newHandler);
      newDev->addBehaviour(newHandler->sunSouth);
    }
    else if (aSubDeviceIndex==3) {
      // this is a sun direction sensor
      newDev->setFunctionDesc("sun east sensor");
      newHandler->sunEast = EnoceanSensorHandler::newSensorBehaviour(A513sunEast, newDev, "sun_east");
      newDev->addChannelHandler(newHandler);
      newDev->addBehaviour(newHandler->sunEast);
    }
    // count it
    aSubDeviceIndex++;
  }
  // return device (or empty if none created)
  return newDev;
}


int EnoceanA5130XHandler::opStateLevel()
{
  if (broken) return 0; // complete failure
  return inherited::opStateLevel();
}

string EnoceanA5130XHandler::getOpStateText()
{
  if (broken) return "Sensor disconnected";
  return inherited::getOpStateText();
}


// handle incoming data from device and extract data for this channel
void EnoceanA5130XHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  if (!aEsp3PacketPtr->radioHasTeachInfo()) {
    // only look at non-teach-in packets
    uint8_t *dataP = aEsp3PacketPtr->radioUserData();
    int datasize = (int)aEsp3PacketPtr->radioUserDataLength();
    if (datasize!=4) return; // wrong data size
    // - check identifier in DB0.7..DB0.4 to see what info we got
    uint8_t identifier = (dataP[3]>>4) & 0x0F;
    bool nowBroken = false;
    switch (identifier) {
      case 1:
        // 00 00 FF 1A
        if (dataP[0]==0 && dataP[1]==0 && dataP[2]==0xFF && (dataP[3]&0x02)!=0) {
          // 00 00 FF 1A = 0lux, -40 degree C, 70km/h wind, rain -> connection to sensor broken
          nowBroken = true;
        }
        else {
          // A5-13-01
          nowBroken = false;
          if (behaviour) handleBitField(A513lowLightSensor, behaviour, dataP, datasize);
          if (outdoorTemp) handleBitField(A513outdoorTemp, outdoorTemp, dataP, datasize);
          if (windSpeed) handleBitField(A513windSpeed, windSpeed, dataP, datasize);
          if (gustSpeed) handleBitField(A513gustSpeed, gustSpeed, dataP, datasize);
          if (twilightIndicator) handleBitField(A513twilightIndicator, twilightIndicator, dataP, datasize);
          if (rainIndicator) handleBitField(A513rainIndicator, rainIndicator, dataP, datasize);
        }
        break;
      case 2:
        // A5-13-02
        if (!broken) {
          if (sunWest) handleBitField(A513sunWest, sunWest, dataP, datasize);
          if (sunSouth) handleBitField(A513sunSouth, sunSouth, dataP, datasize);
          if (sunEast) handleBitField(A513sunEast, sunEast, dataP, datasize);
        }
        break;
      default:
        // A5-13-03..06 are not supported
        break;
    }
    if (nowBroken!=broken) {
      broken = nowBroken;
      VdcHardwareError e = broken ? hardwareError_openCircuit : hardwareError_none;
      if (behaviour) behaviour->setHardwareError(e);
      if (outdoorTemp) outdoorTemp->setHardwareError(e);
      if (windSpeed) windSpeed->setHardwareError(e);
      if (gustSpeed) gustSpeed->setHardwareError(e);
      if (twilightIndicator) twilightIndicator->setHardwareError(e);
      if (rainIndicator) rainIndicator->setHardwareError(e);
      if (sunWest) sunWest->setHardwareError(e);
      if (sunSouth) sunSouth->setHardwareError(e);
      if (sunEast) sunEast->setHardwareError(e);
    }
    // re-validate all sensors whenever we get any radio packet and not broken
    if (!broken) {
      if (behaviour) behaviour->revalidateState();
      if (outdoorTemp) outdoorTemp->revalidateState();
      if (windSpeed) windSpeed->revalidateState();
      if (gustSpeed) gustSpeed->revalidateState();
      if (twilightIndicator) twilightIndicator->revalidateState();
      if (rainIndicator) rainIndicator->revalidateState();
      if (sunWest) sunWest->revalidateState();
      if (sunSouth) sunSouth->revalidateState();
      if (sunEast) sunEast->revalidateState();
    }
  }
}



string EnoceanA5130XHandler::shortDesc()
{
  return string_format("Dawn/Temp/Wind/Rain/Sun outdoor sensor");
}


#endif // ENABLE_ENOCEAN
