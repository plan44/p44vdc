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
#include "homeconnectaction.hpp"

#if ENABLE_HOMECONNECT

#include "homeconnectaction.hpp"

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
  HomeConnectDeviceSettingsPtr settings = new HomeConnectDeviceSettings(*this);
  settings->fireAction = "Stop";

  installSettings(settings);
}

HomeConnectDeviceWasher::~HomeConnectDeviceWasher()
{
  // TODO Auto-generated destructor stub
}

bool HomeConnectDeviceWasher::configureDevice()
{
  bool ret = inherited::configureDevice();

  addProgramNameProperty();
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

  // configure program status properties
  ProgramStatusConfiguration progStatusConfig = { 0 };
  progStatusConfig.hasElapsedTime = false;
  progStatusConfig.hasRemainingTime = true;
  progStatusConfig.hasProgres = true;
  configureProgramStatus(progStatusConfig);

  EventConfiguration eventConfig = { 0 };
  eventConfig.hasAlarmClockElapsed = false;
  eventConfig.hasLocallyOperated = true;
  eventConfig.hasProgramAborted = false;
  eventConfig.hasProgramFinished = true;
  eventConfig.hasProgramStarted = true;
  configureEvents(eventConfig);

  EnumValueDescriptorPtr temperatureCotton = createEnumDescriptor("Temperature", temperature_GC90, temperature_GC40, temperatureNames);
  EnumValueDescriptorPtr temperatureEasyCare = createEnumDescriptor("Temperature", temperature_GC60, temperature_GC40, temperatureNames);
  EnumValueDescriptorPtr temperature = createEnumDescriptor("Temperature", temperature_GC40, temperature_GC40, temperatureNames);

  EnumValueDescriptorPtr spinSpeedCottonMix = createEnumDescriptor("SpinSpeed", spinSpeed_RPM1600, spinSpeed_RPM1000, spinSpeedNames);
  EnumValueDescriptorPtr spinSpeedEasyCare = createEnumDescriptor("SpinSpeed", spinSpeed_RPM1200, spinSpeed_RPM1000, spinSpeedNames);
  EnumValueDescriptorPtr spinSpeedDelicatesSilkWool = createEnumDescriptor("SpinSpeed", spinSpeed_RPM800, spinSpeed_RPM800, spinSpeedNames);

  addAction("Cotton",        "Cotton",           "Cotton",        temperatureCotton,   spinSpeedCottonMix);
  addAction("EasyCare",      "Easy Care",        "EasyCare",      temperatureEasyCare, spinSpeedEasyCare);
  addAction("Mix",           "Mix",              "Mix",           temperature,         spinSpeedCottonMix);
  addAction("DelicatesSilk", "Delicates / Silk", "DelicatesSilk", temperature,         spinSpeedDelicatesSilkWool);
  addAction("Wool",          "Wool",             "Wool",          temperature,         spinSpeedDelicatesSilkWool);

  addDefaultStopAction();

  temperatureProp = createEnumDescriptor("Temperature", temperature_GC90, temperature_GC40, temperatureNames);
  spinSpeedProp = createEnumDescriptor("SpinSpeed", spinSpeed_RPM1600, spinSpeed_RPM1000, spinSpeedNames);

  deviceProperties->addProperty(temperatureProp);
  deviceProperties->addProperty(spinSpeedProp);

  return ret;
}

void HomeConnectDeviceWasher::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  inherited::stateChanged(aChangedState, aEventsToPush);
}

void HomeConnectDeviceWasher::handleEventTypeNotify(const string& aKey, JsonObjectPtr aValue)
{
  ALOG(LOG_INFO, "Washer Event 'NOTIFY' - item: %s, %s", aKey.c_str(), aValue ? aValue->c_strValue() : "<none>");

  if (aKey == "LaundryCare.Washer.Option.Temperature") {
    string value = (aValue != NULL) ? aValue->stringValue() : "";
    temperatureProp->setStringValueCaseInsensitive(removeNamespace(value));
    return;
  }
  if (aKey == "LaundryCare.Washer.Option.SpinSpeed") {
    string value = (aValue != NULL) ? aValue->stringValue() : "";
    spinSpeedProp->setStringValueCaseInsensitive(removeNamespace(value));
    return;
  }

  inherited::handleEventTypeNotify(aKey, aValue);
}

void HomeConnectDeviceWasher::addAction(const string& aName, const string& aDescription, const string& aApiCommandSuffix, ValueDescriptorPtr aTemperature,ValueDescriptorPtr aSpinSpeed)
{
  HomeConnectProgramBuilder builder("LaundryCare.Washer.Program." + aApiCommandSuffix);
  builder.addOption("LaundryCare.Washer.Option.Temperature", "\"LaundryCare.Washer.EnumType.Temperature.@{Temperature}\"");
  builder.addOption("LaundryCare.Washer.Option.SpinSpeed", "\"LaundryCare.Washer.EnumType.SpinSpeed.@{SpinSpeed}\"");

  HomeConnectActionPtr action = HomeConnectActionPtr(new HomeConnectRunProgramAction(*this,
                                                                                     *operationModeDescriptor,
                                                                                     aName,
                                                                                     aDescription,
                                                                                     builder.build()));
  action->addParameter(aTemperature);
  action->addParameter(aSpinSpeed);
  deviceActions->addAction(action);
}

EnumValueDescriptorPtr HomeConnectDeviceWasher::createEnumDescriptor(string aName, int aMaxValue, int aDefValue, const char** aEnumNames)
{
  EnumValueDescriptorPtr descriptor = EnumValueDescriptorPtr(new EnumValueDescriptor(aName, true));
  for(int i = 0 ; i <= aMaxValue; i++)
  {
    descriptor->addEnum(aEnumNames[i], i, (i == aDefValue));
  }
  return descriptor;
}

bool HomeConnectDeviceWasher::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("homeconnect_washer", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT
