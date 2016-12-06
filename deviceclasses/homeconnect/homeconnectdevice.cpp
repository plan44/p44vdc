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
  //   resturlpath[:jsonBody]
  string cmd = apiCommandTemplate;
  err = substitutePlaceholders(cmd, boost::bind(&HomeConnectAction::valueLookup, this, aParams, _1));
  JsonObjectPtr jsonBody;
  if (Error::isOK(err)) {
    string path;
    string body;
    if (!keyAndValue(cmd, path, body)) {
      path = cmd;
    }
    else {
      // make JSON from text
      jsonBody = JsonObject::objFromText(body.c_str());
    }
    // complete path
    string urlpath = "/api/homeappliances/" + getHomeConnectDevice().haId + "/" + path;
    getHomeConnectDevice().homeConnectComm().apiAction("PUT", urlpath, jsonBody, boost::bind(&HomeConnectAction::apiCommandSent, this, aCompletedCB, _1, _2));
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
  // - latte macchiato
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "lattemacchiato", "Latte Macchiato", ":{\"data\":{\"key\":\"ConsumerProducts.CoffeeMaker.Program.Beverage.LatteMacchiato\",\"options\":[{ \"key\":\"ConsumerProducts.CoffeeMaker.Option.CoffeeTemperature\",\"value\":\"ConsumerProducts.CoffeeMaker.EnumType.CoffeeTemperature.Normal\"}]}}"));
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("duration", valueType_numeric, VDC_UNIT(valueUnit_second, unitScaling_1), 10, 1800, 1, true, 60)));
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("power", valueType_numeric, VDC_UNIT(valueUnit_watt, unitScaling_1), 50, 700, 50, true, 500)));
  deviceActions->addAction(a);
  // - stop
//  a = HomeConnectActionPtr(new HomeConnectAction(*this, "stop", "stop", "hh:doTurnOff"));
//  deviceActions->addAction(a);
  // derive the dSUID
  deriveDsUid();
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
  // FIXME: implement
  if (aCompletedCB) aCompletedCB(ErrorPtr());
}




bool HomeConnectDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("homeconnect", aIcon, aWithData, aResolutionPrefix))
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
  return "gs1:(01)7640156799999"; // FIXME: add number from aizo/dS numberspace, as defined in Aug 2016
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

