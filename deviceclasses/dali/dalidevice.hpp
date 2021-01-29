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

#ifndef __p44vdc__dalidevice__
#define __p44vdc__dalidevice__

#include "device.hpp"

#if ENABLE_DALI

#include "dalicomm.hpp"
#include "colorlightbehaviour.hpp"

using namespace std;

namespace p44 {

  class DaliVdc;
  class DaliBusDevice;
  class DaliBusDeviceGroup;
  class DaliOutputDevice;
  class DaliSingleControllerDevice;
  class DaliCompositeDevice;
  class DaliInputDevice;

  typedef boost::intrusive_ptr<DaliBusDevice> DaliBusDevicePtr;
  typedef boost::intrusive_ptr<DaliBusDeviceGroup> DaliBusDeviceGroupPtr;
  typedef boost::intrusive_ptr<DaliOutputDevice> DaliOutputDevicePtr;
  typedef boost::intrusive_ptr<DaliSingleControllerDevice> DaliSingleControllerDevicePtr;
  typedef boost::intrusive_ptr<DaliCompositeDevice> DaliCompositeDevicePtr;
  typedef boost::intrusive_ptr<DaliInputDevice> DaliInputDevicePtr;

  class DaliBusDevice : public P44LoggingObj
  {
    typedef P44LoggingObj inherited;
    friend class DaliBusDeviceGroup;
    friend class DaliOutputDevice;
    friend class DaliCompositeDevice;
    friend class DaliSingleControllerDevice;
    friend class DaliVdc;

    DaliDeviceInfoPtr deviceInfo; ///< the device info of the bus device (ballast)

    DsUid dSUID; ///< the dSUID of the bus device (if single device, this will become the dS device's dSUID)

    DaliVdc &daliVdc;

    MLTicket dimRepeaterTicket; ///< DALI dimming and repeater ticket
    MLTicket outputSyncTicket; ///< output value sync-back ticket
    VdcDimMode currentDimMode; ///< current dim mode

    /// feature set
    bool supportsLED; // supports device type 6/LED features
    bool dt6LinearDim; // linear dimming curve enabled
    bool supportsDT8; // supports device type 8 features
    bool dt8Color; // supports DT 8 color features
    bool dt8CT; // supports DT 8 color temperature features
    uint8_t dt8PrimaryColors; // if>0, how many primary color channels are supported
    uint8_t dt8RGBWAFchannels; // if>0, how many RGBWAF channels are supported
    bool dt8autoactivation; // if set, changing DAPC (brightness) auto-activates colors set in TEMPROARY color registers

    /// cached status (call updateStatus() to update these)
    bool isDummy; ///< set if dummy (not found on bus, but known to be part of a composite device)
    bool isPresent; ///< set if present
    bool lampFailure; ///< set if lamp has failure

    /// cached transition parameters (updated in setTransitionTime() and dimPrepare())
    MLMicroSeconds currentTransitionTime; ///< currently set transition time
    uint8_t currentFadeTime; ///< currently set DALI fade time
    double currentDimPerMS; ///< current dim steps per second
    uint8_t currentFadeRate; ///< currently set DALI fade rate

    /// cached parameters (call updateParams() to update these)
    bool staleParams; ///< set if cached parameters might be stale and should not be used for deciding incremental changes to HW
    Brightness currentBrightness; ///< current brightness
    Brightness minBrightness; ///< currently set minimal brightness
    // - DT8 params
    ColorLightMode currentColorMode; ///< current color mode
    uint16_t currentXorCT; ///< current CIE X or CT or Red
    uint16_t currentY; ///< current CIE Y or Green
    uint8_t currentR; ///< current Red
    uint8_t currentG; ///< current Green
    uint8_t currentB; ///< current Blue
    uint8_t currentW; ///< current White
    uint8_t currentA; ///< current Amber

  public:

    DaliBusDevice(DaliVdc &aDaliVdc);

    /// @return the prefix to be used for logging from this object
    virtual string logContextPrefix() P44_OVERRIDE;

    /// @return the per-instance log level offset
    /// @note is virtual because some objects might want to use the log level offset of another object
    virtual int getLogLevelOffset() P44_OVERRIDE;

    /// use passed device info and derive dSUID from it
    void setDeviceInfo(DaliDeviceInfoPtr aDeviceInfo);

    /// clear all device info except short address and revert to short address derived dSUID
    void invalidateDeviceInfoSerial();

    /// derive the dSUID from collected device info
    virtual void deriveDsUid();

    /// calculate (but not set) dSUID for a specific devInf statzs
    void dsUidForDeviceInfoStatus(DsUid &aDsUid, DaliDeviceInfo::DaliDevInfStatus aDevInfStatus);

    /// check if bus device represents a DALI group
    /// @return true if group
    virtual bool isGrouped() { return false; }

    /// show description
    virtual string description();

  protected:

    /// called to query feature set, BEFORE initialize() and BEFORE any grouping occurs
    /// @param aCompletedCB will be called when initialisation is complete
    void queryFeatureSet(StatusCB aCompletedCB);

    /// initialize device for first use
    /// @param aCompletedCB will be called when initialisation is complete
    /// @param aUsedGroupsMask groups that are in use. Single devices should not be in any of these groups
    virtual void initialize(StatusCB aCompletedCB, uint16_t aUsedGroupsMask);

    /// initializes usage of extra features (such as linear dimming curve for DT6 devices)
    /// @note this should work equally for single devices and groups.
    void initializeFeatures(StatusCB aCompletedCB);

    /// update parameters from device to local vars
    void updateParams(StatusCB aCompletedCB);

    /// update status information from device
    void updateStatus(StatusCB aCompletedCB);


    /// convert DALI arc power to dS brightness value (linear for DT6 LEDs with dt6LinearDim enabled, logarithmic otherwise)
    /// @param aBrightness 0..100%
    /// @return arcpower 0..254
    uint8_t brightnessToArcpower(Brightness aBrightness);

    /// convert DALI arc power to dS brightness value (linear for DT6 LEDs with dt6LinearDim enabled, logarithmic otherwise)
    /// @param aArcpower 0..254
    /// @return brightness 0..100%
    Brightness arcpowerToBrightness(int aArcpower);

    /// set transition time for subsequent brightness or color changes
    /// @param aTransitionTime time for transition
    void setTransitionTime(MLMicroSeconds aTransitionTime);

    /// set new brightness
    /// @param aBrightness new brightness to set
    /// @return true if brightness has changed
    bool setBrightness(Brightness aBrightness);

    /// set color parameters from behaviour
    /// @param aColorLight the color light
    /// @param aTransitional set to use transitional channel values
    /// @param aAlways set to always write color registers, even if unchanged
    /// @param aSilent set to suppress log messages
    /// @return true if any color parameters were changed in the device
    bool setColorParamsFromChannels(ColorLightBehaviourPtr aColorLight, bool aTransitional, bool aAlways, bool aSilent);

    /// set new color parameters as CIE x/y or CT
    /// @param aMode new color mode
    /// @param aCieXorCT CIE X coordinate (0..1) or CT in mired
    /// @param aCieY CIE Y coordinate (0..1)
    /// @param aAlways set to always write color registers, even if unchanged
    /// @return true if any color parameters were changed in the device
    bool setColorParams(ColorLightMode aMode, double aCieXorCT, double aCieY, bool aAlways);

    /// set new color parameters in raw RGBWA
    /// @param aR,aG,aB,aW,aA new values
    /// @param aAlways set to always write color registers, even if unchanged
    /// @return true if any color parameters were changed in the device
    bool setRGBWAParams(uint8_t aR, uint8_t aG, uint8_t aB, uint8_t aW, uint8_t aA, bool aAlways);

    /// activate new color parameters
    void activateColorParams();

    /// save brightness as default for DALI dimmer to use after powerup and at failure
    /// @param aBrightness new brightness to set, <0 to save current brightness
    void setDefaultBrightness(Brightness aBrightness);

    /// prepare dimming (=adjust fade rate if needed)
    /// @param aDimMode according to VdcDimMode: 1=start dimming up, -1=start dimming down, 0=stop dimming
    /// @param aDimPerMS dim speed in brightness value per millsecond
    /// @param aCompletedCB is called when device is ready for dimming
    /// @note this is used by optimized group dimming
    void dimPrepare(VdcDimMode aDimMode, double aDimPerMS, StatusCB aCompletedCB);

    /// start or stop optimized DALI dimming
    /// @param aDimMode according to VdcDimMode: 1=start dimming up, -1=start dimming down, 0=stop dimming
    /// @param aDimPerMS dim speed in brightness value per millsecond
    void dim(VdcDimMode aDimMode, double aDimPerMS);

    /// DALI address to use for querying brightness etc.
    /// @return DALI address
    /// @note this will be overridden in DaliBusDeviceGroup to read info from single master dimmer, not group
    virtual uint8_t addressForQuery() { return deviceInfo->shortAddress; };


    typedef boost::function<void (uint16_t aGroupBitMask, ErrorPtr aError)> DaliGroupsCB;
    /// Utility: Retrieve group membership mask for a given short address
    /// @param aDaliGroupsCB delivers the result
    /// @param aShortAddress which device to query (note: not necessarily myself!)
    void getGroupMemberShip(DaliGroupsCB aDaliGroupsCB, DaliAddress aShortAddress);


    /// check if this device belongs to a particular DALI address (for diagnosis and DALI bus summary)
    /// @param aDaliAddress the DALI address to check
    /// @return true if aDaliAddress represents this busdevice or is a (group) member of it
    virtual bool belongsToShortAddr(DaliAddress aDaliAddress) const;

    /// add bus device level info to summary information
    /// @param aInfo ApiValue object, info fields will be added to it
    virtual void daliBusDeviceSummary(ApiValuePtr aInfo) const;

  private:

    void registerDeviceType(uint8_t aDeviceType);
    void deviceTypeResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);
    void probeDeviceType(StatusCB aCompletedCB, uint8_t aNextDT);
    void probeDeviceTypeResponse(StatusCB aCompletedCB, uint8_t aNextDT, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);
    void queryDTFeatures(StatusCB aCompletedCB);
    void dt8FeaturesResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);
    void dt8GearStatusResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);

    void queryGroup0to7Response(DaliGroupsCB aDaliGroupsCB, DaliAddress aShortAddress, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);
    void queryGroup8to15Response(DaliGroupsCB aDaliGroupsCB, DaliAddress aShortAddress, uint16_t aGroupBitMask, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);
    void checkGroupMembership(StatusCB aCompletedCB, uint16_t aUsedGroupsMask);
    void groupMembershipResponse(StatusCB aCompletedCB, uint16_t aUsedGroupsMask, DaliAddress aShortAddress, uint16_t aGroups, ErrorPtr aError);

    void queryActualLevelResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);
    void queryMinLevelResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);
    void queryColorStatusResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);
    void queryXCoordResponse(StatusCB aCompletedCB, uint16_t aResponse16, ErrorPtr aError);
    void queryYCoordResponse(StatusCB aCompletedCB, uint16_t aResponse16, ErrorPtr aError);
    void queryCTResponse(StatusCB aCompletedCB, uint16_t aResponse16, ErrorPtr aError);
    void queryRGBWAFResponse(StatusCB aCompletedCB, uint16_t aResIndex, uint16_t aResponse16, ErrorPtr aError);

    void dimPrepared(StatusCB aCompletedCB, ErrorPtr aError);
    void dimStart(DaliAddress aDaliAddress, DaliCommand aCommand);
    void dimRepeater(DaliAddress aDaliAddress, DaliCommand aCommand, MLTimer &aTimer);

    void queryStatusResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);

  };


  class DaliBusDeviceGroup : public DaliBusDevice
  {
    typedef DaliBusDevice inherited;
    friend class DaliOutputDevice;
    friend class DaliCompositeDevice;
    friend class DaliVdc;

    string mixID; ///< dSUIDs of all members, XOR mixed
    DaliAddress groupMaster; ///< the DALI short address of the master dimmer (i.e. the one that is read from)
    DaliComm::ShortAddressList groupMembers; ///< short addresses of members of the group

  public:

    /// initialize device for first use
    /// @param aCompletedCB will be called when initialisation is complete
    /// @param aUsedGroupsMask groups that are in use. grouped devices should not be in any of these groups except their own group
    virtual void initialize(StatusCB aCompletedCB, uint16_t aUsedGroupsMask) P44_OVERRIDE;

    /// creates a dimmer group, addressed via a group address rather than single bus address
    /// @note initially, the group is empty. addDaliBusDevice must be used to add devices
    /// @param aGroupNo the group number for this group
    DaliBusDeviceGroup(DaliVdc &aDaliVdc, uint8_t aGroupNo);

    /// add a DALI bus device to the group. This will check if the device in question already is configured
    /// correctly for the group, and if not, device will be made member of the group
    /// @param aDaliBusDevice a completely scanned DALI bus device having a valid dSUID
    /// @note the device passed will be checked to be a member of the specified group, and will be reprogrammed if it isn't yet.
    void addDaliBusDevice(DaliBusDevicePtr aDaliBusDevice);

    /// derive the dSUID from mix of dSUIDs of single bus devices
    virtual void deriveDsUid() P44_OVERRIDE;

    /// check if bus device represents a DALI group
    /// @return true if group
    virtual bool isGrouped() P44_OVERRIDE { return true; }

    /// show description
    virtual string description() P44_OVERRIDE;

  protected:

    /// DALI address to use for querying brightness etc.
    /// @return DALI address
    /// @note reading info from single master dimmer, not group
    virtual uint8_t addressForQuery() P44_OVERRIDE { return groupMaster; };

    /// check if this device belongs to a particular DALI address
    virtual bool belongsToShortAddr(DaliAddress aDaliAddres) const P44_OVERRIDE;

    /// add bus device level info to summary information
    /// @param aInfo ApiValue object, info fields will be added to it
    virtual void daliBusDeviceSummary(ApiValuePtr aInfo) const P44_OVERRIDE;

  private:

    void initNextGroupMember(StatusCB aCompletedCB, DaliComm::ShortAddressList::iterator aNextMember);
    void groupMembershipResponse(StatusCB aCompletedCB, DaliComm::ShortAddressList::iterator aNextMember, uint16_t aGroups, ErrorPtr aError);

  };


  /// types of DALI dS devices
  typedef enum {
    dalidevice_single, ///< single DALI dimmer, single channel
    dalidevice_group, ///< group of DALI dimmers, single channel (or DT8)
    dalidevice_composite ///< multichannel color/tunable white device consisting of multiple dimmers or groups
  } DaliDeviceTypes;


  /// base class for all DALI output/dimmer devices
  class DaliOutputDevice : public Device
  {
    typedef Device inherited;
    friend class DaliDeviceCollector;

  protected:

    MLTicket transitionTicket; ///< transition timing ticket

  public:

    DaliOutputDevice(DaliVdc *aVdcP);

    /// @return Vendor name for display purposes
    virtual string vendorName() P44_OVERRIDE { return ""; }; // Prevent displaying vdc vendor for devices

    /// @return type of DALI device
    virtual DaliDeviceTypes daliTechnicalType() const = 0;

    /// get typed container reference
    DaliVdc &daliVdc();

    /// device level API methods (p44 specific, JSON only, for configuring grouped devices)
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// apply all pending channel value updates to the device's hardware
    /// @note this is the only routine that should trigger actual changes in output values. It must consult all of the device's
    ///   ChannelBehaviours and check isChannelUpdatePending(), and send new values to the device hardware. After successfully
    ///   updating the device hardware, channelValueApplied() must be called on the channels that had isChannelUpdatePending().
    /// @param aCompletedCB if not NULL, must be called when values are applied
    /// @param aForDimming hint for implementations to optimize dimming, indicating that change is only an increment/decrement
    ///   in a single channel (and not switching between color modes etc.)
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming) P44_FINAL;

    /// check if aDaliAddress is a bus device of this device, and if so, return info about it
    /// @param aDaliAddress the dali address to check
    /// @param aInfo ApiValue object, info fields will be added to it
    /// @return true if aDaliAddress is a bus device of this device
    virtual bool daliBusDeviceSummary(DaliAddress aDaliAddress, ApiValuePtr aInfo) const = 0;

    /// return information about all bus devices related to this device
    /// @param aInfo ApiValue object, info fields will be added to it
    virtual void daliDeviceSummary(ApiValuePtr aInfo) const = 0;

  protected:

    /// save current brightness as default for DALI dimmer to use after powerup and at failure
    virtual void saveAsDefaultBrightness() = 0;

    /// set transition time for subsequent brightness changes
    /// @param aTransitionTime time for transition
    virtual void setTransitionTime(MLMicroSeconds aTransitionTime) = 0;

    /// internal implementation for running even very slow light transitions
    virtual void applyChannelValueSteps(bool aForDimming, bool aWithColor, double aStepSize) = 0;

    /// add dS device level context summary (but no *bus* device level info)
    virtual void daliDeviceContextSummary(ApiValuePtr aInfo) const;

  };



  class DaliSingleControllerDevice : public DaliOutputDevice
  {
    typedef DaliOutputDevice inherited;
    friend class DaliDeviceCollector;

  public:

    DaliBusDevicePtr daliController; ///< the actual DALI bus device controlling the entire device

    DaliSingleControllerDevice(DaliVdc *aVdcP);

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// @return technical type of DALI device
    virtual DaliDeviceTypes daliTechnicalType() const P44_OVERRIDE { return daliController && daliController->isGrouped() ? dalidevice_group : dalidevice_single; }

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return daliTechnicalType()==dalidevice_group ? "dali_group" : "dali_single"; };

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;


    /// @name interaction with subclasses, actually representing physical I/O
    /// @{

    /// initializes the physical device for being used
    /// @param aFactoryReset if set, the device will be inititalized as thoroughly as possible (factory reset, default settings etc.)
    /// @note this is called before interaction with dS system starts (usually just after collecting devices)
    /// @note implementation should call inherited when complete, so superclasses could chain further activity
    virtual void initializeDevice(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    /// check presence of this addressable
    /// @param aPresenceResultHandler will be called to report presence status
    virtual void checkPresence(PresenceCB aPresenceResultHandler) P44_OVERRIDE;

    /// disconnect device. For DALI, we'll check if the device is still present on the bus, and only if not
    /// we allow disconnection
    /// @param aForgetParams if set, not only the connection to the device is removed, but also all parameters related to it
    ///   such that in case the same device is re-connected later, it will not use previous configuration settings, but defaults.
    /// @param aDisconnectResultHandler will be called to report true if device could be disconnected,
    ///   false in case it is certain that the device is still connected to this and only this vDC
    virtual void disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler) P44_OVERRIDE;

    /// start or stop dimming (optimized DALI version)
    /// @param aChannel the channel to start or stop dimming for
    /// @param aDimMode according to VdcDimMode: 1=start dimming up, -1=start dimming down, 0=stop dimming
    /// @param aDoApply only if set to true, dimming must be started/stopped in hardware. Otherwise
    ///   the actual operation has been done already by another means (such as native group/scene call on the harware level)
    ///   and must NOT be repeated.
    ///   However, in all cases internal state must be updated to reflect the finalized operation
    /// @note this method can rely on a clean start-stop sequence in all cases, which means it will be called once to
    ///   start a dimming process, and once again to stop it. There are no repeated start commands or missing stops - Device
    ///   class makes sure these cases (which may occur at the vDC API level) are not passed on to dimChannel()
    virtual void dimChannel(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode, bool aDoApply) P44_OVERRIDE;

    /// is called when scene values are applied, either via applyChannelValues or via optimized calls
    /// @param aDoneCB called when all tasks following applying the scene are done
    /// @param aScene the scene that was called
    /// @param aIndirectly if true, applyChannelValues was NOT used to apply the scene, but STILL some other mechanism
    ///   such as optimized group call has changed outputs. device implementation might need to sync back hardware state in this case.
    /// @note aIndirectly is NOT set when there was no output change at all and applyChannelValues() was therefore not called.
    /// @note if derived in subclass, base class' implementation should normally be called as this triggers scene actions
    virtual void sceneValuesApplied(SimpleCB aDoneCB, DsScenePtr aScene, bool aIndirectly) P44_FINAL;

    /// @}

    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    virtual string hardwareGUID() P44_OVERRIDE;

    /// @return model GUID in URN format to identify hardware model of device as uniquely as possible
    virtual string hardwareModelGUID() P44_OVERRIDE;

    /// @return OEM GUID in URN format to identify OEM hardware INSTANCE as uniquely as possible
    virtual string oemGUID() P44_OVERRIDE;

    /// @return OEM GUID in URN format to identify OEM hardware MODEL as uniquely as possible
    virtual string oemModelGUID() P44_OVERRIDE;

    /// Get an indication how good/critical the operation state of the device is (such as lamp failure or reachability on the bus)
    /// @return 0..100 with 0=out of operation, 100=fully operating, <0 = unknown
    virtual int opStateLevel() P44_OVERRIDE;

    /// Get short text to describe the operation state (such as lamp failure or reachability on the bus)
    /// @return string, really short, intended to be shown as a narrow column in a device/vdc list
    virtual string getOpStateText() P44_OVERRIDE;


    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// Get extra info (plan44 specific) to describe the addressable in more detail
    /// @return string, single line extra info describing aspects of the device not visible elsewhere
    virtual string getExtraInfo() P44_OVERRIDE;

    /// @}

    /// derive the dSUID from collected device info
    void deriveDsUid();

    /// check if aDaliAddress is a bus device of this device, and if so, return info about it
    /// @param aDaliAddress the dali address to check
    /// @param aInfo ApiValue object, info fields will be added to it
    /// @return true if aDaliAddress is a bus device of this device
    virtual bool daliBusDeviceSummary(DaliAddress aDaliAddress, ApiValuePtr aInfo) const P44_OVERRIDE;

    /// return information about all bus devices related to this device
    /// @param aInfo ApiValue object, info fields will be added to it
    virtual void daliDeviceSummary(ApiValuePtr aInfo) const P44_OVERRIDE;

  protected:

    /// save current brightness as default for DALI dimmer to use after powerup and at failure
    virtual void saveAsDefaultBrightness() P44_OVERRIDE;

    /// set transition time for subsequent brightness changes
    /// @param aTransitionTime time for transition
    virtual void setTransitionTime(MLMicroSeconds aTransitionTime) P44_OVERRIDE;

    /// internal implementation for running even very slow light transitions
    virtual void applyChannelValueSteps(bool aForDimming, bool aWithColor, double aStepSize) P44_OVERRIDE;

    /// let device implementation prepare for (and possibly reject) optimized set
    /// @param aDeliveryState can be inspected to see the scene or dim parameters
    ///   (optimizedType, actionParam, actionVariant are already set)
    /// @return true if device is ok with being part of optimized set. If false is returned, the call will be
    ///    executed without optimisation
    virtual bool prepareForOptimizedSet(NotificationDeliveryStatePtr aDeliveryState) P44_OVERRIDE;

  private:

    void processUpdatedParams(ErrorPtr aError);
    void outputChangeEndStateRetrieved(ErrorPtr aError);
    void daliControllerSynced(StatusCB aCompletedCB, bool aFactoryReset, ErrorPtr aError);
    void checkPresenceResponse(PresenceCB aPresenceResultHandler);
    void disconnectableHandler(bool aForgetParams, DisconnectCB aDisconnectResultHandler, bool aPresent);

  };


  class DaliCompositeDevice : public DaliOutputDevice
  {
    typedef DaliOutputDevice inherited;
    friend class DaliDeviceCollector;
    friend class DaliVdc;

    uint32_t collectionID; ///< the ID of the collection that created this composite device

  public:


    enum {
      dimmer_red,
      dimmer_green,
      dimmer_blue,
      dimmer_white, // (cold) white
      dimmer_amber, // amber or warm white
      numDimmers
    };
    typedef uint8_t DimmerIndex;

    DaliBusDevicePtr dimmers[numDimmers];

    DaliCompositeDevice(DaliVdc *aVdcP);

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// @return type of DALI device
    virtual DaliDeviceTypes daliTechnicalType() const P44_OVERRIDE { return dalidevice_composite; }

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "dali_rgbw"; };

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;


    /// @name interaction with subclasses, actually representing physical I/O
    /// @{

    /// initializes the physical device for being used
    /// @param aFactoryReset if set, the device will be inititalized as thoroughly as possible (factory reset, default settings etc.)
    /// @note this is called before interaction with dS system starts (usually just after collecting devices)
    /// @note implementation should call inherited when complete, so superclasses could chain further activity
    virtual void initializeDevice(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    /// add a dimmer
    /// @param aDimmerBusDevice the DALI dimmer to add
    /// @param aDimmerType the type of dimmer (which channel: R,G,B,W)
    /// @return true if dimmer of that type could be added
    bool addDimmer(DaliBusDevicePtr aDimmerBusDevice, string aDimmerType);


    /// check presence of this addressable
    /// @param aPresenceResultHandler will be called to report presence status
    virtual void checkPresence(PresenceCB aPresenceResultHandler) P44_OVERRIDE;

    /// disconnect device. For DALI, we'll check if the device is still present on the bus, and only if not
    /// we allow disconnection
    /// @param aForgetParams if set, not only the connection to the device is removed, but also all parameters related to it
    ///   such that in case the same device is re-connected later, it will not use previous configuration settings, but defaults.
    /// @param aDisconnectResultHandler will be called to report true if device could be disconnected,
    ///   false in case it is certain that the device is still connected to this and only this vDC
    virtual void disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler) P44_OVERRIDE;

    /// @}

    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE { return "DALI composite color light"; }

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    virtual string hardwareGUID() P44_OVERRIDE;

    /// @return model GUID in URN format to identify model of device as uniquely as possible
    virtual string hardwareModelGUID() P44_OVERRIDE;

    /// @return OEM GUID in URN format to identify OEM hardware INSTANCE as uniquely as possible
    virtual string oemGUID() P44_OVERRIDE;

    /// @return OEM GUID in URN format to identify OEM hardware MODEL as uniquely as possible
    virtual string oemModelGUID() P44_OVERRIDE;

    /// Get an indication how good/critical the operation state of the device is (such as lamp failure or reachability on the bus)
    /// @return 0..100 with 0=out of operation, 100=fully operating, <0 = unknown
    virtual int opStateLevel() P44_OVERRIDE;

    /// Get short text to describe the operation state (such as lamp failure or reachability on the bus)
    /// @return string, really short, intended to be shown as a narrow column in a device/vdc list
    virtual string getOpStateText() P44_OVERRIDE;

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// Get extra info (plan44 specific) to describe the addressable in more detail
    /// @return string, single line extra info describing aspects of the device not visible elsewhere
    virtual string getExtraInfo() P44_OVERRIDE;

    /// @}

    /// derive the dSUID from collected device info
    void deriveDsUid();

    /// check if aDaliAddress is a bus device of this device, and if so, return info about it
    /// @param aDaliAddress the dali address to check
    /// @param aInfo ApiValue object, info fields will be added to it
    /// @return true if aDaliAddress is a bus device of this device
    virtual bool daliBusDeviceSummary(DaliAddress aDaliAddress, ApiValuePtr aInfo) const P44_OVERRIDE;

    /// return information about all bus devices related to this device
    /// @param aInfo ApiValue object, info fields will be added to it
    virtual void daliDeviceSummary(ApiValuePtr aInfo) const P44_OVERRIDE;

  protected:

    /// save current brightness as default for DALI dimmer to use after powerup and at failure
    virtual void saveAsDefaultBrightness() P44_OVERRIDE;

    /// set transition time for subsequent brightness changes
    /// @param aTransitionTime time for transition
    virtual void setTransitionTime(MLMicroSeconds aTransitionTime) P44_OVERRIDE;

    /// internal implementation for running even very slow light transitions
    virtual void applyChannelValueSteps(bool aForDimming, bool aWithColor, double aStepSize) P44_OVERRIDE;

    /// let device implementation prepare for (and possibly reject) optimized set
    /// @param aDeliveryState can be inspected to see the scene or dim parameters
    /// @return true if device is ok with being part of optimized set. If false is returned, the call will be
    ///    executed without optimisation
    virtual bool prepareForOptimizedSet(NotificationDeliveryStatePtr aDeliveryState) P44_OVERRIDE { return false; /* for now: no optimisation for composite devices */}

  private:

    void updateNextDimmer(StatusCB aCompletedCB, bool aFactoryReset, DimmerIndex aDimmerIndex, ErrorPtr aError);
    DaliBusDevicePtr firstBusDevice();

    void checkPresenceResponse(PresenceCB aPresenceResultHandler, DaliBusDevicePtr aDimmer);
    void disconnectableHandler(bool aForgetParams, DisconnectCB aDisconnectResultHandler, bool aPresent);
    
  };


  #if ENABLE_DALI_INPUTS

  // MARK: - DALI input device

  /// base class for all DALI input devices
  class DaliInputDevice : public Device
  {
    typedef Device inherited;
    friend class DaliVdc;

    long long daliInputDeviceRowID; ///< the ROWID this device was created from (0=none)

    DaliAddress baseAddress;
    int numAddresses;

    typedef enum {
      input_button,
      input_rocker,
      input_motion,
      input_illumination,
      input_bistable,
      input_pulse
    } DaliInputType;

    DaliInputType inputType;

    MLTicket releaseTicket;

  public:

    DaliInputDevice(DaliVdc *aVdcP, const string aDaliInputConfig, DaliAddress aBaseAddress);

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// get typed container reference
    DaliVdc &daliVdc();

    /// check if device can be disconnected by software (i.e. Web-UI)
    /// @return true if device might be disconnectable by the user via software (i.e. web UI)
    /// @note devices returning true here might still refuse disconnection on a case by case basis when
    ///   operational state does not allow disconnection.
    /// @note devices returning false here might still be disconnectable using disconnect() triggered
    ///   by vDC API "remove" method.
    virtual bool isSoftwareDisconnectable() P44_OVERRIDE;

    /// disconnect device. For static device, this means removing the config from the container's DB. Note that command line
    /// static devices cannot be disconnected.
    /// @param aForgetParams if set, not only the connection to the device is removed, but also all parameters related to it
    ///   such that in case the same device is re-connected later, it will not use previous configuration settings, but defaults.
    /// @param aDisconnectResultHandler will be called to report true if device could be disconnected,
    ///   false in case it is certain that the device is still connected to this and only this vDC
    virtual void disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler) P44_OVERRIDE;

    /// free all control gear from responding to any of the bus addresses used by this input device
    void freeAddresses();

    /// @name identification of the addressable entity
    /// @{

    /// device type identifier
    /// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "dali_input"; };

    /// @return Vendor name for display purposes
    virtual string vendorName() P44_OVERRIDE { return ""; }; // Prevent displaying vdc vendor for devices

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// Get extra info (plan44 specific) to describe the addressable in more detail
    /// @return string, single line extra info describing aspects of the device not visible elsewhere
    virtual string getExtraInfo() P44_OVERRIDE;

    /// @}

    /// derive the dSUID from collected device info
    void deriveDsUid();


  protected:

    /// is called to process an incoming DALI event
    /// @param aEvent dali bridge event code
    /// @param aData1 event data 1
    /// @param aData1 event data 2
    /// @return true if event has been fully processed by the device
    bool checkDaliEvent(uint8_t aEvent, uint8_t aData1, uint8_t aData2);

  private:

    void buttonReleased(int aButtonNo);
    void inputReleased(int aInputNo);

  };

  #endif // ENABLE_DALI_INPUTS


} // namespace p44

#endif // ENABLE_DALI
#endif // __p44vdc__dalidevice__
