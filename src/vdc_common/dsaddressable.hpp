//
//  Copyright (c) 2013-2014 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of vdcd.
//
//  vdcd is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  vdcd is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with vdcd. If not, see <http://www.gnu.org/licenses/>.
//

#ifndef __vdcd__dsaddressable__
#define __vdcd__dsaddressable__

#include "dsuid.hpp"
#include "propertycontainer.hpp"

#include "vdcapi.hpp"

using namespace std;

namespace p44 {

  #define VDC_API_DOMAIN 0x0000
  #define VDC_CFG_DOMAIN 0x1000

  class DeviceContainer;

  /// base class representing a entity which is addressable with a dSUID
  /// dS devices are most obvious addressables, but the vDC itself is also addressable and uses this base class
  class DsAddressable : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    friend class DeviceContainer;

    /// the user-assignable name
    string name;

    #if LEGACY_DSID_SUPPORT
    /// the legacy (classic) dsid, derived from the dSUID on demand
    DsUid classidDsid;
    #endif

    /// announcement status
    MLMicroSeconds announced; ///< set when last announced to the vdSM
    MLMicroSeconds announcing; ///< set when announcement has been started (but not yet confirmed)

  protected:
    DeviceContainer *deviceContainerP;

    /// the actual (modern) dSUID
    DsUid dSUID;

  public:
    DsAddressable(DeviceContainer *aDeviceContainerP);
    virtual ~DsAddressable();

    /// the dSUID exposed in the VDC API (might be derived, classic one in --modernids=0 mode)
    const DsUid &getApiDsUid();

    /// the real (always modern, 34 hex) dSUID
    const DsUid &getDsUid() { return dSUID; };

    /// get reference to device container
    DeviceContainer &getDeviceContainer() { return *deviceContainerP; };

    /// get user assigned name of the addressable
    /// @return name string
    string getName() { return name; };

    /// set user assignable name
    /// @param new name of the addressable entity
    /// @note might prevent truncating names (just shortening an otherwise unchanged name)
    /// @note subclasses might propagate the name into actual device hardware (e.g. hue)
    virtual void setName(const string &aName);

    /// initialize user assignable name with a default name or a name obtained from hardware
    /// @note use setName to change a name from the API or UI, as initializeName() does not
    ///   propagate to hardware
    void initializeName(const string &aName);

    /// @name vDC API
    /// @{

    /// convenience method to check for existence of a parameter and return appropriate error if not
    static ErrorPtr checkParam(ApiValuePtr aParams, const char *aParamName, ApiValuePtr &aParam);

    /// convenience method to check if a string value exists and if yes, return its value in one call
    static ErrorPtr checkStringParam(ApiValuePtr aParams, const char *aParamName, string &aParamValue);

    /// convenience method to check if a dSUID value exists and if it does, return its value in one call
    static ErrorPtr checkDsuidParam(ApiValuePtr aParams, const char *aParamName, DsUid &aDsUid);

    /// called by DeviceContainer to handle methods directed to a dSUID
    /// @param aRequest this is the request to respond to
    /// @param aMethod the method
    /// @param aParams the parameters object
    /// @note the parameters object always contains the dSUID parameter which has been
    ///   used already to route the method call to this DsAddressable.
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams);

    /// called by DeviceContainer to handle notifications directed to a dSUID
    /// @param aMethod the notification
    /// @param aParams the parameters object
    /// @note the parameters object always contains the dSUID parameter which has been
    ///   used already to route the notification to this DsAddressable.
    virtual void handleNotification(const string &aMethod, ApiValuePtr aParams);

    /// send a DsAddressable method or notification to vdSM
    /// @param aMethod the method or notification
    /// @param aParams the parameters object, or NULL if none
    /// @param aResponseHandler handler for response. If not set, request is sent as notification
    /// @return true if message could be sent, false otherwise (e.g. no vdSM connection)
    /// @note the dSUID will be automatically added to aParams (generating a params object if none was passed)
    bool sendRequest(const char *aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler = VdcApiResponseCB());

    /// push property value
    /// @param aQuery description of what should be pushed (same syntax as in getProperty API)
    /// @param aDomain the domain for which to access properties (different APIs might have different properties for the same PropertyContainer)
    /// @return true if push could be sent, false otherwise (e.g. no vdSM connection)
    bool pushProperty(ApiValuePtr aQuery, int aDomain);

    /// @}


    /// @name interaction with subclasses, actually representing physical I/O
    /// @{

    typedef boost::function<void (bool aPresent)> PresenceCB;

    /// check presence of this addressable
    /// @param aPresenceResultHandler will be called to report presence status
    virtual void checkPresence(PresenceCB aPresenceResultHandler);

    /// @}


    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() { return "DsAddressable"; }

    /// @return the entity type (one of dSD|vdSD|vDC|dSM|vdSM|dSS|*)
    virtual const char *entityType() { return "*"; }

    /// @return hardware version string or NULL if none
    virtual string hardwareVersion() { return ""; }

    /// @return number of vdSDs (virtual devices represented by a separate dSUID)
    ///   that are contained in the same hardware device. -1 means "not available or ambiguous"
    virtual ssize_t numDevicesInHW() { return -1; }

    /// @return index of this vdSDs (0..numDevicesInHW()-1) among all vdSDs in the same hardware device
    ///   -1 means undefined
    virtual ssize_t deviceIndexInHW() { return -1; }

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    /// @note when grouping vdSDs which belong to the same hardware device using numDevicesInHW() and deviceIndexInHW()
    ///   hardwareGUID() must return the same unique ID for the containing hardware device for all contained dSDs
    virtual string hardwareGUID() { return ""; }

    /// @return OEM GUID in URN format to identify hardware as uniquely as possible
    virtual string oemGUID() { return ""; }

    /// @}



    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc();

    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description();

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

  private:

    void presenceResultHandler(bool aIsPresent);

  };
  typedef boost::intrusive_ptr<DsAddressable> DsAddressablePtr;


} // namespace p44


#endif /* defined(__vdcd__dsaddressable__) */
