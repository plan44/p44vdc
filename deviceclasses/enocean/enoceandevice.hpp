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

#ifndef __p44vdc__enoceandevice__
#define __p44vdc__enoceandevice__

#include "device.hpp"

#if ENABLE_ENOCEAN

#include "enoceancomm.hpp"


using namespace std;

namespace p44 {

  typedef uint64_t EnoceanDeviceID;

  class EnoceanVdc;
  class EnoceanChannelHandler;
  class EnoceanDevice;

  /// EnOcean subdevice
  typedef uint8_t EnoceanSubDevice;


  // per-addressable logging macros
  #define HLOG(lvl, ...) { if (LOGENABLED(lvl)) { device.logAddressable(lvl, ##__VA_ARGS__); } }
  #if FOCUSLOGGING
  #define HFOCUSLOG(...) { HLOG(FOCUSLOGLEVEL, ##__VA_ARGS__); }
  #else
  #define HFOCUSLOG(...)
  #endif


  typedef boost::intrusive_ptr<EnoceanChannelHandler> EnoceanChannelHandlerPtr;

  #define TIMEOUT_FACTOR_FOR_INACTIVE 4

  /// single EnOcean device channel, abstract class
  class EnoceanChannelHandler : public P44Obj
  {
    typedef P44Obj inherited;

    friend class EnoceanDevice;

  protected:

    EnoceanDevice &device; ///< the associated enocean device

    /// private constructor
    /// @note create new channels using factory static methods of specialized subclasses
    EnoceanChannelHandler(EnoceanDevice &aDevice);

    bool lowBat; ///< this can be set by handlers to indicate low battery status (will be reported via opStateLevel/opStateText

  public:

    DsBehaviourPtr behaviour; ///< the associated behaviour
    int8_t dsChannelIndex; ///< for outputs, the dS channel index
    EnoceanChannel channel; ///< channel number

    /// handle radio packet related to this channel
    /// @param aEsp3PacketPtr the radio packet to analyze and extract channel related information
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr) = 0;

    /// collect data for outgoing message from this channel
    /// @param aEsp3PacketPtr must be set to a suitable packet if it is empty, or packet data must be augmented with
    ///   channel's data when packet already exists
    /// @note non-outputs and outputs that directly control hardware via applyChannelValues() at device level
    ///   will usually do nothing in this method
    virtual void collectOutgoingMessageData(Esp3PacketPtr &aEsp3PacketPtr) { /* NOP */ };

    /// check if channel is alive (for regularily sending sensors: has received life sign within timeout window)
    virtual bool isAlive() { return true; } // assume alive by default

    /// Get an indication how good/critical the operation state of this channel is (usually: battery level indicator)
    /// @return 0..100 with 0=out of operation, 100=fully operating, <0 = unknown
    virtual int opStateLevel();

    /// Get short text to describe the operation state (such as radio RSSI, critical battery level, etc.)
    /// @return string, really short, intended to be shown as a narrow column in a device/vdc list
    virtual string getOpStateText();

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc() = 0;

  };


  /// profile variant entry
  typedef struct {
    int profileGroup; ///< zero to terminate list or group number (interchangeable profiles must have same group number)
    EnoceanProfile eep; ///< the EEP
    EnoceanSubDevice subDeviceIndices; ///< number of subdevice indices this profile affects, 0 = all
    const char *description; ///< description of profile variant for UI
    const char *configId; ///< well-known string ID for the variant, NULL when variant is identified by eep
  } ProfileVariantEntry;

  typedef vector<EnoceanChannelHandlerPtr> EnoceanChannelHandlerVector;

  typedef boost::intrusive_ptr<EnoceanDevice> EnoceanDevicePtr;

  /// digitalstrom device representing one or multiple EnOcean device channels
  class EnoceanDevice : public Device
  {
    typedef Device inherited;

    friend class EnoceanChannelHandler;

    EnoceanAddress enoceanAddress; ///< the enocean device address
    EnoceanProfile eeProfile; ///< the EEP (RORG/FUNC/TYPE)
    EnoceanManufacturer eeManufacturer; ///< the manufacturer ID
    EnoceanSubDevice subDevice; ///< the subdevice number (relevant when one physical EnOcean device is represented as multiple vdSDs)

    string eeFunctionDesc; ///< short functional description (like: button, windowhandle, sensor...)
    const char *iconBaseName; ///< icon base name
    bool groupColoredIcon; ///< if set, use color suffix with icon base name

    EnoceanChannelHandlerVector channels; ///< the channel handlers for this device

    bool alwaysUpdateable; ///< if set, device updates are sent immediately, otherwise, updates are only sent as response to a device message
    bool updateAtEveryReceive; ///< if set, current values are sent to the device whenever a message is received, even if output state has not changed
    bool pendingDeviceUpdate; ///< set when update to the device is pending

    MLMicroSeconds lastPacketTime; ///< time when device received last packet (or device was created)
    int16_t lastRSSI; ///< RSSI of last packet received (including learn telegram)
    uint8_t lastRepeaterCount; ///< last packet's repeater count (including learn telegram)

    #if ENABLE_ENOCEAN_SECURE
    EnOceanSecurityPtr securityInfo; ///< security info. If this is set, the device must NOT respond to non-secure packets!
    #endif

  public:

    /// constructor, create device in container
    EnoceanDevice(EnoceanVdc *aVdcP);

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "enocean"; };

    /// check if device can be disconnected by software (i.e. Web-UI)
    /// @return true if device might be disconnectable by the user via software (i.e. web UI)
    /// @note EnOcean devices can be removed not only via unlearning, but also via Web-UI if needed
    virtual bool isSoftwareDisconnectable() P44_OVERRIDE { return true; };

    /// return time when last packet was received for this device
    /// @return time when last packet was received or Never
    MLMicroSeconds getLastPacketTime() { return lastPacketTime; };

    /// check presence of this addressable
    /// @param aPresenceResultHandler will be called to report presence status
    virtual void checkPresence(PresenceCB aPresenceResultHandler) P44_OVERRIDE;

    /// get typed container reference
    EnoceanVdc &getEnoceanVdc();

    /// factory: (re-)create logical device from address|channel|profile|manufacturer tuple
    /// @param aAddress 32bit enocean device address/ID
    /// @param aSubDeviceIndex subdevice number (multiple logical EnoceanDevices might exists for the same EnoceanAddress)
    ///   upon exit, this will be incremented by the number of subdevice indices the device occupies in the index space
    ///   (usually 1, but some profiles might reserve extra space, such as up/down buttons)
    /// @param aEEProfile VARIANT/RORG/FUNC/TYPE EEP profile number
    /// @param aEEManufacturer manufacturer number (or manufacturer_unknown)
    /// @param aSendTeachInResponse if this is set, a teach-in response will be sent for profiles that need one
    ///   (This is set to false when re-creating logical devices from DB)
    static EnoceanDevicePtr newDevice(
      EnoceanVdc *aVdcP,
      EnoceanAddress aAddress,
      EnoceanSubDevice &aSubDeviceIndex,
      EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
      bool aSendTeachInResponse
    );

    /// add channel handler and register behaviour
    /// @param aChannelHandler a handler for a channel (including a suitable behaviour)
    void addChannelHandler(EnoceanChannelHandlerPtr aChannelHandler);

    /// disconnect device. For EnOcean, this means breaking the pairing (learn-in) with the device
    /// @param aForgetParams if set, not only the connection to the device is removed, but also all parameters related to it
    ///   such that in case the same device is re-connected later, it will not use previous configuration settings, but defaults.
    /// @param aDisconnectResultHandler will be called to report true if device could be disconnected,
    ///   false in case it is certain that the device is still connected to this and only this vDC
    virtual void disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler) P44_OVERRIDE;

    /// apply all pending channel value updates to the device's hardware
    /// @note this is the only routine that should trigger actual changes in output values. It must consult all of the device's
    ///   ChannelBehaviours and check isChannelUpdatePending(), and send new values to the device hardware. After successfully
    ///   updating the device hardware, channelValueApplied() must be called on the channels that had isChannelUpdatePending().
    /// @param aDoneCB if not NULL, must be called when values are applied
    /// @param aForDimming hint for implementations to optimize dimming, indicating that change is only an increment/decrement
    ///   in a single channel (and not switching between color modes etc.)
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming) P44_OVERRIDE;

    /// factory: create appropriate logical devices for a given EEP
    /// @param aVdcP the EnoceanVdc to create the devices in
    /// @param aAddress the EnOcean address
    /// @param aProfile the EPP
    /// @param aManufacturer the manufacturer code
    /// @param aSmartAck set if creating devices as part of a smart-ack learn-in
    /// @param aLearnPacket if this is a learn-in process, the learn packet (NULL if devices are recreated from DB)
    /// @param aSecurityInfo the associated security info. If set, the created devices will become secure devices
    //    (and will not receive unencrypted radio packets)
    /// @return number of devices created
    static int createDevicesFromEEP(
      EnoceanVdc *aVdcP,
      EnoceanAddress aAddress,
      EnoceanProfile aProfile,
      EnoceanManufacturer aManufacturer,
      bool aSmartAck,
      Esp3PacketPtr aLearnPacket,
      EnOceanSecurityPtr aSecurityInfo
    );
    

    /// set the enocean address identifying the device
    /// @param aAddress 32bit enocean device address/ID
    /// @param aChannel channel number (multiple logical EnoceanDevices might exists for the same EnoceanAddress)
    virtual void setAddressingInfo(EnoceanAddress aAddress, EnoceanChannel aChannel);

    /// set EEP information
    /// @param aEEProfile VARIANT/RORG/FUNC/TYPE EEP profile number
    /// @param aEEManufacturer manufacturer number (or manufacturer_unknown)
    virtual void setEEPInfo(EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer);

    /// set the icon info for the enocean device
    void setIconInfo(const char *aIconBaseName, bool aGroupColored) { iconBaseName = aIconBaseName; groupColoredIcon = aGroupColored; };

    /// set short functional description for this device (explaining the EEP in short, like "button", "sensor", "window handle")
    /// @param aString the description string
    void setFunctionDesc(string aString) { eeFunctionDesc = aString; };

    /// device and channel handler implementations can call this to enable immediate sending of output changes for the device
    /// (otherwise, output changes are sent only withing 1sec after receiving a message from the device)
    void setAlwaysUpdateable(bool aAlwaysUpdateable = true) { alwaysUpdateable = aAlwaysUpdateable; };

    /// device and channel handler implementations can call this to enable immediate sending of output changes for the device
    /// (otherwise, output changes are sent only withing 1sec after receiving a message from the device)
    void setUpdateAtEveryReceive(bool aUpdateAtEveryReceive = true) { updateAtEveryReceive = aUpdateAtEveryReceive; };


    /// get the enocean address identifying the hardware that contains this logical device
    /// @return EnOcean device ID/address
    /// Note: for some actors, this might also be the sender address (in id base range of the modem module)
    EnoceanAddress getAddress();

    /// get the enocean subdevice number that identifies this logical device among other logical devices in the same
    ///   physical EnOcean device (having the same EnOcean deviceID/address)
    /// @return EnOcean device ID/address
    EnoceanSubDevice getSubDevice();

    /// @return VARIANT/RORG/FUNC/TYPE EEP profile number with optional variant (MSB of 32bit value)
    EnoceanProfile getEEProfile();

    /// @return manufacturer code
    EnoceanManufacturer getEEManufacturer();

    /// update device's radio metrics (last packet time, RSSI, repeater count) from packet
    /// @param aEsp3PacketPtr packet
    void updateRadioMetrics(Esp3PacketPtr aEsp3PacketPtr);

    /// device specific radio packet handling
    /// @note base class implementation passes packet to all registered channels
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr);

    /// signal that we need an outgoing packet at next possible occasion
    /// @note will cause output data from channel handlers to be collected
    /// @note can be called from channel handlers to trigger another update after the current one
    void needOutgoingUpdate();

    /// send outgoing packet updating outputs and device settings
    /// @note do not call this directly, use needOutgoingUpdate() instead to make
    ///   sure outgoing package is sent at appropriate time for device (e.g. just after receiving for battery powered devices)
    void sendOutgoingUpdate();

    /// device specific teach in response
    /// @note will be called from newDevice() when created device needs a teach-in response
    virtual void sendTeachInResponse() { /* NOP in base class */ };

    /// mark base offsets in use by this device
    /// @param aUsedOffsetsMap must be passed a string with 128 chars of '0' or '1'.
    virtual void markUsedBaseOffsets(string &aUsedOffsetsMap) { /* NOP in base class */ };

    #if ENABLE_ENOCEAN_SECURE

    /// set security info
    /// @param aSecurityInfo set secure info for this device
    /// @note if security info is set, the device will not get any non-secure radio packets any more
    void setSecurity(EnOceanSecurityPtr aSecurityInfo) { securityInfo = aSecurityInfo; };

    /// check for secure device
    /// @return true if this device has security info (i.e. MUST NOT accept any insecure packets)
    bool secureDevice() { return securityInfo!=NULL; }

    #endif



    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;


    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    virtual string hardwareGUID() P44_OVERRIDE;

    /// @return model GUID in URN format to identify model of hardware device as uniquely as possible
    virtual string hardwareModelGUID() P44_OVERRIDE;

    /// @return Vendor ID in URN format to identify vendor as uniquely as possible
    virtual string vendorId() P44_OVERRIDE;

    /// @return Vendor name if known
    virtual string vendorName() P44_OVERRIDE;

    /// Get an indication how good/critical the operation state of the device is (such as radio strenght, battery level)
    /// @return 0..100 with 0=out of operation, 100=fully operating, <0 = unknown
    virtual int opStateLevel() P44_OVERRIDE;

    /// Get short text to describe the operation state (such as radio RSSI, critical battery level, etc.)
    /// @return string, really short, intended to be shown as a narrow column in a device/vdc list
    virtual string getOpStateText() P44_OVERRIDE;


    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// @}


  protected:

    /// device configurations implementation
    virtual string getDeviceConfigurationId() P44_OVERRIDE;
    virtual ErrorPtr switchConfiguration(const string aConfigurationId) P44_OVERRIDE;
    virtual void getDeviceConfigurations(DeviceConfigurationsVector &aConfigurations, StatusCB aStatusCB) P44_OVERRIDE;

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

    /// derive dSUID from hardware address
    void deriveDsUid();

    /// switch EEP profile (or interpretation VARIANT thereof)
    /// @param aProfile enocean profile to switch this device to
    /// @note aProfile is not checked for being suitable for this type of device, this is done in setProfileVariant()
    void switchProfiles(const ProfileVariantEntry &aFromVariant, const ProfileVariantEntry &aToVariant);

    /// get table of profile variants
    /// @return NULL or pointer to a list of profile variants
    virtual const ProfileVariantEntry *profileVariantsTable() { return NULL; /* none in base class */ };

    /// get handler associated with a behaviour
    EnoceanChannelHandlerPtr channelForBehaviour(const DsBehaviour *aBehaviourP);

  private:

    bool isAlive();

  };
  
} // namespace p44

#endif // ENABLE_ENOCEAN
#endif // __p44vdc__enoceandevice__
