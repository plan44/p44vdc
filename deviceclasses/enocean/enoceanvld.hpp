//
//  Copyright (c) 2015-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__enoceanvld__
#define __p44vdc__enoceanvld__

#include "p44vdc_common.hpp"

#if ENABLE_ENOCEAN

#include "enoceandevice.hpp"
#include "enoceansensorhandler.hpp"

using namespace std;

namespace p44 {


  class EnoceanVLDDevice : public EnoceanDevice
  {
    typedef EnoceanDevice inherited;

  public:

    /// constructor
    EnoceanVLDDevice(EnoceanVdc *aVdcP);

    /// device type identifier
    /// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const { return "enocean_vld"; };

    /// get table of profile variants
    /// @return NULL or pointer to a list of profile variants
    virtual const ProfileVariantEntry *profileVariantsTable();

    /// factory: (re-)create logical device from address|channel|profile|manufacturer tuple
    /// @param aVdcP the class container
    /// @param aSubDeviceIndex subdevice number (multiple logical EnoceanDevices might exists for the same EnoceanAddress)
    ///   upon exit, this will be incremented by the number of subdevice indices the device occupies in the index space
    ///   (usually 1, but some profiles might reserve extra space, such as up/down buttons)
    /// @param aEEProfile RORG/FUNC/TYPE EEP profile number
    /// @param aEEManufacturer manufacturer number (or manufacturer_unknown)
    /// @param aSendTeachInResponse enable sending teach-in response for this device
    /// @return returns NULL if no device can be created for the given aSubDeviceIndex, new device otherwise
    static EnoceanDevicePtr newDevice(
      EnoceanVdc *aVdcP,
      EnoceanAddress aAddress,
      EnoceanSubDevice &aSubDeviceIndex,
      EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
      bool aSendTeachInResponse
    );

  };


} // namespace p44

#endif // ENABLE_ENOCEAN
#endif // __p44vdc__enoceanvld__
