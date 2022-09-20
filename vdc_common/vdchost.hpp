//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__vdchost__
#define __p44vdc__vdchost__

#include "p44vdc_common.hpp"

#include "dsdefs.h"

#include "persistentparams.hpp"
#include "dsaddressable.hpp"
#include "valuesource.hpp"
#include "digitalio.hpp"
#include "timeutils.hpp"

#include "vdcapi.hpp"

using namespace std;

namespace p44 {

  class Vdc;
  class Device;
  class ButtonBehaviour;
  class DsUid;
  class LocalController;
  class NotificationDeliveryState;

  typedef boost::intrusive_ptr<Vdc> VdcPtr;
  typedef boost::intrusive_ptr<Device> DevicePtr;

  /// Callback for learn events
  /// @param aLearnIn true if new device learned in, false if device learned out
  /// @param aError error occurred during learn-in
  typedef boost::function<void (bool aLearnIn, ErrorPtr aError)> LearnCB;

  /// Callback for device identification (user action) events
  /// @param aDevice device that was activated
  typedef boost::function<void (DevicePtr aDevice, bool aRegular)> DeviceUserActionCB;

  /// Callback for other global device activity
  typedef boost::function<void (VdchostEvent aActivity)> VdchostEventCB;

  /// Rescan modes
  enum {
    rescanmode_none = 0, ///< for reporting supported modes, if no rescan (from UI) is supported
    rescanmode_incremental = 0x01, ///< incremental rescan
    rescanmode_normal = 0x02, ///< normal rescan
    rescanmode_exhaustive = 0x04, ///< exhaustive rescan, should only be used as last resort recovery, as it might cause change of addressing schemes etc.
    rescanmode_clearsettings = 0x08, ///< clear settings (not for incremental)
    rescanmode_reenumerate = 0x10, ///< allow or actively trigger complete re-enumeration of bus device addresses
    rescanmode_force = 0x20, ///< try a rescan even if the vDC has detected a global error already
  };
  typedef uint8_t RescanMode;


  /// persistence for digitalSTROM paramters
  class DsParamStore : public ParamStore
  {
    typedef ParamStore inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };

  /// Scene kind flags
  enum {
    // scope
    scene_global = 0x01, ///< set for global scenes
    scene_room = 0x02, ///< set for room scenes
    scene_area = 0x04, ///< set for area scenes (together with scene_room)
    scene_overlap = 0x08, ///< set for global scenes that also exist with same ID as room scene (together with scene_global)
    // type
    scene_preset = 0x10, ///< preset
    scene_off = 0x20, ///< off (together with scene_preset)
    scene_extended = 0x40, ///< extended
    // extra flag for excluding user-named scenes from non-extended list
    scene_usernamed = 0x80, ///< user-named scene, can be used with getZoneScenes()
  };
  typedef uint8_t SceneKind;

  /// Scene kind
  typedef struct {
    SceneNo no;
    SceneKind kind;
    const char *actionName;
  } SceneKindDescriptor;

  extern const SceneKindDescriptor roomScenes[];
  extern const SceneKindDescriptor globalScenes[];


  class VdcHost;
  typedef boost::intrusive_ptr<VdcHost> VdcHostPtr;
  #if ENABLE_LOCALCONTROLLER
  typedef boost::intrusive_ptr<LocalController> LocalControllerPtr;
  #endif
  typedef map<DsUid, VdcPtr> VdcMap;
  typedef map<DsUid, DevicePtr> DsDeviceMap;
  typedef vector<DevicePtr> DeviceVector;
  typedef list<DevicePtr> DeviceList;
  typedef list<DsAddressablePtr> DsAddressablesList;

  class NotificationGroup
  {
    friend class VdcHost;

    NotificationGroup(VdcPtr aVdc, DsAddressablePtr aFirstMember);

    VdcPtr mVdc; ///< the vDC that might be able to handle a notification for all (device)members together. NULL if members can also be vdcs or vdchost.
    DsAddressablesList mMembers; ///< list of addressables (devices only if vdc is not NULL
  };
  typedef list<NotificationGroup> NotificationAudience;


  /// container for all devices hosted by this application
  /// In dS terminology, this object represents the vDC host (a program/daemon hosting one or multiple virtual device connectors).
  /// - is the connection point to a vDSM
  /// - contains one or multiple vDC containers
  ///   (each representing a specific class of devices, e.g. different bus types etc.)
  class VdcHost : public DsAddressable, public PersistentParams
  {
    typedef DsAddressable inherited;
    typedef PersistentParams inheritedParams;

    friend class Vdc;
    friend class DsAddressable;
    friend class LocalController;

    bool mExternalDsuid; ///< set when dSUID is set to a external value (usually UUIDv1 based)
    int mVdcHostInstance; ///< instance number of this vdc host (defaults to 0, can be set to >0 to have multiple vdchost on the same host/mac address)
    bool mStoredDsuid; ///< set when using stored (DB persisted) dSUID that is not equal to default dSUID 
    uint64_t mac; ///< MAC address as found at startup

    string mIfNameForConn; ///< the name of the network interface to use for getting IP and connectivity status
    bool mAllowCloud; ///< if not set, vdcs are forbidden to use cloud-based services such as N-UPnP that are not actively/obviously configured by the user him/herself

    DsDeviceMap mDSDevices; ///< available devices by API-exposed ID (dSUID or derived dsid)
    DsParamStore mDSParamStore; ///< the database for storing dS device parameters

    string mIconDir; ///< the directory where to load icons from
    string mPersistentDataDir; ///< the directory for the vdc host to store SQLite DBs and possibly other persistent data
    string mConfigDir; ///< the directory to load config files (scene definitions, machine configurations etc.) from

    string mProductName; ///< the name of the vdc host product (model name) as a a whole
    string mProductVersion; ///< the version string of the vdc host product as a a whole

    string mDeviceHardwareId; ///< the device hardware id (such as a serial number) of the vdc host product as a a whole
    string mDescriptionTemplate; ///< how to describe the vdc host (e.g. in service announcements)
    string mVdcModelNameTemplate; ///< how to generate vdc model names (that's what shows up in HW-Info in dS)

    bool mCollecting;
    MLTicket mAnnouncementTicket;
    MLTicket mPeriodicTaskTicket;
    MLMicroSeconds mLastActivity;
    MLMicroSeconds mLastPeriodicRun;

    MLMicroSeconds mTimeOfDayDiff; ///< current difference of monotonic ML time and a pseudo local time to detect changes (TZ changes, NTP updates)

    int8_t mLocalDimDirection;

    // learning
    bool mLearningMode;
    LearnCB mLearnHandler;

    // global status
    bool mNetworkConnected;

    // user action monitor (learn)buttons
    DeviceUserActionCB mDeviceUserActionHandler;

    // global event monitor
    VdchostEventCB mEventMonitorHandler;

    // mainloop statistics
    int mMainloopStatsInterval; ///< 0=none, N=every PERIODIC_TASK_INTERVAL*N seconds
    int mMainLoopStatsCounter;

    // active vDC API session
    int mMaxApiVersion; // limit for API version to support (for testing client's backwards compatibility), 0=no limit
    DsUid mConnectedVdsm;
    MLTicket mSessionActivityTicket;
    VdcApiConnectionPtr mVdsmSessionConnection;

    // settings
    bool mPersistentChannels;

    #if ENABLE_LOCALCONTROLLER
    LocalControllerPtr mLocalController;
    #endif

    #if P44SCRIPT_FULL_SUPPORT
    ScriptSource mMainScript; ///< global init/main script stored in settings
    ScriptMainContextPtr mVdcHostScriptContext; ///< context for global vdc scripts
    bool mGlobalScriptsStarted; ///< global scripts have been started
    #endif

  public:

    VdcHost(bool aWithLocalController = false, bool aWithPersistentChannels = false);
    virtual ~VdcHost();

    /// VdcHost is a singleton, get access to it
    /// @return vdc host
    static VdcHostPtr sharedVdcHost();

    #if ENABLE_LOCALCONTROLLER
    /// @return local controller, might be NULL if local controller is not enabled (but compiled in)
    LocalControllerPtr getLocalController();
    #endif

    /// geolocation of the installation
    GeoLocation mGeolocation;

    /// the list of containers by API-exposed ID (dSUID or derived dsid)
    VdcMap mVdcs;

    /// API for vdSM
    VdcApiServerPtr mVdcApiServer;

    /// active vDSM session
    VdcApiConnectionPtr getVdsmSessionConnection() { return mVdsmSessionConnection; };

    /// get the bridge API (if any)
    virtual VdcApiConnectionPtr getBridgeApi() { return VdcApiConnectionPtr(); /* none in this base class */ }

    /// get an API value that would work for the session connection if we had one
    /// @return an API value of the same type as session connection will use
    ApiValuePtr newApiValue();

    /// set user assignable name
    /// @param aName name of this instance of the vdc host
    virtual void setName(const string &aName) P44_OVERRIDE;

    /// set the human readable name of the vdc host product as a a whole
    /// @param aProductName product (model) name
    void setProductName(const string &aProductName) { mProductName = aProductName; }

    /// set the the human readable version string of the vdc host product as a a whole
    /// @param aProductVersion product version string
    void setProductVersion(const string &aProductVersion) { mProductVersion = aProductVersion; }

    /// set the the human readable hardware id (such as a serial number) of the vdc host product as a a whole
    /// @param aDeviceHardwareId device serial number or similar id
    void setDeviceHardwareId(const string &aDeviceHardwareId) { mDeviceHardwareId = aDeviceHardwareId; }

    /// set the template (or fixed string) for describing the vdc host product as a a whole (e.g. in network advertisements)
    /// @param aTemplate template how to create the description
    /// @note the following sequences will be substituted
    /// - %V : vendor name
    /// - %M : product model (name)
    /// - %N : user-assigned name, if any, preceeded by a space and in double quotes
    /// - %S : device hardware id (if set specifically, otherwise the dSUID is used)
    void setDescriptionTemplate(const string &aTemplate) { mDescriptionTemplate = aTemplate; }

    /// set the template (or fixed string) for describing a vdc within this vdchost in vdc's modelName property (which is used in dSS as HW-Info)
    /// @param aTemplate template how to create vdc modelName
    /// @note the following sequences will be substituted
    /// - %V : vendor name (of the vdc, which defaults to the vendor of the vdchost)
    /// - %M : product model (name) of the vdchost
    /// - %m : vdcModelSuffix (short suffix describing vdc's functionality, such as "hue" or "DALI")
    /// - %S : device hardware id (if set specifically, otherwise the dSUID is used)
    void setVdcModelNameTemplate(const string &aTemplate) { mVdcModelNameTemplate = aTemplate; }

    /// Set how dSUIDs are generated
    /// @param aExternalDsUid if specified, this is used directly as dSUID for the device container
    /// @param aIfNameForMAC if specified, this network interface is used to obtain the MAC address for creating the dSUID from
    /// @param aInstance defaults to 0, can be set to >0 to have multiple vdchost instances based on the same host/MAC-address
    /// @note Must be set before any other activity in the device container, in particular before
    ///   any class containers are added to the device container
    void setIdMode(DsUidPtr aExternalDsUid, const string aIfNameForMAC, int aInstance=0);

    /// Set network interface to use for determining IP address and checking for being available for network connections
    /// @param aIfNameForConnections name of the network device, empty string to use default interface
    void setNetworkIf(const string aIfNameForConnections) { mIfNameForConn = aIfNameForConnections; };

    /// Set maximum API version to support (to test backwards compatibility of connecting vdsms)
    /// @param aMaxApiVersion max API version to support, 0 = all implemented versions
    void setMaxApiVersion(int aMaxApiVersion) { mMaxApiVersion = aMaxApiVersion; };

    /// Enable or forbid use of cloud services not explicitly configured or obviously expected by user (such as N-UPnP)
    /// @param aAllow true to allow cloud services
    void setAllowCloud(bool aAllow) { mAllowCloud = aAllow; };

    /// @return true if not explicitly configured cloud services are allowed, false otherwise
    bool cloudAllowed() { return mAllowCloud; };

    /// Set directory for loading device icons
    /// @param aIconDir  full path to directory to load device icons from. Empty string or NULL means "no icons"
    /// @note the path needs to contain subdirectories for each size, named iconX with X=side lengt in pixels
    ///   At this time, only 16x16 icons are defined, so only one subdirectory named icon16 needs to exist
    ///   within icondir
    void setIconDir(const char *aIconDir);

    /// Get directory for loading device icons from
    /// @return the path to the icon dir, always with a trailing path separator, ready to append subpaths and filenames
    const char *getIconDir();

    /// set the directory where to find configuration files (scene definitions, machine configs etc.)
    /// @param aConfigDir full path to config directory
    void setConfigDir(const char *aConfigDir);

    /// get the config dir path
    /// @return full path to config directory
    const char *getConfigDir();

    /// Set how often mainloop statistics are printed out log (LOG_INFO)
    /// @param aInterval 0=none, N=every PERIODIC_TASK_INTERVAL*N seconds
    void setMainloopStatsInterval(int aInterval) { mMainloopStatsInterval = aInterval; };

    /// prepare device container internals for creating and adding vDCs
    /// In particular, this triggers creating/loading the vdc host dSUID, which serves as a base ID
    /// for most class containers and many devices.
    /// @param aFactoryReset if set, database will be reset
    void prepareForVdcs(bool aFactoryReset);

		/// initialize
    /// @param aCompletedCB will be called when the entire container is initialized or has been aborted with a fatal error
    /// @param aFactoryReset if set, database will be reset
    virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset);

    /// start running normally
    void startRunning();

    /// activity monitor (e.g. for main program to trigger re-advertising or LED activity)
    /// @param aEventCB will be called for globally relevant events.
    void setEventMonitor(VdchostEventCB aEventCB);

    /// activity monitor
    /// @param aUserActionCB will be called when the user has performed an action (usually: button press) in a device
    void setUserActionMonitor(DeviceUserActionCB aUserActionCB);

    #if ENABLE_P44SCRIPT
    /// find a value source
    /// @param aValueSourceID internal, persistent ID of the value source
    ValueSource *getValueSourceById(string aValueSourceID);
    #endif // ENABLE_P44SCRIPT

    /// @name notification delivery
    /// @{

    /// Add a addressable to a notification audience
    /// @param aAudience the audience
    /// @param aTarget the addressable to be added
    void addTargetToAudience(NotificationAudience &aAudience, DsAddressablePtr aTarget);

    /// Add a notification target by dSUID to a notification audience
    /// @param aAudience the audience
    /// @param aDsuid the dSUID to be added
    /// @return error in case dSUID is not address valid notification target
    ErrorPtr addToAudienceByDsuid(NotificationAudience &aAudience, const DsUid &aDsuid);

    /// Add a notification target by x-p44-itemSpec to a notification audience
    /// @param aAudience the audience
    /// @param aItemSpec alternative item specification, in case there is no dSUID
    /// @return error in case aItemSpec does not address valid notification target
    ErrorPtr addToAudienceByItemSpec(NotificationAudience &aAudience, const string &aItemSpec);

    /// Add notification targets selected by matching zone and group
    /// @param aAudience the audience
    /// @param aZone the zone to broadcast to (0 for entire appartment)
    /// @param aGroup the group to broadcast to (group_undefined for all groups)
    void addToAudienceByZoneAndGroup(NotificationAudience &aAudience, DsZoneID aZone, DsGroup aGroup);

    /// deliver notifications to audience
    /// @param aAudience the audience
    /// @param aApiConnection the API connection where the notification originates from
    /// @param aNotification the name of the notification
    /// @param aParams the parameters of the notification
    void deliverToAudience(NotificationAudience &aAudience, VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams);

    /// called when delivery of a scene call or dimming notification to a device has been executed
    /// @param aDevice the device
    /// @param aDeliveryState the delivery state
    /// @note this is not called for all types of notifications, only callScene and dimchannel
    /// @note aDeliveryState lifetime may immediately end when this method returns
    void deviceWillApplyNotification(DevicePtr aDevice, NotificationDeliveryState &aDeliveryState);

    /// @}



    /// @name global status
    /// @{

    /// check if API is connected
    /// @return true if vDC API has a connection from a vdSM right now
    bool isApiConnected();

    /// check if we have network (IP) connection
    /// @return true if device this vdchost is running on has a usable IP address
    bool isNetworkConnected();

    /// get IPv4 address relevant for connection status
    /// @return IPv4 as a 32-bit int, or 0 if none found
    uint32_t getIpV4Address();

    /// global setting for persisting (and restoring after reboot) last output channel states
    /// @return true if output channel states should be saved/restored
    bool doPersistChannels() { return mPersistentChannels; };

    /// @}


		/// @name device detection and registration
    /// @{

    /// collect devices from all vDCs, and have each of them initialized
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
    void collectDevices(StatusCB aCompletedCB, RescanMode aRescanFlags);

    /// Put vDC controllers into learn-in mode
    /// @param aLearnHandler handler to call when a learn-in action occurs
    /// @param aDisableProximityCheck true to disable proximity check (e.g. minimal RSSI requirement for some EnOcean devices)
    void startLearning(LearnCB aLearnHandler, bool aDisableProximityCheck = false);

    /// stop learning mode
    void stopLearning();

    /// @return true if currently in learn mode
    bool isLearning() { return mLearningMode; };

    /// @}


    /// @name persistence
    /// @{

    /// set the directory where to store persistent data (databases etc.)
    /// @param aPersistentDataDir full path to directory to save persistent data
    void setPersistentDataDir(const char *aPersistentDataDir);

		/// get the persistent data dir path
		/// @return full path to directory to save persistent data
		const char *getPersistentDataDir();

    /// get the dsParamStore
    DsParamStore &getDsParamStore() { return mDSParamStore; }

    /// @}


    /// @name DsAddressable API implementation
    /// @{

    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;
    virtual bool handleNotification(VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams) P44_OVERRIDE;

    /// @}

    /// have button clicks checked for local handling
    /// @param aButtonBehaviour the button behaviour that generated the click
    /// @param aClickType the type of click
    /// @return true if locally handled
    bool checkForLocalClickHandling(ButtonBehaviour &aButtonBehaviour, DsClickType aClickType);

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;


    /// @name methods for DeviceClassContainers
    /// @{

    /// get a device by name or dSUID string
    /// @param aName the name or dSUID
    /// @return device or NULL
    DevicePtr getDeviceByNameOrDsUid(const string &aName);

    /// called by vDC containers to report learn event
    /// @param aLearnIn true if new device learned in, false if device learned out
    /// @param aError error occurred during learn-in
    void reportLearnEvent(bool aLearnIn, ErrorPtr aError);

    /// called to signal a user-generated action from a device, which may be used to detect a device
    /// @param aDevice device where user action was detected
    /// @param aRegular if true, the user action is a regular action, such as a button press
    /// @return true if normal user action processing should be suppressed
    bool signalDeviceUserAction(Device &aDevice, bool aRegular);

    /// called by vDC containers to add devices to the container-wide devices list
    /// @param aDevice a device object which has a valid dSUID
    /// @return false if aDevice's dSUID is already known.
    /// @note aDevice is added *only if no device is already known with this dSUID*
    /// @note this can be called as part of a collectDevices scan, or when a new device is detected
    ///   by other means than a scan/collect operation
    /// @note this should NOT be called directly from vdc implementations. Use
    ///   simpleIdentifyAndAddDevice() or identifyAndAddDevice()
    bool addDevice(DevicePtr aDevice);

    /// called by vDC containers to remove devices from the container-wide list
    /// @param aDevice a device object which has a valid dSUID
    /// @param aForget if set, parameters stored for the device will be deleted
    void removeDevice(DevicePtr aDevice, bool aForget);

    /// @param aDeviceList will receive pointers to all devices
    void createDeviceList(DeviceVector &aDeviceList);

    /// @}


    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE { return mProductName.size()>0 ? mProductName : "vDC host"; }

    /// @return human readable product version string
    /// @note it is important to override this here in vdchost, because would loop otherwise when base class calls vdchost's implementation
    virtual string modelVersion() const P44_OVERRIDE { return mProductVersion; }

    /// @return human readable product version string of next available (installable) product version, if any
    virtual string nextModelVersion() const { return ""; /* none by default */ }

    /// @return unique ID for the functional model of this entity
    virtual string modelUID() P44_OVERRIDE { return DSUID_P44VDC_MODELUID_UUID; /* using the p44 modelUID namespace UUID itself */ }

    /// @return the entity type (one of dSD|vdSD|vDChost|vDC|dSM|vdSM|dSS|*)
    virtual const char *entityType() P44_OVERRIDE { return "vDChost"; }

    /// @return hardware version string or NULL if none
    virtual string hardwareVersion() P44_OVERRIDE { return ""; }

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    virtual string hardwareGUID() P44_OVERRIDE { return ""; }

    /// @return OEM GUID in URN format to identify hardware as uniquely as possible
    virtual string oemGUID() P44_OVERRIDE { return ""; }

    /// @return Vendor ID in URN format to identify vendor as uniquely as possible
    /// @note class containers and devices will inherit this (vdc host's) vendor name if not overridden
    virtual string vendorName() P44_OVERRIDE { return "plan44.ch"; };

    /// @return Vendor ID in URN format to identify vendor as uniquely as possible
    string getDeviceHardwareId() { return mDeviceHardwareId; };

    /// @return text describing the vdc host device, suitable for publishing via Avahi etc.
    string publishedDescription();

    /// @return URL for Web-UI (for access from local LAN)
    /// @note it is important to override this here in vdchost, because would loop otherwise when base class calls vdchost's implementation
    virtual string webuiURLString() P44_OVERRIDE { return ""; }

    /// @}


    /// @name vdc host level property persistence
    /// @{

    /// load parameters from persistent DB and determine root (vdc host) dSUID
    ErrorPtr loadAndFixDsUID();

    /// save unsaved parameters to persistent DB
    /// @note this is usually called from periodicTask in regular intervals
    void save();

    /// forget any parameters stored in persistent DB
    ErrorPtr forget();

    #if ENABLE_SETTINGS_FROM_FILES
    // load additional settings from file
    void loadSettingsFromFiles();
    #endif

    /// @}

    // post a vdchost (global) event to all vdcs and via event monitor callback
    void postEvent(VdchostEvent aEvent);

    /// handle global events
    /// @param aEvent the event to handle
    virtual void handleGlobalEvent(VdchostEvent aEvent) P44_OVERRIDE;

    /// add a vDC container
    /// @param aVdcPtr a intrusive_ptr to a vDC container
    /// @note this is a one-time initialisation. Device class containers are not meant to be removed at runtime
    void addVdc(VdcPtr aVdcPtr);

    /// @name method for friend classes to send and process API messages
    /// @note sending results/errors is done via the VcdApiRequest object
    /// @{

    // method and notification dispatching
    ErrorPtr handleMethodForParams(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams);
    ErrorPtr handleNotificationForParams(VdcApiConnectionPtr aApiConnection, const string &aMethod, ApiValuePtr aParams);
    DsAddressablePtr addressableForDsUid(const DsUid &aDsUid);
    DsAddressablePtr addressableForItemSpec(const string &aItemSpec);

    /// @}

    #if !REDUCED_FOOTPRINT

    /// get scene id (dS global scene number, not related to a specific zone) by kind
    /// @param aSceneSpec name of scene kind like "preset 1", "standby" etc. or dS scene number)
    /// @return dS scene number or INVALID_SCENE_NO if none is found
    static SceneNo getSceneIdByKind(string aSceneKindName);

    #endif

    /// get a text description for a scene number
    /// @param aSceneNo the scene number to get a description text for
    /// @param aIsGlobal if set, global names are used for ambiguous scenes
    ///   (those that have different meaning in room context vs. in global context),
    ///   otherwise, room scene names are used in the description text
    static string sceneText(SceneNo aSceneNo, bool aIsGlobal = false);

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

    // activity monitor
    void signalActivity();

    /// identify the vdchost to the user
    virtual void identifyToUser() P44_OVERRIDE;

    /// @return true if the vdchost has a way to actually identify to the user (apart from a log message)
    virtual bool canIdentifyToUser() P44_OVERRIDE;

  private:

    // derive dSUID
    void deriveDsUid();
    void savePrivate();

    // initializing and collecting
    void initializeNextVdc(StatusCB aCompletedCB, bool aFactoryReset, VdcMap::iterator aNextVdc);
    void vdcInitialized(StatusCB aCompletedCB, bool aFactoryReset, VdcMap::iterator aNextVdc, ErrorPtr aError);
    void collectFromNextVdc(StatusCB aCompletedCB, RescanMode aRescanFlags, VdcMap::iterator aNextVdc);
    void vdcCollected(StatusCB aCompletedCB, RescanMode aRescanFlags, VdcMap::iterator aNextVdc, ErrorPtr aError);
    void initializeNextDevice(StatusCB aCompletedCB, DsDeviceMap::iterator aNextDevice);
    void nextDeviceInitialized(StatusCB aCompletedCB, DsDeviceMap::iterator aNextDevice, ErrorPtr aError);

    // local operation mode
    void handleClickLocally(ButtonBehaviour &aButtonBehaviour, DsClickType aClickType);
    void localDimHandler();

    // API connection status handling
    void vdcApiConnectionStatusHandler(VdcApiConnectionPtr aApiConnection, ErrorPtr &aError);

    // API request handling
    void vdcApiRequestHandler(VdcApiConnectionPtr aApiConnection, VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams);

    // vDC level method and notification handlers
    ErrorPtr helloHandler(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    ErrorPtr byeHandler(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    ErrorPtr removeHandler(VdcApiRequestPtr aForRequest, DevicePtr aDevice);
    void removeResultHandler(DevicePtr aDevice, VdcApiRequestPtr aForRequest, bool aDisconnected);
    void duplicateIgnored(DevicePtr aDevice);
    void separateDeviceInitialized(DevicePtr aDevice, ErrorPtr aError);

    // handles freshly initialized device
    void deviceInitialized(DevicePtr aDevice, ErrorPtr aError);

    // announcing dSUID addressable entities within the device container (vdc host)
    void resetAnnouncing();
    void startAnnouncing();
    void announceNext();
    void announceResultHandler(DsAddressablePtr aAddressable, VdcApiRequestPtr aRequest, ErrorPtr &aError, ApiValuePtr aResultOrErrorData);

    // periodic task
    void periodicTask(MLMicroSeconds aNow);
    void checkTimeOfDayChange();

    // getting MAC
    void getMyMac(StatusCB aCompletedCB, bool aFactoryReset);

    #if ENABLE_P44SCRIPT
    /// get all value sources in this vdc host
    /// @param aApiObjectValue must be an object typed API value, will receive available value sources as valueSourceID/description key/values
    void createValueSourcesList(ApiValuePtr aApiObjectValue);
    #endif // ENABLE_P44SCRIPT


    #if !REDUCED_FOOTPRINT
    /// get a list of scene number/name associations
    /// @param aApiObjectValue must be an object typed API value, will receive a list of scenes with dS-id, name etc.
    void createScenesList(ApiValuePtr aApiObjectValue);
    #endif
    #if P44SCRIPT_FULL_SUPPORT
    void runGlobalScripts();
    void globalScriptEnds(ScriptObjPtr aResult, const char *aOriginLabel);
    void scriptExecHandler(VdcApiRequestPtr aRequest, ScriptObjPtr aResult);
    #endif
  };


  #if P44SCRIPT_FULL_SUPPORT

  /// Dummy api call from script "connection" object
  class ScriptCallConnection : public VdcApiConnection
  {
    typedef VdcApiConnection inherited;

  public:

    ScriptCallConnection();

    /// install callback for received API requests
    /// @param aApiRequestHandler will be called when a API request has been received
    void setRequestHandler(VdcApiRequestCB aApiRequestHandler);

    /// The underlying socket connection
    /// @return socket connection
    virtual SocketCommPtr socketConnection() P44_OVERRIDE { return SocketCommPtr(); };

    /// request closing connection after last message has been sent
    virtual void closeAfterSend() P44_OVERRIDE {};

    /// the name of the API or the API's peer for logging
    virtual const char* apiName() P44_OVERRIDE { return "script"; };

    /// get a new API value suitable for this connection
    /// @return new API value of suitable internal implementation to be used on this API connection
    virtual ApiValuePtr newApiValue() P44_OVERRIDE;

    virtual int domain() P44_OVERRIDE { return VDC_CFG_DOMAIN; };

  };


  /// API JSON request from script
  class ScriptApiRequest : public VdcApiRequest
  {
    typedef VdcApiRequest inherited;

    BuiltinFunctionContextPtr mBuiltinFunctionContext;

  public:

    /// constructor
    ScriptApiRequest(BuiltinFunctionContextPtr aBuiltinFunctionContext) : mBuiltinFunctionContext(aBuiltinFunctionContext) {};

    /// return the request ID as a string
    /// @return request ID as string
    virtual string requestId()  P44_OVERRIDE { return ""; }

    /// get the API connection this request originates from
    /// @return API connection
    virtual VdcApiConnectionPtr connection() P44_OVERRIDE;

    /// send a vDC API result (answer for successful method call)
    /// @param aResult the result as a ApiValue. Can be NULL for procedure calls without return value
    /// @result empty or Error object in case of error sending result response
    virtual ErrorPtr sendResult(ApiValuePtr aResult) P44_OVERRIDE;

    /// send a error to the vDC API (answer for unsuccesful method call)
    /// @param aError the error object
    /// @note depending on the Error object's subclass and the vDC API kind (protobuf, json...),
    ///   different information is transmitted. ErrorCode and ErrorMessage are always sent,
    ///   Errors based on class VdcApiError will also include errorType, errorData and userFacingMessage
    /// @note if aError is NULL, a generic "OK" error condition is sent
    /// @result empty or Error object in case of error sending error response
    virtual ErrorPtr sendError(ErrorPtr aError) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<ScriptApiRequest> ScriptApiRequestPtr;


  namespace P44Script {

    class VdcHostLookup : public BuiltInMemberLookup {
      typedef BuiltInMemberLookup inherited;
      VdcHostLookup();
    public:
      static MemberLookupPtr sharedLookup();
    };

  }

  #endif // P44SCRIPT_FULL_SUPPORT


} // namespace p44

#endif /* defined(__p44vdc__vdchost__) */
