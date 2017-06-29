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

#include "homeconnectdevicecoffemaker.hpp"
#include "homeconnectdeviceoven.hpp"
#include "homeconnectdevicedishwasher.hpp"
#include "homeconnectdevicewasher.hpp"
#include "homeconnectdevicedryer.hpp"
#include "homeconnectdevicefridge.hpp"

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
    case FIRE:
    case SLEEPING:
    case ROOM_OFF:
    case DEEP_OFF:
      // turn off the applicance
      value = 0; // not used in ActionOutputBehaviour
      setDontCare(false);
      command = "std.stop"; // "deviceaction:" is default prefix, do not explicitly set it
      break;
    case PANIC:
    case SMOKE:
    case WATER:
    case GAS:
    case ALARM1:
    case ALARM2:
    case ALARM3:
    case ALARM4:
      // no operation for these by default (but still, only sensible action is stop, so we preconfigure that here, with dontCare set)
      value = 0; // not used in ActionOutputBehaviour
      setDontCare(true); // but no action by default
      command = "std.stop"; // "deviceaction:" is default prefix, do not explicitly set it
      break;
    default:
      // no operation by default for all other scenes
      setDontCare(true);
      break;
  }
  markClean(); // default values are always clean
}


// MARK: ====== HomeConnectAction


HomeConnectAction::HomeConnectAction(SingleDevice &aSingleDevice, const string aName, const string aDescription, const string aApiCommandTemplate) :
  inherited(aSingleDevice, aName, aDescription),
  apiCommandTemplate(aApiCommandTemplate)
{
}


HomeConnectDevice &HomeConnectAction::getHomeConnectDevice()
{
  return *(static_cast<HomeConnectDevice *>(singleDeviceP));
}



void HomeConnectAction::performCall(ApiValuePtr aParams, StatusCB aCompletedCB)
{
  ErrorPtr err;
  // direct execution of home connect API commands
  // Syntax:
  //   method:resturlpath[:jsonBody]
  string cmd = apiCommandTemplate;
  err = substitutePlaceholders(cmd, boost::bind(&HomeConnectAction::valueLookup, this, aParams, _1, _2));
  JsonObjectPtr jsonBody;
  if (Error::isOK(err)) {
    string method;
    string r;
    if (keyAndValue(cmd, method, r)) {
      string path;
      string body;
      if (!keyAndValue(r, path, body)) {
        path = r;
      }
      else {
        // make JSON from text
        jsonBody = JsonObject::objFromText(body.c_str());
      }
      // complete path
      string urlpath = "/api/homeappliances/" + getHomeConnectDevice().haId + "/" + path;
      getHomeConnectDevice().homeConnectComm().apiAction(method, urlpath, jsonBody, boost::bind(&HomeConnectAction::apiCommandSent, this, aCompletedCB, _1, _2));
    }
  }
  if (aCompletedCB) aCompletedCB(err);
}


ErrorPtr HomeConnectAction::valueLookup(ApiValuePtr aParams, const string aName, string &aValue)
{
  ApiValuePtr v = aParams->get(aName);
  if (v) {
    aValue = v->stringValue();
    return ErrorPtr();
  }
  return ErrorPtr(TextError::err("no substitution found for '%s'", aName.c_str()));
}



void HomeConnectAction::apiCommandSent(StatusCB aCompletedCB, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (aCompletedCB) aCompletedCB(aError);
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
  hcDevType(homeconnect_unknown)
{
  // home connect appliances are single devices
  setColorClass(class_white_singledevices);
  installSettings(DeviceSettingsPtr(new HomeConnectDeviceSettings(*this)));
  // - set a action output behaviour (no classic output properties and channels)
  OutputBehaviourPtr ab = OutputBehaviourPtr(new ActionOutputBehaviour(*this));
  ab->setGroupMembership(group_black_variable, true);
  addBehaviour(ab);
  LOG(LOG_DEBUG, "ApplianceInfo = %s", aHomeApplicanceInfoRecord->c_strValue());
  // set basic info
  JsonObjectPtr o;
  if (aHomeApplicanceInfoRecord->get("haId", o))
    haId = o->stringValue();
  if (aHomeApplicanceInfoRecord->get("vib", o)) {
    model = o->stringValue();
    if (aHomeApplicanceInfoRecord->get("type", o)) {
      model += " " + o->stringValue();
    }
  }
  if (aHomeApplicanceInfoRecord->get("enumber", o))
    modelGuid = o->stringValue();
  if (aHomeApplicanceInfoRecord->get("brand", o))
    vendor = o->stringValue();
}

HomeConnectDevicePtr HomeConnectDevice::createHomeConenctDevice(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord)
{
  JsonObjectPtr o;
  HomeConnectDevicePtr retVal = NULL;

  // analyze the type of device in this json object and create proper specialized type
  if (aHomeApplicanceInfoRecord->get("type", o)) {
    string ty = o->stringValue();

    if (ty=="CoffeeMaker") {
      retVal = static_cast<HomeConnectDevicePtr>(new HomeConnectDeviceCoffeMaker(aVdcP, aHomeApplicanceInfoRecord));
    }
    else if (ty=="Oven") {
      retVal = static_cast<HomeConnectDevicePtr>(new HomeConnectDeviceOven(aVdcP, aHomeApplicanceInfoRecord));
    }
    else if (ty=="Dishwasher") {
      retVal = static_cast<HomeConnectDevicePtr>(new HomeConnectDeviceDishWasher(aVdcP, aHomeApplicanceInfoRecord));
    }
    else if (ty=="Washer") {
      retVal = static_cast<HomeConnectDevicePtr>(new HomeConnectDeviceWasher(aVdcP, aHomeApplicanceInfoRecord));
    }
    else if (ty=="Dryer") {
      retVal = static_cast<HomeConnectDevicePtr>(new HomeConnectDeviceDryer(aVdcP, aHomeApplicanceInfoRecord));
    }
    else if (ty=="FridgeFreezer") {
      retVal = static_cast<HomeConnectDevicePtr>(new HomeConnectDeviceFridge(aVdcP, aHomeApplicanceInfoRecord));
    }
  }

  return retVal;
}


bool HomeConnectDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  if (!configureDevice()) {
    LOG(LOG_WARNING,"HomeConnect device model '%s' not implemented -> ignored", model.c_str());
    return false; // cannot configure this device
  }
  // derive the dSUID
  deriveDsUid();
  return true; // simple identification, callback will not be called
}



bool HomeConnectDevice::configureDevice()
{
  HomeConnectActionPtr a;
  // configure common things
  // - stop
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.Stop", "stop current program", "DELETE:programs/active"));
  deviceActions->addAction(a);
  // - power state
  EnumValueDescriptorPtr powerState = EnumValueDescriptorPtr(new EnumValueDescriptor("powerState", true));
  powerState->addEnum("On", 0, true); // on, default for action
  powerState->addEnum("Standby", 1);
  powerState->addEnum("Off", 2); // full off
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.Power", "switch power state",
    "PUT:settings/BSH.Common.Setting.PowerState:"
    "{\"data\":{\"key\":\"BSH.Common.Setting.PowerState\",\"value\":\"BSH.Common.EnumType.PowerState.@{powerState}\"}}"
  ));
  a->addParameter(powerState);
  deviceActions->addAction(a);

  // program name
  programName = DeviceStatePtr(
      new DeviceState(*this, "ProgramName", "Program Name", ValueDescriptorPtr(new TextValueDescriptor("state")),
          boost::bind(&HomeConnectDevice::stateChanged, this, _1, _2)));
  deviceStates->addState(programName);

  // configured ok
  return true;
}

// TODO: change for enum values? flags? leave the bool flags?
void HomeConnectDevice::configureOperationModeState(bool aHasInactive, bool aHasDelayedStart, bool aHasPause,
    bool aHasActionrequired, bool aHasError, bool aHasAborting)
{
  // - operation mode
  EnumValueDescriptor *es = new EnumValueDescriptor("state", true);
  int currentEnumValue = 0;

  if (aHasInactive) {
    es->addEnum("Inactive", currentEnumValue++);
  }
  es->addEnum("Ready", currentEnumValue++);
  if (aHasDelayedStart) {
    es->addEnum("DelayedStart", currentEnumValue++);
  }
  es->addEnum("Run", currentEnumValue++);
  if (aHasPause) {
    es->addEnum("Pause", currentEnumValue++);
  }
  if (aHasActionrequired) {
      es->addEnum("ActionRequired", currentEnumValue++);
  }
  es->addEnum("Finished", currentEnumValue++);
  if (aHasError) {
    es->addEnum("Error", currentEnumValue++);
  }
  if (aHasAborting) {
    es->addEnum("Aborting", currentEnumValue++);
  }
  operationMode = DeviceStatePtr(
      new DeviceState(*this, "OperationMode", "Status", ValueDescriptorPtr(es),
          boost::bind(&HomeConnectDevice::stateChanged, this, _1, _2)));
  deviceStates->addState(operationMode);
}

void HomeConnectDevice::configureRemoteControlState()
{
  // - remote control
  EnumValueDescriptor *es = new EnumValueDescriptor("state", true);
  es->addEnum("Enabled", 0);
  es->addEnum("Disabled", 1);
  remoteControl = DeviceStatePtr(
      new DeviceState(*this, "RemoteControl", "Remote Control", ValueDescriptorPtr(es),
          boost::bind(&HomeConnectDevice::stateChanged, this, _1, _2)));
  deviceStates->addState(remoteControl);
}

void HomeConnectDevice::configureRemoteStartState()
{
  // - remote control
  EnumValueDescriptor *es = new EnumValueDescriptor("state", true);
  es->addEnum("Enabled", 0);
  es->addEnum("Disabled", 1);
  remoteStart = DeviceStatePtr(
      new DeviceState(*this, "RemoteStart", "Remote Start", ValueDescriptorPtr(es),
          boost::bind(&HomeConnectDevice::stateChanged, this, _1, _2)));
  deviceStates->addState(remoteStart);
}

void HomeConnectDevice::configureDoorState(bool aHasLocked)
{
  // - remote control
  EnumValueDescriptor *es = new EnumValueDescriptor("state", true);
  es->addEnum("Open", 0);
  es->addEnum("Closed", 1);
  if (aHasLocked) {
    es->addEnum("Locked", 2);
  }
  doorState = DeviceStatePtr(
      new DeviceState(*this, "DoorState", "Door Start", ValueDescriptorPtr(es),
          boost::bind(&HomeConnectDevice::stateChanged, this, _1, _2)));
  deviceStates->addState(doorState);
}

void HomeConnectDevice::configurePowerState(bool aHasOff, bool aHasStandby)
{
  // - operation mode
  EnumValueDescriptor *es = new EnumValueDescriptor("state", true);
  int currentEnumValue = 0;

  if (aHasOff) {
    es->addEnum("Off", currentEnumValue++, true); // init state with this value
  }
  es->addEnum("On", currentEnumValue++);
  if (aHasStandby) {
    es->addEnum("Standby", currentEnumValue++);
  }

  powerState = DeviceStatePtr(
      new DeviceState(*this, "PowerState", "Power State", ValueDescriptorPtr(es),
          boost::bind(&HomeConnectDevice::stateChanged, this, _1, _2)));
  deviceStates->addState(powerState);
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
  // FIXME: implement
  if (aCompletedCB) aCompletedCB(ErrorPtr());
}

void HomeConnectDevice::handleEvent(string aEventType, JsonObjectPtr aEventData, ErrorPtr aError)
{
  JsonObjectPtr oKey;
  JsonObjectPtr oValue;

  // make sure that all needed data are present
  if (aEventData && aEventData->get("key", oKey) && aEventData->get("value", oValue) ) {
    string key = oKey->stringValue();
    string value = oValue->stringValue();

    if (aEventType=="STATUS") {
      if ((key=="BSH.Common.Status.OperationState") && (operationMode != NULL)) {
        string operationValue = removeNamespace(value);
        if (operationMode->value()->setStringValue(operationValue)) {
          ALOG(LOG_NOTICE, "New Operation State: '%s'", operationValue.c_str());
          operationMode->push();
        } else {
          ALOG(LOG_ERR, "Unknown Operation State: '%s' ('%s')", operationValue.c_str(), value.c_str());
        }
      } else if ((key=="BSH.Common.Status.RemoteControlActive") && (remoteControl != NULL)) {
        string remoteControlValue;

        if (value == "true") {
          remoteControlValue = "Enabled";
        } else if (value == "false") {
          remoteControlValue = "Disabled";
        }

        if (!remoteControlValue.empty() && remoteControl->value()->setStringValue(remoteControlValue)) {
          ALOG(LOG_NOTICE, "New Remote Control State: '%s'", remoteControlValue.c_str());
          remoteControl->push();
        } else {
          ALOG(LOG_NOTICE, "Unknown Remote Control State: '%s' ('%s')", remoteControlValue.c_str(), value.c_str());
        }
      } else if ((key=="BSH.Common.Status.RemoteControlStartAllowed") && (remoteStart != NULL)) {
        string remoteStartValue;

        if (value == "true") {
          remoteStartValue = "Enabled";
        } else if (value == "false") {
          remoteStartValue = "Disabled";
        }

        if (!remoteStartValue.empty() && remoteStart->value()->setStringValue(remoteStartValue)) {
          ALOG(LOG_NOTICE, "New Remote Start Allowed State: '%s'", remoteStartValue.c_str());
          remoteStart->push();
        } else {
          ALOG(LOG_NOTICE, "Unknown Remote Start Allowed State: '%s' ('%s')", remoteStartValue.c_str(), value.c_str());
        }
      } else if ((key=="BSH.Common.Status.DoorState") && (doorState != NULL)) {
        string doorValue = removeNamespace(value);
        if (doorState->value()->setStringValue(doorValue)) {
          ALOG(LOG_NOTICE, "Door State: '%s'", doorValue.c_str());
          doorState->push();
        } else {
          ALOG(LOG_ERR, "Unknown Operation State: '%s' ('%s')", doorValue.c_str(), value.c_str());
        }
      }
    } else if (aEventType=="NOTIFY") {
      if ((key=="BSH.Common.Setting.PowerState") && (powerState != NULL)) {
        string powerStateValue = removeNamespace(value);
        if (powerState->value()->setStringValue(powerStateValue)) {
          ALOG(LOG_NOTICE, "New Power State: '%s'", powerStateValue.c_str());
          powerState->push();
        } else {
          ALOG(LOG_ERR, "Unknown Power State: '%s' ('%s')", powerStateValue.c_str(), value.c_str());
        }
      } else if ((key=="BSH.Common.Root.SelectedProgram") && (programName != NULL)) {
        string programNameValue = removeNamespace(value);
        if (programName->value()->setStringValue(programNameValue)) {
          ALOG(LOG_NOTICE, "New Program Name State: '%s'", programNameValue.c_str());
          programName->push();
        } else {
          ALOG(LOG_ERR, "Invalid Program Name: '%s' ('%s')", programNameValue.c_str(), value.c_str());
        }
      }
    }
  }
  // FIXME: tbd

}




bool HomeConnectDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("homeconnect_coffee", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}



string HomeConnectDevice::hardwareGUID()
{
  return string_format("haId:%s", haId.c_str());
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

