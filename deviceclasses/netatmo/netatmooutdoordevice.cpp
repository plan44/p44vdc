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


#if ENABLE_NETATMO_V2

using namespace p44;


// MARK: ===== NetatmoDevice


NetatmoOutdoorDevice::NetatmoOutdoorDevice(NetatmoVdc *aVdcP, INetatmoComm& aINetatmoComm, JsonObjectPtr aDeviceData, const string& aBaseStationId) :
  inherited(aVdcP, aINetatmoComm, aDeviceData, usage_undefined, aBaseStationId)
{

}


void NetatmoOutdoorDevice::configureDevice()
{
  statusBattery = createStatusBattery();
  addBehaviour(statusBattery);

  inherited::configureDevice();
}


void NetatmoOutdoorDevice::updateData(JsonObjectPtr aJson)
{
  if (auto deviceJson = findModuleJson(aJson)) {
    if (auto batteryJson = deviceJson->get("battery_vp")) {
      statusBattery->updateInputState(batteryJson->int32Value() < LOW_BATTERY_THRESHOLD_OUTDOOR);
    }

    inherited::updateData(deviceJson);
  }
}


bool NetatmoOutdoorDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("WeatherStationOutdoorModule_16", aIcon, aWithData, aResolutionPrefix))
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


#endif // ENABLE_NETATMO_V2

