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

#include "homeconnectdevicefridge.hpp"

#if ENABLE_HOMECONNECT

namespace p44 {

HomeConnectDeviceFridge::HomeConnectDeviceFridge(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
    inherited(aVdcP, aHomeApplicanceInfoRecord)
{

}

HomeConnectDeviceFridge::~HomeConnectDeviceFridge()
{
  // TODO Auto-generated destructor stub
}

bool HomeConnectDeviceFridge::configureDevice()
{
  // configure door state
  DoorStateConfiguration dsConfig = { 0 };
  dsConfig.hasOpen = true;
  dsConfig.hasClosed = true;
  dsConfig.hasLocked = false;
  configureDoorState(dsConfig);

  // configure power state
  PowerStateConfiguration psConfig = { 0 };
  psConfig.hasOff = false;
  psConfig.hasOn = true;
  psConfig.hasStandby = false;
  configurePowerState(psConfig);

  fridgeSuperMode = ValueDescriptorPtr(new NumericValueDescriptor("FridgeSuperMode", valueType_boolean, valueUnit_none, 0, 1, 1, true, 0));
  freezerSuperMode = ValueDescriptorPtr(new NumericValueDescriptor("FreezerSuperMode", valueType_boolean, valueUnit_none, 0, 1, 1, true, 0));

  fridgeTemperature = ValueDescriptorPtr(new NumericValueDescriptor("FridgeSetTemperature", valueType_numeric, valueUnit_celsius, 2, 8, 1));
  freezerTemperature = ValueDescriptorPtr(new NumericValueDescriptor("FreezerSetTemperature", valueType_numeric, valueUnit_celsius,-24, -16, 1));

  deviceProperties->addProperty(fridgeSuperMode);
  deviceProperties->addProperty(freezerSuperMode);
  deviceProperties->addProperty(fridgeTemperature);
  deviceProperties->addProperty(freezerTemperature);


  HomeConnectSettingBuilder builder("Refrigeration.FridgeFreezer.Setting.SuperModeFreezer");
  builder.setValue("true");
  deviceActions->addAction(HomeConnectActionPtr(new HomeConnectAction(*this, "SetFreezerSuperMode", "Set freezer Super Mode", builder.build())));

  builder.setValue("false");
  deviceActions->addAction(HomeConnectActionPtr(new HomeConnectAction(*this, "CancelFreezerSuperMode", "Cancel freezer Super Mode", builder.build())));

  builder = HomeConnectSettingBuilder("Refrigeration.FridgeFreezer.Setting.SuperModeRefrigerator");
  builder.setValue("true");
  deviceActions->addAction(HomeConnectActionPtr(new HomeConnectAction(*this, "SetFridgeSuperMode", "Set fridge Super Mode", builder.build())));

  builder.setValue("false");
  deviceActions->addAction(HomeConnectActionPtr(new HomeConnectAction(*this, "CancelFridgeSuperMode", "Cancel fridge Super Mode", builder.build())));

  return inherited::configureDevice();
}

void HomeConnectDeviceFridge::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  inherited::stateChanged(aChangedState, aEventsToPush);
}

void HomeConnectDeviceFridge::handleEventTypeNotify(const string& aKey, JsonObjectPtr aValue)
{
  ALOG(LOG_INFO, "Fridge/Freezer Event 'NOTIFY' - item: %s, %s", aKey.c_str(), aValue ? aValue->c_strValue() : "<none>");

  if (aKey == "Refrigeration.FridgeFreezer.Setting.SetpointTemperatureFreezer") {
    int32_t value = (aValue != NULL) ? aValue->int32Value() : 0;
    freezerTemperature->setInt32Value(value);
    return;
  }

  if (aKey == "Refrigeration.FridgeFreezer.Setting.SetpointTemperatureRefrigerator") {
    int32_t value = (aValue != NULL) ? aValue->int32Value() : 0;
    fridgeTemperature->setInt32Value(value);
    return;
  }

  if (aKey == "Refrigeration.FridgeFreezer.Setting.SuperModeFreezer") {
    bool value = (aValue != NULL) ? aValue->boolValue() : false;
    int32_t intValue = value ? 1 : 0;
    freezerSuperMode->setInt32Value(intValue);
    return;
  }

  if (aKey == "Refrigeration.FridgeFreezer.Setting.SuperModeRefrigerator") {
    bool value = (aValue != NULL) ? aValue->boolValue() : false;
    int32_t intValue = value ? 1 : 0;
    fridgeSuperMode->setInt32Value(intValue);
    return;
  }


  inherited::handleEventTypeNotify(aKey, aValue);
}

string HomeConnectDeviceFridge::oemModelGUID()
{
  return "gs1:(01)7640156792812";
}

bool HomeConnectDeviceFridge::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("homeconnect_fridge", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT
