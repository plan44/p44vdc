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

#include "ds485device.hpp"

#if ENABLE_DS485DEVICES

#include "ds485vdc.hpp"

#include "outputbehaviour.hpp"
#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "colorlightbehaviour.hpp"
#include "shadowbehaviour.hpp"

using namespace p44;


Ds485Device::Ds485Device(Ds485Vdc *aVdcP, DsUid& aDsmDsUid, uint16_t aDevId) :
  inherited((Vdc *)aVdcP),
  mDs485Vdc(*aVdcP),
  mDsmDsUid(aDsmDsUid),
  mDevId(aDevId),
  mIsPresent(false),
  mNumOPC(0),
  mUpdatingCache(false)
{
}


Ds485Device::~Ds485Device()
{
}


bool Ds485Device::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}


Ds485Vdc &Ds485Device::getDs485Vdc()
{
  return *(static_cast<Ds485Vdc *>(mVdcP));
}


string Ds485Device::deviceTypeIdentifier() const
{
  return "ds485";
}


string Ds485Device::modelName()
{
  return "dS terminal block"; // intentionally, old way to write dS
}


string Ds485Device::hardwareGUID()
{
  return "dsid:"+mDSUID.getDSIdString();
}


string Ds485Device::webuiURLString()
{
  return getVdc().webuiURLString();
}


string Ds485Device::vendorName()
{
  return "digitalSTROM"; // intentionally, old way to write dS
}


string Ds485Device::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- dSM: %s, devId=0x%04x, OPC=%d", mDsmDsUid.getString().c_str(), mDevId, mNumOPC);
  return s;
}



void Ds485Device::addedAndInitialized()
{
  updatePresenceState(mIsPresent);
}


// MARK: - message processing

void Ds485Device::handleDeviceUpstreamMessage(bool aIsSensor, uint8_t aKeyNo, DsClickType aClickType)
{
  if (aIsSensor) {
    // TODO: sensor
  }
  else {
    // button
    switch (aClickType) {
      case ct_local_off:
      case ct_local_on: {
        // device has been operated locally
        OutputBehaviourPtr o = getOutput();
        if (o) {
          OLOG(LOG_NOTICE, "dS device output locally switched, update output state");
          ChannelBehaviourPtr ch = o->getChannelByType(channeltype_default);
          if (ch) ch->syncChannelValueBool(aClickType==ct_local_on);
          o->reportOutputState();
        }
      }
      default: {
        // forward to button, if any
        ButtonBehaviourPtr b = getButton(0);
        if (b) {
          OLOG(LOG_NOTICE, "dS device button click received");
          b->injectClick(aClickType);
        }
        break;
      }
    }
  }
}


void Ds485Device::traceChannelChange(DsChannelType aChannelType, uint8_t a8BitChannelValue)
{
  OutputBehaviourPtr o = getOutput();
  if (o) {
    OLOG(LOG_INFO, "channel type=%d got updated value 0x%02x / %d", aChannelType, a8BitChannelValue, a8BitChannelValue);
    ChannelBehaviourPtr ch = o->getChannelByType(aChannelType);
    if (ch) {
      ch->syncChannelValue((double)a8BitChannelValue*100/255);
      o->reportOutputState();
    }
  }
}


#define SCENE_APPLY_RESULT_SAMPLE_DELAY (0.5*Second)

void Ds485Device::traceSceneCall(SceneNo aSceneNo)
{
  // only after a while, read back current value
  // TODO: later, we can optimize and omit this, when we know we have the correct scene value already
  mTracingTimer.executeOnce(boost::bind(&Ds485Device::startTracingFor, this, aSceneNo), SCENE_APPLY_RESULT_SAMPLE_DELAY);
}


void Ds485Device::startTracingFor(SceneNo aSceneNo)
{
  // trigger requesting
  string payload;
  Ds485Comm::payload_append8(payload, 64); // bank RAM
  Ds485Comm::payload_append8(payload, 0); // offset outputvalue
  issueDeviceRequest(DEVICE_CONFIG, DEVICE_CONFIG_GET, payload);
  // - traceChannelChange will get the actual scene value

  // TODO: -> update the local scene value
  // - need a pending scene value update flag for that!
  // - also think of OPCs and shadow

  // TODO: refresh scene values first
  // dSS gets output value this way by DEVICE_CONFIG / DEVICE_CONFIG_GET / EVENT_DEVICE_CONFIG
  //                                                                                              DevId Bk Of
  // 302ED89F43F0000000002BA000011C8800 -> 302ED89F43F0000000000E400000E9D700, t=0x10: [06] 53 01 03 ED 40 00
  //                                                                                                 DevId Bk Of Val
  // 302ED89F43F0000000000E400000E9D700 -> FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF, t=0xff: [08] 74 00 00 03 ED 40 00 FF
  // 302ED89F43F0000000000E400000E9D700 -> FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF, t=0xff: [08] 74 00 00 03 ED 40 01 00


//  mUpdatingCache = true;
//  callScene(aSceneNo, true);
//  mUpdatingCache = false;
}


// MARK: - output

void Ds485Device::identifyToUser(MLMicroSeconds aDuration)
{
  issueDeviceRequest(DEVICE_ACTION_REQUEST, DEVICE_ACTION_REQUEST_ACTION_BLINK);
}


bool Ds485Device::prepareSceneApply(DsScenePtr aScene)
{
  // prevent applying when just updating cache
  if (mUpdatingCache) {
    OLOG(LOG_INFO, "NOT applying scene values - just updating cache");
  }
  return !mUpdatingCache;
}


void Ds485Device::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  LightBehaviourPtr l = getOutput<LightBehaviour>();
  ColorLightBehaviourPtr cl = getOutput<ColorLightBehaviour>();
  ShadowBehaviourPtr sb = getOutput<ShadowBehaviour>();
  MLMicroSeconds transitionTime = 0;
  if (needsToApplyChannels(&transitionTime)) {
    if (cl) {
      // TODO: handle color
    }
    else if (l && l->brightnessNeedsApplying()) {
      string payload;
      Ds485Comm::payload_append8(payload, l->brightnessForHardware()*255/100);
      issueDeviceRequest(DEVICE_ACTION_REQUEST, DEVICE_ACTION_REQUEST_ACTION_SET_OUTVAL, payload);
      l->brightnessApplied();
    }
    else if (sb) {
      // TODO: handle shadow
    }
    else {
      // simple unspecific output
      OutputBehaviourPtr o = getOutput();
      ChannelBehaviourPtr ch = o->getChannelByType(channeltype_default);
      string payload;
      Ds485Comm::payload_append8(payload, ch->getChannelValue()*255/100);
      issueDeviceRequest(DEVICE_ACTION_REQUEST, DEVICE_ACTION_REQUEST_ACTION_SET_OUTVAL, payload);
      ch->channelValueApplied();
    }
  }
  // confirm done
  if (aDoneCB) aDoneCB();
}




// MARK: - local method/notification handling

ErrorPtr Ds485Device::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  // TODO: no speical handling yet
  return inherited::handleMethod(aRequest, aMethod, aParams);
}


void Ds485Device::handleNotification(const string &aNotification, ApiValuePtr aParams, StatusCB aExaminedCB)
{
  // TODO: no speical handling yet
  inherited::handleNotification(aNotification, aParams, aExaminedCB);
}


void Ds485Device::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // TODO: no speical handling yet
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


// MARK: - ds485 helpers


ErrorPtr Ds485Device::issueDeviceRequest(uint8_t aCommand, uint8_t aModifier, const string& aMorePayload)
{
  string payload;
  Ds485Comm::payload_append16(payload, mDevId);
  payload.append(aMorePayload);
  return issueDsmRequest(aCommand, aModifier, payload);
}


ErrorPtr Ds485Device::issueDsmRequest(uint8_t aCommand, uint8_t aModifier, const string& aPayload)
{
  return mDs485Vdc.mDs485Comm.issueRequest(mDsmDsUid, aCommand, aModifier, aPayload);
}


void Ds485Device::executeDsmQuery(QueryCB aQueryCB, MLMicroSeconds aTimeout, uint8_t aCommand, uint8_t aModifier, const string& aPayload)
{
  mDs485Vdc.mDs485Comm.executeQuery(aQueryCB, aTimeout, mDsmDsUid, aCommand, aModifier, aPayload);
}



#endif // ENABLE_DS485DEVICES
