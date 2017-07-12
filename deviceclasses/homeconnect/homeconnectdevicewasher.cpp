//
//  Copyright (c) 2017 digitalSTROM.org, Zurich, Switzerland
//
//  Author: Pawel Kochanowski <pawel.kochanowski@digitalstrom.com>
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

#include "homeconnectdevicewasher.hpp"

#if ENABLE_HOMECONNECT

namespace p44 {

HomeConnectDeviceWasher::HomeConnectDeviceWasher(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
    inherited(aVdcP, aHomeApplicanceInfoRecord)
{

}

HomeConnectDeviceWasher::~HomeConnectDeviceWasher()
{
  // TODO Auto-generated destructor stub
}

const char* HomeConnectDeviceWasher::toString(Temperature aTemperature)
{
  switch(aTemperature)
  {
    case temperature_Cold : return "Cold";
    case temperature_GC20 : return "GC20";
    case temperature_GC30 : return "GC30";
    case temperature_GC40 : return "GC40";
    case temperature_GC50 : return "GC50";
    case temperature_GC60 : return "GC60";
    case temperature_GC70 : return "GC70";
    case temperature_GC80 : return "GC80";
    case temperature_GC90 : return "GC90";
  }
  return "";
}

const char* HomeConnectDeviceWasher::toString(SpinSpeed aSpinSpeed)
{
  switch(aSpinSpeed)
  {
    case spinSpeed_Off :     return "Off";
    case spinSpeed_RPM400 :  return "RPM400";
    case spinSpeed_RPM600 :  return "RPM600";
    case spinSpeed_RPM800 :  return "RPM800";
    case spinSpeed_RPM1000 : return "RPM1000";
    case spinSpeed_RPM1200 : return "RPM1200";
    case spinSpeed_RPM1400 : return "RPM1400";
    case spinSpeed_RPM1600 : return "RPM1600";
  }
  return "";
}



bool HomeConnectDeviceWasher::configureDevice()
{
  // configure operation mode
  OperationModeConfiguration omConfig = { 0 };
  omConfig.hasInactive = false;
  omConfig.hasReady = true;
  omConfig.hasDelayedStart = true;
  omConfig.hasRun = true;
  omConfig.hasPause = true;
  omConfig.hasActionrequired = true;
  omConfig.hasFinished = true;
  omConfig.hasError = true;
  omConfig.hasAborting = false;
  configureOperationModeState(omConfig);

  // configure remote control
  RemoteControlConfiguration rcConfig = { 0 };
  rcConfig.hasControlInactive = true;
  rcConfig.hasControlActive = true;
  rcConfig.hasStartActive = true;
  configureRemoteControlState(rcConfig);

  // configure door state
  DoorStateConfiguration dsConfig = { 0 };
  dsConfig.hasOpen = true;
  dsConfig.hasClosed = true;
  dsConfig.hasLocked = true;
  configureDoorState(dsConfig);

  // configure power state
  PowerStateConfiguration psConfig = { 0 };
  psConfig.hasOff = false;
  psConfig.hasOn = true;
  psConfig.hasStandby = false;
  configurePowerState(psConfig);

  EnumValueDescriptorPtr temperatureCotton = createEnumDescriptor("Temperature", temperature_GC90);
  EnumValueDescriptorPtr temperatureEasyCare = createEnumDescriptor("Temperature", temperature_GC60);
  EnumValueDescriptorPtr temperature = createEnumDescriptor("Temperature", temperature_GC40);

  EnumValueDescriptorPtr spinSpeedCottonMix = createEnumDescriptor("SpinSpeed", spinSpeed_RPM1600);
  EnumValueDescriptorPtr spinSpeedEasyCare = createEnumDescriptor("SpinSpeed", spinSpeed_RPM1200);
  EnumValueDescriptorPtr spinSpeedDelicatesSilkWool = createEnumDescriptor("SpinSpeed", spinSpeed_RPM800);

  addAction("std.Cotton",        "Cotton",           "Cotton",        temperatureCotton,   spinSpeedCottonMix);
  addAction("std.EasyCare",      "Easy Care",        "EasyCare",      temperatureEasyCare, spinSpeedEasyCare);
  addAction("std.Mix",           "Mix",              "Mix",           temperature,         spinSpeedCottonMix);
  addAction("std.DelicatesSilk", "Delicates / Silk", "DelicatesSilk", temperature,         spinSpeedDelicatesSilkWool);
  addAction("std.Wool",          "Wool",             "Wool",          temperature,         spinSpeedDelicatesSilkWool);

  temperatureProp = createEnumDescriptor("Temperature", temperature_GC90);
  spinSpeedProp = createEnumDescriptor("SpinSpeed", spinSpeed_RPM1600);

  deviceProperties->addProperty(temperatureProp);
  deviceProperties->addProperty(spinSpeedProp);

  return inherited::configureDevice();
}

void HomeConnectDeviceWasher::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  inherited::stateChanged(aChangedState, aEventsToPush);
}

void HomeConnectDeviceWasher::handleEvent(string aEventType, JsonObjectPtr aEventData, ErrorPtr aError)
{
  ALOG(LOG_INFO, "Washer Event '%s' - item: %s", aEventType.c_str(), aEventData ? aEventData->c_strValue() : "<none>");

  JsonObjectPtr oKey;
  JsonObjectPtr oValue;

  if (!aEventData || !aEventData->get("key", oKey) || !aEventData->get("value", oValue) ) {
    return;
  }

  string key = (oKey != NULL) ? oKey->stringValue() : "";

  if (aEventType == "NOTIFY"){
    if (key == "LaundryCare.Washer.Option.Temperature") {
      string value = (oValue != NULL) ? oValue->stringValue() : "";
      temperatureProp->setStringValue(removeNamespace(value));
      return;
    }
    if (key == "LaundryCare.Washer.Option.SpinSpeed") {
      string value = (oValue != NULL) ? oValue->stringValue() : "";
      spinSpeedProp->setStringValue(removeNamespace(value));
      return;
    }
  }
  inherited::handleEvent(aEventType, aEventData, aError);
}

void HomeConnectDeviceWasher::addAction(const string& aName, const string& aDescription, const string& aApiCommandSuffix, ValueDescriptorPtr aTemperature,ValueDescriptorPtr aSpinSpeed)
{
  HomeConnectCommandBuilder builder("LaundryCare.Washer.Program." + aApiCommandSuffix);
  builder.addOption("LaundryCare.Washer.Option.Temperature", "\"LaundryCare.Washer.EnumType.Temperature.@{Temperature}\"");
  builder.addOption("LaundryCare.Washer.Option.SpinSpeed", "\"LaundryCare.Washer.EnumType.SpinSpeed.@{SpinSpeed}\"");

  HomeConnectActionPtr action = HomeConnectActionPtr(new HomeConnectAction(*this, aName, aDescription, builder.build()));
  action->addParameter(aTemperature, true);
  action->addParameter(aSpinSpeed, true);
  deviceActions->addAction(action);
}

string HomeConnectDeviceWasher::oemModelGUID()
{
  return "gs1:(01)7640156792799";
}

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT
