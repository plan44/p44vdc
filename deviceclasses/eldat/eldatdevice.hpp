//
//  Copyright (c) 2016-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
    eldat_unknown
  } EldatDeviceType;


  typedef boost::intrusive_ptr<EldatDevice> EldatDevicePtr;

  /// profile variant entry
  typedef struct {
    int typeGroup; ///< zero to terminate list, or group number (interchangeable types must have same group number)
    EldatDeviceType eldatDeviceType; ///< the device type
    EldatSubDevice subDeviceIndices; ///< number of subdevice indices this profile affects, 0 = all
    const char *description; ///< description of profile variant for UI
  } EldatTypeVariantEntry;



  /// digitalstrom device representing one or multiple Eldat device channels
  class EldatDevice : public Device
  {
    typedef Device inherited;

    MLMicroSeconds lastMessageTime; ///< time when device received last message (or device was created)
    int16_t lastRSSI; ///< RSSI of last packet received

  protected:

    EldatAddress eldatAddress; ///< the eldat device address
    EldatDeviceType eldatDeviceType; ///< the type of device
    EldatSubDevice subDevice; ///< the subdevice number (relevant when one physical Eldat device is represented as multiple vdSDs)

    string functionDesc; ///< short functional description (like: button, windowhandle, sensor...)
    const char *iconBaseName; ///< icon base name
    bool groupColoredIcon; ///< if set, use color suffix with icon base name


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
    MLMicroSeconds getLastMessageTime() { return lastMessageTime; };

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
    void setIconInfo(const char *aIconBaseName, bool aGroupColored) { iconBaseName = aIconBaseName; groupColoredIcon = aGroupColored; };

    /// set short functional description for this device (explaining the EEP in short, like "button", "sensor", "window handle")
    /// @param aString the description string
    void setFunctionDesc(string aString) { functionDesc = aString; };


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
    virtual void handleFunction(EldatFunction aFunction) = 0;

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
    EldatDeviceType getEldatDeviceType() { return eldatDeviceType; }

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;

    /// get type variants this device can have
    /// @param aApiObjectValue must be an object typed API value, will receive profile variants as type/description key/values
    /// @return true if device has variants
    bool getTypeVariants(ApiValuePtr aApiObjectValue);

    /// @param aType must be an Eldat device type
    /// @return true if type variant is valid and can be set
    bool setTypeVariant(EldatDeviceType aType);
    

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

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// @}


  protected:

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

    long pressedTicket;

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



} // namespace p44

#endif // ENABLE_ELDAT
#endif // __p44vdc__eldatdevice__
