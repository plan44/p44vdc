//
//  Copyright (c) 2016-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 7

#include "mystromdevice.hpp"

#if ENABLE_STATIC


// interval for polling current state and power consumption
#define STATE_POLL_INTERVAL (30*Second)

using namespace p44;


MyStromDevice::MyStromDevice(StaticVdc *aVdcP, const string &aDeviceConfig) :
  StaticDevice((Vdc *)aVdcP),
  myStromComm(MainLoop::currentMainLoop())
{
  myStromComm.isMemberVariable();
  // config must be: mystromdevicehost[:token]:(light|relay)[+temp]
  size_t i = aDeviceConfig.rfind(":");
  bool isLight = false;
  bool hasTemp = false;
  if (i!=string::npos) {
    string mode = aDeviceConfig.substr(i+1,string::npos);
    deviceHostName = aDeviceConfig.substr(0,i);
    // check for +temp option
    i = mode.find("+temp");
    if (i!=string::npos) {
      hasTemp = true;
      mode.erase(i, 5);
    }
    // check mode
    isLight = (mode=="light");
  }
  else {
    deviceHostName = aDeviceConfig;
  }
  // now see if hostname includes token
  i = deviceHostName.find(":");
  if (i!=string::npos) {
    // split
    deviceToken = deviceHostName.substr(i+1,string::npos);
    deviceHostName.erase(i,string::npos);
  }
  // configure device now
  if (isLight) {
    // light device
    colorClass = class_yellow_light;
    installSettings(DeviceSettingsPtr(new LightDeviceSettings(*this)));
    LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*this));
    l->setHardwareOutputConfig(outputFunction_switch, outputmode_binary, usage_undefined, false, -1);
    l->setHardwareName("on/off light");
    addBehaviour(l);
  }
  else {
    // general purpose relay
    colorClass = class_black_joker;
    installSettings(DeviceSettingsPtr(new SceneDeviceSettings(*this)));
    OutputBehaviourPtr o = OutputBehaviourPtr(new OutputBehaviour(*this));
    o->setHardwareOutputConfig(outputFunction_switch, outputmode_binary, usage_undefined, false, -1);
    o->setHardwareName("on/off switch");
    o->setGroupMembership(group_black_variable, true); // put into joker group by default
    o->addChannel(ChannelBehaviourPtr(new DigitalChannel(*o, "relay")));
    addBehaviour(o);
  }
  // Power sensor
  powerSensor = SensorBehaviourPtr(new SensorBehaviour(*this,"")); // automatic id
  powerSensor->setHardwareSensorConfig(sensorType_power, usage_undefined, 0, 2300, 0.01, STATE_POLL_INTERVAL, 10*STATE_POLL_INTERVAL, 5*STATE_POLL_INTERVAL);
  powerSensor->setSensorNameWithRange("Power");
  addBehaviour(powerSensor);
  if (hasTemp) {
    // Temperature sensor (V2 devices have it)
    temperatureSensor = SensorBehaviourPtr(new SensorBehaviour(*this,"")); // automatic id
    temperatureSensor->setHardwareSensorConfig(sensorType_temperature, usage_room, -40, 60, 0.1, STATE_POLL_INTERVAL, 10*STATE_POLL_INTERVAL, 5*STATE_POLL_INTERVAL);
    temperatureSensor->setSensorNameWithRange("Temperature");
    addBehaviour(temperatureSensor);
  }
  // dsuid
	deriveDsUid();
}


MyStromDevice::~MyStromDevice()
{
}


bool MyStromDevice::myStromApiQuery(JsonWebClientCB aResponseCB, string aPathAndArgs)
{
  string url = string_format("http://%s/%s", deviceHostName.c_str(), aPathAndArgs.c_str());
  FOCUSLOG("myStromApiQuery: %s", url.c_str());
  return myStromComm.jsonReturningRequest(url.c_str(), aResponseCB, "GET");
}


bool MyStromDevice::myStromApiAction(HttpCommCB aResponseCB, string aPathAndArgs)
{
  string url = string_format("http://%s/%s", deviceHostName.c_str(), aPathAndArgs.c_str());
  FOCUSLOG("myStromApiAction: %s", url.c_str());
  return myStromComm.httpRequest(url.c_str(), aResponseCB, "GET");
}




void MyStromDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // get current state of the switch
  if (!myStromApiQuery(boost::bind(&MyStromDevice::initialStateReceived, this, aCompletedCB, aFactoryReset, _1, _2), "report")) {
    // could not even issue request, init complete
    inherited::initializeDevice(aCompletedCB, aFactoryReset);
  }
}


