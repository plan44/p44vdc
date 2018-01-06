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

#include "netatmohealthycoachdevice.hpp"


#if ENABLE_NETATMO_V2

using namespace p44;


// MARK: ===== NetatmoDevice


enum class NetatmoHealthyCoachDevice::StatusHealthIndex {
    healthy,
    fine,
    fair,
    poor,
    unhealthy,
    _num
};


NetatmoHealthyCoachDevice::NetatmoHealthyCoachDevice(NetatmoVdc *aVdcP, INetatmoComm& aINetatmoComm, JsonObjectPtr aDeviceData) :
  inherited(aVdcP, aINetatmoComm, aDeviceData, usage_room)
{

}


void NetatmoHealthyCoachDevice::configureDevice()
{
  sensorCO2 = createSensorCO2();
  addBehaviour(sensorCO2);

  sensorNoise = createSensorNoise();
  addBehaviour(sensorNoise);

  statusBattery = createStatusBattery();
  addBehaviour(statusBattery);

  EnumValueDescriptorPtr healthIndexEnum = EnumValueDescriptorPtr(new EnumValueDescriptor("StatusHealthIndex"));
  healthIndexEnum->addEnum("healthy", static_cast<int>(StatusHealthIndex::healthy));
  healthIndexEnum->addEnum("fine", static_cast<int>(StatusHealthIndex::fine));
  healthIndexEnum->addEnum("fair", static_cast<int>(StatusHealthIndex::fair));
  healthIndexEnum->addEnum("poor", static_cast<int>(StatusHealthIndex::poor));
  healthIndexEnum->addEnum("unhealthy", static_cast<int>(StatusHealthIndex::unhealthy));

  statusHealthIndex = DeviceStatePtr(new DeviceState(*this, "StatusHealthIndex", "Health Index", healthIndexEnum, [](auto...){}));
  deviceStates->addState(statusHealthIndex);

  inherited::configureDevice();
}


void NetatmoHealthyCoachDevice::updateData(JsonObjectPtr aJson)
{
  if (auto deviceJson = findDeviceJson(aJson, netatmoId)) {
     auto co2calibration = true;
     if (auto co2calJson = deviceJson->get("co2_calibrating")) {
       co2calibration = co2calJson->boolValue();
     }

     if (auto dashBoardData = deviceJson->get("dashboard_data")) {
       if (!co2calibration) {
         if (auto CO2Json = dashBoardData->get("CO2")) {
           sensorCO2->updateSensorValue(CO2Json->int32Value());
         }
       }

       if (auto noiseJson = dashBoardData->get("Noise")) {
         sensorNoise->updateSensorValue(noiseJson->int32Value());
       }

       if (auto healthIndexJson = dashBoardData->get("health_idx")) {
         statusHealthIndex->value()->setInt32Value(healthIndexJson->int32Value());
       }
     }

     if (auto batteryJson = deviceJson->get("battery_vp")) {
        statusBattery->updateInputState(batteryJson->int32Value() < LOW_BATTERY_THRESHOLD_INDOOR);
      }

     inherited::updateData(deviceJson);
   }
}


bool NetatmoHealthyCoachDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("HealthyHomeCoach_16", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string NetatmoHealthyCoachDevice::modelName()
{
  return "Healthy Home Coach";
}


string NetatmoHealthyCoachDevice::oemModelGUID()
{
  return "gs1:(01)7640156793772";
}



#endif // ENABLE_NETATMO_V2

