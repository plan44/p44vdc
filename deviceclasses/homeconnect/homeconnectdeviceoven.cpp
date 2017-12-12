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

#include "homeconnectdeviceoven.hpp"
#include "homeconnectaction.hpp"

#if ENABLE_HOMECONNECT

#include "homeconnectaction.hpp"

namespace p44 {

HomeConnectDeviceOven::HomeConnectDeviceOven(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
    inherited(aVdcP, aHomeApplicanceInfoRecord)
{
  HomeConnectDeviceSettingsPtr settings = new HomeConnectDeviceSettings(*this);
  settings->fireAction = "std.StandBy";
  settings->deepOffAction = "std.StopIfNotTimed";
  settings->leaveHomeAction = "std.StopIfNotTimed";
  settings->sleepAction = "std.StopIfNotTimed";

  installSettings(settings);
}

HomeConnectDeviceOven::~HomeConnectDeviceOven()
{
  // TODO Auto-generated destructor stub
}

bool HomeConnectDeviceOven::configureDevice()
{
  bool ret = inherited::configureDevice();

  addProgramNameProperty();

  targetTemperatureProp = ValueDescriptorPtr(
      new NumericValueDescriptor("TargetTemperature", valueType_numeric, VALUE_UNIT(valueUnit_celsius, unitScaling_1), 0, 300, 1));

  currentTemperatureProp = ValueDescriptorPtr(
      new NumericValueDescriptor("CurrentTemperature", valueType_numeric, VALUE_UNIT(valueUnit_celsius, unitScaling_1), 0, 300, 1));

  deviceProperties->addProperty(targetTemperatureProp);
  deviceProperties->addProperty(currentTemperatureProp);

  deviceEvents->addEvent(DeviceEventPtr(new DeviceEvent(*this, "PreheatFinished", "Pre-heating finished")));

  // configure operation mode
  OperationModeConfiguration omConfig = { 0 };
  omConfig.hasInactive = true;
  omConfig.hasReady = true;
  omConfig.hasDelayedStart = true;
  omConfig.hasRun = true;
  omConfig.hasPause = true;
  omConfig.hasActionrequired = true;
  omConfig.hasFinished = true;
  omConfig.hasError = true;
  omConfig.hasAborting = true;
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
  psConfig.hasStandby = true;
  configurePowerState(psConfig);

  // configure program status properties
  ProgramStatusConfiguration progStatusConfig = { 0 };
  progStatusConfig.hasElapsedTime = true;
  progStatusConfig.hasRemainingTime = true;
  progStatusConfig.hasProgres = true;
  configureProgramStatus(progStatusConfig);

  EventConfiguration eventConfig = { 0 };
  eventConfig.hasAlarmClockElapsed = true;
  eventConfig.hasLocallyOperated = true;
  eventConfig.hasProgramAborted = false;
  eventConfig.hasProgramFinished = true;
  eventConfig.hasProgramStarted = true;
  configureEvents(eventConfig);


  addDefaultStandByAction();
  addDefaultPowerOnAction();
  addDefaultStopAction();

  ValueDescriptorPtr temp = ValueDescriptorPtr(
      new NumericValueDescriptor("Temperature", valueType_numeric, VALUE_UNIT(valueUnit_celsius, unitScaling_1), 30,
          250, 1, true, 200));
  ValueDescriptorPtr duration = ValueDescriptorPtr(
      new NumericValueDescriptor("Duration", valueType_numeric, VALUE_UNIT(valueUnit_second, unitScaling_1), 1, 86340,
          1, true, 600));

  addAction("std.Preheating",       "Pre-heating",         "PreHeating",       temp, duration);
  addAction("std.HotAir",           "Hot air",             "HotAir",           temp, duration);
  addAction("std.TopBottomHeating", "Top and bottom heat", "TopBottomHeating", temp, duration);
  addAction("std.PizzaSetting",     "Pizza Setting",       "PizzaSetting",     temp, duration);

  deviceActions->addAction(new HomeConnectStopIfNotTimedAction(*this, *operationModeDescriptor, *remainingProgramTime));

  return ret;
}

void HomeConnectDeviceOven::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  inherited::stateChanged(aChangedState, aEventsToPush);
}

void HomeConnectDeviceOven::handleEventTypeNotify(const string& aKey, JsonObjectPtr aValue)
{
  ALOG(LOG_INFO, "Oven Event 'NOTIFY' - item: %s, %s", aKey.c_str(), aValue ? aValue->c_strValue() : "<none>");

  if (aKey == "Cooking.Oven.Option.SetpointTemperature") {
    int32_t value = (aValue != NULL) ? aValue->int32Value() : 0;
    targetTemperatureProp->setInt32Value(value);
    return;
  }

  inherited::handleEventTypeNotify(aKey, aValue);
}

void HomeConnectDeviceOven::handleEventTypeEvent(const string& aKey)
{
  ALOG(LOG_INFO, "Oven Event 'EVENT' - item: %s", aKey.c_str());

  if(aKey == "Cooking.Oven.Event.PreheatFinished") {
    deviceEvents->pushEvent("PreheatFinished");
  }
}

void HomeConnectDeviceOven::handleEventTypeStatus(const string& aKey, JsonObjectPtr aValue)
{
  ALOG(LOG_INFO, "Oven Event 'STATUS' - item: %s, %s", aKey.c_str(), aValue ? aValue->c_strValue() : "<none>");

  if (aKey == "Cooking.Oven.Status.CurrentCavityTemperature") {
    int32_t value = (aValue != NULL) ? aValue->int32Value() : 0;
    currentTemperatureProp->setInt32Value(value);
    return;
  }

  inherited::handleEventTypeStatus(aKey, aValue);
}

void HomeConnectDeviceOven::addAction(const string& aActionName, const string& aDescription, const string& aProgramName, ValueDescriptorPtr aTemperature, ValueDescriptorPtr aDuration)
{
  HomeConnectProgramBuilder builder("Cooking.Oven.Program.HeatingMode." + aProgramName);
  builder.addOption("Cooking.Oven.Option.SetpointTemperature", "@{Temperature%%0}");
  builder.addOption("BSH.Common.Option.Duration", "@{Duration%%0}");

  HomeConnectActionPtr action = HomeConnectActionPtr(new HomeConnectRunProgramAction(*this, *operationModeDescriptor, aActionName, aDescription, builder.build()));
  action->addParameter(aTemperature);
  action->addParameter(aDuration);
  deviceActions->addAction(action);
}

bool HomeConnectDeviceOven::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("homeconnect_oven", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT

