//
//  Copyright (c) 2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
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

#include "homeconnectdevice.hpp"

#if ENABLE_HOMECONNECT

#include "homeconnectvdc.hpp"
#include "homeconnectaction.hpp"

#include "homeconnectdevicecoffemaker.hpp"
#include "homeconnectdeviceoven.hpp"
#include "homeconnectdevicedishwasher.hpp"
#include "homeconnectdevicewasher.hpp"
#include "homeconnectdevicedryer.hpp"
#include "homeconnectdevicefridge.hpp"
#include "homeconnectaction.hpp"
#include <sstream>
#include <boost/algorithm/string/split.hpp>
#include <functional>

using namespace p44;



// MARK: ===== HomeConnectDeviceSettings + HomeConnectScene

DsScenePtr HomeConnectDeviceSettings::newDefaultScene(SceneNo aSceneNo)
{
  HomeConnectScenePtr homeConnectScene = HomeConnectScenePtr(new HomeConnectScene(*this, aSceneNo));
  homeConnectScene->setDefaultSceneValues(aSceneNo);
  // return it
  return homeConnectScene;
}


void HomeConnectScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // set the common simple scene defaults
  inherited::setDefaultSceneValues(aSceneNo);
  // modify scenes according to dS kettle standard behaviour
  switch (aSceneNo) {
    case ABSENT:
      setActionIfNotEmpty(deviceSettings.leaveHomeAction);
      break;
    case FIRE:
      setActionIfNotEmpty(deviceSettings.fireAction);
      break;
    case SLEEPING:
      setActionIfNotEmpty(deviceSettings.sleepAction);
      break;
    case DEEP_OFF:
      setActionIfNotEmpty(deviceSettings.deepOffAction);
      break;

    default:
      // no operation by default for all other scenes
      setDontCare(true);
      break;
  }
  markClean(); // default values are always clean
}

void HomeConnectScene::setActionIfNotEmpty(const string& aAction)
{
  if(aAction.empty()) {
    setDontCare(true);
    return;
  }

  setDontCare(false);
  value = 0;
  command = aAction;
}

HomeConnectProgramBuilder::HomeConnectProgramBuilder(const string& aProgramName) :
  programName(aProgramName),
  mode(Mode_Activate)
{
}

string HomeConnectProgramBuilder::build()
{
  stringstream ss;

  ss << "PUT:programs/" << toString(mode) << ":{\"data\":{\"key\":\"" << programName << "\",";

  if(options.size() != 0)
  {

    ss << "\"options\":[";

    for(map<string, string>::iterator it = options.begin(); it != options.end(); it++)
    {
      ss << "{ \"key\":\"" << it->first << "\",\"value\":" << it->second << "},";
    }

    ss.get(); //remove last comma
    ss << "]";
  }

  ss << "}}";
  return ss.str();

}

HomeConnectSettingBuilder::HomeConnectSettingBuilder(const string& aSettingName) :
  settingName(aSettingName)
{
}

string HomeConnectSettingBuilder::build()
{
  stringstream ss;

  ss << "PUT:settings/" << settingName << ":{\"data\":{\"key\":\"" << settingName << "\",\"value\":" << value << "}}";

  return ss.str();
}





// MARK: ===== HomeConnectDevice


//  {
//    "haId": "BOSCH-HCS06COM1-xxxxxxxxx",
//    "vib": "HCS06COM1",
//    "brand": "BOSCH",
//    "type": "CoffeeMaker",
//    "name": "CoffeeMaker Simulator",
//    "enumber": "HCS06COM1\/01",
//    "connected": true
//  }

// Standalone device
// Note: This one does NOT support the ConsumerProducts.CoffeeMaker.Option.CoffeeTemperature option,
//  at least not with the enums as described in the API specs
//  {
//    "name": "Kaffeevollautomat",
//    "brand": "Siemens",
//    "vib": "TI909701HC",
//    "connected": true,
//    "type": "CoffeeMaker",
//    "enumber": "TI909701HC\/03",
//    "haId": "SIEMENS-TI909701HC-xxxxxxxx"
//  }

// Example of all simulated devices
//  {
//    "data": {
//      "homeappliances": [
//        {
//          "haId": "BOSCH-HCS06COM1-540745B9FEF475",
//          "vib": "HCS06COM1",
//          "brand": "BOSCH",
//          "type": "CoffeeMaker",
//          "name": "CoffeeMaker Simulator",
//          "enumber": "HCS06COM1/01",
//          "connected": true
//        },
//        {
//          "haId": "SIEMENS-HCS02DWH1-79AFEC3005AA71",
//          "vib": "HCS02DWH1",
//          "brand": "SIEMENS",
//          "type": "Dishwasher",
//          "name": "Dishwasher Simulator",
//          "enumber": "HCS02DWH1/03",
//          "connected": true
//        },
//        {
//          "haId": "BOSCH-HCS04DYR1-AC61438DAE236C",
//          "vib": "HCS04DYR1",
//          "brand": "BOSCH",
//          "type": "Dryer",
//          "name": "Dryer Simulator",
//          "enumber": "HCS04DYR1/03",
//          "connected": true
//        },
//        {
//          "haId": "SIEMENS-HCS05FRF1-D516FBECC462AD",
//          "vib": "HCS05FRF1",
//          "brand": "SIEMENS",
//          "type": "FridgeFreezer",
//          "name": "Fridge Freezer Simulator",
//          "enumber": "HCS05FRF1/03",
//          "connected": true
//        },
//        {
//          "haId": "BOSCH-HCS01OVN1-6796CE1BD8A471",
//          "vib": "HCS01OVN1",
//          "brand": "BOSCH",
//          "type": "Oven",
//          "name": "Oven Simulator",
//          "enumber": "HCS01OVN1/03",
//          "connected": true
//        },
//        {
//          "haId": "SIEMENS-HCS03WCH1-2A48D7099DB1EB",
//          "vib": "HCS03WCH1",
//          "brand": "SIEMENS",
//          "type": "Washer",
//          "name": "Washer Simulator",
//          "enumber": "HCS03WCH1/03",
//          "connected": true
//        }
//      ]
//    }
//  }


HomeConnectDevice::HomeConnectDevice(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
  inherited(aVdcP),
  isConnected(false)
{
  // home connect appliances are single devices
  setColorClass(class_white_singledevices);
  // - set a action output behaviour (no classic output properties and channels)
  OutputBehaviourPtr ab = OutputBehaviourPtr(new ActionOutputBehaviour(*this));
  ab->setGroupMembership(group_black_variable, true);
  addBehaviour(ab);
  LOG(LOG_DEBUG, "ApplianceInfo = %s", aHomeApplicanceInfoRecord->c_strValue());
  // set basic info
  JsonObjectPtr o;
  string vib;
  if (aHomeApplicanceInfoRecord->get("haId", o))
    haId = o->stringValue();

  if (aHomeApplicanceInfoRecord->get("brand", o)) {
    model = o->stringValue();
    if (aHomeApplicanceInfoRecord->get("vib", o))
      vib = o->stringValue();
      model += " " + vib;
  }
  if (aHomeApplicanceInfoRecord->get("enumber", o))
    modelGuid = o->stringValue();
  if (aHomeApplicanceInfoRecord->get("brand", o))
    vendor = o->stringValue();

  if (aHomeApplicanceInfoRecord->get("connected", o))
    isConnected = o->boolValue();

  string dir = getVdcHost().getConfigDir();

  string fn = dir  + "singledevicesettings_homeconnect_" + vib + ".json";
  JsonObjectPtr config = JsonObject::objFromFile(fn.c_str());
  if (!config) {
    ALOG(LOG_WARNING, "Cannot read configuration file: '%s'", fn.c_str());
    return;
  }

  ALOG(LOG_DEBUG, "Configuration file read successfully: '%s'", fn.c_str());

  if (!config->get("dSGTIN", o)) {
    ALOG(LOG_WARNING, "dSGTIN not defined in configuration file");
    return;
  }

  gtin = o->stringValue();

  ALOG(LOG_DEBUG, "Device GTIN read from file: '%s'", gtin.c_str());

  initializeName(createDeviceName(aHomeApplicanceInfoRecord, config));
}

HomeConnectDevice::~HomeConnectDevice()
{

}

HomeConnectDevicePtr HomeConnectDevice::createHomeConenctDevice(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord)
{
  JsonObjectPtr o;
  HomeConnectDevicePtr retVal = NULL;

  // analyze the type of device in this json object and create proper specialized type
  if (aHomeApplicanceInfoRecord->get("type", o)) {
    string ty = o->stringValue();

    if (ty=="CoffeeMaker") {
      retVal = HomeConnectDevicePtr(new HomeConnectDeviceCoffeMaker(aVdcP, aHomeApplicanceInfoRecord));
    }
    else if (ty=="Oven") {
      retVal = HomeConnectDevicePtr(new HomeConnectDeviceOven(aVdcP, aHomeApplicanceInfoRecord));
    }
    else if (ty=="Dishwasher") {
      retVal = HomeConnectDevicePtr(new HomeConnectDeviceDishWasher(aVdcP, aHomeApplicanceInfoRecord));
    }
    else if (ty=="Washer") {
      retVal = HomeConnectDevicePtr(new HomeConnectDeviceWasher(aVdcP, aHomeApplicanceInfoRecord));
    }
    else if (ty=="Dryer") {
      retVal = HomeConnectDevicePtr(new HomeConnectDeviceDryer(aVdcP, aHomeApplicanceInfoRecord));
    }
    else if (ty=="FridgeFreezer") {
      retVal = HomeConnectDevicePtr(new HomeConnectDeviceFridge(aVdcP, aHomeApplicanceInfoRecord));
    }
    else {
      LOG(LOG_NOTICE, "Unknown device type '%s'", ty.c_str());
    }
  }

  return retVal;
}


bool HomeConnectDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  configureDevice(boost::bind(&HomeConnectDevice::configurationDone, this, aIdentifyCB, _1));
  return false;
}

