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

#ifndef __p44vdc__netatmodeviceenumerator__
#define __p44vdc__netatmodeviceenumerator__


#include "p44vdc_common.hpp"

#if ENABLE_NETATMO_V2

#include "jsonobject.hpp"
#include "vdc.hpp"
#include "netatmodevice.hpp"

namespace p44 {

  class NetatmoVdc;
  class NetatmoComm;

  class NetatmoDeviceEnumerator {

      NetatmoVdc* netatmoVdc;
      NetatmoComm& netatmoComm;
      NetatmoDeviceList deviceList;
      string netatmoBaseId;

      pair<time_t, string> firstBase;


    public:
      NetatmoDeviceEnumerator(NetatmoVdc* aNetatmoVdc, NetatmoComm& aNetatmoComm);
      ~NetatmoDeviceEnumerator(){};

      void collectDevices(StatusCB aCompletedCB);

      void enumerateAndEmplaceDevice(JsonObjectPtr aJson);
      void collectWeatherDevices(JsonObjectPtr aJson);
      void collectDevicesFromArray(JsonObjectPtr aJson);
      static JsonObjectPtr getDevicesJson(JsonObjectPtr aJson);
      static JsonObjectPtr getUserEmailJson(JsonObjectPtr aJson);
      void enableOutdoorTemperatureSensor();
  };

} // namespace p44


#endif // ENABLE_NETATMO_V2
#endif // __p44vdc__netatmodeviceenumerator__
