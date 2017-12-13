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

#ifndef __p44vdc__homeconnectvdc__
#define __p44vdc__homeconnectvdc__

#include "p44vdc_common.hpp"

#if ENABLE_HOMECONNECT

#include "vdc.hpp"
#include "device.hpp"

#include "homeconnectcomm.hpp"

using namespace std;

namespace p44 {


  class HomeConnectVdc;
  class HomeConnectDevice;

  /// persistence for home connect device container
  class HomeConnectPersistence : public SQLite3Persistence
  {
    typedef SQLite3Persistence inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };


  typedef boost::intrusive_ptr<HomeConnectVdc> HomeConnectVdcPtr;
  class HomeConnectVdc : public Vdc
  {
    typedef Vdc inherited;
    friend class HomeConnectDevice;

    StatusCB collectedHandler;

    HomeConnectPersistence db;

  public:

    HomeConnectVdc(int aInstanceNumber, bool aDeveloperApi, VdcHost *aVdcHostP, int aTag);

    HomeConnectComm homeConnectComm;

		void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    /// get supported rescan modes for this vDC
    /// @return a combination of rescanmode_xxx bits
    virtual int getRescanModes() const P44_OVERRIDE;

    /// collect and add devices to the container
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    /// vdc level methods
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// @return human readable, language independent suffix to explain vdc functionality.
    ///   Will be appended to product name to create modelName() for vdcs
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "HomeConnect"; }

//    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
//    /// - uuid:UUUUUUU = UUID
//    virtual string hardwareGUID() P44_OVERRIDE { return string_format("uuid:%s", bridgeUuid.c_str()); };

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    // property read/write
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

    /// some vdcs can have definitions of parameters, states, and properties changing depending on the device information
    /// @return if true, this vDC should be queried for all actions parameters, states and properties descriptions
    virtual bool dynamicDefinitions() { return true; } P44_OVERRIDE;// by default dynamic definitions are not used

  private:

    void deviceListReceived(StatusCB aCompletedCB, JsonObjectPtr aResult, ErrorPtr aError);

  };

} // namespace p44

#endif // ENABLE_HOMECONNECT
#endif // __p44vdc__homeconnectvdc__
