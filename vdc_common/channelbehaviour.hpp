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

#ifndef __p44vdc__channelbehaviour__
#define __p44vdc__channelbehaviour__

#include "device.hpp"
#include "dsbehaviour.hpp"
#include "valueunits.hpp"

using namespace std;

namespace p44 {


  class OutputBehaviour;


  /// how long dimming through the full scale of a channel should take by default
  /// @note this is derived from dS-light spec: 11 brightness (1/256) steps per 300mS -> ~7 seconds for full  range
  #define FULL_SCALE_DIM_TIME_MS 7000

  /// represents a single channel of the output
  /// @note this class is not meant to be derived. Device specific channel functionality should
  ///   be implemented in derived Device classes' methods which are passed channels to process.
  ///   The ChannelBehaviour objects only represent the dS interface to channels, not the
  ///   device specific interface from dS channels to actual device hardware.
  class ChannelBehaviour : public PropertyContainer
  {
    typedef PropertyContainer inherited;
    friend class OutputBehaviour;

  protected:

    OutputBehaviour &output;

    /// @name hardware derived parameters (constant during operation)
    /// @{
    int channelIndex; ///< the index of the channel within the device
    double resolution; ///< actual resolution within the device
    string channelId; ///< string identifier for this channel. If string is empty, getApiId() will return decimal string representation of channelType()
    /// @}

    /// @name persistent settings
    /// @{

    /// @}

    /// @name internal volatile state
    /// @{
    bool channelUpdatePending; ///< set if cachedOutputValue represents a value to be transmitted to the hardware
    double cachedChannelValue; ///< the cached channel value
    double previousChannelValue; ///< the previous channel value, can be used for performing transitions
    double transitionProgress; ///< how much the transition has progressed so far (0..1)
    MLMicroSeconds channelLastSync; ///< Never if the cachedChannelValue is not yet applied to the hardware or retrieved from hardware, otherwise when it was last synchronized
    MLMicroSeconds nextTransitionTime; ///< the transition time to use for the next channel value change
    /// @}

  public:

    ChannelBehaviour(OutputBehaviour &aOutput, const string aChannelId);


    /// @name Fixed channel properties, partly from dS specs
    /// @{

    virtual DsChannelType getChannelType() = 0; ///< the dS channel type
    virtual ValueUnit getChannelUnit() = 0; ///< the unit
    virtual const char *getName() = 0; ///< descriptive channel name
    virtual double getMin() = 0; ///< min value
    virtual double getMax() = 0; ///< max value
    virtual double getDimPerMS() { return (getMax()-getMin())/FULL_SCALE_DIM_TIME_MS; }; ///< value to step up or down per Millisecond when dimming, or 0 for non-dimmable channels (default = 7sec for full scale)
    virtual double getMinDim() { return getMin(); }; ///< dimming min value defaults to same value as min
    virtual bool wrapsAround() { return false; }; ///< if true, channel is considered to wrap around, meaning max being the same value as min, and dimming never stopping but wrapping around. Off by default
    virtual bool enforceResolution() { return true; }; ///< if true, actual channel value will always be rounded to resolution of the channel

    /// @}


    /// @name interface towards actual device hardware (or simulation)
    /// @{

    /// set the resolution the hardware actually has
    /// @param aResolution actual resolution (smallest step) of the connected hardware
    void setResolution(double aResolution);

    /// set actual current output value as read from the device on startup, or before saving scenes
    /// to sync local cache value
    /// @param aActualChannelValue the value as read from the device
    /// @param aAlwaysSync if set, value is synchronized even if current value is still pending to be applied
    /// @note only used to get the actual value FROM the hardware.
    ///   NOT to be used to change the hardware output value!
    void syncChannelValue(double aActualChannelValue, bool aAlwaysSync=false);

    /// sync from boolean value
    /// @param aValue value to sync back to channel value
    /// @param aAlwaysSync if set, value is synchronized even if current value is still pending to be applied
    /// @note standard behaviour is not changing channel value when getChannelValueBool() already matches,
    ///   otherwise setting max() for aValue true, min() for false.
    virtual void syncChannelValueBool(bool aValue, bool aAlwaysSync=false);

    /// set new channel value and transition time to be applied with next device-level applyChannelValues()
    /// @param aNewValue the new output value
    /// @param aTransitionTime time in microseconds to be spent on transition from current to new channel value
    /// @param aAlwaysApply if set, new value will be applied to hardware even if not different from currently known value
    void setChannelValue(double aNewValue, MLMicroSeconds aTransitionTime=0, bool aAlwaysApply=false);

    /// set new channel value and separate transition times for increasing/decreasing value at applyChannelValues()
    /// @param aNewValue the new output value
    /// @param aTransitionTimeUp time in microseconds to be spent on transition from current to higher channel value
    /// @param aTransitionTimeDown time in microseconds to be spent on transition from current to lower channel value
    /// @param aAlwaysApply if set, new value will be applied to hardware even if not different from currently known value
    void setChannelValue(double aNewValue, MLMicroSeconds aTransitionTimeUp, MLMicroSeconds aTransitionTimeDown, bool aAlwaysApply);

    /// convenience variant of setChannelValue, which also checks the associated dontCare flag from the scene passed
    /// and only assigns the new value if the dontCare flags is NOT set.
    /// @param aAlwaysApply if set, new value will be applied to hardware even if not different from currently known value
    void setChannelValueIfNotDontCare(DsScenePtr aScene, double aNewValue, MLMicroSeconds aTransitionTimeUp, MLMicroSeconds aTransitionTimeDown, bool aAlwaysApply);

    /// dim channel value up or down, preventing going below getMinDim().
    /// @param aIncrement how much to increment/decrement the value
    /// @param aTransitionTime time in microseconds to be spent on transition from current to new channel value
    /// @return new channel value after increment/decrement
    double dimChannelValue(double aIncrement, MLMicroSeconds aTransitionTime);

    /// get current value of channel. This is always the target value, even if channel is still in transition
    /// @note does not trigger a device read, but returns chached value
    //   (initialized from actual value only at startup via initChannelValue(), updated when using setChannelValue)
    double getChannelValue();

    /// get as boolean value
    /// @return true when channel indicates "on" state
    /// @note standard behaviour is returning true when value is at 50% or more of the available range
    virtual bool getChannelValueBool();

    /// get current value of channel, which might be a calculated intermediate value between a previous value and getChannelValue()
    /// @note does not trigger a device read, but returns chached value
    //   (initialized from actual value only at startup via initChannelValue(), updated when using setChannelValue)
    double getTransitionalValue();

    /// step through transitions
    /// @param aStepSize how much to step. Default is zero and means starting transition
    /// @return true if there's another step to take, false if end of transition already reached
    bool transitionStep(double aStepSize=0);

    /// set transition progress
    /// @param aProgress progress between 0 (just started) to 1 (completed).
    void setTransitionProgress(double aProgress);

    /// set transition progress from intermediate value (instead of 0..1 progress as with setTransitionProgress())
    /// @param aCurrentValue value actually reached in transition right now, will update internal transition progress accordingly
    /// @param aIsInitial if set, this is considered the start value of the transition
    ///   (rather than as intermediate value between previously established start and target value)
    void setTransitionValue(double aCurrentValue, bool aIsInitial);

    /// check if in transition
    /// @return true if transition not complete and getTransitionalValue() will return a intermediate value
    bool inTransition();

    /// get time of last sync with hardware (applied or synchronized back)
    /// @return time of last sync, p44::Never if value never synchronized
    MLMicroSeconds getLastSync() { return channelLastSync; };

    /// get current value of this channel - and calculate it if it is not set in the device, but must be calculated from other channels
    virtual double getChannelValueCalculated() { return getChannelValue(); /* no calculated channels in base class */ };

    /// the transition time to use to change value in the hardware
    /// @return time to be used to transition to new value
    MLMicroSeconds transitionTimeToNewValue() { return nextTransitionTime; };

    /// check if channel value needs to be sent to device hardware
    /// @return true if the cached channel value was changed and should be applied to hardware via device's applyChannelValues()
    bool needsApplying() { return channelUpdatePending; }

    /// to be called when channel value has been successfully applied to hardware
    /// @param aAnyWay if true, lastSent state will be set even if channel was not in needsApplying() state
    void channelValueApplied(bool aAnyWay = false);

    /// @}


    /// @name interaction with digitalSTROM system
    /// @{

    /// get the channel ID as used in the API
    /// @param aApiVersion the API version to get the ID for. APIs before v3 always return the channel type as a numeric string
    /// @return the channel ID. The channel id must be unique within the device.
    string getApiId(int aApiVersion);

    /// get the channel index
    /// @return the channel index (0..N, 0=primary)
    int getChannelIndex() { return channelIndex; };

