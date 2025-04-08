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

#ifndef __p44vdc__ds485device__
#define __p44vdc__ds485device__

#include "device.hpp"

#if ENABLE_DS485DEVICES

#include "jsoncomm.hpp"

using namespace std;

namespace p44 {

  class Ds485Vdc;
  class Ds485Device;

  class Ds485Device final : public Device
  {
    typedef Device inherited;
    friend class Ds485Vdc;

  public:

    Ds485Device(Ds485Vdc *aVdcP, JsonObjectPtr aDeviceJSON);

    virtual ~Ds485Device();

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE;

    Ds485Vdc &getDs485Vdc();

    /// check if device can be disconnected by software (i.e. Web-UI)
    /// @return true if device might be disconnectable (deletable) by the user via software (i.e. web UI)
    /// @note devices returning true here might still refuse disconnection on a case by case basis when
    ///   operational state does not allow disconnection.
    /// @note devices returning false here might still be disconnectable using disconnect() triggered
    ///   by vDC API "remove" method.
    virtual bool isSoftwareDisconnectable() P44_OVERRIDE { return false; };

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;

    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// @}


    /// overridden to handle or proxy method calls, depending on what it is
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// overridden to handle (i.e. forward) notifications TO the device
    virtual void handleNotification(const string &aNotification, ApiValuePtr aParams, StatusCB aExaminedCB) P44_OVERRIDE;

    /// initializes the physical device for being used
    /// @param aFactoryReset if set, the device will be inititalized as thoroughly as possible (factory reset, default settings etc.)
    /// @note this is called before interaction with dS system starts
    /// @note implementation should call inherited when complete, so superclasses could chain further activity
    virtual void initializeDevice(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<Ds485Device> Ds485DevicePtr;

} // namespace p44

#endif // ENABLE_DS485DEVICES
#endif // __p44vdc__ds485device__
