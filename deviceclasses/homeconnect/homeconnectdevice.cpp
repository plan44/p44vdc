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
  err = substitutePlaceholders(cmd, boost::bind(&HomeConnectAction::valueLookup, this, aParams, _1));
  JsonObjectPtr jsonBody;
  if (Error::isOK(err)) {
    string method;
    string r;
    if (keyAndValue(cmd, method, r)) {
      string path;
      string body;
      if (!keyAndValue(r, path, body)) {
        path = cmd;
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


ErrorPtr HomeConnectAction::valueLookup(ApiValuePtr aParams, string &aValue)
{
  ApiValuePtr v = aParams->get(aValue);
  if (v) {
    aValue = v->stringValue();
    return ErrorPtr();
  }
  return ErrorPtr(TextError::err("no substitution found for '%s'", aValue.c_str()));
}



void HomeConnectAction::apiCommandSent(StatusCB aCompletedCB, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (aCompletedCB) aCompletedCB(aError);
}





// MARK: ===== HomeConnectDevice


//  {
//    "haId": "BOSCH-HCS06COM1-CBF9981D149632",
//    "vib": "HCS06COM1",
//    "brand": "BOSCH",
//    "type": "CoffeeMaker",
//    "name": "CoffeeMaker Simulator",
//    "enumber": "HCS06COM1\/01",
//    "connected": true
//  }


HomeConnectDevice::HomeConnectDevice(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
  inherited(aVdcP)
{
  // home connect appliances are single devices
  setColorClass(class_white_singledevices);
  installSettings(DeviceSettingsPtr(new HomeConnectDeviceSettings(*this)));
  // - set a action output behaviour (no classic output properties and channels)
  OutputBehaviourPtr ab = OutputBehaviourPtr(new ActionOutputBehaviour(*this));
  ab->setGroupMembership(group_black_variable, true);
  addBehaviour(ab);
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
  // FIXME: set up device details
  // create standard actions
  HomeConnectActionPtr a;
  // - enums that can be shared between actions
  EnumValueDescriptorPtr tempLevel = EnumValueDescriptorPtr(new EnumValueDescriptor("temperatureLevel", true));
  tempLevel->addEnum("Normal", 0, true); // default
  tempLevel->addEnum("High", 1);
  tempLevel->addEnum("VeryHigh", 2);
  EnumValueDescriptorPtr beanAmount = EnumValueDescriptorPtr(new EnumValueDescriptor("beanAmount", true));
  beanAmount->addEnum("VeryMild", 0);
  beanAmount->addEnum("Mild", 1);
  beanAmount->addEnum("Normal", 2, true); // default
  beanAmount->addEnum("Strong", 3);
  beanAmount->addEnum("VeryStrong", 4);
  beanAmount->addEnum("DoubleShot", 5);
  beanAmount->addEnum("DoubleShotPlus", 6);
  beanAmount->addEnum("DoubleShotPlusPlus", 7);
  // - command template
  string cmdTemplate =
    "PUT:programs/active:{\"data\":{\"key\":\"ConsumerProducts.CoffeeMaker.Program.Beverage.%s\","
    "\"options\":["
      "{ \"key\":\"ConsumerProducts.CoffeeMaker.Option.CoffeeTemperature\",\"value\":\"ConsumerProducts.CoffeeMaker.EnumType.CoffeeTemperature.@{temperatureLevel}\"},"
      "{ \"key\":\"ConsumerProducts.CoffeeMaker.Option.BeanAmount\",\"value\":\"ConsumerProducts.CoffeeMaker.EnumType.BeanAmount.@{beanAmount}\"},"
      "{ \"key\":\"ConsumerProducts.CoffeeMaker.Option.FillQuantity\",\"value\":@{fillQuantity%%0}}"
    "]}}";
  // - espresso
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.espresso", "Espresso", string_format(cmdTemplate.c_str(),"Espresso")));
  a->addParameter(tempLevel);
  a->addParameter(beanAmount);
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("fillQuantity", valueType_numeric, VDC_UNIT(valueUnit_liter, unitScaling_milli), 35, 60, 5, true, 40)));
  deviceActions->addAction(a);
  // - espresso macciato
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.espressoMacchiato", "Espresso Macchiato", string_format(cmdTemplate.c_str(),"EspressoMacchiato")));
  a->addParameter(tempLevel);
  a->addParameter(beanAmount);
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("fillQuantity", valueType_numeric, VDC_UNIT(valueUnit_liter, unitScaling_milli), 40, 60, 10, true, 50)));
  deviceActions->addAction(a);
  // - (plain) coffee
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.coffee", "Coffee", string_format(cmdTemplate.c_str(),"Coffee")));
  a->addParameter(tempLevel);
  a->addParameter(beanAmount);
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("fillQuantity", valueType_numeric, VDC_UNIT(valueUnit_liter, unitScaling_milli), 60, 250, 10, true, 120)));
  deviceActions->addAction(a);
  // - Cappuccino
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.cappuccino", "Cappuccino", string_format(cmdTemplate.c_str(),"Cappuccino")));
  a->addParameter(tempLevel);
  a->addParameter(beanAmount);
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("fillQuantity", valueType_numeric, VDC_UNIT(valueUnit_liter, unitScaling_milli), 100, 250, 10, true, 180)));
  deviceActions->addAction(a);
  // - latte macchiato
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.latteMacchiato", "Latte Macchiato", string_format(cmdTemplate.c_str(),"LatteMacchiato")));
  a->addParameter(tempLevel);
  a->addParameter(beanAmount);
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("fillQuantity", valueType_numeric, VDC_UNIT(valueUnit_liter, unitScaling_milli), 200, 400, 20, true, 300)));
  deviceActions->addAction(a);
  // - latte macchiato
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.caffeLatte", "Caffe Latte", string_format(cmdTemplate.c_str(),"CaffeLatte")));
  a->addParameter(tempLevel);
  a->addParameter(beanAmount);
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("fillQuantity", valueType_numeric, VDC_UNIT(valueUnit_liter, unitScaling_milli), 100, 400, 20, true, 250)));
  deviceActions->addAction(a);
  // - stop
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.stop", "stop current program", "DELETE:programs/active"));
  deviceActions->addAction(a);
  // create states
  // - operation
  EnumValueDescriptor *es = new EnumValueDescriptor("state", true);
  es->addEnum("Inactive", 0, true); // init state with this value
  es->addEnum("Ready", 1);
  es->addEnum("Run", 3);
  es->addEnum("ActionRequired", 5);
  es->addEnum("Finished", 6);
  es->addEnum("Error", 7);
  es->addEnum("Aborting", 8);
  operationState = DeviceStatePtr(new DeviceState(
    *this, "operation", "Operation", ValueDescriptorPtr(es),
    boost::bind(&HomeConnectDevice::stateChanged, this, _1, _2)
  ));
  deviceStates->addState(operationState);
  // derive the dSUID
  deriveDsUid();
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
  // FIXME: tbd
  ALOG(LOG_INFO, "Event '%s' - item: %s", aEventType.c_str(), aEventData ? aEventData->c_strValue() : "<none>");
  if (aEventType=="STATUS") {
    JsonObjectPtr o;
    if (aEventData && aEventData->get("key", o)) {
      if (o->stringValue()=="BSH.Common.Status.OperationState") {
        // get value
        if (aEventData->get("value", o)) {
          string os = o->stringValue();
          ssize_t sp = os.rfind('.');
          if (sp!=string::npos) {
            string ostate = os.substr(sp+1);
            if (operationState->value()->setStringValue(ostate)) {
              ALOG(LOG_NOTICE, "New Operation State: %s", ostate.c_str());
              operationState->push();
            }
          }
        }
      }
    }
  }
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


string HomeConnectDevice::oemModelGUID()
{
  return "gs1:(01)7640156792096"; // Einbaumodell - from aizo/dS number space, as defined 2016-12-11
  // FIXME: differentiate Einbaumodell from Tischmodell
  // return "gs1:(01)7640156792102"; // Tischmodell - from aizo/dS number space, as defined 2016-12-11
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


#endif // ENABLE_HOMECONNECT