void HomeConnectDevice::configurationDone(IdentifyDeviceCB aIdentifyCB, ErrorPtr aError)
{
  autoAddStandardActions();
  deriveDsUid();
  if (aIdentifyCB) aIdentifyCB(aError, this);
}

void HomeConnectDevice::configureOperationModeState(const OperationModeConfiguration& aConfiguration)
{
  // - operation mode
  operationModeDescriptor = new EnumValueDescriptor("OperationMode", true);
  int currentEnumValue = 0;

  if (aConfiguration.hasInactive) {
    operationModeDescriptor->addEnum("ModeInactive", currentEnumValue++);
  }
  if (aConfiguration.hasReady) {
    operationModeDescriptor->addEnum("ModeReady", currentEnumValue++);
  }
  if (aConfiguration.hasDelayedStart) {
    operationModeDescriptor->addEnum("ModeDelayedStart", currentEnumValue++);
  }
  if (aConfiguration.hasRun) {
    operationModeDescriptor->addEnum("ModeRun", currentEnumValue++);
  }
  if (aConfiguration.hasPause) {
    operationModeDescriptor->addEnum("ModePause", currentEnumValue++);
  }
  if (aConfiguration.hasActionrequired) {
    operationModeDescriptor->addEnum("ModeActionRequired", currentEnumValue++);
  }
  if (aConfiguration.hasFinished) {
    operationModeDescriptor->addEnum("ModeFinished", currentEnumValue++);
  }
  if (aConfiguration.hasError) {
    operationModeDescriptor->addEnum("ModeError", currentEnumValue++);
  }
  if (aConfiguration.hasAborting) {
    operationModeDescriptor->addEnum("ModeAborting", currentEnumValue++);
  }
  operationMode = DeviceStatePtr(
      new DeviceState(*this, "OperationMode", "Status", operationModeDescriptor,
          boost::bind(&HomeConnectDevice::stateChanged, this, _1, _2)));
  deviceStates->addState(operationMode);
}

void HomeConnectDevice::configureRemoteControlState(const RemoteControlConfiguration& aConfiguration)
{
  // - remote control
  remoteControlDescriptor= new EnumValueDescriptor("RemoteControl", true);
  int currentEnumValue = 0;

  if (aConfiguration.hasControlInactive) {
    remoteControlDescriptor->addEnum("RemoteControlInactive", currentEnumValue++);
  }
  if (aConfiguration.hasControlActive) {
    remoteControlDescriptor->addEnum("RemoteControlActive", currentEnumValue++);
  }
  if (aConfiguration.hasStartActive) {
    remoteControlDescriptor->addEnum("RemoteStartActive", currentEnumValue++);
  }

  remoteControl = DeviceStatePtr(
      new DeviceState(*this, "RemoteControl", "Remote Control", remoteControlDescriptor,
          boost::bind(&HomeConnectDevice::stateChanged, this, _1, _2)));
  deviceStates->addState(remoteControl);
}

