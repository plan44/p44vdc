//
//  Copyright (c) 2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#pragma once

#include "singledevice.hpp"

#if ENABLE_HOMECONNECT

#include "jsonobject.hpp"
#include <boost/optional.hpp>

using namespace std;

namespace p44 {

  class HomeConnectDevice;

  class HomeConnectAction : public DeviceAction
  {
    typedef DeviceAction inherited;

  protected:

    string apiCommandTemplate;
    void performCall(ApiValuePtr aParams, StatusCB aCompletedCB, const string& aCommandTemplate);

  public:

    /// create the action
    /// @param aSingleDevice the single device this action belongs to
    /// @param aName the name of the action.
    /// @param aDescription a description string for the action.
    /// @param aApiCommandTemplate the API command template to use for this action
    HomeConnectAction(SingleDevice &aSingleDevice, const string& aName, const string& aDescription, const string& aApiCommandTemplate);

    HomeConnectDevice &getHomeConnectDevice();

    /// implementation of action
    virtual void performCall(ApiValuePtr aParams, StatusCB aCompletedCB) P44_OVERRIDE;

  private:

    ErrorPtr valueLookup(ApiValuePtr aParams, const string aName, string &aValue);
    void apiCommandSent(StatusCB aCompletedCB, JsonObjectPtr aResult, ErrorPtr aError);
    
  };
  typedef boost::intrusive_ptr<HomeConnectAction> HomeConnectActionPtr;
  
  class HomeConnectPowerOnAction : public HomeConnectAction
  {
    typedef HomeConnectAction inherited;
    DeviceState& powerState;
    DeviceState& operationMode;
    string ifPowerOffCommand;
    string ifPowerOnCommand;

    static const MLMicroSeconds RESCHEDULE_INTERVAL = 5 * Second;
    static const unsigned int RETRY_COUNT = 10;

    void powerOnDevice(ApiValuePtr aParams, StatusCB aCompletedCB);
    void devicePoweredOn(ApiValuePtr aParams, StatusCB aCompletedCB, ErrorPtr aError);
    void runActionWhenReady(ApiValuePtr aParams, StatusCB aCompletedCB, unsigned int aRetriesLeft);

  public:

    HomeConnectPowerOnAction(SingleDevice &aSingleDevice,
                             const string& aName,
                             const string& aDescription,
                             const string& aIfPowerOnCommand,
                             const string& aIfPowerOffCommand,
                             DeviceState& aPowerState,
                             DeviceState& aOperationMode);

    virtual void performCall(ApiValuePtr aParams, StatusCB aCompletedCB) P44_OVERRIDE;
  };
  
} // namespace p44


#endif // ENABLE_HOMECONNECT
