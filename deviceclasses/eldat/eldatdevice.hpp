//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2016-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__eldatdevice__
#define __p44vdc__eldatdevice__

#include "device.hpp"

#if ENABLE_ELDAT

#include "eldatcomm.hpp"


using namespace std;

namespace p44 {

  class EldatVdc;
  class EldatDevice;

  /// Eldat subdevice
  typedef uint8_t EldatSubDevice;


  typedef enum {
    eldat_rocker, // A-B or C-D rocker
    eldat_rocker_reversed, // B-A or D-C rocker
    eldat_button, // single A,B,C or D button
    eldat_motiondetector, // A=motion detected, B=motion no longer detected
    eldat_ABlight, // relay that goes on on A message, off on B message
    eldat_ABrelay, // relay that goes on on A message, off on B message
    eldat_windowcontact_onoff, // window contact that sends A/ON message when opened, B/OFF message when closed
    eldat_windowcontact_offon, // window contact that sends B/OFF message when opened, A/ON message when closed
    eldat_windowcontact_onoff_s, // window contact that sends A/ON message when opened, B/OFF message when closed, with status every 24h
    eldat_windowcontact_offon_s, // window contact that sends B/OFF message when opened, A/ON message when closed, with status every 24h
    eldat_windowhandle_onoff, // window handle that sends A/ON message when opened, B/OFF message when closed
    eldat_windowhandle_offon, // window handle that sends B/OFF message when opened, A/ON message when closed
    eldat_windowhandle_onoff_s, // window handle that sends A/ON message when opened, B/OFF message when closed, with status every 24h
    eldat_windowhandle_offon_s, // window handle that sends B/OFF message when opened, A/ON message when closed, with status every 24h
    eldat_unknown
  } EldatDeviceType;


  typedef boost::intrusive_ptr<EldatDevice> EldatDevicePtr;

  /// profile variant entry
  typedef struct {
    int typeGroup; ///< zero to terminate list, or group number (interchangeable types must have same group number)
    EldatDeviceType eldatDeviceType; ///< the device type
    EldatSubDevice subDeviceIndices; ///< number of subdevice indices this profile affects, 0 = all
    const char *description; ///< description of profile variant for UI
    const char *configId; ///< well-known string ID for the variant, NULL when variant is identified by eep
  } EldatTypeVariantEntry;



  /// Digital Strom device representing one or multiple Eldat device channels
  class EldatDevice : public Device
  {
    typedef Device inherited;

    MLMicroSeconds mLastMessageTime; ///< time when device received last message (or device was created)
    int16_t mLastRSSI; ///< RSSI of last packet received

  protected:

    EldatAddress mEldatAddress; ///< the eldat device address
    EldatDeviceType mEldatDeviceType; ///< the type of device
    EldatSubDevice mSubDevice; ///< the subdevice number (relevant when one physical Eldat device is represented as multiple vdSDs)

    string mFunctionDesc; ///< short functional description (like: button, windowhandle, sensor...)
    const char *mIconBaseName; ///< icon base name
    bool mGroupColoredIcon; ///< if set, use color suffix with icon base name


  public:

    /// constructor, create device in container
    /// @param aVdcP the Eldat vDC
    /// @param aDeviceType the device type
    EldatDevice(EldatVdc *aVdcP, EldatDeviceType aDeviceType);

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "eldat"; };

    /// check if device can be disconnected by software (i.e. Web-UI)
    /// @return true if device might be disconnectable by the user via software (i.e. web UI)
    /// @note Eldat devices can be removed not only via unlearning, but also via Web-UI if needed
    virtual bool isSoftwareDisconnectable() P44_OVERRIDE { return true; };



    /// return time when last packet was received for this device
    /// @return time when last packet was received or Never
    MLMicroSeconds getLastMessageTime() { return mLastMessageTime; };

    /// check presence of this addressable
    /// @param aPresenceResultHandler will be called to report presence status
    virtual void checkPresence(PresenceCB aPresenceResultHandler) P44_OVERRIDE;

    /// get typed container reference
    EldatVdc &getEldatVdc();

    /// factory: (re-)create logical device from address|channel|profile|manufacturer tuple
    /// @param aAddress 32bit Eldat device address/ID
    /// @param aSubDeviceIndex subdevice number (multiple logical EldatDevices might exists for the same address)
    ///   upon exit, this will be incremented by the number of subdevice indices the device occupies in the index space
    ///   (usually 1, but some profiles might reserve extra space, such as up/down buttons)
    /// @param aEldatDeviceType the device type
    /// @param aFirstSubDevice the subdevice index to be used for the first device
    static EldatDevicePtr newDevice(
      EldatVdc *aVdcP,
      EldatAddress aAddress,
      EldatSubDevice &aSubDeviceIndex,
      EldatDeviceType aEldatDeviceType,
      EldatSubDevice aFirstSubDevice
    );

    /// factory: create appropriate logical devices for a given device type
    /// @param aVdcP the EldatVdc to create the devices in
    /// @param aAddress the Eldat address
    /// @param aEldatDeviceType the device type
    /// @param aFirstSubDevice the subdevice index to be used for the first device
    /// @return number of devices created
    static int createDevicesFromType(
      EldatVdc *aVdcP,
      EldatAddress aAddress,
      EldatDeviceType aEldatDeviceType,
      EldatSubDevice aFirstSubDevice
    );

    /// set the address and subdevice index identifying the device
    /// @param aAddress Eldat device address
    /// @param aSubDeviceIndex subdevice number (multiple logical devices might exists for the same device address)
    virtual void setAddressingInfo(EldatAddress aAddress, EldatSubDevice aSubDeviceIndex);

    /// set the icon info for the eldat device
    void setIconInfo(const char *aIconBaseName, bool aGroupColored) { mIconBaseName = aIconBaseName; mGroupColoredIcon = aGroupColored; };

    /// set short functional description for this device (explaining the EEP in short, like "button", "sensor", "window handle")
    /// @param aString the description string
    void setFunctionDesc(string aString) { mFunctionDesc = aString; };


    /// disconnect device. For Eldat, this means breaking the pairing (learn-in) with the device
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

    /// message handling
    void handleMessage(uint8_t aMode, int aRSSI, string aData);

    /// device specific function handling
    virtual void handleFunction(EldatFunction aFunction) { /* NOP in base class */ };

    /// get the ELDAT sender address identifying the hardware that contains this logical device
    /// @return ELDAT device ID/address
    /// Note: for actors this is the modem's sender address that is used to operate the actor
    EldatAddress getAddress();

    /// get the Eldat subdevice number that identifies this logical device among other logical devices in the same
    ///   physical Eldat device (having the same Eldat deviceID/address)
    /// @return Eldat device ID/address
    EldatSubDevice getSubDevice();

    /// get the Eldat device type
    /// @return Eldat device type
    EldatDeviceType getEldatDeviceType() { return mEldatDeviceType; }

    /// mark send channels used by this device
    /// @param aUsedSendChannelsMap must be passed a string with 128 chars of '0' or '1'.
    virtual void markUsedSendChannels(string &aUsedSendChannelsMap) { /* NOP in base class */ };

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;

    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    virtual string hardwareGUID() P44_OVERRIDE;

//    /// @return model GUID in URN format to identify model of hardware device as uniquely as possible
//    virtual string hardwareModelGUID() P44_OVERRIDE;

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

    /// switch device type
    /// @param aProfile eldat profile to switch this device to
    /// @note aProfile is not checked for being suitable for this type of device, this is done in setProfileVariant()
    void switchTypes(const EldatTypeVariantEntry &aFromVariant, const EldatTypeVariantEntry &aToVariant);

    /// get table of profile variants
    /// @return NULL or pointer to a list of profile variants
    virtual const EldatTypeVariantEntry *deviceTypeVariantsTable();
  };


  class EldatButtonDevice : public EldatDevice
  {
    typedef EldatDevice inherited;

    MLTicket pressedTicket;

  public:

    /// constructor, create device in container
    /// @param aVdcP the Eldat vDC
    /// @param aDeviceType must be a button device type
    EldatButtonDevice(EldatVdc *aVdcP, EldatDeviceType aDeviceType);

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// device specific function handling
    virtual void handleFunction(EldatFunction aFunction) P44_OVERRIDE;

  private:

    void buttonReleased(int aButtonNo);

  };



  class EldatMotionDetector : public EldatDevice
  {
    typedef EldatDevice inherited;

  public:

    /// constructor, create device in container
    /// @param aVdcP the Eldat vDC
    EldatMotionDetector(EldatVdc *aVdcP);

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// device specific function handling
    virtual void handleFunction(EldatFunction aFunction) P44_OVERRIDE;
    
  };


  class EldatWindowContact : public EldatDevice
  {
    typedef EldatDevice inherited;

  public:

    /// constructor, create device in container
    /// @param aVdcP the Eldat vDC
    /// @param aOffOnType if set, this is a OFF/ON type
    /// @param aWithStatus if set, this device is expected to send status at least once every 24h
    EldatWindowContact(EldatVdc *aVdcP, bool aOffOnType, bool aWithStatus);

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// device specific function handling
    virtual void handleFunction(EldatFunction aFunction) P44_OVERRIDE;

  };



  class EldatWindowHandle : public EldatDevice
  {
    typedef EldatDevice inherited;

  public:

    /// constructor, create device in container
    /// @param aVdcP the Eldat vDC
    /// @param aOffOnType if set, this is a OFF/ON type
    /// @param aWithStatus if set, this device is expected to send status at least once every 24h
    EldatWindowHandle(EldatVdc *aVdcP, bool aOffOnType, bool aWithStatus);

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// device specific function handling
    virtual void handleFunction(EldatFunction aFunction) P44_OVERRIDE;

  };



  class EldatRemoteControlDevice : public EldatDevice
  {
    typedef EldatDevice inherited;

  public:

    /// constructor
    /// @param aVdcP the Eldat vDC
    EldatRemoteControlDevice(EldatVdc *aVdcP, EldatDeviceType aDeviceType);

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// device type identifier
    /// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "eldat_remotecontrol"; };

    /// @param aVariant -1 to just get number of available teach-in variants. 0..n to send teach-in signal;
    ///   some devices may have different teach-in signals (like: one for ON, one for OFF).
    /// @return number of teach-in signal variants the device can send
    /// @note will be called via UI for devices that need to be learned into remote actors
    virtual uint8_t teachInSignal(int8_t aVariant) P44_OVERRIDE;

    /// mark send channels used by this device
    /// @param aUsedSendChannelsMap must be passed a string with 128 chars of '0' or '1'.
    virtual void markUsedSendChannels(string &aUsedSendChannelsMap) P44_OVERRIDE;

    /// apply channel values
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming) P44_OVERRIDE;

  protected:

    /// utility function to send messages (using channel from address LSByte)
    /// @param aFunction: function A..D
    void sendFunction(EldatFunction aFunction);

  private:

    void sentFunction(string aAnswer, ErrorPtr aError);

  };



} // namespace p44

#endif // ENABLE_ELDAT
#endif // __p44vdc__eldatdevice__