void HomeConnectDevice::configureDoorState(const DoorStateConfiguration& aConfiguration)
{
  // - door state
  doorStateDescriptor = new EnumValueDescriptor("DoorState", true);
  int currentEnumValue = 0;

  if (aConfiguration.hasOpen) {
    doorStateDescriptor->addEnum("DoorOpen", currentEnumValue++);
  }
  if (aConfiguration.hasClosed) {
    doorStateDescriptor->addEnum("DoorClosed", currentEnumValue++);
  }
  if (aConfiguration.hasLocked) {
    doorStateDescriptor->addEnum("DoorLocked", currentEnumValue++);
  }
  doorState = DeviceStatePtr(
      new DeviceState(*this, "DoorState", "Door State", doorStateDescriptor,
          boost::bind(&HomeConnectDevice::stateChanged, this, _1, _2)));
  deviceStates->addState(doorState);
}

void HomeConnectDevice::configurePowerState(const PowerStateConfiguration& aConfiguration)
{
  // - operation mode
  powerStateDescriptor = new EnumValueDescriptor("PowerState", true);
  int currentEnumValue = 0;

  if (aConfiguration.hasOff) {
    powerStateDescriptor->addEnum("PowerOff", currentEnumValue++);
  }
  if (aConfiguration.hasOn) {
    powerStateDescriptor->addEnum("PowerOn", currentEnumValue++);
  }
  if (aConfiguration.hasStandby) {
    powerStateDescriptor->addEnum("PowerStandby", currentEnumValue++);
  }

  powerState = DeviceStatePtr(
      new DeviceState(*this, "PowerState", "Power State", powerStateDescriptor,
          boost::bind(&HomeConnectDevice::stateChanged, this, _1, _2)));
  deviceStates->addState(powerState);
}

void HomeConnectDevice::configureProgramStatus(const ProgramStatusConfiguration& aConfiguration)
{
  if (aConfiguration.hasElapsedTime) {
    elapsedProgramTime = ValueDescriptorPtr(
        new NumericValueDescriptor("ElapsedProgramTime", valueType_numeric, VALUE_UNIT(valueUnit_second, unitScaling_1), 0, 86340, 1));
    deviceProperties->addProperty(elapsedProgramTime);
  }

  if (aConfiguration.hasRemainingTime) {
    remainingProgramTime = ValueDescriptorPtr(
        new NumericValueDescriptor("RemainingProgramTime", valueType_numeric, VALUE_UNIT(valueUnit_second, unitScaling_1), 0, 86340, 1));
    deviceProperties->addProperty(remainingProgramTime);
  }

  if (aConfiguration.hasProgres) {
    programProgress = ValueDescriptorPtr(
        new NumericValueDescriptor("ProgramProgress", valueType_numeric, VALUE_UNIT(valueUnit_percent, unitScaling_1), 0, 100, 1));
    deviceProperties->addProperty(programProgress);
  }
}


void HomeConnectDevice::configureEvents(const EventConfiguration& aConfiguration)
{
  if (aConfiguration.hasAlarmClockElapsed) {
    deviceEvents->addEvent(DeviceEventPtr(new DeviceEvent(*this, "AlarmClockElapsed", "Alarm Clock Elapsed")));
  }
  if (aConfiguration.hasLocallyOperated) {
    deviceEvents->addEvent(DeviceEventPtr(new DeviceEvent(*this, "LocallyOperated", "Locally Operated")));
  }
  if (aConfiguration.hasProgramAborted) {
    deviceEvents->addEvent(DeviceEventPtr(new DeviceEvent(*this, "ProgramAborted", "Program Aborted")));
  }
  if (aConfiguration.hasProgramFinished) {
    deviceEvents->addEvent(DeviceEventPtr(new DeviceEvent(*this, "ProgramFinished", "Program Finished")));
  }
  if (aConfiguration.hasProgramStarted) {
    deviceEvents->addEvent(DeviceEventPtr(new DeviceEvent(*this, "ProgramStarted", "Program Started")));
  }
}

void HomeConnectDevice::addDefaultPowerOnAction()
{
  addPowerStateAction("PowerOn", "Switch power state on", "On");
}

void HomeConnectDevice::addDefaultStandByAction()
{
  addPowerStateAction("StandBy", "Switch power state standby", "Standby");
}

void HomeConnectDevice::addDefaultPowerOffAction()
{
  addPowerStateAction("PowerOff", "Switch power state off", "Off");
}

void HomeConnectDevice::addDefaultStopAction()
{
  HomeConnectActionPtr a = HomeConnectActionPtr(new HomeConnectStopAction(*this, *operationModeDescriptor, "Stop", "Stop current program"));
  deviceActions->addAction(a);
}

void HomeConnectDevice::addPowerStateAction(const string& aName, const string& aDescription, const string& aParameter)
{
  HomeConnectSettingBuilder settingBuilder =
      HomeConnectSettingBuilder("BSH.Common.Setting.PowerState").setValue("\"BSH.Common.EnumType.PowerState." + aParameter + "\"");
  HomeConnectActionPtr a = HomeConnectActionPtr(new HomeConnectAction(*this, aName, aDescription, settingBuilder.build()));
  deviceActions->addAction(a);
}


void HomeConnectDevice::addProgramNameProperty()
{
  programName = ValueDescriptorPtr(new TextValueDescriptor("ProgramName"));
  deviceProperties->addProperty(programName);
}


void HomeConnectDevice::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  // nop for now
  AFOCUSLOG("- stateChanged: changed from '%s' to '%s'",
    aChangedState->value()->getStringValue(false, true).c_str(),
    aChangedState->value()->getStringValue(false, false).c_str()
  );
}

HomeConnectVdc &HomeConnectDevice::homeConnectVdc()
{
  return *(static_cast<HomeConnectVdc *>(vdcP));
}


HomeConnectComm &HomeConnectDevice::homeConnectComm()
{
  return (static_cast<HomeConnectVdc *>(vdcP))->homeConnectComm;
}

void HomeConnectDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // create event stream monitor
  eventMonitor = HomeConnectEventMonitorPtr(new HomeConnectEventMonitor(
    homeConnectComm(),
    string_format("/api/homeappliances/%s/events",haId.c_str()).c_str(),
    boost::bind(&HomeConnectDevice::handleEvent, this, _1, _2, _3))
  );
  // we need to poll the state once
  pollState();

  if (aCompletedCB) aCompletedCB(ErrorPtr());
}

void HomeConnectDevice::handleEvent(EventType aEventType, JsonObjectPtr aEventData, ErrorPtr aError)
{
  if (aEventType == eventType_Disconnected) {
    handleEventTypeDisconnected();
    return;
  }

  if (aEventType == eventType_Connected) {
    handleEventTypeConnected();
    return;
  }

  JsonObjectPtr oKey;
  JsonObjectPtr oValue;

  // make sure that all needed data are present
  if (!aEventData || !aEventData->get("key", oKey) || !aEventData->get("value", oValue) ) {
    return;
  }

  string key = (oKey != NULL) ? oKey->stringValue() : "";

  switch(aEventType) {
    case eventType_Status : {
      handleEventTypeStatus(key, oValue);
      return;
    }
    case eventType_Notify : {
      handleEventTypeNotify(key, oValue);
      return;
    }
    case eventType_Event : {
      handleEventTypeEvent(key);
      return;
    }
    default: return;
  }
}

void HomeConnectDevice::handleEventTypeNotify(const string& aKey, JsonObjectPtr aValue)
{
  string value = (aValue != NULL) ? aValue->stringValue() : "";

  if ((aKey == "BSH.Common.Setting.PowerState") && (powerState != NULL)) {
    string powerStateValue = "Power" + removeNamespace(value);
    if (powerStateDescriptor->setStringValueCaseInsensitive(powerStateValue)) {
      ALOG(LOG_NOTICE, "New Power State: '%s'", powerStateValue.c_str());
      powerState->push();
    }
    return;
  }

  if ((aKey == "BSH.Common.Root.SelectedProgram" || aKey == "BSH.Common.Root.ActiveProgram") &&
       programName != NULL && !value.empty()) {
    string programNameValue = removeNamespace(value);
    if (programName->setStringValue(programNameValue)) {
      ALOG(LOG_NOTICE, "New Program Name State: '%s'", programNameValue.c_str());
    }
    return;
  }

  if ((aKey == "BSH.Common.Option.ElapsedProgramTime") && (elapsedProgramTime != NULL)) {
    int32_t value = (aValue != NULL) ? aValue->int32Value() : 0;
    elapsedProgramTime->setInt32Value(value);
    return;
  }

  if ((aKey == "BSH.Common.Option.RemainingProgramTime") && (remainingProgramTime != NULL)) {
    if (aValue == NULL) {
      remainingProgramTime->invalidate();
    }
    else {
      remainingProgramTime->setInt32Value(aValue->int32Value());
    }
    return;
  }

  if ((aKey == "BSH.Common.Option.ProgramProgress") && (programProgress != NULL)) {
    int32_t value = (aValue != NULL) ? aValue->int32Value() : 0;
    programProgress->setInt32Value(value);
    return;
  }
}

void HomeConnectDevice::handleEventTypeEvent(const string& aKey)
{
  if (operationMode == NULL) {
    return;
  }

  DeviceEventPtr event;
  if (aKey=="BSH.Common.Event.ProgramFinished") {
    event = deviceEvents->getEvent("ProgramFinished");
  } else if (aKey=="BSH.Common.Event.ProgramAborted") {
    event = deviceEvents->getEvent("ProgramAborted");
  } else if (aKey=="BSH.Common.Event.AlarmClockElapsed") {
    event = deviceEvents->getEvent("AlarmClockElapsed");
  }

  if (event) {
    operationMode->pushWithEvent(event);
  }
}

void HomeConnectDevice::handleEventTypeStatus(const string& aKey, JsonObjectPtr aValue)
{
  string value = (aValue != NULL) ? aValue->stringValue() : "";

  if (aKey == "BSH.Common.Status.OperationState" && (operationMode != NULL)) {
    handleOperationStateChange(value);
    return;
  }

  if ((aKey == "BSH.Common.Status.RemoteControlActive") && (remoteControl != NULL)) {
    handleRemoteControlActiveChange(aValue);
    return;
  }

  if ((aKey == "BSH.Common.Status.RemoteControlStartAllowed") && (remoteControl != NULL)) {
    handleRemoteStartAllowedChange(aValue);
    return;
  }

  if ((aKey=="BSH.Common.Status.DoorState") && (doorState != NULL)) {
    string doorValue = "Door" + removeNamespace(value);
    if (doorStateDescriptor->setStringValueCaseInsensitive(doorValue)) {
      ALOG(LOG_NOTICE, "Door State: '%s'", doorValue.c_str());
      doorState->push();
    }
    return;
  }

  if ((aKey=="BSH.Common.Status.LocalControlActive") && (operationMode != NULL)) {
    DeviceEventPtr event = deviceEvents->getEvent("LocallyOperated");
    if (event) {
      operationMode->pushWithEvent(event);
    }
  }
}

void HomeConnectDevice::handleEventTypeDisconnected()
{
  reportVanished();
  isConnected = false;
  ALOG(LOG_NOTICE, "Device disconnected");
}

void HomeConnectDevice::handleEventTypeConnected()
{
  isConnected = true;
  vdcP->scheduleRecollect(rescanmode_normal, Second);
  ALOG(LOG_NOTICE, "Device connected");
  pollState();
}

void HomeConnectDevice::handleOperationStateChange(const string& aNewValue)
{
  string operationValue = "Mode" + removeNamespace(aNewValue);
  if (!operationModeDescriptor->setStringValueCaseInsensitive(operationValue)) {
    return;
  }

  ALOG(LOG_NOTICE, "New Operation State: '%s'", operationValue.c_str());

  if (operationValue == "ModeRun") {
    DeviceEventPtr event = deviceEvents->getEvent("ProgramStarted");
    if (event) {
      operationMode->pushWithEvent(event);
    }
  } else {
    operationMode->push();

    // the following information is valid only in case the program is running
    if (elapsedProgramTime != NULL) {
      elapsedProgramTime->invalidate();
    }
    if (remainingProgramTime != NULL) {
      remainingProgramTime->invalidate();
    }
    if (programProgress != NULL) {
      programProgress->invalidate();
    }
  }
}

void HomeConnectDevice::handleRemoteStartAllowedChange(JsonObjectPtr aNewValue)
{
  if (aNewValue == NULL) {
    return;
  }
  string remoteStartValue;
  bool value = aNewValue->boolValue();

  if (value) {
    remoteStartValue = "RemoteStartActive";
  } else {
    if (remoteControlDescriptor->getStringValue() == "RemoteStartActive") {
      remoteStartValue = "RemoteControlActive";
    }
  }

  if (!remoteStartValue.empty() && remoteControlDescriptor->setStringValueCaseInsensitive(remoteStartValue)) {
    ALOG(LOG_NOTICE, "New Remote Start Allowed State: '%s'", remoteStartValue.c_str());
    remoteControl->push();
  }
}

void HomeConnectDevice::handleRemoteControlActiveChange(JsonObjectPtr aNewValue)
{
  if (aNewValue == NULL) {
    return;
  }

  string remoteControlValue;
  bool value = aNewValue->boolValue();

  if (value) {
    if (remoteControlDescriptor->getStringValue() != "RemoteStartActive") {
      remoteControlValue = "RemoteControlActive";
    }
  } else {
    remoteControlValue = "RemoteControlInactive";
  }

  if (!remoteControlValue.empty() && remoteControlDescriptor->setStringValueCaseInsensitive(remoteControlValue)) {
    ALOG(LOG_NOTICE, "New Remote Control State: '%s'", remoteControlValue.c_str());
    remoteControl->push();
  }
}

void HomeConnectDevice::pollState()
{
  // Start query the statuses and settings of the device
  homeConnectComm().apiQuery(string_format("/api/homeappliances/%s/status", haId.c_str()).c_str(),
      boost::bind(&HomeConnectDevice::pollStateStatusDone, this, _1, _2));
}

void HomeConnectDevice::pollStateStatusDone(JsonObjectPtr aResult, ErrorPtr aError)
{
  // if we got proper response then analyze it
  if ( (aResult != NULL) && (Error::isOK(aError)) ) {
    JsonObjectPtr data = aResult->get("data");

    if (data != NULL) {
      JsonObjectPtr statusArray = data->get("status");

      if (statusArray != NULL) {
        for (int i = 0; i < statusArray->arrayLength(); i++) {
          handleEvent(eventType_Status, statusArray->arrayGet(i), aError);
        }
      }
    }

    homeConnectComm().apiQuery(string_format("/api/homeappliances/%s/settings", haId.c_str()).c_str(),
        boost::bind(&HomeConnectDevice::pollStateSettingsDone, this, _1, _2));
  }
}

void HomeConnectDevice::pollStateSettingsDone(JsonObjectPtr aResult, ErrorPtr aError)
{
  // if we got proper response then analyze it
  if ( (aResult != NULL) && (Error::isOK(aError)) ) {
    JsonObjectPtr data = aResult->get("data");

    if (data != NULL) {
      JsonObjectPtr settingsArray = data->get("settings");

      if (settingsArray != NULL) {
        for (int i = 0; i < settingsArray->arrayLength(); i++) {
          handleEvent(eventType_Notify, settingsArray->arrayGet(i), aError);
        }
      }
    }

    homeConnectComm().apiQuery(string_format("/api/homeappliances/%s/programs/selected", haId.c_str()).c_str(),
        boost::bind(&HomeConnectDevice::pollStateProgramDone, this, _1, _2));
  }
}