void MyStromDevice::initialStateReceived(StatusCB aCompletedCB, bool aFactoryReset, JsonObjectPtr aJsonResponse, ErrorPtr aError)
{
  if (Error::isOK(aError) && aJsonResponse) {
    JsonObjectPtr o = aJsonResponse->get("relay");
    if (o) {
      getOutput()->getChannelByIndex(0)->syncChannelValue(o->boolValue() ? 100 : 0);
    }
  }
  // set up regular polling
  sensorPollTicket.executeOnce(boost::bind(&MyStromDevice::sampleState, this), 1*Second);
  // anyway, consider initialized
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


void MyStromDevice::sampleState()
{
  sensorPollTicket.cancel();
  if (!myStromApiQuery(boost::bind(&MyStromDevice::stateReceived, this, _1, _2), "report")) {
    // error, try again later (after pausing 10 normal poll periods)
    sensorPollTicket.executeOnce(boost::bind(&MyStromDevice::sampleState, this), 10*STATE_POLL_INTERVAL);
  }
}


void MyStromDevice::stateReceived(JsonObjectPtr aJsonResponse, ErrorPtr aError)
{
  if (Error::isOK(aError) && aJsonResponse) {
    JsonObjectPtr o = aJsonResponse->get("power");
    if (o && powerSensor) {
      powerSensor->updateSensorValue(o->doubleValue());
    }
    o = aJsonResponse->get("temperature");
    if (o && temperatureSensor) {
      temperatureSensor->updateSensorValue(o->doubleValue());
    }
    o = aJsonResponse->get("relay");
    if (o) {
      getOutput()->getChannelByIndex(0)->syncChannelValueBool(o->boolValue());
    }
  }
  // schedule next poll
  sensorPollTicket.executeOnce(boost::bind(&MyStromDevice::sampleState, this), STATE_POLL_INTERVAL);
}




void MyStromDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  // assume present if we had a recent succesful poll
  aPresenceResultHandler(powerSensor && powerSensor->hasCurrentValue(STATE_POLL_INTERVAL*1.2));
}



void MyStromDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  bool sendState = false;
  bool newState;
  LightBehaviourPtr l = getOutput<LightBehaviour>();
  if (l) {
    // light
    if (l->brightnessNeedsApplying()) {
      // need to update switch state
      sendState = true;
      newState = l->brightnessForHardware()>0;
    }
  }
  else {
    // standard output
    ChannelBehaviourPtr ch = getOutput()->getChannelByIndex(0);
    if (ch->needsApplying()) {
      sendState = true;
      newState = ch->getChannelValueBool();
    }
  }
  if (sendState) {
    myStromApiAction(boost::bind(&MyStromDevice::channelValuesSent, this, aDoneCB, _1, _2), string_format("relay?state=%d", newState ? 1 : 0));
    return;
  }
  // no other operation for this call
  if (aDoneCB) aDoneCB();
  return;
}


void MyStromDevice::channelValuesSent(SimpleCB aDoneCB, string aResponse, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    getOutput()->getChannelByIndex(0)->channelValueApplied();
    // sample the state and power
    sampleState();
  }
  else {
    FOCUSLOG("myStrom API error: %s", aError->description().c_str());
  }
  // confirm done
  if (aDoneCB) aDoneCB();
}



void MyStromDevice::syncChannelValues(SimpleCB aDoneCB)
{
  // query switch state
  myStromApiQuery(boost::bind(&MyStromDevice::channelValuesReceived, this, aDoneCB, _1, _2), "report");
}



void MyStromDevice::channelValuesReceived(SimpleCB aDoneCB, JsonObjectPtr aJsonResponse, ErrorPtr aError)
{
  if (Error::isOK(aError) && aJsonResponse) {
    JsonObjectPtr o = aJsonResponse->get("relay");
    if (o) {
      getOutput()->getChannelByIndex(0)->syncChannelValueBool(o->boolValue());
    }
  }
  // done
  inherited::syncChannelValues(aDoneCB);
}



void MyStromDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  //   UUIDv5 with name = classcontainerinstanceid::mystromhost_xxxx where xxxx=IP address or host name
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = vdcP->vdcInstanceIdentifier();
  s += "::mystromhost_" + deviceHostName;
  dSUID.setNameInSpace(s, vdcNamespace);
}



string MyStromDevice::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- myStrom Switch @ %s", deviceHostName.c_str());
  return s;
}


#endif // ENABLE_STATIC
