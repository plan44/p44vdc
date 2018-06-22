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
#include "homeconnectaction.hpp"

#if ENABLE_HOMECONNECT

#include "homeconnectaction.hpp"
#include <ctime>

namespace p44 {

namespace
{
  static const string DISHWASHER_CONFIG_FILE_NAME = HOMECONNECT_CONFIG_FILE_NAME_BASE + "Dishwasher";
}

HomeConnectDeviceDishWasher::HomeConnectDeviceDishWasher(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
    inherited(aVdcP, aHomeApplicanceInfoRecord, DISHWASHER_CONFIG_FILE_NAME)
{
  HomeConnectDeviceSettingsPtr settings = new HomeConnectDeviceSettings(*this);
  settings->fireAction = "PowerOff";

  installSettings(settings);
}

HomeConnectDeviceDishWasher::~HomeConnectDeviceDishWasher()
{
  // TODO Auto-generated destructor stub
}

void HomeConnectDeviceDishWasher::configureDevice(StatusCB aStatusCB)
{

  addProgramNameProperty();
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

  EventConfiguration eventConfig = { 0 };
  eventConfig.hasAlarmClockElapsed = false;
  eventConfig.hasLocallyOperated = false;
  eventConfig.hasProgramAborted = true;
  eventConfig.hasProgramFinished = true;
  eventConfig.hasProgramStarted = true;
  configureEvents(eventConfig);

  ValueDescriptorPtr delayedStart = ValueDescriptorPtr(
    new NumericValueDescriptor("DelayedStart", valueType_numeric, VALUE_UNIT(valueUnit_minute, unitScaling_1), 0, 1439, 1, true));


  addDefaultPowerOffAction();
  addDefaultPowerOnAction();
  addDefaultStopAction();

  addAction("Auto4565",    "Auto 45-65C", "Auto2",   delayedStart);
  addAction("Auto6575",    "Auto 65-75C", "Auto3",   delayedStart);
  addAction("Eco50",       "Eco 50C",     "Eco50",   delayedStart);
  addAction("QuickWash45", "Quick 45C",   "Quick45", delayedStart);


  delayedStartProp = ValueDescriptorPtr(
    new NumericValueDescriptor("DelayedStart", valueType_numeric, VALUE_UNIT(valueUnit_minute, unitScaling_1), 0, 1439, 1, true));

  deviceProperties->addProperty(delayedStartProp, true);

  homeConnectComm().apiQuery(string_format("/api/homeappliances/%s/programs/available", haId.c_str()).c_str(),
      boost::bind(&HomeConnectDeviceDishWasher::gotAvailablePrograms, this, _1, _2, delayedStart, aStatusCB));
}

void HomeConnectDeviceDishWasher::gotAvailablePrograms(JsonObjectPtr aResult, ErrorPtr aError, ValueDescriptorPtr aDelayedStart, StatusCB aStatusCB)
{
  if (!Error::isOK(aError)) {
    aStatusCB(aError);
    return;
  }

  JsonObjectPtr programs;
  JsonObjectPtr data = aResult->get("data");
  if (data) {
    programs = data->get("programs");
  }

  if (programs) {
    for(int i = 0 ; i < programs->arrayLength(); i++) {
      JsonObjectPtr key = programs->arrayGet(i)->get("key");
      if (key && key->stringValue() == "Dishcare.Dishwasher.Program.Auto1") {
        ALOG(LOG_DEBUG, "Found Auto1 program, adding action");
        addAction("Auto3545", "Auto 35-45C", "Auto1", aDelayedStart);
      }
    }
  }
  aStatusCB(Error::ok());
}


void HomeConnectDeviceDishWasher::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  inherited::stateChanged(aChangedState, aEventsToPush);
}

void HomeConnectDeviceDishWasher::handleEventTypeNotify(const string& aKey, JsonObjectPtr aValue)
{
  ALOG(LOG_INFO, "DishWasher Event 'NOTIFY' - item: %s, %s", aKey.c_str(), aValue ? aValue->c_strValue() : "<none>");

  if (aKey == "BSH.Common.Option.StartInRelative") {
    handleStartInRelativeChange((aValue != NULL) ? aValue->int32Value() : 0);
    return;
  }

  inherited::handleEventTypeNotify(aKey, aValue);
}

void HomeConnectDeviceDishWasher::handleStartInRelativeChange(int32_t aValue)
{
  if (aValue == 0) {
    delayedStartProp->invalidate();
    return;
  }

  std::time_t currentTime = time(0);
  std::tm* localTime = localtime(&currentTime);

  delayedStartProp->setInt32Value((localTime->tm_hour * 60) + localTime->tm_min + (aValue / 60));
}

void HomeConnectDeviceDishWasher::addAction(const string& aActionName, const string& aDescription, const string& aProgramName, ValueDescriptorPtr aParameter)
{
  HomeConnectProgramBuilder builder("Dishcare.Dishwasher.Program." + aProgramName);
  builder.addOption("BSH.Common.Option.StartInRelative", "@{DelayedStart*60%%0}");

  string command = builder.build();
  HomeConnectActionPtr action = HomeConnectActionPtr(new HomeConnectPowerOnAction(*this,
                                                                                  aActionName,
                                                                                  aDescription,
                                                                                  command,
                                                                                  command,
                                                                                  *powerStateDescriptor,
                                                                                  *operationModeDescriptor));
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
