//
//  Copyright (c) 2017 digitalSTROM.org, Zurich, Switzerland
//
//  Author: Krystian Heberlein <krystian.heberlein@digitalstrom.com>
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

#ifndef __p44vdc__netatmohealthycoachdevice__
#define __p44vdc__netatmohealthycoachdevice__


#if ENABLE_NETATMO

#include "netatmodevice.hpp"

using namespace std;

namespace p44 {



  // MARK: ===== NetatmoHealthyCoachDevice,


  class NetatmoHealthyCoachDevice;

  using NetatmoHealthyCoachDevicePtr = boost::intrusive_ptr<NetatmoHealthyCoachDevice>;

  class NetatmoHealthyCoachDevice : public NetatmoDevice
  {
    using inherited = NetatmoDevice;

    protected:

    /*device properties*/

    /*device sensors*/
    SensorBehaviourPtr sensorCO2;
    SensorBehaviourPtr sensorNoise;

    /*device states*/
    BinaryInputBehaviourPtr statusBattery;
    DeviceStatePtr statusHealthIndex;
    enum class StatusHealthIndex;


    public:
    NetatmoHealthyCoachDevice(NetatmoVdc *aVdcP, INetatmoComm& aINetatmoComm, JsonObjectPtr aDeviceData);
    virtual ~NetatmoHealthyCoachDevice() {}


    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// @return OEM model GUID in URN format to identify the OEM product MODEL hardware as uniquely as possible
    virtual string oemModelGUID() P44_OVERRIDE;

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// @}


    protected:
    /// Configure device before initialization
    virtual void configureDevice() P44_OVERRIDE;
    virtual void updateData(JsonObjectPtr aJson) P44_OVERRIDE;

  };
  

} // namespace p44


#endif // ENABLE_NETATMO
#endif // __p44vdc__netatmohealthycoachdevice__
