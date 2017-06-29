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
  hcDevType = homeconnect_oven;
}

HomeConnectDeviceOven::~HomeConnectDeviceOven()
{
  // TODO Auto-generated destructor stub
}

bool HomeConnectDeviceOven::configureDevice()
{
  // Create standard actions
  HomeConnectActionPtr a;
  // - command template
  string cmdTemplate = "PUT:programs/active:{\"data\":{\"key\":\"Cooking.Oven.Program.HeatingMode.%s\","
      "\"options\":["
      "{ \"key\":\"Cooking.Oven.Option.SetpointTemperature\",\"value\":@{Temperature%%0}},"
      "{ \"key\":\"BSH.Common.Option.Duration\",\"value\":@{Duration%%0}}"
      "]}}";
  // - common params
  ValueDescriptorPtr temp = ValueDescriptorPtr(
      new NumericValueDescriptor("Temperature", valueType_numeric, VALUE_UNIT(valueUnit_celsius, unitScaling_1), 30,
          250, 1, true, 180));
  ValueDescriptorPtr duration = ValueDescriptorPtr(
      new NumericValueDescriptor("Duration", valueType_numeric, VALUE_UNIT(valueUnit_second, unitScaling_1), 1, 86340,
          1, true, 600));
  // - preheating
  a = HomeConnectActionPtr(
      new HomeConnectAction(*this, "std.Preheating", "Pre-heating", string_format(cmdTemplate.c_str(), "PreHeating")));
  a->addParameter(temp);
  a->addParameter(duration);
  deviceActions->addAction(a);
  // - Hot Air
  a = HomeConnectActionPtr(
      new HomeConnectAction(*this, "std.HotAir", "Hot air", string_format(cmdTemplate.c_str(), "HotAir")));
  a->addParameter(temp);
  a->addParameter(duration);
  deviceActions->addAction(a);
  // - Top and bottom heating
  a = HomeConnectActionPtr(
      new HomeConnectAction(*this, "std.TopBottomHeat", "Top and bottom heat",
          string_format(cmdTemplate.c_str(), "TopBottomHeating")));
  a->addParameter(temp);
  a->addParameter(duration);
  deviceActions->addAction(a);
  // - Pizza setting
  a = HomeConnectActionPtr(
      new HomeConnectAction(*this, "std.PizzaSetting", "Pizza Setting",
          string_format(cmdTemplate.c_str(), "PizzaSetting")));
  a->addParameter(temp);
  a->addParameter(duration);
  deviceActions->addAction(a);

  // create states
  configureOperationModeState(true, true, true, true, true, true);
  configureRemoteControlState();
  configureRemoteStartState();
  configureDoorState(true);
  configurePowerState(false, true);

  return inherited::configureDevice();
}

void HomeConnectDeviceOven::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  inherited::stateChanged(aChangedState, aEventsToPush);
}

void HomeConnectDeviceOven::handleEvent(string aEventType, JsonObjectPtr aEventData, ErrorPtr aError)
{
  ALOG(LOG_INFO, "Oven Event '%s' - item: %s", aEventType.c_str(), aEventData ? aEventData->c_strValue() : "<none>");
  inherited::handleEvent(aEventType, aEventData, aError);
}

string HomeConnectDeviceOven::oemModelGUID()
{
  return "gs1:(01)7640156792546";
}

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT

