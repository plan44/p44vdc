//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2015-2026 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "mattervdc.hpp"

#if ENABLE_MATTER

using namespace p44;


// MARK: - MatterDevice


MatterDevice::MatterDevice(Vdc *aVdcP, ExternalDeviceConnectorPtr aDeviceConnector, string aTag) :
  inherited(aVdcP, aDeviceConnector, aTag, false)
{
  mTypeIdentifier = "matter";
  mModelNameString = "matter device";
  mIconBaseName = "matter";
}


MatterDevice::~MatterDevice()
{
}


MatterVdc &MatterDevice::getMatterVdc()
{
  return *(static_cast<MatterVdc *>(mVdcP));
}



// MARK: - matter device container


MatterVdc::MatterVdc(int aInstanceNumber, const string &aSocketPathOrPort, bool aNonLocal, VdcHost *aVdcHostP, int aTag) :
  ExternalVdc(aInstanceNumber, aSocketPathOrPort, aNonLocal, aVdcHostP, aTag)
{
  // set default icon base name
  mIconBaseName = "vdc_matter";
}


void MatterVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // Note: in case we'd do stuff ourselves later, don't forget we need to do load() and default hidewhenempty handling
  inherited::initialize(aCompletedCB, aFactoryReset);
}


SocketCommPtr MatterVdc::deviceApiConnectionHandler(SocketCommPtr aServerSocketCommP)
{
  if (mMatterConnector) {
    OLOG(LOG_ERR, "server connected while connection already established -> shutdown older connection");
    mMatterConnector->shutdown();
    mMatterConnector.reset();
  }
  JsonCommPtr conn = JsonCommPtr(new JsonComm(MainLoop::currentMainLoop()));
  // new connection means new device connector (which will add devices to container once it has received proper init message(s))
  mMatterConnector = ExternalDeviceConnectorPtr(new ExternalDeviceConnector(*this, conn));
  return conn;
}


void MatterVdc::connectionClosed(ExternalDeviceConnector& aConnector)
{
  // remove reference when it is our current connector
  if (&aConnector==mMatterConnector) {
    OLOG(LOG_ERR, "matter connection closed");
    mMatterConnector.reset();
  }
  inherited::connectionClosed(aConnector);
}



const char *MatterVdc::vdcClassIdentifier() const
{
  return "Matter_Device_Container";
}



void MatterVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  inherited::scanForDevices(aCompletedCB, aRescanFlags);
}


ErrorPtr MatterVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="pair") {
    // matter specific pairing
    if (!mMatterConnector) return TextError::err("no matter controller connected, cannot pair");
    string payload;
    respErr = checkStringParam(aParams, "payload", payload);
    if (Error::notOK(respErr)) return respErr;
    JsonObjectPtr pmsg = JsonObject::newObj();
    pmsg->add("message", pmsg->newString("pair"));
    pmsg->add("payload", pmsg->newString(payload)); // QR code or manual setup code
    mMatterConnector->sendDeviceApiJsonMessage(pmsg);
    // TODO: expect and process "paired" message
    respErr = Error::ok();
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}



#endif // ENABLE_MATTER
