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

#include "homeconnectdevicedishwasher.hpp"

#if ENABLE_HOMECONNECT

namespace p44 {

HomeConnectDeviceDishWasher::HomeConnectDeviceDishWasher(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
    inherited(aVdcP, aHomeApplicanceInfoRecord)
{

}

HomeConnectDeviceDishWasher::~HomeConnectDeviceDishWasher()
{
  // TODO Auto-generated destructor stub
}

bool HomeConnectDeviceDishWasher::configureDevice()
{
  // configure operation mode
  OperationModeConfiguration omConfig = { 0 };
  omConfig.hasInactive = true;
  omConfig.hasReady = true;
  omConfig.hasDelayedStart = true;
  omConfig.hasRun = true;
  omConfig.hasPause = false;
  omConfig.hasActionrequired = false;
  omConfig.hasFinished = true;
  omConfig.hasError = false;
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
  dsConfig.hasLocked = false;
  configureDoorState(dsConfig);

  // configure power state
  PowerStateConfiguration psConfig = { 0 };
  psConfig.hasOff = true;
  psConfig.hasOn = true;
  psConfig.hasStandby = false;
  configurePowerState(psConfig);
  ValueDescriptorPtr delayedStart = ValueDescriptorPtr(
    new NumericValueDescriptor("DelayedStart", valueType_numeric, VALUE_UNIT(valueUnit_second, unitScaling_1), 0, 86340, 1, true));

  addAction("std.Auto3545",    "Auto 35-45C", "Auto1",   delayedStart);
  addAction("std.Auto4565",    "Auto 45-65C", "Auto2",   delayedStart);
  addAction("std.Auto6575",    "Auto 65-75C", "Auto3",   delayedStart);
  addAction("std.Eco50",       "Eco 50C",     "Eco50",   delayedStart);
  addAction("std.QuickWash45", "Quick 45C",   "Quick45", delayedStart);


  delayedStartProp = ValueDescriptorPtr(
    new NumericValueDescriptor("DelayedStart", valueType_numeric, VALUE_UNIT(valueUnit_second, unitScaling_1), 0, 86340, 1, true));

  deviceProperties->addProperty(delayedStartProp, true);

  delayedStartProp = ValueDescriptorPtr(
    new NumericValueDescriptor("DelayedStart", valueType_numeric, VALUE_UNIT(valueUnit_second, unitScaling_1), 0, 86340, 1, true));

  deviceProperties->addProperty(delayedStartProp, true);

  return inherited::configureDevice();
}

void HomeConnectDeviceDishWasher::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  inherited::stateChanged(aChangedState, aEventsToPush);
}

void HomeConnectDeviceDishWasher::handleEvent(string aEventType, JsonObjectPtr aEventData, ErrorPtr aError)
{
  ALOG(LOG_INFO, "DishWasher Event '%s' - item: %s", aEventType.c_str(), aEventData ? aEventData->c_strValue() : "<none>");

  JsonObjectPtr oKey;
  JsonObjectPtr oValue;

  if (!aEventData || !aEventData->get("key", oKey) || !aEventData->get("value", oValue) ) {
    return;
  }

  string key = (oKey != NULL) ? oKey->stringValue() : "";

  if (aEventType == "NOTIFY" && key == "BSH.Common.Option.StartInRelative") {
    int32_t value = (oValue != NULL) ? oValue->int32Value() : 0;
    delayedStartProp->setInt32Value(value);
    return;
  }
  inherited::handleEvent(aEventType, aEventData, aError);
}

void HomeConnectDeviceDishWasher::addAction(const string& aName, const string& aDescription, const string& aApiCommandSuffix, ValueDescriptorPtr aParameter)
{
  HomeConnectCommandBuilder builder(string("Dishcare.Dishwasher.Program.") + aApiCommandSuffix);
  builder.addOption("BSH.Common.Option.StartInRelative", "@{DelayedStart%%0}");

  HomeConnectActionPtr action = HomeConnectActionPtr(new HomeConnectAction(*this, aName, aDescription, builder.build()));
  action->addParameter(aParameter);
  deviceActions->addAction(action);
}

string HomeConnectDeviceDishWasher::oemModelGUID()
{
  return "gs1:(01)7640156792829";
}

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT
