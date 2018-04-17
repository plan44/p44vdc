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

#ifndef __p44vdc__vdc__
#define __p44vdc__vdc__

#include "p44vdc_common.hpp"

#include "vdchost.hpp"

#include "dsuid.hpp"

using namespace std;

namespace p44 {

  class VdcError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      NoDevice, ///< no device could be identified
      Initialize, ///< initialisation failed
      Collecting, ///< is busy collecting devices already, new collection request irgnored
    } ErrorCodes;
    
    static const char *domain() { return "DeviceClass"; }
    virtual const char *getErrorDomain() const { return VdcError::domain(); };
    VdcError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
  };
	
	
	
  class Device;
  typedef boost::intrusive_ptr<Device> DevicePtr;

  /// callback from identifyDevice()
  /// @param aError error in case setup has failed
  /// @param aIdentifiedDevice will be assigned the identified device, which *might* be another object than
  ///   the one identifyDevice() was called on, in cases where the identify process implemented in a base class placeholder
  ///   device figures out a more specialized subclass that will actually handle the device. In simpler
  ///   cases, aIdentifiedDevice will return *this*.
  /// @note aIdentifiedDevice can't be a DevicePtr but needs to be a plain Device* because boost::bind can't work with
  ///   forward declaration. 
  typedef boost::function<void (ErrorPtr aError, Device *aIdentifiedDevice)> IdentifyDeviceCB;


  class Vdc;
  typedef boost::intrusive_ptr<Vdc> VdcPtr;
  typedef std::vector<DevicePtr> DeviceVector;
  typedef std::list<DevicePtr> DeviceList;

  /// This is the base class for a "class" (usually: type of hardware) of virtual devices.
  /// In dS terminology, this object represents a vDC (virtual device connector).
  class Vdc : public PersistentParams, public DsAddressable
  {
    typedef DsAddressable inherited;
    typedef PersistentParams inheritedParams;

    friend class VdcHost;

    int instanceNumber; ///< the instance number identifying this instance among other instances of this class
    int tag; ///< tag used to in self test failures for showing on LEDs
    MLTicket pairTicket; ///< used for pairing

    /// generic vdc flag word
    int vdcFlags;
    /// default dS zone ID
    DsZoneID defaultZoneID;

    /// periodic rescan, collecting
    MLMicroSeconds rescanInterval; ///< rescan interval, 0 if none
    RescanMode rescanMode; ///< mode to use for periodic rescan
    MLTicket rescanTicket; ///< rescan ticket
    bool collecting; ///< currently collecting

    ErrorPtr vdcErr; ///< global error, set when something prevents the vdc from working at all

  protected:
  
    DeviceVector devices; ///< the devices of this class

  public:

    /// @param aInstanceNumber index which uniquely (and as stable as possible) identifies a particular instance
    ///   of this class container. This is used when generating dsuids for devices that don't have their own
    ///   unique ID, by using a hashOf(DeviceContainer's id, vdcClassIdentifier(), aInstanceNumber)
    /// @param aVdcHostP device container this vDC is contained in
    /// @param aTag numeric tag for this device container (e.g. for blinking self test error messages)
    Vdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);

    /// destructor
    virtual ~Vdc();

    /// add this vDC to vDC host.
    void addVdcToVdcHost();

		/// initialize
		/// @param aCompletedCB will be called when initialisation is complete
		///   callback will return an error if initialisation has failed and the vDC is not functional
		/// @param aFactoryReset if set, also perform factory reset for data persisted for this vDC
    virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset);
		
    /// @name persistence
    /// @{

		/// get the persistent data dir path
		/// @return full path to directory to save persistent data
		const char *getPersistentDataDir();

    /// get the tag
    int getTag() const { return tag; };

    /// get number of devices
    size_t getNumberOfDevices() const { return devices.size(); };

    /// get vDC status or error
    ErrorPtr getVdcStatus() { return vdcErr; }

    /// @}
		
		
    /// @name identification
    /// @{

    /// deviceclass identifier
		/// @return constant identifier for this container class (no spaces, filename-safe)
    virtual const char *vdcClassIdentifier() const = 0;
		
    /// Instance number (to differentiate multiple vDC containers of the same class)
		/// @return instance index number
		int getInstanceNumber() const;

    /// get a sufficiently unique identifier for this class container
    /// @return ID that identifies this container running on a specific hardware
    ///   the ID should not be dependent on the software version
    ///   the ID must differ for each of multiple vDC containers run on the same hardware
    ///   the ID MUST change when same software runs on different hardware
    /// @note Current implementation derives this from the devicecontainer's dSUID,
    ///   the deviceClassIdentitfier and the instance number in the form "class:instanceIndex@devicecontainerDsUid"
    string vdcInstanceIdentifier() const;

    /// some containers (statically defined devices for example) should be invisible for the dS system when they have no
    /// devices.
    /// @return if true, this vDC should not be announced towards the dS system when it has no devices
    virtual bool invisibleWhenEmpty() { return false; }

    /// some vdcs can have definitions of parameters, states, and properties changing depending on the device information
    /// @return if true, this vDC should be queried for all actions parameters, states and properties descriptions
    virtual bool dynamicDefinitions() { return false; } // by default dynamic definitions are not used

    /// get user assigned name of the vDC container, or if there is none, a synthesized default name
    /// @return name string
    virtual string getName() P44_OVERRIDE;

    /// set user assignable name
    /// @param aName name of the addressable entity
    virtual void setName(const string &aName) P44_OVERRIDE;

    /// @}


    /// @name device collection, learning/pairing, self test
    /// @{

    /// get supported rescan modes for this vDC. This indicates (usually to a web-UI) which
    /// of the flags to collectDevices() make sense for this vDC.
    /// @return a combination of rescanmode_xxx bits
    virtual int getRescanModes() const { return rescanmode_none; }; // by default, assume not rescannable

    /// (re)collect devices from this vDCs for normal operation
    /// @param aCompletedCB will be called when device scan for this vDC has been completed
    /// @param aRescanFlags selects mode of rescan:
    /// - if rescanmode_incremental is set, search is only made for additional new devices. Disappeared devices
    ///   might not get detected this way.
    /// - if rescanmode_exhaustive is set, device search is made exhaustive (may include longer lasting procedures to
    ///   recollect lost devices, assign bus addresses etc.). Without this flag set, device search will
    ///   still be complete under normal conditions, but might not find all devices when the subsystem is in a
    ///   state that needs recovery operations (such as resolving addressing conflicts etc). Exhaustive search
    ///   should be used with care, as it *might* cause addressing changes that *might* also cause dSUID changes
    ///   in case of devices which do not have a stable unique ID.
    /// - if rescanmode_clearsettings is set, persistent settings of currently known devices will be deleted before
    ///   re-scanning for devices, which means devices will have default settings after collecting.
    ///   Note that this is mutually exclusive with aIncremental (as incremental scan does not remove any devices,
    ///   and thus cannot remove any settings, either)
    /// @note this public method is protected against getting called again when already collecting, and
    ///   handles re-scheduling periodic recollects.
    ///   Actual vdc-specific implementation is in protected scanForDevices()
    void collectDevices(StatusCB aCompletedCB, RescanMode aRescanFlags);

    /// request regular recollects of devices in this vdc
    /// @note this is for vDC implementations to allow them to define a rescan mode/interval appropriate for the vdc's/device's needs
    /// @param aRecollectInterval how long to wait until re-collecting devices
    /// @param aRescanFlags rescan mode to use for collecting
    void setPeriodicRecollection(MLMicroSeconds aRecollectInterval, RescanMode aRescanFlags);

    /// schedule recollecting
    /// @param aRescanMode rescan mode to use
    /// @param aDelay when to trigger the rescan from now
    /// @note this will cancel the current periodic recollect, but re-schedule it again after this rescan is done
    void scheduleRecollect(RescanMode aRescanMode, MLMicroSeconds aDelay);

    /// @return true if vdc is currently collecting (scanning for) devices
    bool isCollecting() { return collecting; };

    /// handle global events
    /// @param aEvent the event to handle
    virtual void handleGlobalEvent(VdchostEvent aEvent) { /* NOP in base class */ };

    /// set vdc-global error
    /// @param aVdcError if NotOK, vdc cannot collect devices any more (or at all)
    void setVdcError(ErrorPtr aVdcError) { vdcErr = aVdcError; };

    /// perform self test
    /// @param aCompletedCB will be called when self test is done, returning ok or error
    /// @note self will be called *instead* of collectDevices() but might need to do some form of
    ///   collecting devices to perform the test. It might do that by calling collectDevices(), but
    ///   must make sure NOT to modify or generate any persistent data for the class.
    virtual void selfTest(StatusCB aCompletedCB);

    /// Forget all previously collected devices
    /// @param aForget if set, all parameters stored for the device (if any) will be deleted.
    /// @note the device is not disconnected (=unlearned) by this, so it will re-appear when scanned again
    /// @note if vdc API client is connected, it will receive "vanish" messages for all devices
    virtual void removeDevices(bool aForget);

    /// set container learn mode
    /// @param aEnableLearning true to enable learning mode
    /// @param aDisableProximityCheck true to disable proximity check (e.g. minimal RSSI requirement for some radio devices)
    /// @param aOnlyEstablish set this to yes to only learn in, to no to only learn out or to undefined to allow both learn-in and out.
    /// @note learn events (new devices found or devices removed) must be reported by calling reportLearnEvent() on VdcHost.
    virtual void setLearnMode(bool aEnableLearning, bool aDisableProximityCheck, Tristate aOnlyEstablish) { /* NOP in base class */ }

    /// @}


    /// @name services for actual vDC controller implementations
    /// @{

    /// Remove device known no longer connected to the system (for example: explicitly unlearned EnOcean switch)
    /// @param aDevice a device object which has a valid dSUID
    /// @param aForget if set, all parameters stored for the device will be deleted. Note however that
    ///   the device is not disconnected (=unlearned) by this.
    virtual void removeDevice(DevicePtr aDevice, bool aForget = false);

    /// utility to identify and add devices with simple identification (not requiring callback)
    /// @param aNewDevice the device to be identified and added.
    ///   Must support instant identifyDevice() returning true.
    /// @return false if aDevice was not added (due to duplicate dSUID or because it does not support simpleIdentify)
    /// @note if aDevice's dSUID is already known, it will *not* be added again. This facilitates
    ///   implementation of incremental collection of newly appeared devices (scanning entire bus,
    ///   known ones will just be ignored when encountered again)
    bool simpleIdentifyAndAddDevice(DevicePtr aNewDevice);

    /// utility method for implementation of scanForDevices in Vdc subclasses: identify device with retries
    /// @param aNewDevice the device to be identified and added
    /// @param aCompletedCB will be called when device has been added or had error
    /// @param aMaxRetries how many retries (excluding the first try) should be attempted
    /// @param aRetryDelay how long to wait between retries
    void identifyAndAddDevice(DevicePtr aNewDevice, StatusCB aCompletedCB, int aMaxRetries = 0, MLMicroSeconds aRetryDelay = 0);

    /// utility method for implementation of scanForDevices in Vdc subclasses: identify and add a list of devices
    /// @param aToBeAddedDevices a list of devices to be identified and added
    /// @param aCompletedCB will be called when all devices have been added
    /// @param aMaxRetries how many retries (excluding the first try) should be attempted
    /// @param aRetryDelay how long to wait between retries
    /// @param aAddDelay how long to wait between adding devices
    void identifyAndAddDevices(DeviceList aToBeAddedDevices, StatusCB aCompletedCB, int aMaxRetries = 0, MLMicroSeconds aRetryDelay = 0, MLMicroSeconds aAddDelay = 0);

		/// @}


    /// @name vdc level property persistence
    /// @{

    /// load parameters from persistent DB
    /// @note this is usually called from the device container when device is added (detected)
    ErrorPtr load();

    /// save unsaved parameters to persistent DB
    /// @note this is usually called from the device container in regular intervals
    ErrorPtr save();

    /// forget any parameters stored in persistent DB
    ErrorPtr forget();

    // load additional settings from files
    void loadSettingsFromFiles();

		/// @}


    /// @name identification of the addressable entity
    /// @{

    /// @return human readable, language independent model name/short description
    /// @note base class will construct this from global product name and vdcModelSuffix()
    virtual string modelName() P44_OVERRIDE;

    /// @return human readable model name/short description
    virtual string vdcModelSuffix() const = 0;

    /// @return human readable version string of vDC model
    /// @note vDC implementation always uses the base class implementation returns version string of vdc host by default.
    ///   Derived device classes should return version information for the actual device,
    ///   if available from device hardware.
    ///   Do not derive modelVersion() in vDCs, but override Vdc::vdcModelVersion() instead.
    virtual string modelVersion() const P44_FINAL;

    /// @return human readable model version specific to that vDC, meaning for example a firmware version
    ///    of external hardware governing the access to a device bus/network such as a hue bridge.
    ///    If not empty, this will be appended to the modelVersion() string.
    virtual string vdcModelVersion() const { return ""; };

    /// @return unique ID for the functional model of this entity
    virtual string modelUID() P44_OVERRIDE;

    /// @return the entity type (one of dSD|vdSD|vDChost|vDC|dSM|vdSM|dSS|*)
    virtual const char *entityType() P44_OVERRIDE { return "vDC"; }

    /// @return hardware version string or NULL if none
    virtual string hardwareVersion() P44_OVERRIDE { return ""; }

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    virtual string hardwareGUID() P44_OVERRIDE { return ""; }

    /// @return OEM GUID in URN format to identify hardware as uniquely as possible
    virtual string oemGUID() P44_OVERRIDE { return ""; }

    /// @return Vendor name for display purposes
    /// @note if not empty, value will be used by vendorId() default implementation to create vendorname:xxx URN schema id
    virtual string vendorName() P44_OVERRIDE;

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// @}


    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;

    /// Get short text for a "first glance" status of the behaviour
    /// @return string, really short, intended to be shown as a narrow column in a list
    virtual string getStatusText() P44_OVERRIDE;



  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

    // persistence implementation
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

    // derive dSUID
    void deriveDsUid();

    /// actual implementation of scanning for devices from this vDC
    /// @param aRescanFlags selects mode of rescan:
    /// - if rescanmode_incremental is set, search is only made for additional new devices. Disappeared devices
    ///   might not get detected this way.
    /// - if rescanmode_exhaustive is set, device search is made exhaustive (may include longer lasting procedures to
    ///   recollect lost devices, assign bus addresses etc.). Without this flag set, device search will
    ///   still be complete under normal conditions, but might not find all devices when the subsystem is in a
    ///   state that needs recovery operations (such as resolving addressing conflicts etc). Exhaustive search
    ///   should be used with care, as it *might* cause addressing changes that *might* also cause dSUID changes
    ///   in case of devices which do not have a stable unique ID.
    /// - if rescanmode_clearsettings is set, persistent settings of currently known devices will be deleted before
    ///   re-scanning for devices, which means devices will have default settings after collecting.
    ///   Note that this is mutually exclusive with aIncremental (as incremental scan does not remove any devices,
    ///   and thus cannot remove any settings, either)
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) = 0;


    /// called to let vdc handle vdc-level methods
    /// @param aMethod the method
    /// @param aRequest the request to be passed to answering methods
    /// @param aParams the parameters object
    /// @return NULL if method implementation has or will take care of sending a reply (but make sure it
    ///   actually does, otherwise API clients will hang or run into timeouts)
    ///   Returning any Error object, even if ErrorOK, will cause a generic response to be returned.
    /// @note the parameters object always contains the dSUID parameter which has been
    ///   used already to route the method call to this device.
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;


    /// @name grouped delivery of notification to devices
    /// @{

    /// deliver notifications to audience
    /// @param aAudience the audience (list of devices in this vDC that should receive the notification
    /// @param aApiConnection the API connection where the notification originates from
    /// @param aNotification the name of the notification
    /// @param aParams the parameters of the notification
    void deliverToAudience(DsAddressablesList aAudience, VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams);

    /// @}


  private:

    void collectedDevices(StatusCB aCompletedCB, ErrorPtr aError);
    void schedulePeriodicRecollecting();
    void initiateRecollect(RescanMode aRescanMode);
    void recollectDone();

    /// utility method for identifyAndAddDevice(s): identify device with retries
    /// @param aNewDevice the device to be identified
    /// @param aIdentifyCB will be called with the identified device object or an error (unless return value is true for instantly identifying devices)
    /// @param aMaxRetries how many retries (excluding the first try) should be attempted
    /// @param aRetryDelay how long to wait between retries
    void identifyDevice(DevicePtr aNewDevice, IdentifyDeviceCB aIdentifyCB, int aMaxRetries, MLMicroSeconds aRetryDelay);
    void identifyDeviceCB(DevicePtr aNewDevice, IdentifyDeviceCB aIdentifyCB, int aMaxRetries, MLMicroSeconds aRetryDelay, ErrorPtr aError, Device *aIdentifiedDevice);
    void identifyDeviceFailed(DevicePtr aNewDevice, ErrorPtr aError, IdentifyDeviceCB aIdentifyCB);
    void identifyAndAddDeviceCB(StatusCB aCompletedCB, ErrorPtr aError, Device *aIdentifiedDevice);
    void identifyAndAddDevicesCB(DeviceList aToBeAddedDevices, StatusCB aCompletedCB, int aMaxRetries, MLMicroSeconds aRetryDelay, MLMicroSeconds aAddDelay);

    void performPair(VdcApiRequestPtr aRequest, Tristate aEstablish, bool aDisableProximityCheck, MLMicroSeconds aTimeout);
    void pairingEvent(VdcApiRequestPtr aRequest, bool aLearnIn, ErrorPtr aError);
    void pairingTimeout(VdcApiRequestPtr aRequest);


  };

} // namespace p44

#endif /* defined(__p44vdc__vdc__) */
