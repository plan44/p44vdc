//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__dsaddressable__
#define __p44vdc__dsaddressable__

#include "dsuid.hpp"
#include "propertycontainer.hpp"
#include "dsdefs.h"

#include "vdcapi.hpp"

// per-addressable logging macros
#define ALOG(lvl, ...) { if (LOGENABLED(lvl)) { logAddressable(lvl, ##__VA_ARGS__); } }
#define SALOG(addressable,lvl, ...) { if (LOGENABLED(lvl)) { addressable.logAddressable(lvl, ##__VA_ARGS__); } }
#if FOCUSLOGGING
#define AFOCUSLOG(...) { ALOG(FOCUSLOGLEVEL, ##__VA_ARGS__); }
#else
#define AFOCUSLOG(...)
#endif

using namespace std;

namespace p44 {

  #define VDC_API_DOMAIN 0x0000
  #define VDC_CFG_DOMAIN 0x1000

  class VdcHost;

  /// base class representing a entity which is addressable with a dSUID
  /// dS devices are most obvious addressables, but vDCs and the vDC host itself is also addressable and uses this base class
  class DsAddressable : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    friend class VdcHost;

    /// the user-assignable name
    string name;

    /// announcement status
    MLMicroSeconds announced; ///< set when last announced to the vdSM
    MLMicroSeconds announcing; ///< set when announcement has been started (but not yet confirmed)

    bool present; ///< current presence status
    MLMicroSeconds lastPresenceUpdate; ///< when presence state was last updated

  protected:
    VdcHost *vdcHostP;

    /// the actual (modern) dSUID
    DsUid dSUID;

  public:
    DsAddressable(VdcHost *aVdcHostP);
    virtual ~DsAddressable();

    /// check if this is a public dS addressable (usually: device or vdc) - which should be registered with vdSM
    /// @return true if addressable is public
    virtual bool isPublicDS() { return true; }; // base class assumes that all devices are public

    /// called when vdsm acknowledges announcement of this addressable. Can be used in subclasses to
    /// re-trigger pushing sensor values etc.
    virtual void announcementAcknowledged() { /* NOP in base class */ }

    /// check if this instance (device or vdc) has been announced
    /// @return true if device has been announced
    bool isAnnounced() { return announced != Never; }

    /// the real (always modern, 34 hex) dSUID
    const DsUid &getDsUid() { return dSUID; };

    /// get reference to device container
    VdcHost &getVdcHost() const { return *vdcHostP; };

    /// get user assigned name of the addressable
    /// @return name string
    /// @note this is returned as a reference to make sure it represents stable data (needed e.g. for bind to SQL)
    const string &getAssignedName() { return name; };

    /// @return name string
    /// @note derived classes might provide a default name if no actual name is set
    virtual string getName() { return getAssignedName(); };

    /// set user assignable name
    /// @param aName name of the addressable entity
    /// @note might prevent truncating names (just shortening an otherwise unchanged name)
    /// @note subclasses might propagate the name into actual device hardware (e.g. hue)
    virtual void setName(const string &aName);

    /// initialize user assignable name with a default name or a name obtained from hardware
    /// @note use setName to change a name from the API or UI, as initializeName() does not
    ///   propagate to hardware
    void initializeName(const string &aName);

    /// report that this addressable has vanished (temporarily or permanently disconnected)
    /// @note only addressables that have been announced on the vDC API will send a vanish message
    void reportVanished();

    /// @name vDC API
    /// @{

    /// convenience method to check for existence of a parameter and return appropriate error if not
    /// @param aParams API value object containing parameters
    /// @param aParamName name of the parameter within aParams to check for
    /// @param aParam will be set to the API value of the parameter
    /// @return returns error if not parameter named aParamName exists in aParams
    static ErrorPtr checkParam(ApiValuePtr aParams, const char *aParamName, ApiValuePtr &aParam);

    /// convenience method to check if a string value exists and if yes, return its value in one call
    /// @param aParams API value object containing parameters
    /// @param aParamName name of the parameter within aParams to check for
    /// @param aString will be set to the string value if aParamName is present, otherwise it will be left untouched
    /// @return returns error if not parameter named aParamName exists in aParams
    static ErrorPtr checkStringParam(ApiValuePtr aParams, const char *aParamName, string &aString);

    /// convenience method to check if a boolean value exists and if yes, return its value in one call
    /// @param aParams API value object containing parameters
    /// @param aParamName name of the parameter within aParams to check for
    /// @param aBool will be set to the boolean value if aParamName is present, otherwise it will be left untouched
    /// @return returns error if not parameter named aParamName exists in aParams
    static ErrorPtr checkBoolParam(ApiValuePtr aParams, const char *aParamName, bool &aBool);

    /// convenience method to check if a dSUID value exists and if it does, return its value in one call
    /// @param aParams API value object containing parameters
    /// @param aParamName name of the parameter within aParams to check for
    /// @param aDsUid will be set to the dSUID value if aParamName is present, otherwise it will be left untouched
    /// @return returns error if not parameter named aParamName exists in aParams
    static ErrorPtr checkDsuidParam(ApiValuePtr aParams, const char *aParamName, DsUid &aDsUid);

    /// called by VdcHost to handle methods directed to a dSUID
    /// @param aRequest this is the request to respond to
    /// @param aMethod the method
    /// @param aParams the parameters object
    /// @return NULL if method implementation has or will take care of sending a reply (but make sure it
    ///   actually does, otherwise API clients will hang or run into timeouts)
    ///   Returning any Error object, even if ErrorOK, will cause a generic response to be returned.
    /// @note the parameters object always contains the dSUID parameter which has been
    ///   used already to route the method call to this DsAddressable.
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams);

    /// utility function that can be passed as callback for simple OK/Error type method completion handlers
    /// @param aRequest the request which invoked the method
    /// @param aError method return status - if NULL or OK, a empty response will be returned to the caller signalling OK,
    ///   otherwise, a error response will be returned
    void methodCompleted(VdcApiRequestPtr aRequest, ErrorPtr aError);

    /// called by VdcHost to handle notifications directed to a dSUID
    /// @param aApiConnection this is the API connection from which the notification originates
    /// @param aNotification the notification
    /// @param aParams the parameters object
    /// @note the parameters object always contains the dSUID parameter which has been
    ///   used already to route the notification to this DsAddressable.
    virtual void handleNotification(VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams);

    /// send a DsAddressable method or notification to vdSM
    /// @param aMethod the method or notification
    /// @param aParams the parameters object, or NULL if none
    /// @param aResponseHandler handler for response. If not set, request is sent as notification
    /// @return true if message could be sent, false otherwise (e.g. no vdSM connection)
    /// @note the dSUID will be automatically added to aParams (generating a params object if none was passed)
    bool sendRequest(const char *aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler = VdcApiResponseCB());

    /// push notification (changed property value and/or events)
    /// @param aPropertyQuery description of what property change should be pushed (same syntax as in getProperty API), can be NULL
    /// @param aEvents list of events to be pushed, can be NULL
    /// @param aDomain the domain for which to access properties (different APIs might have different properties for the same PropertyContainer)
    /// @param aApiVersion the API version relevant for this push notification
    /// @param aDeletedProperty if set, the aPropertyQuery describes a property that has been deleted
    /// @return true if push could be sent, false otherwise (e.g. no vdSM connection, or device not yet announced)
    bool pushNotification(ApiValuePtr aPropertyQuery, ApiValuePtr aEvents, int aDomain, int aApiVersion, bool aDeletedProperty = false);

    /// @}


    /// @name interaction with subclasses, actually representing physical I/O
    /// @{

    /// Callback to deliver status from checkPresence()
    /// @param aPresent the new presence state
    typedef boost::function<void (bool aPresent)> PresenceCB;

    /// trigger re-checking presence state of this addressable, possibly involving hardware access
    /// @param aPresenceResultHandler will be called to report presence status
    virtual void checkPresence(PresenceCB aPresenceResultHandler);

    /// explictly update presence state from device level.
    /// @param aPresent the new presence state
    /// @param aPush if set, change in presence state will be pushed
    /// @note This can be called from device level implementation at any time to update the
    ///   presence state when it is detected as part of another operation
    void updatePresenceState(bool aPresent, bool aPush = true);

    /// @}


    /// @name identification of the addressable entity
    /// @{

    /// @return human readable, language independent model name/short description
    virtual string modelName() = 0;

    /// @return human readable version string of the device or vDC model
    /// @note base class implementation returns version string of vdc host by default.
    ///   Derived device classes should return version information for the actual device,
    ///   if available from device hardware.
    ///   Do not derive modelVersion() in vDCs, but override Vdc::vdcModelVersion() instead.
    virtual string modelVersion() const;

    /// @return human readable display Id (such as serial number or similar, device instance identifying string)
    /// @note this is intended as a way to show the user the hardwareGUID in a more readable, possibly beautified manner.
    ///   Base class implementation returns hardwareGUID with the schema identifier removed. If there
    ///   is no hardwareGUID, the dSUID is returned.
    ///   Derived classes might have a more elaborate way to present a user facing device ID
    virtual string displayId();


    /// @return unique ID for the functional model of this entity
    /// @note modelUID must be equal between all devices of the same model/class/kind, where "same" means
    ///   the functionality relevant for the dS system. If different connected hardware devices
    ///   (different hardwareModelGuid) provide EXACTLY the same dS functionality, these devices MAY
    ///   have the same modelUID. Vice versa, two identical hardware devices (two digital inputs for example)
    ///   might have the same hardwareModelGuid, but different modelUID if for example one input is mapped
    ///   as a button, and the other as a binaryInput.
    virtual string modelUID() = 0;

    /// @return the entity type (one of dSD|vdSD|vDC|dSM|vdSM|dSS|*)
    virtual const char *entityType() { return "*"; };

    /// @return hardware version string or NULL if none
    virtual string hardwareVersion() { return ""; };

    /// @return hardware GUID in URN format to identify the hardware INSTANCE as uniquely as possible
    /// @note Already defined schemas for hardwareGUID are
    /// - enoceanaddress:XXXXXXXX = 8 hex digits enOcean device address
    /// - gs1:(01)ggggg = GS1 formatted GTIN
    /// - uuid:UUUUUUU = UUID
    /// - macaddress:MM:MM:MM:MM:MM:MM = MAC Address in hex
    /// - sparkcoreid:ssssss = spark core ID
    virtual string hardwareGUID() { return ""; };

    /// @return model GUID in URN format to identify MODEL of the connected hardware device as uniquely as possible
    /// @note model GUID must be equal between all devices of the same model/class/kind, where "same" should be
    ///   focused to the context of functionality relevant for the dS system, if possible. On the other hand,
    ///   identifiers allowing global lookup (such as GTIN) are preferred if available over less generic
    ///   model identification.
    /// Already defined schemas for modelGUID are
    /// - enoceaneep:RRFFTT = 6 hex digits enOcean EEP
    /// - gs1:(01)ggggg = GS1 formatted GTIN
    /// - uuid:UUUUUUU = UUID
    virtual string hardwareModelGUID() { return ""; };

    /// @return OEM GUID in URN format to identify the OEM product INSTANCE (the product that encloses/uses the technical
    ///   device, such as a particular designer lamp) as uniquely as possible
    /// @note see hardwareGUID for possible schemas
    virtual string oemGUID() { return ""; };

    /// @return OEM model GUID in URN format to identify the OEM product MODEL hardware as uniquely as possible
    /// @note see hardwareModelGUID for possible schemas
    virtual string oemModelGUID() { return ""; };

    /// @return Vendor ID in URN format to identify vendor as uniquely as possible
    /// Already defined schemas for vendorId are
    /// - enoceanvendor:VVV[:nnn] = 3 hex digits enOcean vendor ID, optionally followed by vendor name (if known)
    /// - vendorname:nnnnn = vendor name in plain text
    /// @note default implementation uses vendorName() (if not empty) to create vendorname:xxx URN schema id
    virtual string vendorId();

    /// @return Vendor name for display purposes
    /// @note if not empty, value will be used by vendorId() default implementation to create vendorname:xxx URN schema id
    virtual string vendorName() { return ""; };

    /// @return URL for Web-UI (for access from local LAN)
    virtual string webuiURLString();

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix);

    /// Get extra info (plan44 specific) to describe the addressable in more detail
    /// @return string, single line extra info describing aspects of the device not visible elsewhere
    virtual string getExtraInfo() { return ""; };

    /// Get short text for a "first glance" status of the device
    /// @return string, really short, intended to be shown as a narrow column in a device/vdc list
    virtual string getStatusText() { return ""; };

    /// Get an indication how good/critical the operation state of the device is (such as radio strenght, battery level)
    /// @return 0..100 with 0=out of operation, 100=fully operating, <0 = unknown
    virtual int opStateLevel() { return -1; }; // unknown operating state by default

    /// Get short text to describe the operation state (such as radio RSSI, critical battery level, etc.)
    /// @return string, really short, intended to be shown as a narrow column in a device/vdc list
    virtual string getOpStateText() { return ""; };

    /// @}



    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc();

    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description() = 0;

    /// log a message, prefixed with addressable's identification
    /// @param aErrLevel error level of the message
    /// @param aFmt ... printf style error message
    void logAddressable(int aErrLevel, const char *aFmt, ... ) __printflike(3,4);

  protected:

    /// @name icon loading mechanism
    /// @{

    /// get icon data or name
    /// @param aIconName basic name of the icon to load (no filename extension!)
    /// @param aIcon string to get icon name or data assigned
    /// @param aWithData if set, aIcon will be set to the icon's PNG data, otherwise aIcon will be set to the icon name
    /// @param aResolutionPrefix subfolder within icondir to look for files. Usually "icon16".
    /// @return true if icon data or name could be obtained, false otherwise
    /// @note when icondir (in vdchost) is set, the method will check in this dir if a file with aIconName exists
    ///   in the subdirectory specified by aResolutionPrefix - even if only querying for icon name. This allows for
    ///   implementations of getDeviceIcon to start with the most specific icon name, and falling back to more
    ///   generic icons defined by superclass when specific icons don't exist.
    ///   When icondir is NOT set, asking for icon data will always fail, and asking for name will always succeed
    ///   and return aIconName in aIcon.
    bool getIcon(const char *aIconName, string &aIcon, bool aWithData, const char *aResolutionPrefix);

    /// get icon colored according to aClass
    /// @param aIconName basic name of the icon to load (no color suffix nor filename extension!)
    /// @param aClass the dS class color. The color will be appended as suffix to aIconName to build the
    ///   final icon name. If no specific icon exists for the group, the suffix "_other" will be tried before
    ///   returning false.
    /// @param aWithData if set, aIcon will be set to the icon's PNG data, otherwise aIcon will be set to the icon name
    /// @param aResolutionPrefix subfolder within icondir to look for files. Usually "icon16".
    /// @return true if icon data or name could be obtained, false otherwise
    bool getClassColoredIcon(const char *aIconName, DsClass aClass, string &aIcon, bool aWithData, const char *aResolutionPrefix);

    /// @}


    /// load settings from CSV file
    /// @param aCSVFilepath full file path to a CSV file to read. If file does not exist, the function does nothing. If
    ///   an error occurs loading the file, the error is logged
    /// @param aOnlyExplicitlyOverridden if set, only properties are applied which are explicitly marked with a exclamation mark prefix
    /// @return true if some settings were applied
    bool loadSettingsFromFile(const char *aCSVFilepath, bool aOnlyExplicitlyOverridden);

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual void prepareAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor, StatusCB aPreparedCB);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

  private:

    void propertyAccessed(VdcApiRequestPtr aRequest, ApiValuePtr aResultObject, ErrorPtr aError);
    void pushPropertyReady(ApiValuePtr aEvents, ApiValuePtr aResultObject, ErrorPtr aError);
    void pingResultHandler(bool aIsPresent);
    void presenceSampleHandler(StatusCB aPreparedCB, bool aIsPresent);

  };
  typedef boost::intrusive_ptr<DsAddressable> DsAddressablePtr;


} // namespace p44


#endif /* defined(__p44vdc__dsaddressable__) */
