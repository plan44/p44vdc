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

#include "netatmovdc.hpp"
#include "netatmodevice.hpp"


#if ENABLE_NETATMO_V2

using namespace p44;


// MARK: ===== NetatmoDevice


NetatmoDevice::NetatmoDevice(NetatmoVdc *aVdcP, INetatmoComm& aINetatmoComm, JsonObjectPtr aDeviceData, VdcUsageHint aUsageArea, const string& aBaseStationId) :
  inherited(aVdcP),
  usageArea(aUsageArea),
  baseStationId(aBaseStationId),
  isPresent(true),
  measurementAbsloluteTimestamp(chrono::system_clock::to_time_t(chrono::system_clock::now()))
{
  setIdentificationData(aDeviceData);

  setColorClass(class_white_singledevices);
  installSettings(DeviceSettingsPtr(new NetatmoDeviceSettings(*this)));
  // - set a action output behaviour (no classic output properties and channels)
  OutputBehaviourPtr ab = OutputBehaviourPtr(new ActionOutputBehaviour(*this));
  ab->setGroupMembership(group_undefined, true);
  addBehaviour(ab);

  cbConnection = aINetatmoComm.registerCallback([=](auto...params){ this->updateData(params...); });

}


NetatmoDevice::~NetatmoDevice()
{
  cbConnection.disconnect();
}


void NetatmoDevice::setIdentificationData(JsonObjectPtr aJson)
{
  if (aJson) {
    if (auto typeJson = aJson->get("type")) {
      netatmoType = typeJson->stringValue();
    }

    if (auto idJson = aJson->get("_id")) {
      netatmoId = idJson->stringValue();
    }

    if (auto nameJson = aJson->get("module_name")) {
      netatmoName = nameJson->stringValue();
      initializeName(netatmoName);
    }

    if (auto fwJson = aJson->get("firmware")) {
      netatmoFw = fwJson->stringValue();
    }
  }
}


void NetatmoDevice::configureDevice()
{

  swVersion = ValueDescriptorPtr(new TextValueDescriptor("SwVersion"));
  deviceProperties->addProperty(swVersion, true);

  measurementTimestamp = ValueDescriptorPtr(new NumericValueDescriptor("MeasurementTimestamp", valueType_numeric, VALUE_UNIT(valueUnit_second, unitScaling_1), 0, (24*60*60), 1));
  deviceProperties->addProperty(measurementTimestamp, true);

  sensorTemperature = SensorBehaviourPtr(new SensorBehaviour(*this, "SensorTemperature"));
  sensorTemperature->setHardwareSensorConfig(sensorType_temperature, usageArea, -40, 65, 0.1, SENSOR_UPDATE_INTERVAL, SENSOR_ALIVESIGN_INTERVAL);
  sensorTemperature->setSensorNameWithRange("Temperature");
  addBehaviour(sensorTemperature);

  sensorHumidity = SensorBehaviourPtr(new SensorBehaviour(*this, "SensorHumidity"));
  sensorHumidity->setHardwareSensorConfig(sensorType_humidity, usageArea, 0, 100, 1, SENSOR_UPDATE_INTERVAL, SENSOR_ALIVESIGN_INTERVAL);
  sensorHumidity->setSensorNameWithRange("Humidity");
  sensorHumidity->setGroup(group_undefined);
  addBehaviour(sensorHumidity);

  EnumValueDescriptorPtr tempTrendEnum = createTrendEnum("StatusTempTrend");
  statusTempTrend = DeviceStatePtr(new DeviceState(
      *this, "StatusTempTrend", "Temperature trend", tempTrendEnum, [](auto...){}
  ));
  deviceStates->addState(statusTempTrend);

  // derive the dSUID
  deriveDsUid();
}


void NetatmoDevice::updateData(JsonObjectPtr aJson)
{
  if (aJson) {
    if (auto swVersionJson = aJson->get("firmware")) {
      swVersion->setStringValue(swVersionJson->stringValue());
    }

    if (auto dashBoardData = aJson->get("dashboard_data")) {
      if (auto tempJson = dashBoardData->get("Temperature")) {
        sensorTemperature->updateSensorValue(tempJson->doubleValue());
      }

      if (auto humidityJson = dashBoardData->get("Humidity")) {
        sensorHumidity->updateSensorValue(humidityJson->doubleValue());
      }

      if (auto tempTrendJson = dashBoardData->get("temp_trend")) {
        auto statusTrend = getStatusTrend(tempTrendJson->stringValue());
        if (statusTrend < StatusTrend::_num){
          if (statusTempTrend->value()->setInt32Value(static_cast<int>(statusTrend))){
            statusTempTrend->push();
          }
        }
      }

      if (auto timestampJson = dashBoardData->get("time_utc")){
        measurementAbsloluteTimestamp = timestampJson->int64Value();
        auto secondsToday = getSecondsFromMidnight(measurementAbsloluteTimestamp);
        measurementTimestamp->setInt32Value(secondsToday.count());
      }

    }
  }

  bool wasPresent = isPresent;
  isPresent = getElapsedHoursFromLastMeasure(measurementAbsloluteTimestamp).count() < LAST_MEASUREMENT_ELAPSED_HOURS_MAX;

  // if last measurement was measured longer than 12 hrs ago, set device to inactive
  if (wasPresent != isPresent && !isPresent) reportVanished();

}


