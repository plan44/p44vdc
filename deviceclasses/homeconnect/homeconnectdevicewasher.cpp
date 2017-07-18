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

const char* HomeConnectDeviceWasher::temperatureNames[temperature_Num] = {
  "Cold",
  "GC20",
  "GC30",
  "GC40",
  "GC50",
  "GC60",
  "GC70",
  "GC80",
  "GC90"
  };

const char* HomeConnectDeviceWasher::spinSpeedNames[spinSpeed_Num] = {
  "Off",
  "RPM400",
  "RPM600",
  "RPM800",
  "RPM1000",
  "RPM1200",
  "RPM1400",
  "RPM1600"
  };

HomeConnectDeviceWasher::HomeConnectDeviceWasher(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
    inherited(aVdcP, aHomeApplicanceInfoRecord)
{

}

HomeConnectDeviceWasher::~HomeConnectDeviceWasher()
{
  // TODO Auto-generated destructor stub
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

  EnumValueDescriptorPtr temperatureCotton = createEnumDescriptor("Temperature", temperature_GC90, temperatureNames);
  EnumValueDescriptorPtr temperatureEasyCare = createEnumDescriptor("Temperature", temperature_GC60, temperatureNames);
  EnumValueDescriptorPtr temperature = createEnumDescriptor("Temperature", temperature_GC40, temperatureNames);

  EnumValueDescriptorPtr spinSpeedCottonMix = createEnumDescriptor("SpinSpeed", spinSpeed_RPM1600, spinSpeedNames);
  EnumValueDescriptorPtr spinSpeedEasyCare = createEnumDescriptor("SpinSpeed", spinSpeed_RPM1200, spinSpeedNames);
  EnumValueDescriptorPtr spinSpeedDelicatesSilkWool = createEnumDescriptor("SpinSpeed", spinSpeed_RPM800, spinSpeedNames);

  addAction("std.Cotton",        "Cotton",           "Cotton",        temperatureCotton,   spinSpeedCottonMix);
  addAction("std.EasyCare",      "Easy Care",        "EasyCare",      temperatureEasyCare, spinSpeedEasyCare);
  addAction("std.Mix",           "Mix",              "Mix",           temperature,         spinSpeedCottonMix);
  addAction("std.DelicatesSilk", "Delicates / Silk", "DelicatesSilk", temperature,         spinSpeedDelicatesSilkWool);
  addAction("std.Wool",          "Wool",             "Wool",          temperature,         spinSpeedDelicatesSilkWool);

  temperatureProp = createEnumDescriptor("Temperature", temperature_GC90, temperatureNames);
  spinSpeedProp = createEnumDescriptor("SpinSpeed", spinSpeed_RPM1600, spinSpeedNames);

  deviceProperties->addProperty(temperatureProp);
  deviceProperties->addProperty(spinSpeedProp);

  return inherited::configureDevice();
}

void HomeConnectDeviceWasher::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  inherited::stateChanged(aChangedState, aEventsToPush);
}

void HomeConnectDeviceWasher::handleEventTypeNotify(string aKey, JsonObjectPtr aValue)
{
  ALOG(LOG_INFO, "Washer Event 'NOTIFY' - item: %s, %s", aKey.c_str(), aValue ? aValue->c_strValue() : "<none>");

  if (aKey == "LaundryCare.Washer.Option.Temperature") {
    string value = (aValue != NULL) ? aValue->stringValue() : "";
    temperatureProp->setStringValue(removeNamespace(value));
    return;
  }
  if (aKey == "LaundryCare.Washer.Option.SpinSpeed") {
    string value = (aValue != NULL) ? aValue->stringValue() : "";
    spinSpeedProp->setStringValue(removeNamespace(value));
    return;
  }

  inherited::handleEventTypeNotify(aKey, aValue);
}

void HomeConnectDeviceWasher::addAction(const string& aName, const string& aDescription, const string& aApiCommandSuffix, ValueDescriptorPtr aTemperature,ValueDescriptorPtr aSpinSpeed)
{
  HomeConnectProgramBuilder builder("LaundryCare.Washer.Program." + aApiCommandSuffix);
  builder.addOption("LaundryCare.Washer.Option.Temperature", "\"LaundryCare.Washer.EnumType.Temperature.@{Temperature}\"");
  builder.addOption("LaundryCare.Washer.Option.SpinSpeed", "\"LaundryCare.Washer.EnumType.SpinSpeed.@{SpinSpeed}\"");

  HomeConnectActionPtr action = HomeConnectActionPtr(new HomeConnectAction(*this, aName, aDescription, builder.build()));
  action->addParameter(aTemperature, true);
  action->addParameter(aSpinSpeed, true);
  deviceActions->addAction(action);
}

EnumValueDescriptorPtr HomeConnectDeviceWasher::createEnumDescriptor(string aName, int aMaxValue, const char** aEnumNames)
{
  EnumValueDescriptorPtr descriptor = EnumValueDescriptorPtr(new EnumValueDescriptor(aName, true));
  for(int i = 0 ; i <= aMaxValue; i++)
  {
    descriptor->addEnum(aEnumNames[i], i, false);
  }
  return descriptor;
}

string HomeConnectDeviceWasher::oemModelGUID()
{
  return "gs1:(01)7640156792799";
}

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT
