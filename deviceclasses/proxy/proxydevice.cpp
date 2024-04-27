//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2024 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "proxydevice.hpp"

#if ENABLE_PROXYDEVICES

#include "proxyvdc.hpp"

#include "jsonvdcapi.hpp"

#include "outputbehaviour.hpp"
#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "sensorbehaviour.hpp"

using namespace p44;


ProxyDevice::ProxyDevice(ProxyVdc *aVdcP, JsonObjectPtr aDeviceJSON) :
  inherited((Vdc *)aVdcP)
{
  JsonObjectPtr o;
  if (aDeviceJSON->get("dSUID", o)) {
    // set dSUID
    mDSUID.setAsString(o->stringValue());
    configureStructure(aDeviceJSON);
  }
  else {
    OLOG(LOG_ERR, "proxy device info contained no dSUID!");
  }
  // Note: bridged is set at initializeDevice()
}


ProxyDevice::~ProxyDevice()
{
}


bool ProxyDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}


ProxyVdc &ProxyDevice::getProxyVdc()
{
  return *(static_cast<ProxyVdc *>(mVdcP));
}


string ProxyDevice::deviceTypeIdentifier() const
{
  // Note: when read via API, clients (e.g. WebUI) will get actual device's values, not this
  return "proxy";
}


string ProxyDevice::modelName()
{
  // Note: when read via API, clients (e.g. WebUI) will get actual device's values, not this
  return "proxy device";
}


string ProxyDevice::webuiURLString()
{
  // Note: when read via API, clients (e.g. WebUI) will get actual device's values, not this
  // So this is only in case for some reason API access did not work
  // FIXME: maybe we want clients to use the proxy host's webui, not the real device?
  return getVdc().webuiURLString();
}


string ProxyDevice::description()
{
  // TODO: possibly forward original device's description
  string s = inherited::description();
  string_format_append(s, "\n- proxy has no description of its own");
  return s;
}


// MARK: - api helpers

ErrorPtr ProxyDevice::notify(const string aNotification, JsonObjectPtr aParams)
{
  if (!aParams) aParams = JsonObject::newObj();
  OLOG(LOG_INFO, "proxy -> remote: sending notification '%s': %s", aNotification.c_str(), aParams->json_c_str());
  aParams->add("dSUID", JsonObject::newString(mDSUID.getString()));
  return getProxyVdc().api().notify(aNotification, aParams);
}


void ProxyDevice::call(const string aMethod, JsonObjectPtr aParams, JSonMessageCB aResponseCB)
{
  if (!aParams) aParams = JsonObject::newObj();
  OLOG(LOG_INFO, "proxy -> remote: calling method '%s': %s", aMethod.c_str(), aParams->json_c_str());
  aParams->add("dSUID", JsonObject::newString(mDSUID.getString()));
  getProxyVdc().api().call(aMethod, aParams, aResponseCB);
}


// MARK: - local method/notification handling

ErrorPtr ProxyDevice::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  // let some getting handled locally
  if (
    // we handle those via accessProperty
    aMethod=="getProperty" ||
    aMethod=="setProperty" ||
    // also handle these device-global ones locally, not in the proxied device
    aMethod=="loglevel" ||
    aMethod=="logoptions"
  ) {
    // handle locally
    return inherited::handleMethod(aRequest, aMethod, aParams);
  }
  else {
    // forward everything else to original device
    JsonObjectPtr params = JsonApiValue::getAsJson(aParams);
    call(aMethod, params, boost::bind(&ProxyDevice::handleProxyMethodCallResponse, this, aRequest, _1, _2));
    return ErrorPtr(); // we'll answer later
  }
}


void ProxyDevice::handleProxyMethodCallResponse(VdcApiRequestPtr aRequest, ErrorPtr aError, JsonObjectPtr aJsonObject)
{
  if (aError) {
    OLOG(LOG_WARNING, "remote -> proxy: method call returns error: %s", Error::text(aError));
    aRequest->sendError(aError);
  }
  else {
    OLOG(LOG_INFO, "remote -> proxy: method call response: %s", JsonObject::text(aJsonObject));
    ApiValuePtr response = aRequest->newApiValue();
    JsonApiValue::setAsJson(response, aJsonObject);
    aRequest->sendResult(response);
  }
}


void ProxyDevice::handleNotification(const string &aNotification, ApiValuePtr aParams, StatusCB aExaminedCB)
{
  // Note: callScene and dimChannel are intercepted at the vDC level and sent to proxied devices directly
  JsonObjectPtr params = JsonApiValue::getAsJson(aParams);
  ErrorPtr err = notify(aNotification, params);
  // successfully examined (forwarded)
  if (aExaminedCB) aExaminedCB(err);
}


// MARK: - bridge notification handling


bool ProxyDevice::handleBridgedDeviceNotification(const string aNotification, JsonObjectPtr aParams)
{
  if (aNotification=="pushNotification") {
    JsonObjectPtr props;
    if (aParams->get("changedproperties", props, true)) {
      updateCachedProperties(props);
      return true;
    }
  }
  else if (aNotification=="vanish") {
    // device got removed
    LOG(LOG_WARNING, "original device has vanished -> vanish proxy as well");
    hasVanished(false);
    return true;
  }
  return false; // not handled
}


// MARK: - property access forwarding

void ProxyDevice::accessProperty(PropertyAccessMode aMode, ApiValuePtr aQueryObject, int aDomain, int aApiVersion, PropertyAccessCB aAccessCompleteCB)
{
  // TODO: implement, forward to proxied device
  JsonObjectPtr params = JsonObject::newObj();
  JsonObjectPtr props = JsonApiValue::getAsJson(aQueryObject);
  string method;
  if (aMode==access_read) {
    // read
    method = "getProperty";
    params->add("query", props);
  }
  else {
    // write
    method = "setProperty";
    params->add("properties", props);
    if (aMode==access_write_preload) {
      params->add("preload", JsonObject::newBool(true));
    }
  }
  call(method, params, boost::bind(&ProxyDevice::handleProxyPropertyAccessResponse, this, aAccessCompleteCB, aQueryObject->newObject(), _1, _2));
}


void ProxyDevice::handleProxyPropertyAccessResponse(PropertyAccessCB aAccessCompleteCB, ApiValuePtr aEmptyResult, ErrorPtr aError, JsonObjectPtr aJsonObject)
{
  if (aError) {
    OLOG(LOG_WARNING, "remote -> proxy: property access call returns error: %s", Error::text(aError));
  }
  else {
    OLOG(LOG_INFO, "remote -> proxy: property access response: %s", JsonObject::text(aJsonObject));
    JsonApiValue::setAsJson(aEmptyResult, aJsonObject);
  }
  if (aAccessCompleteCB) aAccessCompleteCB(aEmptyResult, aError);
}


// MARK: - cached properties

void ProxyDevice::updateCachedProperties(JsonObjectPtr aProps)
{
  JsonObjectPtr elements;
  JsonObjectPtr props;
  string id;
  JsonObjectPtr o;
  // active state
  if (aProps->get("active", o)) {
    updatePresenceState(o->boolValue());
  }
  if (aProps->get("x-p44-bridgeable", o)) {
    // note: bridgeable status just treated like presence
    updatePresenceState(o->boolValue());
  }
  // input states we actually need to propagate
  if (aProps->get("buttonInputStates", elements)) {
    while(elements->nextKeyValue(id, props)) {
      if (ButtonBehaviourPtr bb = getButton(by_id, id)) {
        // update plain button state first
        if (props->get("value", o)) {
          bb->injectState(o->boolValue());
        }
        // check and forward actions and clicks
        if (props->get("actionMode", o)) {
          VdcButtonActionMode actionMode = static_cast<VdcButtonActionMode>(o->int32Value());
          if (props->get("actionId", o)) {
            bb->sendAction(actionMode, o->int32Value());
          }
        }
        else if (props->get("clickType", o)) {
          bb->injectClick(static_cast<DsClickType>(o->int32Value()));
        }
      }
    }
  }
  if (aProps->get("binaryInputStates", elements)) {
    while(elements->nextKeyValue(id, props)) {
      if (BinaryInputBehaviourPtr ib = getInput(by_id, id)) {
        if (props->get("value", o)) {
          if (o->isType(json_type_null)) ib->invalidateInputState();
          else ib->updateInputState(o->int32Value());
        }
      }
    }
  }
  if (aProps->get("sensorStates", elements)) {
    while(elements->nextKeyValue(id, props)) {
      if (SensorBehaviourPtr sb = getSensor(by_id, id)) {
        if (props->get("value", o)) {
          if (o->isType(json_type_null)) sb->invalidateSensorValue();
          else sb->updateSensorValue(o->doubleValue());
        }
      }
    }
  }
  // output states are special, we do not have local representation of the channels
  // so we just forward the state changes as we get them, so clients (DS, upstream bridges)
  // can track output changes.
  // Note: this means that unlike local outputs, which can be used as value sources,
  //   proxy outputs cannot, at this time, because they have no local value sources.
  if (aProps->get("channelStates", elements)) {
    // TODO: implement DS push as well, should dS-vDC-API ever evolve to allow this
    #if ENABLE_JSONBRIDGEAPI
    if (isBridged()) {
      // forward push to upstream bridges
      VdcApiConnectionPtr api = getVdcHost().getBridgeApi();
      if (api) {
        ApiValuePtr pushedprops = api->newApiValue();
        pushedprops->setType(apivalue_object);
        ApiValuePtr data = pushedprops->newNull();
        JsonApiValue::setAsJson(data, elements);
        pushedprops->add("channelStates", data);
        pushNotification(api, pushedprops, ApiValuePtr(), true);
      }
    }
    #endif
  }
  // properties we need for multicast addressing
  // - zone ID
  if (aProps->get("zoneID", o)) {
    setZoneID(o->int32Value());
  }
  // - device level color class
  if (aProps->get("primaryGroup", o)) {
    setColorClass(static_cast<DsClass>(o->int32Value()));
  }
  // - output group settings
  if (getOutput() && aProps->get("outputSettings", props)) {
    // - output level color class
    if (props->get("colorClass", o)) {
      getOutput()->initColorClass(static_cast<DsClass>(o->int32Value()));
    }
    // - group memberships
    JsonObjectPtr groups;
    if (props->get("groups", groups)) {
      string groupstr;
      getOutput()->resetGroupMembership();
      while(groups->nextKeyValue(groupstr, o)) {
        int groupno;
        if (sscanf(groupstr.c_str(), "%d", &groupno)==1) {
          getOutput()->setGroupMembership(static_cast<DsGroup>(groupno), o->boolValue());
        }
      }
    }
  }
  // - button settings needed for localcontroller
  if (aProps->get("buttonInputSettings", elements)) {
    while(elements->nextKeyValue(id, props)) {
      if (ButtonBehaviourPtr bb = getButton(by_id, id)) {
        // we need group, mode, function and channel for LocalController::processButtonClick
        if (props->get("group", o)) {
          bb->setGroup(static_cast<DsGroup>(o->int32Value()));
        }
        if (props->get("mode", o)) {
          bb->mButtonMode = static_cast<DsButtonMode>(o->int32Value());
        }
        if (props->get("function", o)) {
          bb->mButtonFunc = static_cast<DsButtonFunc>(o->int32Value());
        }
        if (props->get("channel", o)) {
          bb->mButtonChannel = static_cast<DsChannelType>(o->int32Value());
        }
      }
    }
  }
  // - input settings needed for local event monitoring in evaluators/p44script
  if (aProps->get("binaryInputSettings", elements)) {
    while(elements->nextKeyValue(id, props)) {
      if (BinaryInputBehaviourPtr ib = getInput(by_id, id)) {
        // we may need group
        if (props->get("group", o)) {
          ib->setGroup(static_cast<DsGroup>(o->int32Value()));
        }
      }
    }
  }
  // - sensor settings needed for local event monitoring in evaluators/p44script
  if (aProps->get("sensorSettings", elements)) {
    while(elements->nextKeyValue(id, props)) {
      if (SensorBehaviourPtr sb = getSensor(by_id, id)) {
        // we may need group
        if (props->get("group", o)) {
          sb->setGroup(static_cast<DsGroup>(o->int32Value()));
        }
      }
    }
  }
  // other cached properties for internal purposes such as logging
  // - name
  if (aProps->get("name", o)) {
    initializeName(o->stringValue());
  }
  // nothing of all this must be made persistent!
  markClean();
}