chrono::seconds NetatmoDevice::getSecondsFromMidnight(time_t aTimestamp)
{
  using namespace chrono;

  system_clock::time_point timestamp = system_clock::from_time_t(aTimestamp);

  struct tm * timeStruct = localtime(&aTimestamp);
  // set midnight time
  timeStruct->tm_sec = 0;
  timeStruct->tm_min = 0;
  timeStruct->tm_hour = 0;

  system_clock::time_point midnight = system_clock::from_time_t(mktime(timeStruct));

  return duration_cast<seconds>(timestamp - midnight);
}


chrono::hours NetatmoDevice::getElapsedHoursFromLastMeasure(time_t aTimestamp)
{
  using namespace chrono;

  system_clock::time_point timestamp = system_clock::from_time_t(aTimestamp);
  system_clock::time_point now = system_clock::now();

  return duration_cast<hours>(now - timestamp);
}


JsonObjectPtr NetatmoDevice::findDeviceJson(JsonObjectPtr aJsonArray, const string& aDeviceId)
{
  if (aJsonArray) {
    for (int index=0; auto device = aJsonArray->arrayGet(index); index++) {
      if (auto id = device->get("_id")) {
        if (id->stringValue() == aDeviceId) return device;
      }
    }
  }
  return nullptr;
}


JsonObjectPtr NetatmoDevice::findModuleJson(JsonObjectPtr aJsonArray)
{

  if (auto baseStationDevice = findDeviceJson(aJsonArray, baseStationId)) {
    if (auto modules = baseStationDevice->get("modules")) {
      if (auto module = findDeviceJson(modules, netatmoId)) return module;
    }
  }
  return nullptr;
}


NetatmoDevice::StatusTrend NetatmoDevice::getStatusTrend(const string& aTrend)
{
  if      (aTrend == "up")      return StatusTrend::rising;
  else if (aTrend == "stable")  return StatusTrend::steady;
  else if (aTrend == "down")    return StatusTrend::sinking;

  return StatusTrend::_num;
}


SensorBehaviourPtr NetatmoDevice::createSensorCO2()
{
  auto sensorCO2 = SensorBehaviourPtr(new SensorBehaviour(*this, "SensorCO2"));
  sensorCO2->setHardwareSensorConfig(sensorType_gas_CO2, usageArea, 0, 5000, 1, SENSOR_UPDATE_INTERVAL, SENSOR_ALIVESIGN_INTERVAL);
  sensorCO2->setSensorNameWithRange("CO2 Concentration");
  return sensorCO2;
}


SensorBehaviourPtr NetatmoDevice::createSensorNoise()
{
  auto sensorNoise = SensorBehaviourPtr(new SensorBehaviour(*this, "SensorNoise"));
  sensorNoise->setHardwareSensorConfig(sensorType_sound_volume, usageArea, 35, 120, 1, SENSOR_UPDATE_INTERVAL, SENSOR_ALIVESIGN_INTERVAL);
  sensorNoise->setSensorNameWithRange("Noise");
  return sensorNoise;
}


BinaryInputBehaviourPtr NetatmoDevice::createStatusBattery()
{
  auto battery = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this,"StatusBattery"));
  battery->setHardwareInputConfig(binInpType_lowBattery, usageArea, true, Never);
  battery->setGroup(group_black_variable);
  battery->setHardwareName("Battery");
  return battery;
}

EnumValueDescriptorPtr NetatmoDevice::createTrendEnum(const string& aName)
{
  EnumValueDescriptorPtr trendEnum = new EnumValueDescriptor(aName);
  trendEnum->addEnum("rising", static_cast<int>(StatusTrend::rising));
  trendEnum->addEnum("steady", static_cast<int>(StatusTrend::steady));
  trendEnum->addEnum("sinking", static_cast<int>(StatusTrend::sinking));
  return trendEnum;
}


void NetatmoDevice::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  ALOG(LOG_INFO, "- stateChanged: %s changed from '%s' to '%s'",
      aChangedState->getId().c_str(),
      aChangedState->value()->getStringValue(false, true).c_str(),
      aChangedState->value()->getStringValue(false, false).c_str()
  );
}


bool NetatmoDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  configureDevice();
  // Note: not using instant identification here, because we eventually need API calls here.
  identificationOK(aIdentifyCB);
  return false;
}


NetatmoVdc &NetatmoDevice::netatmoVdc()
{
  return *(static_cast<NetatmoVdc *>(vdcP));
}



void NetatmoDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  if (aCompletedCB) aCompletedCB(Error::ok());
}


string NetatmoDevice::hardwareGUID()
{
  return string_format("netatmoDeviceId:%s", netatmoId.c_str());
}


string NetatmoDevice::modelName()
{
  return netatmoFw;
}


string NetatmoDevice::vendorName()
{
  return "Netatmo";
}


void NetatmoDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  if (aPresenceResultHandler) aPresenceResultHandler(isPresent);
}


void NetatmoDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
 // TODO
}


void NetatmoDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = "netatmodevice::";
  s += netatmoId;
  dSUID.setNameInSpace(s, vdcNamespace);
}


string NetatmoDevice::description()
{
  return string_format("\n- device model: %s, device id: %s", modelName().c_str(), netatmoId.c_str());
}


#endif // ENABLE_NETATMO_V2