void HomeConnectDevice::pollStateProgramDone(JsonObjectPtr aResult, ErrorPtr aError)
{
  if ( (aResult != NULL) && (Error::isOK(aError)) ) {
    JsonObjectPtr data = aResult->get("data");

    if (data != NULL) {
      JsonObjectPtr key = data->get("key");

      if (key != NULL) {
        JsonObjectPtr event = JsonObject::newObj();

        // create a dummy event that contain information about current program
        event->add("key", JsonObject::newString("BSH.Common.Root.SelectedProgram"));
        event->add("value", JsonObject::newString(data->get("key")->stringValue()));
        handleEvent(eventType_Notify, event, aError);

        // selected program can have selected options, we also should inform devices about theirs values
        JsonObjectPtr optionsArray = data->get("options");
        if (optionsArray != NULL) {
          for (int i = 0; i < optionsArray->arrayLength(); i++) {
            handleEvent(eventType_Notify, optionsArray->arrayGet(i), aError);
          }
        }
      }
    }
  }
}

string HomeConnectDevice::hardwareGUID()
{
  vector<string> splitedStrings;
  boost::split(splitedStrings, haId, boost::bind(std::equal_to<char>(), _1, '-'));
  // getting last part of haId, ex. "SIEMENS-HCS05FRF1-D516FBECC462AD"
  string last;
  if (!splitedStrings.empty()) {
    last = splitedStrings.back();
  }
  return string_format("haId:%s", last.c_str());
}


string HomeConnectDevice::hardwareModelGUID()
{
  return modelGuid;
}


string HomeConnectDevice::modelName()
{
  return model;
}


string HomeConnectDevice::vendorName()
{
  return vendor;
}

string HomeConnectDevice::oemModelGUID()
{
  return "gs1:(01)" + gtin;
}

bool HomeConnectDevice::isKnownDevice()
{
  return !gtin.empty();
}


void HomeConnectDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  // FIXME: implement
  if (aPresenceResultHandler) aPresenceResultHandler(true); // FIXME: for now just assume present
}



//void HomeConnectDevice::presenceStateReceived(PresenceCB aPresenceResultHandler, JsonObjectPtr aDeviceInfo, ErrorPtr aError)
//{
//  bool reachable = false;
//  if (Error::isOK(aError) && aDeviceInfo) {
//    JsonObjectPtr state = aDeviceInfo->get("state");
//    if (state) {
//      // Note: 2012 hue bridge firmware always returns 1 for this.
//      JsonObjectPtr o = state->get("reachable");
//      reachable = o && o->boolValue();
//    }
//  }
//  aPresenceResultHandler(reachable);
//}
//


void HomeConnectDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  checkPresence(boost::bind(&HomeConnectDevice::disconnectableHandler, this, aForgetParams, aDisconnectResultHandler, _1));
}


void HomeConnectDevice::disconnectableHandler(bool aForgetParams, DisconnectCB aDisconnectResultHandler, bool aPresent)
{
  if (!aPresent) {
    // call inherited disconnect
    inherited::disconnect(aForgetParams, aDisconnectResultHandler);
  }
  else {
    // not disconnectable
    if (aDisconnectResultHandler) {
      aDisconnectResultHandler(false);
    }
  }
}

string HomeConnectDevice::createDeviceName(JsonObjectPtr aNetworkJson, JsonObjectPtr aFileJson)
{
  JsonObjectPtr name = aNetworkJson->get("name");
  if (name && !name->stringValue().empty()) {
    string deviceName = name->stringValue();
    ALOG(LOG_DEBUG, "Using device name returned by Home connect cloud: '%s'", deviceName.c_str());
    return deviceName;
  }


  name = aFileJson->get("defaultName");
  if (name) {
    string deviceName = name->stringValue();
    ALOG(LOG_DEBUG, "Using device name from configuration file : '%s'", deviceName.c_str());
    return deviceName;
  }

  ALOG(LOG_DEBUG, "Cannot create device name");
  return "";

}

void HomeConnectDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  // FIXME: implement
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s;
  s = "homeConnectApplicanceId::";
  s += haId;
  dSUID.setNameInSpace(s, vdcNamespace);
}


string HomeConnectDevice::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- haId: %s", haId.c_str());
  return s;
}

string HomeConnectDevice::removeNamespace(const string& aString)
{
  ssize_t sp = aString.rfind('.');
  if (sp!=string::npos) {
    return aString.substr(sp+1);
  } else {
    return aString;
  }
}

#endif // ENABLE_HOMECONNECT

