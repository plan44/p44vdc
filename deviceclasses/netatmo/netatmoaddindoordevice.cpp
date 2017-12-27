//
//  Copyright (c) 2017 digitalSTROM.org, Zurich, Switzerland
//
//  Author: Krystian Heberlein <krystian.heberlein@digitalstrom.com>
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
#include "netatmoaddindoordevice.hpp"
#include "netatmodeviceenumerator.hpp"


#if ENABLE_NETATMO_V2

using namespace p44;


// MARK: ===== NetatmoDevice


NetatmoAddIndoorDevice::NetatmoAddIndoorDevice(NetatmoVdc *aVdcP, INetatmoComm& aINetatmoComm, JsonObjectPtr aDeviceData, const string& aBaseStationId) :
  inherited(aVdcP, aINetatmoComm, aDeviceData, usage_room, aBaseStationId)
{

}


void NetatmoAddIndoorDevice::configureDevice()
{
  sensorCO2 = createSensorCO2();
  addBehaviour(sensorCO2);

  statusBattery = createStatusBattery();
  addBehaviour(statusBattery);

  inherited::configureDevice();
}


void NetatmoAddIndoorDevice::updateData(JsonObjectPtr aJson)
{
  if (auto deviceJson = findModuleJson(aJson)) {

    if (auto dashBoardData = deviceJson->get("dashboard_data")) {
      // check if co2 is calibration now. Data available only on base station
      auto co2calibration = true;
      if (auto baseStationJson = findDeviceJson(aJson, baseStationId)){
        if (auto co2calJson = baseStationJson->get("co2_calibrating")) {
          co2calibration = co2calJson->boolValue();
        } else {
          LOG(LOG_INFO, "co2_calibrating not found ");
        }
      } else {
        LOG(LOG_INFO, "baseStationJson not found ");
      }

      if (!co2calibration) {
        if (auto CO2Json = dashBoardData->get("CO2")) {
          sensorCO2->updateSensorValue(CO2Json->int32Value());
        }
      }
    }

    if (auto batteryJson = deviceJson->get("battery_vp")) {
      statusBattery->updateInputState(batteryJson->int32Value() < LOW_BATTERY_THRESHOLD_INDOOR);
    }

    inherited::updateData(deviceJson);
  }
}


bool NetatmoAddIndoorDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("AdditionalIndoorModule_16", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string NetatmoAddIndoorDevice::modelName()
{
  return "Additional Indoor Module";
}


string NetatmoAddIndoorDevice::oemModelGUID()
{
  return "gs1:(01)7640156793765";
}


#endif // ENABLE_NETATMO_V2

