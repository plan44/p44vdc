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

#if ENABLE_HOMECONNECT

namespace p44 {

HomeConnectDeviceOven::HomeConnectDeviceOven(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
    inherited(aVdcP, aHomeApplicanceInfoRecord)
{

}

HomeConnectDeviceOven::~HomeConnectDeviceOven()
{
  // TODO Auto-generated destructor stub
}

bool HomeConnectDeviceOven::configureDevice()
{
  // - common params
  ValueDescriptorPtr temp = ValueDescriptorPtr(
      new NumericValueDescriptor("Temperature", valueType_numeric, VALUE_UNIT(valueUnit_celsius, unitScaling_1), 30,
          250, 1, true, 180));
  ValueDescriptorPtr duration = ValueDescriptorPtr(
      new NumericValueDescriptor("Duration", valueType_numeric, VALUE_UNIT(valueUnit_second, unitScaling_1), 1, 86340,
          1, true, 600));

  addAction("std.Preheating",    "Pre-heating",         "PreHeating",       temp, duration);
  addAction("std.HotAir",        "Hot air",             "HotAir",           temp, duration);
  addAction("std.TopBottomHeat", "Top and bottom heat", "TopBottomHeating", temp, duration);
  addAction("std.PizzaSetting",  "Pizza Setting",       "PizzaSetting",     temp, duration);

  setTemperatureProp = ValueDescriptorPtr(
      new NumericValueDescriptor("SetTemperature", valueType_numeric, VALUE_UNIT(valueUnit_celsius, unitScaling_1), 0, 300, 1));

  currentTemperatureProp = ValueDescriptorPtr(
      new NumericValueDescriptor("CurrentTemperature", valueType_numeric, VALUE_UNIT(valueUnit_celsius, unitScaling_1), 0, 300, 1));

  elapsedProgramTimeProp = ValueDescriptorPtr(
      new NumericValueDescriptor("ElapsedProgramTime", valueType_numeric, VALUE_UNIT(valueUnit_second, unitScaling_1), 0, 86340, 1));

  remainingProgramTimeProp = ValueDescriptorPtr(
      new NumericValueDescriptor("RemainingProgramTime", valueType_numeric, VALUE_UNIT(valueUnit_second, unitScaling_1), 0, 86340, 1));

  programProgressProp = ValueDescriptorPtr(
      new NumericValueDescriptor("ProgramProgress", valueType_numeric, VALUE_UNIT(valueUnit_percent, unitScaling_1), 0, 100, 1));

  deviceProperties->addProperty(setTemperatureProp);
  deviceProperties->addProperty(currentTemperatureProp);
  deviceProperties->addProperty(elapsedProgramTimeProp);
  deviceProperties->addProperty(remainingProgramTimeProp);
  deviceProperties->addProperty(programProgressProp);

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

  return inherited::configureDevice();
}

void HomeConnectDeviceOven::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  inherited::stateChanged(aChangedState, aEventsToPush);
}

void HomeConnectDeviceOven::handleEvent(string aEventType, JsonObjectPtr aEventData, ErrorPtr aError)
{
  ALOG(LOG_INFO, "Oven Event '%s' - item: %s", aEventType.c_str(), aEventData ? aEventData->c_strValue() : "<none>");

  JsonObjectPtr oKey;
  JsonObjectPtr oValue;

  if (!aEventData || !aEventData->get("key", oKey) || !aEventData->get("value", oValue) ) {
    return;
  }

  string key = (oKey != NULL) ? oKey->stringValue() : "";

  if (aEventType == "NOTIFY") {
    if (key == "BSH.Common.Option.ElapsedProgramTime") {
      int32_t value = (oValue != NULL) ? oValue->int32Value() : 0;
      elapsedProgramTimeProp->setInt32Value(value);
      return;
    }

    if (key == "BSH.Common.Option.RemainingProgramTime") {
      int32_t value = (oValue != NULL) ? oValue->int32Value() : 0;
      remainingProgramTimeProp->setInt32Value(value);
      return;
    }

    if (key == "BSH.Common.Option.ProgramProgress") {
      int32_t value = (oValue != NULL) ? oValue->int32Value() : 0;
      programProgressProp->setInt32Value(value);
      return;
    }

    if (key == "Cooking.Oven.Option.SetpointTemperature") {
      int32_t value = (oValue != NULL) ? oValue->int32Value() : 0;
      setTemperatureProp->setInt32Value(value);
      return;
    }
  }

  if (aEventType == "STATUS") {
    if (key == "Cooking.Oven.Status.CurrentCavityTemperature") {
      int32_t value = (oValue != NULL) ? oValue->int32Value() : 0;
      currentTemperatureProp->setInt32Value(value);
      return;
    }
  }

  inherited::handleEvent(aEventType, aEventData, aError);
}

void HomeConnectDeviceOven::addAction(const string& aActionName, const string& aDescription, const string& aProgramName, ValueDescriptorPtr aTemperature, ValueDescriptorPtr aDuration)
{
  HomeConnectCommandBuilder builder("Cooking.Oven.Program.HeatingMode." + aProgramName);
  builder.addOption("Cooking.Oven.Option.SetpointTemperature", "@{Temperature%%0}");
  builder.addOption("BSH.Common.Option.Duration", "@{Duration%%0}");

  HomeConnectActionPtr action = HomeConnectActionPtr(new HomeConnectAction(*this, aActionName, aDescription, builder.build()));
  action->addParameter(aTemperature);
  action->addParameter(aDuration);
  deviceActions->addAction(action);
}
string HomeConnectDeviceOven::oemModelGUID()
{
  return "gs1:(01)7640156792546";
}

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT

