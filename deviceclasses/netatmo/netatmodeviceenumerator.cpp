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

#include "netatmodeviceenumerator.hpp"
#include "netatmoaddindoordevice.hpp"
#include "netatmohealthycoachdevice.hpp"
#include "netatmoindoorbasedevice.hpp"
#include "netatmooutdoordevice.hpp"
#include "netatmovdc.hpp"

#if ENABLE_NETATMO_V2

using namespace p44;


NetatmoDeviceEnumerator::NetatmoDeviceEnumerator(NetatmoVdc* aNetatmoVdc, NetatmoComm& aNetatmoComm) :
    netatmoVdc(aNetatmoVdc),
    netatmoComm(aNetatmoComm),
    firstBase(make_pair(numeric_limits<time_t>::max(), ""))
{

}

void NetatmoDeviceEnumerator::collectDevices(StatusCB aCompletedCB)
{
  // make sure that list is clear
  deviceList.clear();

  // get weather devices
  getWeatherDevices(aCompletedCB);
}


void NetatmoDeviceEnumerator::getWeatherDevices(StatusCB aCompletedCB)
{
  netatmoComm.apiQuery(NetatmoComm::Query::getStationsData, [=](const string &aResponse, ErrorPtr aError) {

    if (Error::isOK(aError)) {
      if (auto jsonResponse = (JsonObject::objFromText(aResponse.c_str()))) {
        if (NetatmoComm::hasAccessTokenExpired(jsonResponse)) {
          netatmoComm.refreshAccessToken([=](){ this->collectDevices(aCompletedCB); });
        } else {
          // save user email
          if (auto email = getUserEmailJson(jsonResponse)) {
            netatmoComm.setUserEmail(email->stringValue());
          }
          // parse devices
          if (auto devices = getDevicesJson(jsonResponse)) {
            collectWeatherDevices(devices);
          }
          // get home coach devices
          getHomeCoachDevices(aCompletedCB);
        }
      }
    } else {
      deviceList.clear();
      if (aCompletedCB) aCompletedCB(aError);
    }

  });
}


void NetatmoDeviceEnumerator::getHomeCoachDevices(StatusCB aCompletedCB)
{
  netatmoComm.apiQuery(NetatmoComm::Query::getHomeCoachsData, [=](const string &aResponse, ErrorPtr aError){
    if (Error::isOK(aError)) {
      if (auto devices = getDevicesJson(JsonObject::objFromText(aResponse.c_str()))) {
        collectDevicesFromArray(devices);
      }
      // discovery has been completed, add devices now
      discoveryCompleted(aCompletedCB);
    } else {
      deviceList.clear();
      if (aCompletedCB) aCompletedCB(aError);
    }

  });
}


void NetatmoDeviceEnumerator::discoveryCompleted(StatusCB aCompletedCB)
{
  netatmoVdc->identifyAndAddDevices(deviceList, aCompletedCB);
  deviceList.clear();
}


void NetatmoDeviceEnumerator::collectWeatherDevices(JsonObjectPtr aJson)
{
  if (aJson) {
    for (int index = 0; auto device = aJson->arrayGet(index); index++) {
      enumerateAndEmplaceDevice(device);
      if (auto modules = device->get("modules")) {
        collectDevicesFromArray(modules);
      }
    }

    enableOutdoorTemperatureSensor();
  }
}


void NetatmoDeviceEnumerator::collectDevicesFromArray(JsonObjectPtr aJson)
{
  if (aJson) {
    for (int index = 0; auto device = aJson->arrayGet(index); index++) {
      enumerateAndEmplaceDevice(device);
    }
  }
}


void NetatmoDeviceEnumerator::enumerateAndEmplaceDevice(JsonObjectPtr aJson)
{
  if (aJson) {
    if (auto typeJson = aJson->get("type")) {
      auto type = typeJson->stringValue();
      if (type == "NAMain") {
        if (auto idJson = aJson->get("_id")) {
          netatmoBaseId = idJson->stringValue();


          if (auto dateSetupJson = aJson->get("date_setup")) {
            auto dateSetup = dateSetupJson->int64Value();
            if (dateSetup < firstBase.first){
              firstBase = make_pair(dateSetup, netatmoBaseId);
            }
          }

        }

        deviceList.emplace_back(DevicePtr(new NetatmoIndoorBaseDevice(netatmoVdc, netatmoComm, aJson)));
      } else if (type == "NAModule1") {
        deviceList.emplace_back(DevicePtr(new NetatmoOutdoorDevice(netatmoVdc, netatmoComm, aJson, netatmoBaseId)));
      } else if (type == "NAModule4") {
        deviceList.emplace_back(DevicePtr(new NetatmoAddIndoorDevice(netatmoVdc, netatmoComm, aJson, netatmoBaseId)));
      } else if (type == "NHC") {
        deviceList.emplace_back(DevicePtr(new NetatmoHealthyCoachDevice(netatmoVdc, netatmoComm, aJson)));
      }
    }
  }
}


void NetatmoDeviceEnumerator::enableOutdoorTemperatureSensor()
{
  auto it = find_if(deviceList.begin(), deviceList.end(), [=](DevicePtr aDevice){
    auto dev = boost::dynamic_pointer_cast<NetatmoDevice>(aDevice);
    return ((dev->getNetatmoType() == "NAModule1") && (dev->getBaseStationId() == firstBase.second));
  });

  if (it != deviceList.end()){
    auto dev = boost::dynamic_pointer_cast<NetatmoDevice>(*it);
    dev->setUsageArea(usage_outdoors);
  }
}


JsonObjectPtr NetatmoDeviceEnumerator::getDevicesJson(JsonObjectPtr aJson)
{
  if (aJson) {
    if (auto body = aJson->get("body")) {
      if (auto devices = body->get("devices")) {
        return devices;
      }
    }
  }
  return nullptr;
}


JsonObjectPtr NetatmoDeviceEnumerator::getUserEmailJson(JsonObjectPtr aJson)
{
  if (aJson) {
    if (auto body = aJson->get("body")) {
      if (auto user = body->get("user")) {
        if (auto mail = user->get("mail")) {
          return mail;
        }
      }
    }
  }
  return nullptr;
}




#endif // ENABLE_NETATMO_V2

