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


// MARK: - initialisation

void Ds485Vdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // start
  mDs485Comm.start(aCompletedCB);
}


string Ds485Vdc::hardwareGUID()
{
  // TODO: assume we just return the dSUID
  return mDSUID.getString();
}


void Ds485Vdc::deriveDsUid()
{
  // TODO: for now: use standard static method
  inherited::deriveDsUid();
}


const char *Ds485Vdc::vdcClassIdentifier() const
{
  // The class identifier is only for addressing by specifier
  return "DS485_Device_Container";
}


string Ds485Vdc::webuiURLString()
{
  return inherited::webuiURLString();
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
  // TODO: implement scanning here
  aCompletedCB(ErrorPtr());
}

// MARK: - operation


void Ds485Vdc::deliverToDevicesAudience(DsAddressablesList aAudience, VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams)
{
  for (DsAddressablesList::iterator apos = aAudience.begin(); apos!=aAudience.end(); ++apos) {
    // TODO: implement
  }
}


#endif // ENABLE_PROXYDEVICES

