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

#include "homeconnectaction.hpp"

namespace p44 {

HomeConnectDeviceDishWasher::HomeConnectDeviceDishWasher(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
    inherited(aVdcP, aHomeApplicanceInfoRecord)
{
  HomeConnectDeviceSettingsPtr settings = new HomeConnectDeviceSettings(*this);
  settings->fireAction = "std.PowerOff";

  installSettings(settings);
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

  // configure program status properties
  ProgramStatusConfiguration progStatusConfig = { 0 };
  progStatusConfig.hasElapsedTime = false;
  progStatusConfig.hasRemainingTime = true;
  progStatusConfig.hasProgres = true;
  configureProgramStatus(progStatusConfig);

  ValueDescriptorPtr delayedStart = ValueDescriptorPtr(
    new NumericValueDescriptor("DelayedStart", valueType_numeric, VALUE_UNIT(valueUnit_second, unitScaling_1), 0, 86340, 1, true));


  addDefaultPowerOffAction();
  addDefaultPowerOnAction();

  addAction("std.Auto3545",    "Auto 35-45C", "Auto1",   delayedStart);
  addAction("std.Auto4565",    "Auto 45-65C", "Auto2",   delayedStart);
  addAction("std.Auto6575",    "Auto 65-75C", "Auto3",   delayedStart);
  addAction("std.Eco50",       "Eco 50C",     "Eco50",   delayedStart);
  addAction("std.QuickWash45", "Quick 45C",   "Quick45", delayedStart);


  delayedStartProp = ValueDescriptorPtr(
    new NumericValueDescriptor("DelayedStart", valueType_numeric, VALUE_UNIT(valueUnit_second, unitScaling_1), 0, 86340, 1, true));

  deviceProperties->addProperty(delayedStartProp, true);

  return inherited::configureDevice();
}

void HomeConnectDeviceDishWasher::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  inherited::stateChanged(aChangedState, aEventsToPush);
}

void HomeConnectDeviceDishWasher::handleEventTypeNotify(const string& aKey, JsonObjectPtr aValue)
{
  ALOG(LOG_INFO, "DishWasher Event 'NOTIFY' - item: %s, %s", aKey.c_str(), aValue ? aValue->c_strValue() : "<none>");

  if (aKey == "BSH.Common.Option.StartInRelative") {
    int32_t value = (aValue != NULL) ? aValue->int32Value() : 0;
    delayedStartProp->setInt32Value(value);
    return;
  }
  inherited::handleEventTypeNotify(aKey, aValue);
}

void HomeConnectDeviceDishWasher::addAction(const string& aActionName, const string& aDescription, const string& aProgramName, ValueDescriptorPtr aParameter)
{
  HomeConnectProgramBuilder builder("Dishcare.Dishwasher.Program." + aProgramName);
  builder.addOption("BSH.Common.Option.StartInRelative", "@{DelayedStart%%0}");

  HomeConnectActionPtr action = HomeConnectActionPtr(new HomeConnectAction(*this, aActionName, aDescription, builder.build()));
  action->addParameter(aParameter);
  deviceActions->addAction(action);
}

bool HomeConnectDeviceDishWasher::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("homeconnect_dishwasher", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT
