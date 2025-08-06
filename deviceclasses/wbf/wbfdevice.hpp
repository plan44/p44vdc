//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__wbfdevice__
#define __p44vdc__wbfdevice__

#include "device.hpp"

#if ENABLE_WBF

#include "colorlightbehaviour.hpp"
#include "shadowbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "buttonbehaviour.hpp"
#include "jsonobject.hpp"

using namespace std;

namespace p44 {

  class WbfVdc;
  class WbfDevice;
  class WbfComm;


  typedef struct {
    const char* wbfType;
    VdcSensorType vdcSensorType;
    DsBinaryInputType dsInputType;
    VdcUsageHint usageHint;
    double min;
    double max;
    double resolution;
    DsClass colorclass;
    DsGroup group;
  } WbfSensorTypeInfo;


  typedef std::map<int, DsBehaviourPtr> PartIdToBehaviourMap;


  typedef boost::intrusive_ptr<WbfDevice> WbfDevicePtr;
  class WbfDevice : public Device
  {
    typedef Device inherited;
    friend class WbfVdc;

    // information from the device itself
    string mWbfId; ///< the ID of the entire device
    int mLoadId; ///< the load ID, -1 if none
    uint8_t mSubDeviceIndex; ///< subdevice index when creating multiple p44 devices from one wbf device
    string mWbfCommNames; ///< the commerical name(s) of the device's module(s)
    string mWbfCommRefs; ///< the commercial reference(s) of the device's module(s)
    string mSerialNos; ///< the serial no (or c/a serials) of the device's module(s)
    MLMicroSeconds mLastSeen; ///< when seen last time
    bool mHasWhiteChannel; ///< set when connected light is RGBW (vs. only RGB)
    MLTicket mIdentifyTicket;

    PartIdToBehaviourMap mPendingInputMappings; ///< temporary input mappings to be applied at initializeDevice()

  public:

    /// @param aDevDesc overall device descriptor which might be shared among more than one device if it has multiple outputs
    /// @param aOutDesc output descriptor if this device instance should have an output
    /// @param aInputsArr array of input descriptors that are available in the overall device. Implementation must
    ///   pick some or all of them, AND DELETE those picked from the array. This allows for matching buttons with
    ///   the corresponding outputs
    /// @param aInputsUsed number of inputs actually used for this device. Can be 0 when no usable/mappable input is left in aInputsArr
    WbfDevice(WbfVdc *aVdcP, uint8_t aSubdeviceIndex, JsonObjectPtr aDevDesc, JsonObjectPtr aOutDesc, JsonObjectPtr aInputsArr, int& aInputsUsed);
    virtual ~WbfDevice();

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "wbf"; };

    WbfVdc &wbfVdc();
    WbfComm &wbfComm();

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;

    /// set user assignable name
    /// @param aName new name of the wbf device
    /// @note will propagate the name to the wbf gateway
    //virtual void setName(const string &aName) P44_OVERRIDE;


    /// @name interaction with subclasses, actually representing physical I/O
    /// @{

    /// @return true if the addressable has a way to actually identify to the user (apart from a log message)
    virtual bool canIdentifyToUser() P44_OVERRIDE;

    /// identify the device to the user
    /// @param aDuration if !=Never, this is how long the identification should be recognizable. If this is \<0, the identification should stop
    /// @note for lights, this is usually implemented as a blink operation, but depending on the device type,
    ///   this can be anything.
    /// @note device delegates this to the output behaviour (if any)
    virtual void identifyToUser(MLMicroSeconds aDuration) P44_OVERRIDE;

    /// initializes the physical device for being used
    /// @param aFactoryReset if set, the device will be inititalized as thoroughly as possible (factory reset, default settings etc.)
    /// @note this is called before interaction with dS system starts (usually just after collecting devices)
    /// @note implementation should call inherited when complete, so superclasses could chain further activity
    virtual void initializeDevice(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    /// check presence of this addressable
    /// @param aPresenceResultHandler will be called to report presence status
    virtual void checkPresence(PresenceCB aPresenceResultHandler) P44_OVERRIDE;

    /// apply all pending channel value updates to the device's hardware
    /// @param aDoneCB will called when values are applied
    /// @param aForDimming hint for implementations to optimize dimming, indicating that change is only an increment/decrement
    ///   in a single channel (and not switching between color modes etc.)
    /// @note this is the only routine that should trigger actual changes in output values. It must consult all of the device's
    ///   ChannelBehaviours and check isChannelUpdatePending(), and send new values to the device hardware. After successfully
    ///   updating the device hardware, channelValueApplied() must be called on the channels that had isChannelUpdatePending().
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming) P44_OVERRIDE;

    /// synchronize channel values by reading them back from the device's hardware (if possible)
    /// @param aDoneCB will be called when values are updated with actual hardware values
    /// @note this method is only called at startup and before saving scenes to make sure changes done to the outputs directly (e.g. using
    ///   a direct remote control for a lamp) are included. Just reading a channel state does not call this method.
    /// @note implementation must use channel's syncChannelValue() method
    virtual void syncChannelValues(SimpleCB aDoneCB) P44_OVERRIDE;

    /// @}

    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    virtual string hardwareGUID() P44_OVERRIDE;

    /// @return human readable version string
    virtual string modelVersion() const P44_OVERRIDE;

    /// @return Vendor name for display purposes
    virtual string vendorName() P44_OVERRIDE;

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

    /// Get an indication how good/critical the operation state of the device is (such as lamp failure or reachability on the bus)
    /// @return 0..100 with 0=out of operation, 100=fully operating, <0 = unknown
    virtual int opStateLevel() P44_OVERRIDE;

    /// Get short text to describe the operation state (such as lamp failure or reachability on the bus)
    /// @return string, really short, intended to be shown as a narrow column in a device/vdc list
    virtual string getOpStateText() P44_OVERRIDE;

    /// @}

  protected:

    /// handle updated sensor state
    void handleSensorState(JsonObjectPtr aState, DsBehaviourPtr aBehaviour);

    /// handle updated load state
    void handleLoadState(JsonObjectPtr aState, DsBehaviourPtr aBehaviour);

    /// handle button event
    void handleButtonCmd(JsonObjectPtr aCmd, DsBehaviourPtr aBehaviour);


  private:

    void deriveDsUid();

    void identifyBlink(int aRemainingBlinks);

    void targetStateApplied(SimpleCB aDoneCB, JsonObjectPtr aApplyStateResult, ErrorPtr aError);
    void loadStateReceived(SimpleCB aDoneCB, JsonObjectPtr aLoadStateResult, ErrorPtr aError);
    void deviceInfoReceived(PresenceCB aPresenceResultHandler, JsonObjectPtr aDeviceInfo, ErrorPtr aError);

  };
  
} // namespace p44


#endif // ENABLE_WBF
#endif // !__p44vdc__wbfdevice__
