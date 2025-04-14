//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 5

#include "ds485vdc.hpp"
#include "ds485device.hpp"

#if ENABLE_DS485DEVICES

#include "colorlightbehaviour.hpp"
#include "shadowbehaviour.hpp"
#include "ventilationbehaviour.hpp"
#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "sensorbehaviour.hpp"

using namespace p44;

// MARK: - Ds485Vdc

Ds485Vdc::Ds485Vdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag)
{
  mDs485Comm.isMemberVariable();
}


Ds485Vdc::~Ds485Vdc()
{
  mDs485Comm.stop();
}


void Ds485Vdc::handleGlobalEvent(VdchostEvent aEvent)
{
  if (aEvent==vdchost_vdcapi_connected) {
    mDs485Comm.mDs485HostIP = VdcHost().vdsmHostIp();
    // re-connecting vdsm should re-scan ds485 devices
    collectDevices(NoOP, rescanmode_normal);
  }
  inherited::handleGlobalEvent(aEvent);
}


// MARK: - initialisation

void Ds485Vdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // install handler
  mDs485Comm.setDs485MessageHandler(boost::bind(&Ds485Vdc::ds485MessageHandler, this, _1, _2, _3));
  // start
  mDs485Comm.start(aCompletedCB);
}


const char *Ds485Vdc::vdcClassIdentifier() const
{
  // The class identifier is only for addressing by specifier
  return "dS485_Device_Container";
}


string Ds485Vdc::webuiURLString()
{
  if (mDs485Comm.mDs485HostIP.empty()) return inherited::webuiURLString();
  return string_format("http://%s", mDs485Comm.mDs485HostIP.c_str());
}


bool Ds485Vdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_ds485", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


int Ds485Vdc::getRescanModes() const
{
  return rescanmode_incremental+rescanmode_normal;
}


/// collect devices from this vDC
/// @param aCompletedCB will be called when device scan for this vDC has been completed
void Ds485Vdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  if (!(aRescanFlags & rescanmode_incremental)) {
    // full collect, remove all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
  }
  mDs485Devices.clear();
  mDs485Comm.mDs485ClientThread->executeOnChildThreadAsync(boost::bind(&Ds485Vdc::scanDs485BusSync, this, _1), boost::bind(&Ds485Vdc::ds485BusScanned, this, _1, aCompletedCB));
}


string fullDevId(const DsUid& aDsuid, const uint16_t aDevId)
{
  string fullid(aDsuid.getBinary());
  fullid.append(1, (uint8_t)(aDevId>>8));
  fullid.append(1, (uint8_t)(aDevId&0xFF));
  return fullid;
}


Ds485DevicePtr Ds485Vdc::deviceFor(DsUidPtr aDsmDsUid, uint16_t aDevId)
{
  Ds485DevicePtr dev;
  if (aDsmDsUid) {
    Ds485DeviceMap::iterator pos = mDs485Devices.find(fullDevId(*aDsmDsUid, aDevId));
    if (pos!=mDs485Devices.end()) return pos->second;
  }
  return dev;
}


void Ds485Vdc::ds485BusScanned(ErrorPtr aScanStatus, StatusCB aCompletedCB)
{
  if (Error::isOK(aScanStatus)) {
    // now add my devices
    for (Ds485DeviceMap::iterator pos = mDs485Devices.begin(); pos!=mDs485Devices.end(); ++pos) {
      Ds485DevicePtr dev = pos->second;
      if (simpleIdentifyAndAddDevice(dev)) {
        // TODO: maybe something
      }
    }
  }
  if (aCompletedCB) aCompletedCB(aScanStatus);
}



// MARK: - operation

void Ds485Vdc::ds485MessageHandler(DsUidPtr aSource, DsUidPtr aTarget, const string aPayload)
{
  OLOG(LOG_NOTICE,"dS485 Message: %s -> %s: [%zu] %s", DsUid::text(aSource).c_str(), DsUid::text(aTarget).c_str(), aPayload.size(), binaryToHexString(aPayload, ' ').c_str());
  size_t pli = 0;
  uint8_t command;
  if ((pli = Ds485Comm::payload_get8(aPayload, pli, command))==0) goto error;
  uint8_t modifier;
  if ((pli = Ds485Comm::payload_get8(aPayload, pli, modifier))==0) goto error;
  switch (command) {
    case EVENT_COMMUNICATION_LOG: {
      switch (modifier) {
        case EVENT_COMMUNICATION_LOG_UPSTREAM_SHORT: {
          pli++; // skip that 3rd byte dsm events seem to have
          uint16_t devId;
          if ((pli = Ds485Comm::payload_get16(aPayload, pli, devId))==0) goto error;
          pli++; // skip CircuitId
          pli++; // skip Resend // TODO: maybe evaluate this
          uint8_t isSensor;
          if ((pli = Ds485Comm::payload_get8(aPayload, pli, isSensor))==0) goto error;
          uint8_t keyNo;
          if ((pli = Ds485Comm::payload_get8(aPayload, pli, keyNo))==0) goto error;
          uint8_t click;
          if ((pli = Ds485Comm::payload_get8(aPayload, pli, click))==0) goto error;
          Ds485DevicePtr dev = deviceFor(aSource, devId);
          if (dev) dev->handleDeviceUpstreamMessage(isSensor, keyNo, (DsClickType)click);
          break;
        }
      }
      break;
    }
    case DEVICE_ACTION_REQUEST: {
      uint16_t devId;
      if ((pli = Ds485Comm::payload_get16(aPayload, pli, devId))==0) goto error;
      Ds485DevicePtr dev = deviceFor(aTarget, devId);
      if (dev) {
        switch (modifier) {
          // 302ED89F43F0000000002BA000011C8800 -> 302ED89F43F0000000000E400000E9D700: [05] 51 01 02 98 0E
          case DEVICE_ACTION_REQUEST_ACTION_CALL_SCENE: {
            SceneNo scene;
            if ((pli = Ds485Comm::payload_get8(aPayload, pli, scene))==0) goto error;
            dev->traceSceneCall(scene);
            break;
          }
          // 302ED89F43F0000000002BA000011C8800 -> 302ED89F43F0000000000E400000E9D700: [5] 51 07 03 ED B8
          case DEVICE_ACTION_REQUEST_ACTION_SET_OUTVAL: {
            uint8_t outval;
            if ((pli = Ds485Comm::payload_get8(aPayload, pli, outval))==0) goto error;
            dev->traceChannelChange(channeltype_default, outval);
          }
        }
      }
      break;
    }
    case EVENT_DEVICE_CONFIG: {
      // this is the response for requesting a bank/offset type DEVICE_CONFIG request
      // 302ED89F43F0000000000E400000E9D700 -> FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF, t=0xff: [08] 74 00 00 03 ED 40 00 FF
      // 302ED89F43F0000000000E400000E9D700 -> FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF, t=0xff: [08] 74 00 00 03 ED 40 01 00
      pli++; // skip that 3rd byte dsm events seem to have
      uint16_t devId;
      if ((pli = Ds485Comm::payload_get16(aPayload, pli, devId))==0) goto error;
      Ds485DevicePtr dev = deviceFor(aSource, devId);
      if (dev) {
        // this is a device config readout from this device
        uint8_t bank;
        if ((pli = Ds485Comm::payload_get8(aPayload, pli, bank))==0) goto error;
        uint8_t offs;
        if ((pli = Ds485Comm::payload_get8(aPayload, pli, offs))==0) goto error;
        uint8_t byte;
        if ((pli = Ds485Comm::payload_get8(aPayload, pli, byte))==0) goto error;
        switch (bank) {
          case 64:// RAM
            switch (offs) {
              case 0: // standard output value
                dev->traceChannelChange(channeltype_default, byte);
                break;
              // TODO: gray, blue and green blocks have the relevant values in other offsets
            }
            break;
        }
      }
    }
  }
  return;
error:
  LOG(LOG_WARNING, "payload too short (%zu) to access data at %zu", aPayload.size(), pli);
}


void Ds485Vdc::deliverToDevicesAudience(DsAddressablesList aAudience, VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams)
{
  inherited::deliverToDevicesAudience(aAudience, aApiConnection, aNotification, aParams);
  // TODO: implement optimisations to call native scenes instead of device adjustment

//  for (DsAddressablesList::iterator apos = aAudience.begin(); apos!=aAudience.end(); ++apos) {
//    // TODO: implement
//  }
}


// MARK: - things that need to run on ds485 thread because they are blocking

ErrorPtr Ds485Vdc::scanDs485BusSync(ChildThreadWrapper &aThread)
{
  // startup, collect info
  // - the bus devices
  ErrorPtr err;
  const int maxbusdevices = 64;
  dsuid_t busdevices[maxbusdevices];
  int numDsms = ds485_client_query_devices(mDs485Comm.mDs485Client, busdevices, maxbusdevices);
  // iterate dSMs
  for (int di=0; di<numDsms; di++) {
    DsUidPtr dsmDsuid = new DsUid(busdevices[di]);
    OLOG(LOG_NOTICE, "dSM #%d: %s", di, dsmDsuid->getString().c_str());
    // prevent asking myself
    if (*dsmDsuid!=*mDs485Comm.mMyDsuid) {
      string resp;
      size_t pli;
      // - get the dSM info
      err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DSM_INFO);
      if (Error::notOK(err)) return err;
      pli = 3;
      uint32_t dsmHwVersion;
      pli = Ds485Comm::payload_get32(resp, pli, dsmHwVersion);
      uint32_t dsmArmVersion;
      pli = Ds485Comm::payload_get32(resp, pli, dsmArmVersion);
      uint32_t dsmDSPVersion;
      pli = Ds485Comm::payload_get32(resp, pli, dsmDSPVersion);
      uint16_t dsmAPIVersion;
      pli = Ds485Comm::payload_get16(resp, pli, dsmAPIVersion);
      pli += 12; // skip "dSID"
      string dsmName;
      pli = Ds485Comm::payload_getString(resp, pli, 21, dsmName);
      OLOG(LOG_NOTICE, "dSM #%d: '%s', hwV=0x%08x, armV=0x%08x, dspV=0x%08x, apiV=0x%04x", di, dsmName.c_str(), dsmHwVersion, dsmArmVersion, dsmDSPVersion, dsmAPIVersion);
      // - get the zone count
      err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, ZONE_COUNT);
      if (Error::notOK(err)) return err;
      pli = 3;
      uint8_t zoneCount;
      pli = Ds485Comm::payload_get8(resp, pli, zoneCount);
      OLOG(LOG_NOTICE, "dSM #%d: has %d zones", di, zoneCount);
      // - the zones
      for (int i=0; i<zoneCount; i++) {
        string req;
        Ds485Comm::payload_append8(req, i);
        err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, ZONE_INFO, ZONE_INFO_BY_INDEX, req);
        if (Error::notOK(err)) return err;
        size_t pli;
        pli = 3;
        uint16_t zoneId;
        pli = Ds485Comm::payload_get16(resp, pli, zoneId);
        uint8_t vzoneId;
        pli = Ds485Comm::payload_get8(resp, pli, vzoneId);
        uint8_t numGroups;
        pli = Ds485Comm::payload_get8(resp, pli, numGroups);
        string zonename;
        pli = Ds485Comm::payload_getString(resp, pli, 21, zonename);
        OLOG(LOG_NOTICE, "zone #%d: id=%d, virtid=%d, numgroups=%d, name='%s'", i, zoneId, vzoneId, numGroups, zonename.c_str());
        // - the devices in the zone
        req.clear();
        Ds485Comm::payload_append16(req, zoneId);
        err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, ZONE_DEVICE_COUNT, ZONE_DEVICE_COUNT_ALL, req);
        if (Error::notOK(err)) return err;
        uint16_t numZoneDevices;
        pli = 3;
        pli = Ds485Comm::payload_get16(resp, pli, numZoneDevices);
        OLOG(LOG_NOTICE, "zone #%d: number of devices = %d", i, numZoneDevices);
        for (int j=0; j<numZoneDevices; j++) {
          string req;
          Ds485Comm::payload_append16(req, zoneId);
          Ds485Comm::payload_append16(req, j);
          err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_INFO, DEVICE_INFO_BY_INDEX, req);
          if (Error::notOK(err)) return err;
          pli = 3;
          uint16_t devId;
          pli = Ds485Comm::payload_get16(resp, pli, devId);
          uint16_t vendId;
          pli = Ds485Comm::payload_get16(resp, pli, vendId);
          uint16_t prodId;
          pli = Ds485Comm::payload_get16(resp, pli, prodId);
          uint16_t funcId;
          pli = Ds485Comm::payload_get16(resp, pli, funcId);
          uint16_t vers;
          pli = Ds485Comm::payload_get16(resp, pli, vers);
          uint16_t zoneId;
          pli = Ds485Comm::payload_get16(resp, pli, zoneId);
          uint8_t active;
          pli = Ds485Comm::payload_get8(resp, pli, active);
          uint8_t locked;
          pli = Ds485Comm::payload_get8(resp, pli, locked);
          uint8_t outMode;
          pli = Ds485Comm::payload_get8(resp, pli, outMode);
          uint8_t ltMode;
          pli = Ds485Comm::payload_get8(resp, pli, ltMode);
          DsGroupMask groups;
          pli = Ds485Comm::payload_getGroups(resp, pli, groups);
          string devName;
          pli = Ds485Comm::payload_getString(resp, pli, 21, devName);
          DsUidPtr dSUID = new DsUid;
          dSUID->setAsBinary(resp.substr(pli, 17)); pli += 17;
          uint8_t activeGroup;
          pli = Ds485Comm::payload_get8(resp, pli, activeGroup);
          uint8_t defaultGroup;
          pli = Ds485Comm::payload_get8(resp, pli, defaultGroup);
          OLOG(LOG_NOTICE,
            "device #%d: %s [0x%04x] - '%s'\n"
            "- vendId=0x%04x, prodId=0x%04x, funcId=0x%04x, vers=0x%04x\n"
            "- zoneID=%d/0x%04x, active=%d, locked=%d\n"
            "- outMode=0x%04x, ltMode=0x%04x\n"
            "- groups=0x%016llx, activeGroup=%d, defaultGroup=%d",
            j, dSUID->getString().c_str(), devId, devName.c_str(),
            vendId, prodId, funcId, vers,
            zoneId, zoneId, active, locked,
            outMode, ltMode,
            groups, activeGroup, defaultGroup
          );
          Ds485DevicePtr dev = new Ds485Device(this, *dsmDsuid, devId);
          dev->mIsPresent = active;
          // make a real dSUID out of it
          dev->mDSUID.setAsDSId(dSUID->getBinary().substr(12, 4));
          dev->initializeName(devName);
          // - output channel info for determining output function
          req.clear();
          Ds485Comm::payload_append16(req, devId);
          err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_O_P_C_TABLE, DEVICE_O_P_C_TABLE_GET_COUNT, req);
          if (Error::notOK(err)) return err;
          pli = 3;
          uint8_t numOPC;
          pli = Ds485Comm::payload_get8(resp, pli, numOPC);
          OLOG(LOG_NOTICE, "device #%d: number of OPC channels = %d", j, numOPC);
          dev->mNumOPC = numOPC;
          // - output mode and function
          VdcOutputMode mode = outputmode_disabled;
          if ((outMode>=17 && outMode<=24) || outMode==28 || outMode==30) mode = outputmode_gradual;
          else if (outMode!=0) mode = outputmode_binary;
          VdcOutputFunction func = mode==outputmode_binary ? outputFunction_switch : outputFunction_dimmer;
          VdcUsageHint usage = usage_room;
          // - OPC channels
          for (int oi=0; oi<numOPC; oi++) {
            // - OPC channel info
            req.clear();
            Ds485Comm::payload_append16(req, devId);
            Ds485Comm::payload_append8(req, oi);
            err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_O_P_C_TABLE, DEVICE_O_P_C_TABLE_GET_BY_INDEX, req);
            if (Error::notOK(err)) return err;
            pli = 3;
            uint8_t channelId;
            pli = Ds485Comm::payload_get8(resp, pli, channelId);
            OLOG(LOG_NOTICE, "device #%d: channel #%d: channelId=%d", j, oi, channelId);
            // check channelid, gives indication for output function
            if (channelId==channeltype_hue) func = outputFunction_colordimmer;
            if (channelId==channeltype_colortemp && func!=outputFunction_colordimmer) func = outputFunction_ctdimmer;
            if (channelId==channeltype_colortemp && func!=outputFunction_colordimmer) func = outputFunction_ctdimmer;
            if (channelId==channeltype_shade_position_outside || channelId==channeltype_shade_position_inside) func = outputFunction_positional;
            if (channelId==channeltype_shade_position_outside || channelId==channeltype_shade_angle_outside) usage = usage_outdoors;
          }
          // examine the funcid for basic device setup
          // - color class
          uint8_t funcClass = (funcId>>12)&0x0F;
          // default to joker for unsupported or DS special ones (such as 16==server controlled)
          dev->setColorClass(static_cast<DsClass>(funcClass==0 || funcClass>=numColorClasses ? class_black_joker : funcClass));
          if (mode!=outputmode_disabled) {
            // - instantiate output
            OutputBehaviourPtr ob;
            if (funcClass==class_yellow_light) {
              if (func==outputFunction_colordimmer || func==outputFunction_ctdimmer) {
                // color or CT light
                dev->installSettings(new ColorLightDeviceSettings(*dev));
                ob = new ColorLightBehaviour(*dev, func==outputFunction_ctdimmer);
                ob->setHardwareName(func==outputFunction_ctdimmer ? "CT light" : "color light");
              }
              else {
                // single color light
                dev->installSettings(new LightDeviceSettings(*dev));
                ob = new LightBehaviour(*dev);
                ob->setHardwareName("light");
              }
            }
            else if (funcClass==class_grey_shadow) {
              // shadow
              dev->installSettings(new ShadowDeviceSettings(*dev));
              ShadowBehaviourPtr sb = new ShadowBehaviour(*dev, (DsGroup)defaultGroup);
              sb->setDeviceParams(shadowdevice_jalousie, false, 0, 0, 0, true); // no move semantics, just set values
              ob = sb;
              ob->setHardwareName("light");
            }
            else {
              // just a simple single channel output
              dev->installSettings(DeviceSettingsPtr(new SceneDeviceSettings(*dev)));
              ob = new OutputBehaviour(*dev);
              if (mode==outputmode_gradual) {
                ob->addChannel(new PercentageLevelChannel(*ob, "dimmer"));
              }
              else {
                ob->addChannel(new DigitalChannel(*ob, "relay"));
              }
            }
            ob->setHardwareOutputConfig(func, mode, usage, false, -1);
            ob->resetGroupMembership(groups);
            dev->addBehaviour(ob);
          }
          else {
            dev->installSettings();
          }
          // - zoneid (needs instantiated settings)
          dev->setZoneID(zoneId);
          // - button info
          //   Note: DS does have terminal blocks with multiple button inputs, but these are represented as multiple devices on the bus
          bool hasButton = true; // most dS blocks do
          if (
            ((funcId&0xFFC0)==0x1000 && (funcId&0x07)==7) || // dS R100
            ((funcId&0xFFC0)==0x1100 && (funcId&0x07)==0) // dS R105
          ) {
            hasButton = false;
          }
          if (hasButton) {
            req.clear();
            Ds485Comm::payload_append16(req, devId);
            err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_BUTTON_INFO, DEVICE_BUTTON_INFO_BY_DEVICE, req);
            if (Error::notOK(err)) return err;
            pli = 3;
            uint8_t ltNumGrp0;
            pli = Ds485Comm::payload_get8(resp, pli, ltNumGrp0);
            pli++; // skip "DeprecatedGroupIfUpTo15"
            uint8_t buttongroup;
            pli = Ds485Comm::payload_get8(resp, pli, buttongroup);
            uint8_t buttonflags;
            pli = Ds485Comm::payload_get8(resp, pli, buttonflags);
            uint8_t buttonchannel;
            pli = Ds485Comm::payload_get8(resp, pli, buttonchannel);
            pli++; // skip "unused"
            OLOG(LOG_NOTICE,
              "device #%d '%s': button: id/LTNUMGRP0=0x%02x, group=%d, flags=0x%02x, channel=%d",
              j, devName.c_str(),
              ltNumGrp0, buttongroup, buttonflags, buttonchannel
            );
            DsButtonMode buttonMode = (DsButtonMode)((ltNumGrp0>>4) & 0x0F);
            VdcButtonType bty = buttonType_single;
            VdcButtonElement bel = buttonElement_center;
            const char* bname = "button";
            int bcount = 1;
            if (ltMode>=5 && ltMode<=12) bty = buttonType_2way;
            else if (ltMode==2 || ltMode==3) bty = buttonType_onOffSwitch;
            else if (ltMode==13) {
              bname = "up";
              bel = buttonElement_up;
              bcount = 2;
            }
            for (int bidx=0; bidx<bcount; bidx++) {
              ButtonBehaviourPtr bb = new ButtonBehaviour(*dev, bname); // automatic id
              bb->setHardwareButtonConfig(0, bty, bel, false, 0, 0); // not combinable
              bb->setGroup((DsGroup)buttongroup);
              bb->setChannel((DsChannelType)buttonchannel);
              bb->setFunction((DsButtonFunc)(ltNumGrp0 & 0x0F));
              bb->setDsMode(buttonMode);
              bb->setCallsPresent(buttonflags&(1<<1));
              bb->setSetsLocalPriority(buttonflags&(1<<0));
              dev->addBehaviour(bb);
              bname = "down";
              bel = buttonElement_down;
            }
          }
          // - binary input info
          req.clear();
          Ds485Comm::payload_append16(req, devId);
          err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_BINARY_INPUT, DEVICE_BINARY_INPUT_GET_COUNT, req);
          if (Error::notOK(err)) return err;
          pli = 3;
          uint8_t numBinInps;
          pli = Ds485Comm::payload_get8(resp, pli, numBinInps);
          OLOG(LOG_NOTICE, "device #%d: number of binary inputs = %d", j, numBinInps);
          for (int bi=0; bi<numBinInps; bi++) {
            // - binary input info
            req.clear();
            Ds485Comm::payload_append16(req, devId);
            Ds485Comm::payload_append8(req, bi);
            err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_BINARY_INPUT, DEVICE_BINARY_INPUT_GET_BY_INDEX, req);
            if (Error::notOK(err)) return err;
            pli = 3;
            uint8_t inpTargetGroupType;
            pli = Ds485Comm::payload_get8(resp, pli, inpTargetGroupType);
            uint8_t inpTargetGroup;
            pli = Ds485Comm::payload_get8(resp, pli, inpTargetGroup);
            uint8_t inpType;
            pli = Ds485Comm::payload_get8(resp, pli, inpType);
            uint8_t inpButtonId;
            pli = Ds485Comm::payload_get8(resp, pli, inpButtonId);
            uint8_t inpIndependent;
            pli = Ds485Comm::payload_get8(resp, pli, inpIndependent);
            OLOG(LOG_NOTICE,
              "- device #%d: binary input #%d: targetGroupType=%d, targetGroup=%d, type=%d, buttonId=0x%02x, independent=%d",
              j, bi, inpTargetGroupType, inpTargetGroup, inpType, inpButtonId, inpIndependent
            );
            BinaryInputBehaviourPtr ib = new BinaryInputBehaviour(*dev, ""); // automatic ID
            ib->setHardwareInputConfig((DsBinaryInputType)inpType, usage_undefined, true, Never, Never);
            // TODO: maybe need to model some inputs as buttons
            ib->setGroup((DsGroup)inpTargetGroup);
            dev->addBehaviour(ib);
          }
          // - sensor info
          req.clear();
          Ds485Comm::payload_append16(req, devId);
          err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_SENSOR, DEVICE_SENSOR_GET_COUNT, req);
          if (Error::notOK(err)) return err;
          pli = 3;
          uint8_t numSensors;
          pli = Ds485Comm::payload_get8(resp, pli, numSensors);
          OLOG(LOG_NOTICE, "device #%d: number of sensors = %d", j, numSensors);
          for (int si=0; si<numSensors; si++) {
            // - sensor info
            req.clear();
            Ds485Comm::payload_append16(req, devId);
            Ds485Comm::payload_append8(req, si);
            err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_SENSOR, DEVICE_SENSOR_GET_BY_INDEX, req);
            if (Error::notOK(err)) return err;
            pli = 3;
            uint8_t sensorType;
            pli = Ds485Comm::payload_get8(resp, pli, sensorType);
            uint32_t sensorPollinterval;
            pli = Ds485Comm::payload_get32(resp, pli, sensorPollinterval);
            uint8_t sensorZone;
            pli = Ds485Comm::payload_get8(resp, pli, sensorZone);
            uint8_t sensorPushConvert;
            pli = Ds485Comm::payload_get8(resp, pli, sensorPushConvert);
            OLOG(LOG_NOTICE,
              "device #%d: sensor #%d: type=%d, pollinterval=%d, globalZone=%d, pushConvert=%d",
              j, si, sensorType, sensorPollinterval, sensorZone, sensorPushConvert
            );
            VdcSensorType st = sensorType_none;
            VdcUsageHint su = usage_undefined;
            bool internalSensor = false;
            switch (sensorType) {
              // internal power sensors
              case 3: st = sensorType_power; internalSensor = true; break; // zone power
              case 4: st = sensorType_power; internalSensor = true; break; // output power
              case 5: st = sensorType_current; internalSensor = true; break; // output current
              case 6: st = sensorType_energy; internalSensor = true; break; // energy counter
              case 61: st = sensorType_temperature; internalSensor = true; break; // chip temperature
              case 64: st = sensorType_current; internalSensor = true; break; // output current
              case 65: st = sensorType_power; internalSensor = true; break; // output power
              // user facing sensors
              case 9: st = sensorType_temperature; usage = usage_room; break;
              case 10: st = sensorType_temperature; usage = usage_outdoors; break;
              case 11: st = sensorType_illumination; usage = usage_room; break;
              case 12: st = sensorType_illumination; usage = usage_outdoors; break;
              case 13: st = sensorType_humidity; usage = usage_room; break;
              case 14: st = sensorType_humidity; usage = usage_outdoors; break;
              case 15: st = sensorType_air_pressure; usage = usage_outdoors; break;
              case 16: break; // m3/sec gas
              case 17: break; // m3/sec liquid, maybe use sensorType_water_flowrate
              case 18: st = sensorType_wind_speed; usage = usage_outdoors; break;
              case 19: st = sensorType_wind_direction; usage = usage_outdoors; break;
              case 20: st = sensorType_precipitation; usage = usage_outdoors; break;
              case 21: st = sensorType_gas_CO2; usage = usage_outdoors; break;
              case 22: break; // Messumformer
              case 23: break; // Messumformer
              case 24: break; // Sound events
              case 66: break; // UV-A mw/cm2
              case 67: break; // UV-B mw/cm2
              case 68: break; // Infrared mw/cm2
              default: internalSensor = true; // not really known
            }
            if (!internalSensor) {
              SensorBehaviourPtr sb = new SensorBehaviour(*dev, ""); // automatic ID
              sb->setHardwareSensorConfig(st, usage, 0, 4096, 1, sensorPollinterval*Second, sensorPollinterval*Second*3);
              dev->addBehaviour(sb);
            }
          } // sensors
          // save device in list
          mDs485Devices[fullDevId(dev->mDsmDsUid, dev->mDevId)] = dev;
        } // device
      } // zone
    } // not me
  } // dsms

  return ErrorPtr();
}


#endif // ENABLE_DS485DEVICES

