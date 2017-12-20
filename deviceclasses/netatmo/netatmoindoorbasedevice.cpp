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

#include "netatmoindoorbasedevice.hpp"


#if ENABLE_NETATMO_V2

using namespace p44;


// MARK: ===== NetatmoIndoorBaseDevice


NetatmoIndoorBaseDevice::NetatmoIndoorBaseDevice(NetatmoVdc *aVdcP, INetatmoComm& aINetatmoComm, JsonObjectPtr aDeviceData) :
  inherited(aVdcP, aINetatmoComm, aDeviceData, usage_room)
{

}


void NetatmoIndoorBaseDevice::configureDevice()
{
  sensorPressure =  SensorBehaviourPtr(new SensorBehaviour(*this, "SensorPressure"));
  sensorPressure->setHardwareSensorConfig(sensorType_air_pressure, usageArea, 260, 1160, 1, SENSOR_UPDATE_INTERVAL, SENSOR_ALIVESIGN_INTERVAL);
  sensorPressure->setSensorNameWithRange("Air Pressure");
  addBehaviour(sensorPressure);

  sensorCO2 = createSensorCO2();
  addBehaviour(sensorCO2);

  sensorNoise = createSensorNoise();
  addBehaviour(sensorNoise);

  EnumValueDescriptorPtr pressureTrendEnum = createTrendEnum("StatusPressureTrend");
  statusPressureTrend = DeviceStatePtr(new DeviceState(
      *this, "StatusPressureTrend", "Pressure trend", pressureTrendEnum, [](auto...){}
  ));
  deviceStates->addState(statusPressureTrend);

  inherited::configureDevice();
}


void NetatmoIndoorBaseDevice::updateData(JsonObjectPtr aJson)
{
  if (auto deviceJson = findDeviceJson(aJson, netatmoId)) {
    auto co2calibration = true;
    if (auto co2calJson = deviceJson->get("co2_calibrating")) {
      co2calibration = co2calJson->boolValue();
    }

    if (auto dashBoardData = deviceJson->get("dashboard_data")) {

      if (auto pressureJson = dashBoardData->get("Pressure")) {
        sensorPressure->updateSensorValue(pressureJson->doubleValue());
      }

      if (!co2calibration) {
        if (auto CO2Json = dashBoardData->get("CO2")) {
          sensorCO2->updateSensorValue(CO2Json->int32Value());
        }
      }

      if (auto noiseJson = dashBoardData->get("Noise")) {
        sensorNoise->updateSensorValue(noiseJson->int32Value());
      }

      if (auto pressureTrendJson = dashBoardData->get("pressure_trend")) {
        auto statusTrend = getStatusTrend(pressureTrendJson->stringValue());
        if (statusTrend < StatusTrend::_num){
          if (statusPressureTrend->value()->setInt32Value(static_cast<int>(statusTrend))) {
            statusPressureTrend->push();
          }
        }
      }
    }

    inherited::updateData(deviceJson);
  }
}



bool NetatmoIndoorBaseDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("WeatherStationIndoorBase_16", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string NetatmoIndoorBaseDevice::modelName()
{
  return "Weather Station Indoor Base";
}


string NetatmoIndoorBaseDevice::oemModelGUID()
{
  return "gs1:(01)7640156793741";
}


#endif // ENABLE_NETATMO_V2