    /// get the channel id (may be empty)
    /// @return the channelId string
    const string& getChannelId() { return channelId; }

    /// get the resolution this channel has in the hardware of this particular device
    /// @return resolution of channel value (size of smallest step output can take, LSB)
    double getResolution() { return resolution; }; ///< actual resolution of the hardware

    /// check if this is the primary channel
    /// @return true if this is the primary (default) channel of a device
    bool isPrimary();

    /// call to make update pending
    /// @param aTransitionTime if >=0, sets new transition time (useful when re-applying values)
    void setNeedsApplying(MLMicroSeconds aTransitionTime = -1) { channelUpdatePending = true; if (aTransitionTime>=0) nextTransitionTime = aTransitionTime; }

    /// @}

    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description();

    /// Get short text for a "first glance" status of the behaviour
    /// @return string, really short, intended to be shown as a narrow column in a list
    virtual string getStatusText();


  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

  };

  typedef boost::intrusive_ptr<ChannelBehaviour> ChannelBehaviourPtr;
  typedef vector<ChannelBehaviourPtr> ChannelBehaviourVector;


  // MARK: ===== generic channel implementations


  /// index value channel
  class IndexChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

    uint32_t numIndices; ///< number of valid indices (indices are 0..numIndices-1)

  public:
    IndexChannel(OutputBehaviour &aOutput, const string aChannelId) : inherited(aOutput, aChannelId) { resolution = 1; numIndices = 0; };
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_default; }; ///< no real dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_none, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "index"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // 0..numIndices-1
    virtual double getMax() P44_OVERRIDE { return numIndices>0 ? numIndices-1 : 0; };
    int getIndex() { return getChannelValue(); }; // return as int for convenience
    virtual double getDimPerMS() P44_OVERRIDE { return 0; }; // not dimmable

    void setNumIndices(uint32_t aNumIndices) { numIndices = aNumIndices; };

  };
  typedef boost::intrusive_ptr<IndexChannel> IndexChannelPtr;


  /// boolean flag channel
  class FlagChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    FlagChannel(OutputBehaviour &aOutput, const string aChannelId) : inherited(aOutput, aChannelId) { resolution = 1; };
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_default; }; ///< no real dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_none, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "flag"; };
    virtual double getMin() P44_OVERRIDE { return 0; };
    virtual double getMax() P44_OVERRIDE { return 1; };
    bool getFlag() { return getChannelValue()!=0; }; // return as bool for convenience
    virtual double getDimPerMS() P44_OVERRIDE { return 0; }; // not dimmable

  };
  typedef boost::intrusive_ptr<FlagChannel> FlagChannelPtr;


  /// digital switch channel
  class DigitalChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    DigitalChannel(OutputBehaviour &aOutput, const string aChannelId) : inherited(aOutput, aChannelId) { resolution = 100; /* on or off */ };
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_default; }; ///< no real dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "switch"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // compatible with brightness: 0 to 100%
    virtual double getMax() P44_OVERRIDE { return 100; };
    virtual double getDimPerMS() P44_OVERRIDE { return 0; }; // not dimmable

  };
  typedef boost::intrusive_ptr<DigitalChannel> DigitalChannelPtr;


  /// general purpose "dial" channel
  class DialChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

    double max; ///< maximum value

  public:
    DialChannel(OutputBehaviour &aOutput, const string aChannelId) : inherited(aOutput, aChannelId) { max=100; /* standard dimmer range */ resolution = 1; /* integer by default */ };
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_default; }; ///< no real dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_none, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "dial"; };
    virtual double getMin() P44_OVERRIDE { return 0; };
    virtual double getMax() P44_OVERRIDE { return max; };

    void setMax(double aMax) { max = aMax; };
  };
  typedef boost::intrusive_ptr<DialChannel> DialChannelPtr;


  // MARK: ===== specific purpose channel implementations

  /// Power state channel
  class PowerStateChannel : public IndexChannel
  {
    typedef IndexChannel inherited;

  public:
    PowerStateChannel(OutputBehaviour &aOutput) : inherited(aOutput, "powerState") { setNumIndices(numDsPowerStates); }; ///< see DsPowerState enum

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_power_state; }; ///< the dS channel type
    virtual const char *getName() P44_OVERRIDE { return "power state"; };

  };
  typedef boost::intrusive_ptr<PowerStateChannel> PowerStateChannelPtr;


} // namespace p44

#endif /* defined(__p44vdc__channelbehaviour__) */
