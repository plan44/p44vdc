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
#include "homeconnectaction.hpp"

#if ENABLE_HOMECONNECT

#include "homeconnectaction.hpp"

namespace p44 {

HomeConnectDeviceFridge::HomeConnectDeviceFridge(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
    inherited(aVdcP, aHomeApplicanceInfoRecord)
{
  installSettings(new HomeConnectDeviceSettings(*this));
}

HomeConnectDeviceFridge::~HomeConnectDeviceFridge()
{
  // TODO Auto-generated destructor stub
}

bool HomeConnectDeviceFridge::configureDevice()
{
  bool ret = inherited::configureDevice();
  // configure door state
  DoorStateConfiguration dsConfig = { 0 };
  dsConfig.hasOpen = true;
  dsConfig.hasClosed = true;
  dsConfig.hasLocked = false;
  configureDoorState(dsConfig);

  fridgeSuperMode = ValueDescriptorPtr(new NumericValueDescriptor("FridgeSuperMode", valueType_boolean, valueUnit_none, 0, 1, 1, true, 0));
  freezerSuperMode = ValueDescriptorPtr(new NumericValueDescriptor("FreezerSuperMode", valueType_boolean, valueUnit_none, 0, 1, 1, true, 0));

  fridgeTemperature = ValueDescriptorPtr(new NumericValueDescriptor("FridgeTargetTemperature", valueType_numeric, VALUE_UNIT(valueUnit_celsius, unitScaling_1), 2, 8, 1));
  freezerTemperature = ValueDescriptorPtr(new NumericValueDescriptor("FreezerTargetTemperature", valueType_numeric, VALUE_UNIT(valueUnit_celsius, unitScaling_1),-24, -16, 1));

  deviceProperties->addProperty(fridgeSuperMode);
  deviceProperties->addProperty(freezerSuperMode);
  deviceProperties->addProperty(fridgeTemperature);
  deviceProperties->addProperty(freezerTemperature);

  deviceProperties->setPropertyChangedHandler(boost::bind(&HomeConnectDeviceFridge::propertyChanged, this, _1));


  HomeConnectSettingBuilder builder("Refrigeration.FridgeFreezer.Setting.SuperModeFreezer");
  builder.setValue("true");
  deviceActions->addAction(HomeConnectActionPtr(new HomeConnectAction(*this, "std.SetFreezerSuperMode", "Set freezer Super Mode", builder.build())));

  builder.setValue("false");
  deviceActions->addAction(HomeConnectActionPtr(new HomeConnectAction(*this, "std.CancelFreezerSuperMode", "Cancel freezer Super Mode", builder.build())));

  builder = HomeConnectSettingBuilder("Refrigeration.FridgeFreezer.Setting.SuperModeRefrigerator");
  builder.setValue("true");
  deviceActions->addAction(HomeConnectActionPtr(new HomeConnectAction(*this, "std.SetFridgeSuperMode", "Set fridge Super Mode", builder.build())));

  builder.setValue("false");
  deviceActions->addAction(HomeConnectActionPtr(new HomeConnectAction(*this, "std.CancelFridgeSuperMode", "Cancel fridge Super Mode", builder.build())));

  return ret;
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

void HomeConnectDeviceFridge::propertyChanged(ValueDescriptorPtr aChangedProperty)
{
  ALOG(LOG_DEBUG, "Fridge/Freezer property changed, name:  %s, value: %s", aChangedProperty->getName().c_str(), aChangedProperty->getStringValue().c_str());


  if (aChangedProperty == fridgeTemperature) {
    sendNewSetting("Refrigeration.FridgeFreezer.Setting.SetpointTemperatureRefrigerator", aChangedProperty->getStringValue());
    return;
  }

  if (aChangedProperty == freezerTemperature) {
    sendNewSetting("Refrigeration.FridgeFreezer.Setting.SetpointTemperatureFreezer", aChangedProperty->getStringValue());
    return;
  }
}

void HomeConnectDeviceFridge::sendNewSetting(const string& aSettingName,const string& aValue)
{
  ALOG(LOG_DEBUG, "Fridge/Freezer - setting:  %s, to value: %s", aSettingName.c_str(), aValue.c_str());

  string jsonData = "{ \"data\": { \"key\": \"" + aSettingName + "\", \"value\":" + aValue + "}}";
  homeConnectComm().apiAction("PUT",
                              "/api/homeappliances/" + haId + "/settings/" + aSettingName,
                              JsonObject::objFromText(jsonData.c_str()),
                              boost::bind(&HomeConnectDeviceFridge::sendSettingFinished, this, _1, _2));
}

void HomeConnectDeviceFridge::sendSettingFinished(JsonObjectPtr aResult, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    ALOG(LOG_DEBUG, "Fridge/Freezer - setting parameter finished, result %s", aResult ? aResult->c_strValue() : "<none>");
  }
  else {
    ALOG(LOG_WARNING, "Fridge/Freezer - setting parameter failed, error: %s", aError->getErrorMessage());
  }

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
