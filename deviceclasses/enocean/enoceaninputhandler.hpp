//
//  Copyright (c) 1-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__enoceaninputhandler__
#define __p44vdc__enoceaninputhandler__

#include "p44vdc_common.hpp"

#if ENABLE_ENOCEAN

#include "enoceandevice.hpp"

/// enocean bit specification to bit number macro
#define DB(byte,bit) (byte*8+bit)
/// bit no to bitmask
#define BITMASK(bitno) ((uint32_t)1<<bitno)
/// enocean bit specification to bit mask macro (within 32-bit 4BS data)
#define DBMASK(byte,bit) (BITMASK(DB(byte,bit)))
/// get byte from radio data according to enocean spec byte number (reversed index)
#define ENOBYTE(byte,data,size) (data[size-byte-1])
/// get bit value from radio data according to enocean spec byte and bit number
#define ENOBIT(byte,bit,data,size) ((ENOBYTE(byte,data,size) & BITMASK(bit))!=0)

using namespace std;

namespace p44 {

  struct EnoceanInputDescriptor;

  /// decoder function
  /// @param aDescriptor descriptor for data to extract
  /// @param aBehaviour the beehaviour that will receive the extracted value
  /// @param aDataP pointer to data, MSB comes first, LSB comes last (for 4BS data: MSB=enocean DB_3, LSB=enocean DB_0)
  /// @param aDataSize number of data bytes
  typedef void (*BitFieldHandlerFunc)(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP);

  /// enocean sensor value descriptor
  typedef struct EnoceanInputDescriptor {
    uint8_t variant; ///< the variant from the EEP signature
    uint8_t func; ///< the function code from the EPP signature
    uint8_t type; ///< the type code from the EPP signature
    uint8_t subDevice; ///< subdevice index, in case EnOcean device needs to be split into multiple logical vdSDs
    DsClass colorClass; ///< the dS group for the entire device
    DsGroup channelGroup; ///< the dS group for this channel
    BehaviourType behaviourType; ///< the behaviour type
    uint8_t behaviourParam; ///< VdcSensorType, DsBinaryInputType or VdcOutputFunction resp., depending on behaviourType
    VdcUsageHint usage; ///< usage hint
    float min; ///< min value
    float max; ///< max value
    uint8_t msBit; ///< most significant bit of sensor value field in data (for 4BS: 31=Bit7 of DB_3, 0=Bit0 of DB_0)
    uint8_t lsBit; ///< least significant bit of sensor value field in data (for 4BS: 31=Bit7 of DB_3, 0=Bit0 of DB_0)
    double updateInterval; ///< normal update interval (average time resolution) in seconds. For non-periodic sensors, this denotes the average response time to changes (e.g. for a user dial)
    double aliveSignInterval; ///< maximum interval between two reports of a sensor (0 if there is no minimal report interval). If sensor does not push a value for longer than that, it should be considered out-of-order
    BitFieldHandlerFunc bitFieldHandler; ///< function used to convert between bit field in telegram and engineering value for the behaviour
    const char *typeText; ///< text describing the channel. NULL to terminate list
  } EnoceanInputDescriptor;


  /// @name functions and texts for use in EnoceanInputDescriptor table entries
  /// @{

  namespace EnoceanInputs {

    void handleBitField(const EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP);
    uint64_t bitsExtractor(const struct EnoceanInputDescriptor &aInputDescriptor, uint8_t *aDataP, int aDataSize);

    void stdSensorHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP);
    void invSensorHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP);

    void stdInputHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP);

    void stdButtonHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP);

    void lowBatInputHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP);
    void batVoltSensorHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP);
    void batPercSensorHandler(const struct EnoceanInputDescriptor &aInputDescriptor, DsBehaviourPtr aBehaviour, uint8_t *aDataP, int aDataSize, EnoceanChannelHandler* aChannelP);


    // texts
    extern const char *tempText;
    extern const char *humText;
    extern const char *coText;
    extern const char *co2Text;
    extern const char *illumText;
    extern const char *occupText;
    extern const char *motionText;
    extern const char *setPointText;
    extern const char *fanSpeedText;
    extern const char *dayNightText;
    extern const char *contactText;
    extern const char *supplyText;
    extern const char *lowBatText;

  }

  /// @}


  /// generic, table driven input channel handler
  class EnoceanInputHandler : public EnoceanChannelHandler
  {
    typedef EnoceanChannelHandler inherited;

  protected:

    /// protected constructor, static factory function newDevice of derived class is the place to call it from
    EnoceanInputHandler(EnoceanDevice &aDevice);

  public:

    /// device creator function
    typedef EnoceanDevicePtr (*CreateDeviceFunc)(EnoceanVdc *aVdcP);

    /// the sensor channel descriptor
    const EnoceanInputDescriptor *sensorChannelDescriptorP;

    /// factory: (re-)create logical device from address|channel|profile|manufacturer tuple
    /// @param aVdcP the class container
    /// @param aSubDeviceIndex subdevice number (multiple logical EnoceanDevices might exists for the same EnoceanAddress)
    ///   upon exit, this will be incremented by the number of subdevice indices the device occupies in the index space
    ///   (usually 1, but some profiles might reserve extra space, such as up/down buttons)
    /// @param aEEProfile VARIANT/RORG/FUNC/TYPE EEP profile number
    /// @param aEEManufacturer manufacturer number (or manufacturer_unknown)
    /// @param aSendTeachInResponse enable sending teach-in response for this device
    /// @return returns NULL if no device can be created for the given aSubDeviceIndex, new device otherwise
    static EnoceanDevicePtr newDevice(
      EnoceanVdc *aVdcP,
      CreateDeviceFunc aCreateDeviceFunc,
      const EnoceanInputDescriptor *aDescriptorTable,
      EnoceanAddress aAddress,
      EnoceanSubDevice &aSubDeviceIndex, // current subdeviceindex, factory returns NULL when no device can be created for this subdevice index
      EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
      bool aSendTeachInResponse
    );


    /// factory: add sensor/binary input channel to device by descriptor
    /// @param aDevice the device to add the channel to
    /// @param aInputDescriptor a sensor or binary input descriptor
    /// @param aSetDeviceDescription if set, this sensor channel is the "main" channel and will set description on the device itself
    /// @param aId if not NULL, a string to be used for the behaviour ID
    static void addInputChannel(
      EnoceanDevicePtr aDevice,
      const EnoceanInputDescriptor &aInputDescriptor,
      bool aSetDeviceDescription,
      const char *aId
    );

    /// factory: create behaviour (sensor/binary input) by descriptor
    /// @param aDevice the device to add the behaviour to
    /// @param aInputDescriptor a sensor or binary input descriptor
    /// @param aId if not NULL, a string to be used for the behaviour ID
    /// @return the behaviour
    static DsBehaviourPtr newInputChannelBehaviour(const EnoceanInputDescriptor &aInputDescriptor, DevicePtr aDevice, const char *aId);

    /// utility: get description string from sensor descriptor info
    static string inputDesc(const EnoceanInputDescriptor &aInputDescriptor);

    /// handle radio packet related to this channel
    /// @param aEsp3PacketPtr the radio packet to analyze and extract channel related information
    virtual void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr);

    /// check if channel is alive = has received life sign within timeout window
    virtual bool isAlive();

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc();

  };
  typedef boost::intrusive_ptr<EnoceanInputHandler> EnoceanInputHandlerPtr;


} // namespace p44

#endif // ENABLE_ENOCEAN
#endif // __p44vdc__enoceaninputhandler__
