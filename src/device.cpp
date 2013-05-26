//
//  device.cpp
//  p44bridged
//
//  Created by Lukas Zeller on 18.04.13.
//  Copyright (c) 2013 plan44.ch. All rights reserved.
//

#include "device.hpp"

using namespace p44;


#pragma mark - digitalSTROM behaviour


DSBehaviour::DSBehaviour(Device *aDeviceP) :
  deviceP(aDeviceP)
{
}


DSBehaviour::~DSBehaviour()
{
}


ErrorPtr DSBehaviour::handleMessage(string &aOperation, JsonObjectPtr aParams)
{
  // base class behaviour does not support any operations
  return ErrorPtr(new vdSMError(
    vdSMErrorUnknownDeviceOperation,
    string_format(
      "unknown device behaviour operation '%s' for %s/%s",
      aOperation.c_str(), shortDesc().c_str(), deviceP->shortDesc().c_str()
    )
  ));
}


bool DSBehaviour::sendMessage(const char *aOperation, JsonObjectPtr aParams)
{
  // just forward to device
  return deviceP->sendMessage(aOperation, aParams);
}




#pragma mark - Device


Device::Device(DeviceClassContainer *aClassContainerP) :
  registered(Never),
  registering(Never),
  busAddress(0),
  classContainerP(aClassContainerP),
  behaviourP(NULL)
{

}


Device::~Device()
{
  setDSBehaviour(NULL);
}


void Device::setDSBehaviour(DSBehaviour *aBehaviour)
{
  if (behaviourP)
    delete behaviourP;
  behaviourP = aBehaviour;
}


void Device::ping()
{
  // base class just sends the pong, but derived classes which can actually ping their hardware should
  // do so and send the pong only if the hardware actually responds.
  pong();
}


void Device::pong()
{
  sendMessage("pong", JsonObjectPtr());
}



JsonObjectPtr Device::registrationParams()
{
  // create the registration request
  JsonObjectPtr req = JsonObject::newObj();
  // add the parameters
  req->add("dSID", JsonObject::newString(dsid.getString()));
  req->add("VendorId", JsonObject::newInt32(1)); // TODO: %%% luz: must be 1=aizo, dsa cannot expand other ids so far
  req->add("FunctionId", JsonObject::newInt32(behaviourP->functionId()));
  req->add("ProductId", JsonObject::newInt32(behaviourP->productId()));
  req->add("Version", JsonObject::newInt32(behaviourP->version()));
  req->add("LTMode", JsonObject::newInt32(behaviourP->ltMode()));
  req->add("Mode", JsonObject::newInt32(behaviourP->outputMode()));
  // return it
  return req;
}


void Device::confirmRegistration(JsonObjectPtr aParams)
{
  JsonObjectPtr o = aParams->get("BusAddress");
  if (o) {
    busAddress = o->int32Value();
  }
  // registered now
  registered = MainLoop::now();
  registering = Never;
}

//  if request['operation'] == 'DeviceRegistrationAck':
//      self.address = request['parameter']['BusAddress']
//      self.zone = request['parameter']['Zone']
//      self.groups = request['parameter']['GroupMemberships']
//      print 'BusAddress:', request['parameter']['BusAddress']
//      print 'Zone:', request['parameter']['Zone']
//      print 'Groups:', request['parameter']['GroupMemberships']



ErrorPtr Device::handleMessage(string &aOperation, JsonObjectPtr aParams)
{
  // check for generic device operations
  if (aOperation=="ping") {
    // 
  }
  // TODO: add generic device operations
  // no generic device operation, let behaviour handle it
  if (behaviourP) {
    return behaviourP->handleMessage(aOperation, aParams);
  }
  else {
    return ErrorPtr(new vdSMError(
      vdSMErrorUnknownDeviceOperation,
      string_format("unknown device operation '%s' for %s", aOperation.c_str(), shortDesc().c_str())
    ));
  }
}



bool Device::sendMessage(const char *aOperation, JsonObjectPtr aParams)
{
  if (!aParams) {
    // no parameters passed, create new parameter object
    aParams = JsonObject::newObj();
  }
  // add dsid and bus address parameters
  aParams->add("dSID", JsonObject::newString(dsid.getString()));
  if (registered) {
    aParams->add("BusAddress", JsonObject::newInt32(busAddress));
  }
  // have device container send it
  return classContainerP->getDeviceContainerP()->sendMessage(aOperation, aParams);
}







string Device::shortDesc()
{
  // short description is dsid
  return dsid.getString();
}



string Device::description()
{
  string s = string_format("Device %s", shortDesc().c_str());
  if (registered)
    string_format_append(s, " (BusAddress %d)", busAddress);
  else
    s.append(" (unregistered)");
  s.append("\n");
  if (behaviourP) {
    string_format_append(s, "- Input: %d/%d, DSBehaviour : %s\n", getInputIndex()+1, getNumInputs(), behaviourP->shortDesc().c_str());
  }
  return s;
}
