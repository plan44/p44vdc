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

#include "netatmooutdoordevice.hpp"


#if ENABLE_NETATMO

using namespace p44;


// MARK: ===== NetatmoDevice


NetatmoOutdoorDevice::NetatmoOutdoorDevice(NetatmoVdc *aVdcP, INetatmoComm& aINetatmoComm, JsonObjectPtr aDeviceData, const string& aBaseStationId) :
  inherited(aVdcP, aINetatmoComm, aDeviceData, usage_outdoors, aBaseStationId)
{

}


void NetatmoOutdoorDevice::configureDevice()
{
  sensorPressure =  SensorBehaviourPtr(new SensorBehaviour(*this, "SensorPressure"));
  sensorPressure->setHardwareSensorConfig(sensorType_air_pressure, usageArea, 260, 1160, 1, SENSOR_UPDATE_INTERVAL, SENSOR_ALIVESIGN_INTERVAL);
  sensorPressure->setSensorNameWithRange("Air Pressure");
  addBehaviour(sensorPressure);

  statusBattery = createStatusBattery();
  addBehaviour(statusBattery);

  EnumValueDescriptorPtr pressureTrendEnum = createTrendEnum("StatusPressureTrend");
  statusPressureTrend = DeviceStatePtr(new DeviceState(
      *this, "StatusPressureTrend", "Pressure trend", pressureTrendEnum, [](auto...){}
  ));
  deviceStates->addState(statusPressureTrend);

  inherited::configureDevice();
}


void NetatmoOutdoorDevice::updateData(JsonObjectPtr aJson)
{
  // FIXME:    missing statusPressureTrend;

  /* pressure is stored in base station*/
  if (auto baseStationJson = findDeviceJson(aJson, baseStationId)) {
    if (auto dashBoardData = baseStationJson->get("dashboard_data")) {

      if (auto pressureJson = dashBoardData->get("Pressure")) {
        sensorPressure->updateSensorValue(pressureJson->doubleValue());
      }
    }
  }

  if (auto deviceJson = findModuleJson(aJson)) {
    if (auto batteryJson = deviceJson->get("battery_vp")) {
      statusBattery->updateInputState(batteryJson->int32Value() < LOW_BATTERY_THRESHOLD_OUTDOOR);
    }



    inherited::updateData(deviceJson);
  }
}


bool NetatmoOutdoorDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  //TODO: device icons
  if (getIcon("harmony", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string NetatmoOutdoorDevice::modelName()
{
  return "Weather Station Outdoor Module";
}


string NetatmoOutdoorDevice::oemModelGUID()
{
  return "gs1:(01)7640156793758"; // from aizo/dS number space, as defined 2016-12-11
}


#endif // ENABLE_NETATMO

