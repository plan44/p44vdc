//
//  Copyright (c) 2017 digitalSTROM.org, Zurich, Switzerland
//
//  Author: Michal Badecki <michal.badecki@digitalstrom.com>
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

#include "homeconnectaction.hpp"

#if ENABLE_HOMECONNECT

#include "homeconnectvdc.hpp"

#include "homeconnectdevicecoffemaker.hpp"
#include "homeconnectdeviceoven.hpp"
#include "homeconnectdevicedishwasher.hpp"
#include "homeconnectdevicewasher.hpp"
#include "homeconnectdevicedryer.hpp"
#include "homeconnectdevicefridge.hpp"
#include <sstream>
#include <boost/algorithm/string/split.hpp>
#include <functional>

using namespace p44;


HomeConnectAction::HomeConnectAction(SingleDevice &aSingleDevice, const string& aName, const string& aDescription, const string& aApiCommandTemplate) :
  inherited(aSingleDevice, aName, aDescription),
  apiCommandTemplate(aApiCommandTemplate)
{
}


HomeConnectDevice &HomeConnectAction::getHomeConnectDevice()
{
  return *(static_cast<HomeConnectDevice *>(singleDeviceP));
}

void HomeConnectAction::performCall(ApiValuePtr aParams, StatusCB aCompletedCB, const string& aCommandTemplate)
{
  // direct execution of home connect API commands
  // Syntax:
  //   method:resturlpath[:jsonBody]
  string cmd = aCommandTemplate;
  ErrorPtr err = substitutePlaceholders(cmd, boost::bind(&HomeConnectAction::valueLookup, this, aParams, _1, _2));
  string method;
  string r;

  if (Error::isOK(err)) {
    if(!keyAndValue(cmd, method, r)) {
      err = TextError::err("Invalid Home Connect command template: '%s'", cmd.c_str());
    }
  }

  if (!Error::isOK(err)) {
    if (aCompletedCB) aCompletedCB(err);
    return;
  }

  string path;
  string body;
  JsonObjectPtr jsonBody;
  if (!keyAndValue(r, path, body)) {
    path = r;
  }
  else {
    // make JSON from text
    jsonBody = JsonObject::objFromText(body.c_str());
  }
  // complete path
  string urlpath = "/api/homeappliances/" + getHomeConnectDevice().haId + "/" + path;
  getHomeConnectDevice().homeConnectComm().apiAction(method, urlpath, jsonBody, boost::bind(&HomeConnectAction::apiCommandSent, this, aCompletedCB, _1, _2));
}

void HomeConnectAction::performCall(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  performCall(aParams, aCompletedCB, apiCommandTemplate);
}


ErrorPtr HomeConnectAction::valueLookup(ApiValuePtr aParams, const string aName, string &aValue)
{
  ApiValuePtr v = aParams->get(aName);
  if (v) {
    aValue = v->stringValue();
    return ErrorPtr();
  }
  return ErrorPtr(TextError::err("no substitution found for '%s'", aName.c_str()));
}



void HomeConnectAction::apiCommandSent(StatusCB aCompletedCB, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (aCompletedCB) aCompletedCB(aError);
}


HomeConnectRunProgramAction::HomeConnectRunProgramAction(SingleDevice &aSingleDevice,
                                                         EnumValueDescriptor& aOperationMode,
                                                         const string& aName,
                                                         const string& aDescription,
                                                         const string& aApiCommandTemplate) :
    inherited(aSingleDevice, aName, aDescription, aApiCommandTemplate),
    operationMode(aOperationMode) {}

void HomeConnectRunProgramAction::performCall(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  if (operationMode.getStringValue() != "ModeReady") {
    aCompletedCB(TextError::err("Cannot run program, because device is not ready"));
    return;
  }

  inherited::performCall(aParams, aCompletedCB);
}

HomeConnectActionWithOperationMode::HomeConnectActionWithOperationMode(SingleDevice &aSingleDevice,
                                                                       EnumValueDescriptor& aOperationMode,
                                                                       const string& aName,
                                                                       const string& aDescription,
                                                                       const string& aApiCommandTemplate) :
    inherited(aSingleDevice, aName, aDescription, aApiCommandTemplate),
    operationMode(aOperationMode) {}


void HomeConnectActionWithOperationMode::runActionWhenReady(ApiValuePtr aParams, StatusCB aCompletedCB, const string& aActionCommand, unsigned int aRetriesLeft)
{
  if (operationMode.getStringValue() != "ModeReady") {

    if (aRetriesLeft-- == 0) {
      LOG(LOG_WARNING, "Device is still not ready after %u retries, stop trying", RETRY_COUNT);
      ErrorPtr err = TextError::err("Device operation mode is not ready");
      if (aCompletedCB) aCompletedCB(err);
      return;
    }

    LOG(LOG_DEBUG, "Device is not ready, reschedule action but call completed callback anyway");
    if (aCompletedCB) aCompletedCB(Error::ok());
    aCompletedCB.clear();
    MainLoop::currentMainLoop().executeOnce(boost::bind(&HomeConnectPowerOnAction::runActionWhenReady, this, aParams, aCompletedCB, aActionCommand, aRetriesLeft), RESCHEDULE_INTERVAL);
    return;
  }

  LOG(LOG_DEBUG, "Device is powered on and ready, proceed with action");
  inherited::performCall(aParams, aCompletedCB, aActionCommand);
}

HomeConnectPowerOnAction::HomeConnectPowerOnAction(SingleDevice &aSingleDevice,
                                                   const string& aName,
                                                   const string& aDescription,
                                                   const string& aStandardCommand,
                                                   const string& aIfDelayedCommand,
                                                   EnumValueDescriptor& aPowerState,
                                                   EnumValueDescriptor& aOperationMode) :
    inherited(aSingleDevice, aOperationMode, aName, aDescription, aStandardCommand),
    powerState(aPowerState),
    standardCommand(aStandardCommand),
    ifDelayedCommand(aIfDelayedCommand) {}

void HomeConnectPowerOnAction::performCall(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  if (powerState.getStringValue() != "PowerOn") {
    powerOnDevice(aParams, aCompletedCB);
    return;
  }

  if (operationMode.getStringValue() != "ModeReady") {
    runActionWhenReady(aParams, aCompletedCB, ifDelayedCommand, RETRY_COUNT);
    return;
  }

  LOG(LOG_DEBUG, "Device is powered on, proceed with action");
  inherited::performCall(aParams, aCompletedCB, standardCommand);
  return;
}

void HomeConnectPowerOnAction::powerOnDevice(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  LOG(LOG_DEBUG, "Device will be powered on, before proceeding with action");
  HomeConnectSettingBuilder settingBuilder =
      HomeConnectSettingBuilder("BSH.Common.Setting.PowerState").setValue("\"BSH.Common.EnumType.PowerState.On\"");

  string powerOnCommand = settingBuilder.build();

  inherited::performCall(aParams->newNull(),
                         boost::bind(&HomeConnectPowerOnAction::devicePoweredOn, this, aParams, aCompletedCB, _1),
                         powerOnCommand);
}

void HomeConnectPowerOnAction::devicePoweredOn(ApiValuePtr aParams, StatusCB aCompletedCB, ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    LOG(LOG_WARNING, "Device could not be powered on, probably because it was on already. Proceed with action");
    inherited::performCall(aParams, aCompletedCB, standardCommand);
    return;
  }

  runActionWhenReady(aParams, aCompletedCB, ifDelayedCommand, RETRY_COUNT);
}


HomeConnectGoToStandbyAction::HomeConnectGoToStandbyAction(SingleDevice &aSingleDevice,
                                                           EnumValueDescriptor& aPowerState,
                                                           EnumValueDescriptor& aOperationMode) :
    inherited(aSingleDevice,
              aOperationMode,
              "StandBy",
              "Switch power state standby",
              HomeConnectSettingBuilder("BSH.Common.Setting.PowerState").setValue("\"BSH.Common.EnumType.PowerState.Standby\"").build()),
    powerState(aPowerState) {}

void HomeConnectGoToStandbyAction::performCall(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  if (powerState.getStringValue() == "PowerStandby") {
    LOG(LOG_DEBUG, "Device is already in Standby, ignoring action");
    aCompletedCB(Error::ok());
    return;
  }

  if (operationMode.getStringValue() != "ModeReady") {
    LOG(LOG_DEBUG, "Cannot go to standby now, there is action in progress. Wait until it is finished");
    runActionWhenReady(aParams, aCompletedCB, apiCommandTemplate, RETRY_COUNT);
    return;
  }

  inherited::performCall(aParams, aCompletedCB);
}

HomeConnectStopAction::HomeConnectStopAction(SingleDevice &aSingleDevice,
                                             EnumValueDescriptor& aOperationMode,
                                             const string& aName,
                                             const string& aDescription) :
    inherited(aSingleDevice,
              aOperationMode,
              aName,
              aDescription,
              "DELETE:programs/active") {}

void HomeConnectStopAction::performCall(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  string value = operationMode.getStringValue();
  if (value != "ModeRun" &&
      value != "ModeDelayedStart" &&
      value != "ModePause" &&
      value != "ModeActionRequired") {
    LOG(LOG_DEBUG, "Request cannot be performed since no active program is set, ignoring action");
    aCompletedCB(Error::ok());
    return;
  }

  inherited::performCall(aParams, aCompletedCB);
}


HomeConnectStopIfNotTimedAction::HomeConnectStopIfNotTimedAction(SingleDevice &aSingleDevice,
                                                                 EnumValueDescriptor& aOperationMode,
                                                                 ValueDescriptor& aRemainingProgramTime) :
   inherited(aSingleDevice,
             aOperationMode,
             "StopIfNotTimed",
             "Stop program if it is not timed"),
   remainingProgramTime(aRemainingProgramTime) {}

void HomeConnectStopIfNotTimedAction::performCall(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  ApiValuePtr value =  VdcHost::sharedVdcHost()->newApiValue();
  if (remainingProgramTime.getValue(value)) {
    LOG(LOG_DEBUG, "Program is timed, ignoring action");
    aCompletedCB(Error::ok());
    return;
  }

  inherited::performCall(aParams, aCompletedCB);
}

#endif // ENABLE_HOMECONNECT