// MARK: - device setup

void ProxyDevice::configureStructure(JsonObjectPtr aDeviceJSON)
{
  // replicate the basic structure / behaviours
  // as much as needed by localcontroller processing and value sources
  JsonObjectPtr desc;
  // - output
  if (aDeviceJSON->get("outputDescription", desc)) {
    OutputBehaviourPtr o = OutputBehaviourPtr(new OutputBehaviour(*this));
    o->setHardwareOutputConfig(outputFunction_custom, outputmode_default, usage_undefined, false, -1);
    o->setHardwareName("proxy output");
    addBehaviour(o);
  }
  JsonObjectPtr descs;
  string id;
  // - buttons
  if (aDeviceJSON->get("buttonInputDescriptions", descs)) {
    descs->resetKeyIteration();
    while(descs->nextKeyValue(id, desc)) {
      ButtonBehaviourPtr bb = ButtonBehaviourPtr(new ButtonBehaviour(*this, id));
      // - for LocalController::processButtonClick we only need settings props,
      //   we get these in updateCachedProperties
      // - completely generic description is sufficient here
      bb->setHardwareName("proxy button");
      addBehaviour(bb);
    }
  }
  // - binary inputs
  if (aDeviceJSON->get("binaryInputDescriptions", descs)) {
    descs->resetKeyIteration();
    while(descs->nextKeyValue(id, desc)) {
      BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this, id));
      // - completely generic description is sufficient here
      ib->setHardwareName("proxy input");
      addBehaviour(ib);
    }
  }
  // - proxy sensor
  if (aDeviceJSON->get("sensorDescriptions", descs)) {
    descs->resetKeyIteration();
    while(descs->nextKeyValue(id, desc)) {
      SensorBehaviourPtr sb = SensorBehaviourPtr(new SensorBehaviour(*this, id));
      // - completely generic description is sufficient here
      sb->setHardwareName("proxy sensor");
      addBehaviour(sb);
    }
  }
  // get the properties we cache locally for addressing and information
  updateCachedProperties(aDeviceJSON);
}


void ProxyDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // make bridgable
  // enable it for bridging on the other side
  JsonObjectPtr params = JsonObject::newObj();
  JsonObjectPtr props = JsonObject::newObj();
  props->add("x-p44-bridged", JsonObject::newBool(true));
  params->add("properties", props);
  call("setProperty", params, boost::bind(&ProxyDevice::bridgingEnabled, this, aCompletedCB, aFactoryReset));
}


void ProxyDevice::bridgingEnabled(StatusCB aCompletedCB, bool aFactoryReset)
{
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


#endif // ENABLE_PROXYDEVICES
