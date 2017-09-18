//
//  Copyright (c) 2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

HomeConnectPowerOnAction::HomeConnectPowerOnAction(SingleDevice &aSingleDevice,
                                                   const string& aName,
                                                   const string& aDescription,
                                                   const string& aIfPowerOnCommand,
                                                   const string& aIfPowerOffCommand,
                                                   DeviceState& aPowerState,
                                                   DeviceState& aOperationMode) :
    inherited(aSingleDevice, aName, aDescription, aIfPowerOnCommand),
    powerState(aPowerState),
    operationMode(aOperationMode),
    ifPowerOnCommand(aIfPowerOnCommand),
    ifPowerOffCommand(aIfPowerOffCommand) {}

void HomeConnectPowerOnAction::performCall(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  if (powerState.value()->getStringValue() != "PowerOn") {
    powerOnDevice(aParams, aCompletedCB);
    return;
  }

  if (operationMode.value()->getStringValue() != "ModeReady") {
    runActionWhenReady(aParams, aCompletedCB, RETRY_COUNT);
    return;
  }

  LOG(LOG_DEBUG, "Device is powered on, proceed with action");
  inherited::performCall(aParams, aCompletedCB, ifPowerOnCommand);
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
    inherited::performCall(aParams, aCompletedCB, ifPowerOnCommand);
    return;
  }

  runActionWhenReady(aParams, aCompletedCB, RETRY_COUNT);

}

void HomeConnectPowerOnAction::runActionWhenReady(ApiValuePtr aParams, StatusCB aCompletedCB, unsigned int aRetriesLeft)
{
  if (operationMode.value()->getStringValue() != "ModeReady") {

    if (aRetriesLeft-- == 0) {
      LOG(LOG_WARNING, "Device is still not ready after %u retries, stop trying", RETRY_COUNT);
      ErrorPtr err = TextError::err("Device operation mode is not ready");
      if (aCompletedCB) aCompletedCB(err);
      return;
    }

    LOG(LOG_DEBUG, "Device is not ready, reschedule action but call completed callback anyway");
    if (aCompletedCB) aCompletedCB(Error::ok());
    aCompletedCB.clear();
    MainLoop::currentMainLoop().executeOnce(boost::bind(&HomeConnectPowerOnAction::runActionWhenReady, this, aParams, aCompletedCB, aRetriesLeft), RESCHEDULE_INTERVAL);
    return;
  }

  LOG(LOG_DEBUG, "Device is powered on and ready, proceed with action");
  inherited::performCall(aParams, aCompletedCB, ifPowerOffCommand);
}


#endif // ENABLE_HOMECONNECT

