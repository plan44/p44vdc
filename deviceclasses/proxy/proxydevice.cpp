//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2024-2026 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
    installSettings(); // Standard device settings without scene table, but hosting zoneID
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


string ProxyDevice::modelName() const
{
  // Note: when read via API, clients (e.g. WebUI) will get actual device's values, not this
  return "proxy device";
}


string ProxyDevice::webuiURLString() const
{
  // Note: when read via API, clients (e.g. WebUI) will get actual device's values, not this
  // So this is only in case for some reason API access did not work
  // FIXME: maybe we want clients to use the proxy host's webui, not the real device?
  return getVdc().webuiURLString();
}


string ProxyDevice::description()
{
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
      updateLocallyAvailableProperties(props);
      return true;
    }
  }
  else if (aNotification=="vanish") {
    // device got removed
    OLOG(LOG_WARNING, "original device has vanished -> vanish proxy as well");
    hasVanished(false);
    return true;
  }
  return false; // not handled
}


// MARK: - property access forwarding

class ProxyDeviceRootDescriptor : public RootPropertyDescriptor
{
  typedef RootPropertyDescriptor inherited;
public:
  ProxyDeviceRootDescriptor(int aApiVersion, PropertyDescriptorPtr aParentDescriptor) : inherited(aApiVersion, aParentDescriptor) {};
  virtual bool needsPreparation(PropertyAccessMode aMode) const P44_OVERRIDE { return true; };
};


void ProxyDevice::adaptRootDescriptor(PropertyDescriptorPtr& aContainerDescriptor)
{
  // replace by modified descriptor which forces preparation
  aContainerDescriptor = PropertyDescriptorPtr(new ProxyDeviceRootDescriptor(aContainerDescriptor->getApiVersion(), aContainerDescriptor->mParentDescriptor));
}


bool ProxyDevice::localPropertyOverride(JsonObjectPtr aProps, PropertyAccessMode aMode)
{
  if (!aProps) return false;
  // check for special properties that need to be handled locally
  JsonObjectPtr o;
  if (aMode==access_read) {
    // for read
    if (aProps->get("x-p44-bridged", o)) {
      aProps->add("x-p44-bridged", JsonObject::newBool(isBridged()));
    }
    if (aProps->get("x-p44-bridgeable", o)) {
      #if ENABLE_JSONBRIDGEAPI
      aProps->add("x-p44-bridgeable", JsonObject::newBool(bridgeable()));
      #endif
    }
    if (aProps->get("x-p44-bridgingFlags", o)) {
      #if ENABLE_JSONBRIDGEAPI
      aProps->add("x-p44-bridgingFlags", JsonObject::newInt32(mDeviceSettings->bridgingFlags()));
      #endif
    }
    // for now we don't have any overrides, so do not waste time for them
    // TODO: re-enable when needed, otherwise delete, along with overrideRemoteProperties() in all behaviours!
    #if 0
    // also check behaviours for overrides
    JsonObjectPtr elements;
    JsonObjectPtr props;
    string id;
    JsonObjectPtr o;
    if (aProps->get("outputDescription", props)) {
      if (OutputBehaviourPtr ob = getOutput()) {
        ob->overrideRemoteProperties(props, DsBehaviour::behaviourProps_descriptions, false);
      }
    }
    if (aProps->get("buttonInputDescriptions", elements)) {
      elements->resetKeyIteration();
      while(elements->nextKeyValue(id, props)) {
        if (ButtonBehaviourPtr bb = getButton(by_id, id)) {
          bb->overrideRemoteProperties(props, DsBehaviour::behaviourProps_descriptions, false);
        }
      }
    }
    if (aProps->get("binaryInputDescriptions", elements)) {
      elements->resetKeyIteration();
      while(elements->nextKeyValue(id, props)) {
        if (BinaryInputBehaviourPtr ib = getInput(by_id, id)) {
          ib->overrideRemoteProperties(aProps, DsBehaviour::behaviourProps_descriptions, false);
        }
      }
    }
    if (aProps->get("sensorDescriptions", elements)) {
      elements->resetKeyIteration();
      while(elements->nextKeyValue(id, props)) {
        if (SensorBehaviourPtr sb = getSensor(by_id, id)) {
          sb->overrideRemoteProperties(aProps, DsBehaviour::behaviourProps_descriptions, false);
        }
      }
    }
    #endif // 0
  }
  else {
    // for write
    if (aProps->get("x-p44-bridged", o)) {
      #if ENABLE_JSONBRIDGEAPI
      mBridged = o->boolValue();
      pushBridgingStatus(false); // report as generic API push, not to bridge
      #endif
      aProps->del("x-p44-bridged"); // do not propagate write to proxy!
    }
    if (aProps->get("x-p44-bridgingFlags", o)) {
      #if ENABLE_JSONBRIDGEAPI
      if (mDeviceSettings->setPVar(mDeviceSettings->mBridgingFlags, (DeviceSettings::BridgingFlags)o->int32Value())) {
        pushBridgingStatus(true);
      }
      #endif
      aProps->del("x-p44-bridgingFlags"); // do not propagate write to proxy!
    }
  }
  return aProps->numKeys()>0; // non-empty properties object
}


void ProxyDevice::accessProperty(PropertyAccessMode aMode, ApiValuePtr aQueryObject, int aDomain, int aApiVersion, PropertyAccessCB aAccessCompleteCB)
{
  JsonObjectPtr params = JsonObject::newObj();
  JsonObjectPtr props = JsonApiValue::getAsJson(aQueryObject);
  string method;
  if (aMode==access_read) {
    // read from remote, maybe override some results with local value when we got the result, in handleProxyPropertyAccessResponse()
    method = "getProperty";
    params->add("query", props);
  }
  else {
    // write to remote, but first check for omitting/intercepting some properties not meant for the remote
    if (!localPropertyOverride(props, aMode)) {
      // nothing to set at all (e.g. everything consumed locally) -> 
      if (aAccessCompleteCB) aAccessCompleteCB(ApiValuePtr(), ErrorPtr());
      return;
    }
    // make sure we mirror changes to settings locally
    updateLocallyAvailableProperties(props);
    // now send to remote
    method = "setProperty";
    params->add("properties", props);
    if (aMode==access_write_preload) {
      params->add("preload", JsonObject::newBool(true));
    }
  }
  call(method, params, boost::bind(&ProxyDevice::handleProxyPropertyAccessResponse, this, aMode, aAccessCompleteCB, aQueryObject->newObject(), _1, _2));
}


void ProxyDevice::handleProxyPropertyAccessResponse(PropertyAccessMode aMode, PropertyAccessCB aAccessCompleteCB, ApiValuePtr aResultObj, ErrorPtr aError, JsonObjectPtr aJsonObject)
{
  if (aError) {
    OLOG(LOG_WARNING, "remote -> proxy: property access call failed on transport level: %s", Error::text(aError));
    // error propagates immediately
  }
  else {
    OLOG(LOG_INFO, "remote -> proxy: property access response: %s", JsonObject::text(aJsonObject));
    JsonObjectPtr o;
    if (aJsonObject->get("error", o)) {
      // error
      ErrorCode e = o->int32Value();
      string msg;
      if (aJsonObject->get("errormessage", o)) msg = o->stringValue();
      aError = Error::err<VdcApiError>(e, "%s", msg.c_str());
    }
    else {
      // result will be accessed later by accessPropertyInternal()
      JsonObjectPtr props = aJsonObject->get("result");
      localPropertyOverride(props, aMode);
      JsonApiValue::setAsJson(aResultObj, props);
    }
  }
  if (aAccessCompleteCB) aAccessCompleteCB(aResultObj, aError);
}



// MARK: - cached properties

// we get here on setup initially, and on further property read's results and write intentions,
// so we'll always have up-to-date versions of the to-be-cached roperties
// (=those to be locally used by localcontroller and script functions)
void ProxyDevice::updateLocallyAvailableProperties(JsonObjectPtr aProps)
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
    FOCUSOLOG("update bridgeable state to %d", o->boolValue());
    updatePresenceState(o->boolValue());
    // but confirm to bridgeAPI we have recognized it (will also stop getting notifications)
    JsonObjectPtr p = JsonObject::newBool(o->boolValue());
    p = p->wrapAs("x-p44-bridged")->wrapAs("properties");
    call("setProperty", p, NoOP);
  }
  // properties we need for multicast addressing
  // - zone ID
  if (aProps->get("zoneID", o)) {
    FOCUSOLOG("update mirrored zoneid to %d", o->int32Value());
    setZoneID(o->int32Value());
  }
  // - device level color class
  if (aProps->get("primaryGroup", o)) {
    FOCUSOLOG("update mirrored primaryGroup to %d", o->int32Value());
    setColorClass(static_cast<DsClass>(o->int32Value()));
  }
  // other cached properties for internal purposes such as logging
  // - name
  if (aProps->get("name", o)) {
    FOCUSOLOG("update mirrored name to '%s'", o->stringValue().c_str());
    initializeName(o->stringValue());
  }
  // - output group settings
  if (getOutput()) {
    if (aProps->get("outputSettings", props)) {
      FOCUSOLOG("updating mirrored output settings: %s", JsonObject::text(props));
      // - output level color class
      if (props->get("colorClass", o)) {
        FOCUSOLOG("- update cached colorClass to %d", o->int32Value());
        getOutput()->initColorClass(static_cast<DsClass>(o->int32Value()));
      }
      // - group memberships
      JsonObjectPtr groups;
      if (props->get("groups", groups)) {
        FOCUSOLOG("- update cached groups to %s", JsonObject::text(groups));
        string groupstr;
        getOutput()->resetGroupMembership();
        groups->resetKeyIteration();
        while(groups->nextKeyValue(groupstr, o)) {
          int groupno;
          if (sscanf(groupstr.c_str(), "%d", &groupno)==1) {
            getOutput()->setGroupMembership(static_cast<DsGroup>(groupno), o->boolValue());
          }
        }
      }
    }
    else {
      if (aProps->get("outputStates", props)) {
        FOCUSOLOG("updating mirrored output states: %s", JsonObject::text(props));
        if (props->get("error")) getOutput()->setHardwareError((VdcHardwareError)o->int32Value());
        if (props->get("localPriority")) getOutput()->setLocalPriority(o->boolValue());
      }
    }
  }
  // - output descriptions/settings needed for local control and p44script
  if (aProps->get("channelDescriptions", elements)) {
    elements->resetKeyIteration();
    while(elements->nextKeyValue(id, props)) {
      if (CustomChannelPtr ccb = dynamic_pointer_cast<CustomChannel>(getChannelById(id))) {
        FOCUSPOLOG(ccb, "update mirrored description properties: %s", JsonObject::text(props));
        ccb->updateMirroredProperties(props, DsBehaviour::behaviourProps_descriptions);
      }
    }
  }
  if (aProps->get("channelSettings", elements)) {
    elements->resetKeyIteration();
    while(elements->nextKeyValue(id, props)) {
      if (CustomChannelPtr ccb = dynamic_pointer_cast<CustomChannel>(getChannelById(id))) {
        FOCUSPOLOG(ccb, "update mirrored settings properties: %s", JsonObject::text(props));
        ccb->updateMirroredProperties(props, DsBehaviour::behaviourProps_settings);
      }
    }
  }
  // - button descriptions/settings needed for local control and p44script
  if (aProps->get("buttonInputDescriptions", elements)) {
    elements->resetKeyIteration();
    while(elements->nextKeyValue(id, props)) {
      if (ButtonBehaviourPtr bb = getButton(by_id, id)) {
        FOCUSPOLOG(bb, "update mirrored description properties: %s", JsonObject::text(props));
        bb->updateMirroredProperties(props, DsBehaviour::behaviourProps_descriptions);
      }
    }
  }
  if (aProps->get("buttonInputSettings", elements)) {
    elements->resetKeyIteration();
    while(elements->nextKeyValue(id, props)) {
      if (ButtonBehaviourPtr bb = getButton(by_id, id)) {
        FOCUSPOLOG(bb, "update mirrored settings properties: %s", JsonObject::text(props));
        bb->updateMirroredProperties(props, DsBehaviour::behaviourProps_settings);
      }
    }
  }
  // - input descriptions/settings needed for local control and p44script
  if (aProps->get("binaryInputDescriptions", elements)) {
    elements->resetKeyIteration();
    while(elements->nextKeyValue(id, props)) {
      if (BinaryInputBehaviourPtr ib = getInput(by_id, id)) {
        FOCUSPOLOG(ib, "update mirrored description properties: %s", JsonObject::text(props));
        ib->updateMirroredProperties(aProps, DsBehaviour::behaviourProps_descriptions);
      }
    }
  }
  if (aProps->get("binaryInputSettings", elements)) {
    elements->resetKeyIteration();
    while(elements->nextKeyValue(id, props)) {
      if (BinaryInputBehaviourPtr ib = getInput(by_id, id)) {
        FOCUSPOLOG(ib, "update mirrored settings properties: %s", JsonObject::text(props));
        ib->updateMirroredProperties(props, DsBehaviour::behaviourProps_settings);
      }
    }
  }
  // - sensor descriptions/settings needed for local control and p44script
  if (aProps->get("sensorDescriptions", elements)) {
    elements->resetKeyIteration();
    while(elements->nextKeyValue(id, props)) {
      if (SensorBehaviourPtr sb = getSensor(by_id, id)) {
        FOCUSPOLOG(sb, "update mirrored description properties: %s", JsonObject::text(props));
        sb->updateMirroredProperties(aProps, DsBehaviour::behaviourProps_descriptions);
      }
    }
  }
  if (aProps->get("sensorSettings", elements)) {
    elements->resetKeyIteration();
    while(elements->nextKeyValue(id, props)) {
      if (SensorBehaviourPtr sb = getSensor(by_id, id)) {
        FOCUSPOLOG(sb, "update mirrored settings properties: %s", JsonObject::text(props));
        sb->updateMirroredProperties(props, DsBehaviour::behaviourProps_settings);
      }
    }
  }
  // input states we actually need to propagate
  if (aProps->get("buttonInputStates", elements)) {
    elements->resetKeyIteration();
    while(elements->nextKeyValue(id, props)) {
      if (ButtonBehaviourPtr bb = getButton(by_id, id)) {
        FOCUSPOLOG(bb, "update mirrored state properties: %s", JsonObject::text(props));
        bb->updateMirroredProperties(props, DsBehaviour::behaviourProps_states);
      }
    }
  }
  if (aProps->get("binaryInputStates", elements)) {
    elements->resetKeyIteration();
    while(elements->nextKeyValue(id, props)) {
      if (BinaryInputBehaviourPtr ib = getInput(by_id, id)) {
        FOCUSPOLOG(ib, "update mirrored state properties: %s", JsonObject::text(props));
        ib->updateMirroredProperties(props, DsBehaviour::behaviourProps_states);
      }
    }
  }
  if (aProps->get("sensorStates", elements)) {
    elements->resetKeyIteration();
    while(elements->nextKeyValue(id, props)) {
      if (SensorBehaviourPtr sb = getSensor(by_id, id)) {
        FOCUSPOLOG(sb, "update mirrored state properties: %s", JsonObject::text(props));
        sb->updateMirroredProperties(props, DsBehaviour::behaviourProps_states);
      }
    }
  }
  // output channel states we also want to propagate (mainly for UI, or bridges)
  if (aProps->get("channelStates", elements)) {
    elements->resetKeyIteration();
    while(elements->nextKeyValue(id, props)) {
      if (CustomChannelPtr ccb = dynamic_pointer_cast<CustomChannel>(getChannelById(id))) {
        FOCUSPOLOG(ccb, "update mirrored state properties: %s", JsonObject::text(props));
        ccb->updateMirroredProperties(props, DsBehaviour::behaviourProps_states);
      }
    }
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
  JsonObjectPtr descs;
  string id;
  // - output
  if (aDeviceJSON->get("outputDescription", desc)) {
    OutputBehaviourPtr o = OutputBehaviourPtr(new OutputBehaviour(*this));
    o->setHardwareOutputConfig(outputFunction_custom, outputmode_default, usage_undefined, false, -1);
    o->setHardwareName("proxy output");
    addBehaviour(o);
    // - channels
    if (aDeviceJSON->get("channelDescriptions", descs)) {
#error need to add in the same dsIndex order as original, as indices matter
      descs->resetKeyIteration();
      while(descs->nextKeyValue(id, desc)) {
        ChannelBehaviourPtr ch = ChannelBehaviourPtr(new CustomChannel(*o, id));
        o->addChannel(ch);
      }
    }
  }
  // - buttons
  if (aDeviceJSON->get("buttonInputDescriptions", descs)) {
    descs->resetKeyIteration();
    while(descs->nextKeyValue(id, desc)) {
#error need to add in the same dsIndex order as original, as indices matter
      ButtonBehaviourPtr bb = ButtonBehaviourPtr(new ButtonBehaviour(*this, id));
      // - for LocalController::processButtonClick we only need settings props,
      //   we get these in updateCachedProperties
      // - completely generic description is sufficient here
      bb->setHardwareName("proxy button");
      addBehaviour(bb);
      // make button bridge exclusive
      JsonObjectPtr p = JsonObject::newBool(true);
      p = p->wrapAs("x-p44-bridgeExclusive")->wrapAs(id)->wrapAs("buttonInputSettings")->wrapAs("properties");
      call("setProperty", p, NoOP);
    }
  }
  // - binary inputs
  if (aDeviceJSON->get("binaryInputDescriptions", descs)) {
#error need to add in the same dsIndex order as original, as indices matter
    descs->resetKeyIteration();
    while(descs->nextKeyValue(id, desc)) {
      BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this, id));
      // - completely generic description is sufficient here
      ib->setHardwareName("proxy input");
      addBehaviour(ib);
      // make input bridge exclusive
      JsonObjectPtr p = JsonObject::newBool(true);
      p = p->wrapAs("x-p44-bridgeExclusive")->wrapAs(id)->wrapAs("binaryInputSettings")->wrapAs("properties");
      call("setProperty", p, NoOP);
    }
  }
  // - proxy sensor
  if (aDeviceJSON->get("sensorDescriptions", descs)) {
#error need to add in the same dsIndex order as original, as indices matter
    descs->resetKeyIteration();
    while(descs->nextKeyValue(id, desc)) {
      SensorBehaviourPtr sb = SensorBehaviourPtr(new SensorBehaviour(*this, id));
      // - completely generic description is sufficient here
      sb->setHardwareName("proxy sensor");
      addBehaviour(sb);
      // make sensor bridge exclusive
      JsonObjectPtr p = JsonObject::newBool(true);
      p = p->wrapAs("x-p44-bridgeExclusive")->wrapAs(id)->wrapAs("sensorSettings")->wrapAs("properties");
      call("setProperty", p, NoOP);
    }
  }
  // get the properties we also maintain locally for addressing, information, localcontroller and scripting
  updateLocallyAvailableProperties(aDeviceJSON);
}


void ProxyDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // make bridgable
  // enable it for bridging on the other side
  JsonObjectPtr p = JsonObject::newBool(true);
  p = p->wrapAs("x-p44-bridged")->wrapAs("properties");
  call("setProperty", p, boost::bind(&ProxyDevice::bridgingEnabled, this, aCompletedCB, aFactoryReset));
}


void ProxyDevice::bridgingEnabled(StatusCB aCompletedCB, bool aFactoryReset)
{
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


#endif // ENABLE_PROXYDEVICES
