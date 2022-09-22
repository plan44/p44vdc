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

#ifndef __p44vdc__channelbehaviour__
#define __p44vdc__channelbehaviour__

#include "device.hpp"
#include "dsbehaviour.hpp"
#if REDUCED_FOOTPRINT
#include "valueunits.hpp"
#else
#include "valuedescriptor.hpp"
#endif

using namespace std;

namespace p44 {


  class OutputBehaviour;


  /// how long dimming through the full scale of a channel should take by default
  /// @note this is derived from dS-light spec: 11 brightness (1/256) steps per 300mS -> ~7 seconds for full  range
  #define FULL_SCALE_DIM_TIME_MS 7000

  /// represents a single channel of the output
  /// @note this class is not meant to be derived in a device implementation specific way.
  ///   Device specific channel functionality should
  ///   be implemented in derived Device classes' methods which are passed channels to process.
  ///   The ChannelBehaviour objects only represent the dS interface to channels, not the
  ///   device specific interface from dS channels to actual device hardware.
  class ChannelBehaviour :
    public PropertyContainer, public PersistentParams
    #if P44SCRIPT_FULL_SUPPORT
    ,public ValueSource
    #endif
  {
    typedef PropertyContainer inheritedProps;
    typedef PersistentParams inheritedParams;
    friend class OutputBehaviour;

  protected:

    OutputBehaviour &mOutput;
    #if !REDUCED_FOOTPRINT
    EnumListPtr mEnumList;
    #endif

    /// @name hardware derived parameters (constant during operation)
    /// @{
    int mChannelIndex; ///< the index of the channel within the device
    double mResolution; ///< actual resolution within the device
    string mChannelId; ///< string identifier for this channel. If string is empty, getApiId() will return decimal string representation of channelType()
    /// @}

    /// @name persistent settings
    /// @{

    /// @}

    /// @name internal volatile state
    /// @{
    bool mChannelUpdatePending; ///< set if cachedOutputValue represents a value to be transmitted to the hardware
    bool mIsVolatileValue; ///< set if value is not a defining part of the output state (not to be persisted, not necessarily valid)
    double mCachedChannelValue; ///< the cached channel value
    MLMicroSeconds mChannelLastSync; ///< Never if the cachedChannelValue is not yet applied to the hardware or retrieved from hardware, otherwise when it was last synchronized
    MLMicroSeconds mNextTransitionTime; ///< the transition time to use for the next channel value change
    MLMicroSeconds mTransitionStarted; ///< time of when current transition has started
    double mPreviousChannelValue; ///< the previous channel value, can be used for performing transitions
    double mProgress; ///< transition progress between 0..1, 1=finished
    double mCustomDimPerMS; ///< non-standard dimming rate, 0=none
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
    virtual double getStdDimPerMS() { return (getMax()-getMin())/FULL_SCALE_DIM_TIME_MS; }; ///< default value to step up or down per Millisecond for this channel, defaults to 7sec for full scale
    double getDimPerMS() { return mCustomDimPerMS>0 ? mCustomDimPerMS : getStdDimPerMS(); }; ///< value to step up or down per Millisecond when dimming, or 0 for non-dimmable channels

    virtual double getMinDim() { return getMin(); }; ///< dimming min value defaults to same value as min
    virtual bool wrapsAround() { return false; }; ///< if true, channel is considered to wrap around, meaning max being the same value as min, and dimming never stopping but wrapping around. Off by default
    virtual bool enforceResolution() { return true; }; ///< if true, actual channel value will always be rounded to resolution of the channel
    virtual ApiValueType getChannelValueType() { return apivalue_double; }
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
    /// @param aVolatile if set, the resulting value will not be persisted
    /// @note only used to get the actual value FROM the hardware.
    ///   NOT to be used to change the hardware output value!
    void syncChannelValue(double aActualChannelValue, bool aAlwaysSync=false, bool aVolatile=false);

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
    /// @param aScene the scene to check for dontCare flags
    /// @param aNewValue the new output value
    /// @param aTransitionTimeUp time in microseconds to be spent on transition from current to higher channel value
    /// @param aTransitionTimeDown time in microseconds to be spent on transition from current to lower channel value
    /// @param aAlwaysApply if set, new value will be applied to hardware even if not different from currently known value
    /// @return true if channel value was actually set (aScene does not have the channel's dontCare flag set)
    bool setChannelValueIfNotDontCare(DsScenePtr aScene, double aNewValue, MLMicroSeconds aTransitionTimeUp, MLMicroSeconds aTransitionTimeDown, bool aAlwaysApply);

    /// dim channel value up or down, preventing going below getMinDim().
    /// @param aIncrement how much to increment/decrement the value
    /// @param aTransitionTime time in microseconds to be spent on transition from current to new channel value
    /// @return new channel value after increment/decrement
    double dimChannelValue(double aIncrement, MLMicroSeconds aTransitionTime);

    /// set/reset custom dimming per millisecond rate (0 = standard dimming rate)
    void setCustomDimPerMS(double aDimPerMS = 0) { mCustomDimPerMS = aDimPerMS; };

    /// get current value of channel.
    /// @param aTransitional if set and the channel is in transition, a calculated intermediate value
    ///   between a previous value and the current target value is returned.
    ///   Otherwise, the target value is returned, even if channel is still in transition
    /// @note does not trigger a device read, but returns chached value
    ///   (initialized from actual value only at startup via initChannelValue(), updated when using setChannelValue)
    double getChannelValue(bool aTransitional = false);

    /// get as boolean value
    /// @return true when channel indicates "on" state
    /// @note standard behaviour is returning true when value is at 50% or more of the available range
    virtual bool getChannelValueBool();

    /// initialize a transition or update its progress over time
    /// @param aNow current time, used to calculate progress. Default is 0 and means starting a new transition NOW,
    ///   negative means completing transition immediately.
    /// @return true if the transition must be updated again, false if end of transition already reached
    bool updateTransition(MLMicroSeconds aNow = 0);

    /// set transition progress
    /// @param aProgress progress between 0 (just started) to 1 (completed).
    /// @return true if no longer in transition (aProgress>=1)
    bool setTransitionProgress(double aProgress);

    /// end transition, making current transitional value the cached one
    /// @note this may be called from device implementations that actually use calculated transitions
    void stopTransition();

    /// check if in transition
    /// @return true if transition not complete and getTransitionalValue() will return a intermediate value
    bool inTransition();

    /// get time of last sync with hardware (applied or synchronized back)
    /// @return time of last sync, p44::Never if value never synchronized
    MLMicroSeconds getLastSync() { return mChannelLastSync; };

    /// get current value of this channel - and calculate it if it is not set in the device, but must be calculated from other channels
    virtual double getChannelValueCalculated() { return getChannelValue(); /* no calculated channels in base class */ };

    /// the transition time to use to change value in the hardware
    /// @return time to be used to transition to new value
    MLMicroSeconds transitionTimeToNewValue() { return mNextTransitionTime; };

    /// set time for next transition (for overrides - normally, setChannelValue() sets the transition time)
    /// @param aTransitionTime transition time
    void setTransitionTime(MLMicroSeconds aTransitionTime) { mNextTransitionTime = aTransitionTime; }

    /// check if channel value needs to be sent to device hardware
    /// @return true if the cached channel value was changed and should be applied to hardware via device's applyChannelValues()
    bool needsApplying() { return mChannelUpdatePending; }

    /// make channel value pending for sending to hardware (or reset pending state)
    /// @param aPending if set to false, channel pending flag will be reset
    void makeApplyPending(bool aPending = true) { mChannelUpdatePending = aPending; }

    /// to be called when channel value has been successfully applied to hardware
    /// @param aAnyWay if true, lastSent state will be set even if channel was not in needsApplying() state
    void channelValueApplied(bool aAnyWay = false);

    /// can be called to explicitly set a channel's volatile flag, which means it is not carrying relevant data
    /// for defining the output state (e.g. CIE x,y channels when light is in HSV mode)
    /// @param aVolatile true to set volatile, false otherwise
    void setVolatile(bool aVolatile) { setPVar(mIsVolatileValue, aVolatile); }

    /// add an enumeration mapping for the channel value
    /// @note normally, channels do not have an enumeration description ("values" in channel description), but
    ///    first call to this method creates one.
    /// @param aEnumText the text value
    /// @param aEnumValue the integer value corresponding to the text
    void addEnum(const char *aEnumText, uint32_t aEnumValue);

    /// @}


    /// @name interaction with digitalSTROM system
    /// @{

    /// get the identifier (unique within this device instance)
    /// @return channel id string as internally identifying the channel
    string getId();

    /// get the channel ID as used in the API
    /// @param aApiVersion the API version to get the ID for. APIs before v3 always return the channel type as a numeric string
    /// @return the channel ID. The channel id must be unique within the device.
    string getApiId(int aApiVersion);

    /// get the channel index
    /// @return the channel index (0..N, 0=primary)
    int getChannelIndex() { return mChannelIndex; };

    /// get the channel id (may be empty)
    /// @return the channelId string
    const string& getChannelId() { return mChannelId; }

    /// get the resolution this channel has in the hardware of this particular device
    /// @return resolution of channel value (size of smallest step output can take, LSB)
    double getResolution() { return mResolution; }; ///< actual resolution of the hardware

    /// check if this is the primary channel
    /// @return true if this is the primary (default) channel of a device
    bool isPrimary();

    /// call to make update pending
    /// @param aTransitionTime if >=0, sets new transition time (useful when re-applying values)
    void setNeedsApplying(MLMicroSeconds aTransitionTime = -1) { mChannelUpdatePending = true; if (aTransitionTime>=0) mNextTransitionTime = aTransitionTime; }

    /// @}

    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description();

    /// @return a prefix for log messages from this addressable
    virtual string logContextPrefix() P44_OVERRIDE;

    /// @return log level offset (overridden to use something other than the P44LoggingObj's)
    virtual int getLogLevelOffset() P44_OVERRIDE;

    /// Get short text for a "first glance" status of the behaviour
    /// @return string, really short, intended to be shown as a narrow column in a list
    virtual string getStatusText();


    /// load behaviour parameters from persistent DB
    ErrorPtr load();

    /// save unsaved behaviour parameters to persistent DB
    ErrorPtr save();

    /// forget any parameters stored in persistent DB
    ErrorPtr forget();

    #if P44SCRIPT_FULL_SUPPORT

    /// @name ValueSource interface
    /// @{

    /// get id - unique at least in the vdhost's scope
    virtual string getSourceId() P44_OVERRIDE;

    /// get descriptive name identifying the source within the entire vdc host (for using in selection lists)
    virtual string getSourceName() P44_OVERRIDE;

    /// get value
    virtual double getSourceValue() P44_OVERRIDE;

    /// get time of last update
    virtual MLMicroSeconds getSourceLastUpdate() P44_OVERRIDE;

    /// get operation level (how good/critical the operation state of the underlying device is)
    virtual int getSourceOpLevel() P44_OVERRIDE;

    /// @}

    #endif // P44SCRIPT_FULL_SUPPORT

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    #if !REDUCED_FOOTPRINT
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;
    #endif
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

    // persistence implementation
    virtual const char *tableName() P44_OVERRIDE P44_FINAL;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

  private:

    // key for saving this channel in the DB
    string getDbKey();

  };

  typedef boost::intrusive_ptr<ChannelBehaviour> ChannelBehaviourPtr;
  typedef vector<ChannelBehaviourPtr> ChannelBehaviourVector;


  // MARK: - generic channel implementations


  class PercentageLevelChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    PercentageLevelChannel(OutputBehaviour &aOutput, const string aChannelId = "level") : inherited(aOutput, aChannelId) { mResolution = 1; };
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_default; }; ///< no real dS channel type

    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "level"; };
    virtual double getMin() P44_OVERRIDE { return 0; };
    virtual double getMax() P44_OVERRIDE { return 100; };

  };
  typedef boost::intrusive_ptr<PercentageLevelChannel> PercentageLevelChannelPtr;


  /// index value channel
  class IndexChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

    uint32_t mNumIndices; ///< number of valid indices (indices are 0..numIndices-1)

  public:
    IndexChannel(OutputBehaviour &aOutput, const string aChannelId) : inherited(aOutput, aChannelId) { mResolution = 1; mNumIndices = 0; };
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_default; }; ///< no real dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_none, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "index"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // 0..numIndices-1
    virtual double getMax() P44_OVERRIDE { return mNumIndices>0 ? mNumIndices-1 : 0; };
    int getIndex() { return getChannelValue(); }; // return as int for convenience
    virtual double getStdDimPerMS() P44_OVERRIDE { return 0; }; // not dimmable

    void setNumIndices(uint32_t aNumIndices) { mNumIndices = aNumIndices; };

  };
  typedef boost::intrusive_ptr<IndexChannel> IndexChannelPtr;


  /// boolean flag channel
  class FlagChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    FlagChannel(OutputBehaviour &aOutput, const string aChannelId) : inherited(aOutput, aChannelId) { mResolution = 1; };
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_default; }; ///< no real dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_none, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "flag"; };
    virtual double getMin() P44_OVERRIDE { return 0; };
    virtual double getMax() P44_OVERRIDE { return 1; };
    bool getFlag() { return getChannelValue()!=0; }; // return as bool for convenience
    virtual double getStdDimPerMS() P44_OVERRIDE { return 0; }; // not dimmable

  };
  typedef boost::intrusive_ptr<FlagChannel> FlagChannelPtr;


  /// digital switch channel
  class DigitalChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    DigitalChannel(OutputBehaviour &aOutput, const string aChannelId) : inherited(aOutput, aChannelId) { mResolution = 100; /* on or off */ };
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_default; }; ///< no real dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "switch"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // compatible with brightness: 0 to 100%
    virtual double getMax() P44_OVERRIDE { return 100; };
    virtual double getStdDimPerMS() P44_OVERRIDE { return 0; }; // not dimmable

  };
  typedef boost::intrusive_ptr<DigitalChannel> DigitalChannelPtr;


  /// general purpose "dial" channel
  class DialChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

    double mMax; ///< maximum value

  public:
    DialChannel(OutputBehaviour &aOutput, const string aChannelId) : inherited(aOutput, aChannelId) { mMax=100; /* standard dimmer range */ mResolution = 1; /* integer by default */ };
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_default; }; ///< no real dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_none, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "dial"; };
    virtual double getMin() P44_OVERRIDE { return 0; };
    virtual double getMax() P44_OVERRIDE { return mMax; };

    void setMax(double aMax) { mMax = aMax; };
  };
  typedef boost::intrusive_ptr<DialChannel> DialChannelPtr;


  // MARK: - specific purpose channel implementations

  /// Power state channel
  class PowerStateChannel : public IndexChannel
  {
    typedef IndexChannel inherited;

  public:
    PowerStateChannel(OutputBehaviour &aOutput) : inherited(aOutput, "powerState")
    {
      setNumIndices(numDsPowerStates); ///< see DsPowerState enum
      addEnum("off", powerState_off);
      addEnum("on", powerState_on);
      addEnum("forcedOff", powerState_forcedOff);
      addEnum("standby", powerState_standby);
    };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_power_state; }; ///< the dS channel type
    virtual const char *getName() P44_OVERRIDE { return "power state"; };

  };
  typedef boost::intrusive_ptr<PowerStateChannel> PowerStateChannelPtr;


  /// Audio volume channel, 0..100%
  class AudioVolumeChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;
    double mDimPerMS;

  public:
    AudioVolumeChannel(OutputBehaviour &aOutput) : inherited(aOutput, "audioVolume")
    {
      mResolution = 0.1; // arbitrary, 1:1000 seems ok
      mDimPerMS = 0; // standard rate
    };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_audio_volume; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "volume"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // dS volume goes from 0 to 100%
    virtual double getMax() P44_OVERRIDE { return 100; };
    virtual double getStdDimPerMS() P44_OVERRIDE { return mDimPerMS>0 ? mDimPerMS : inherited::getStdDimPerMS(); };

    virtual void setDimPerMS(double aDimPerMS) { mDimPerMS = aDimPerMS; }; ///< set HW-specific dimming per MS to make actual audio steps and dimming steps align better than with standard step

  };
  typedef boost::intrusive_ptr<AudioVolumeChannel> AudioVolumeChannelPtr;


  #if !REDUCED_FOOTPRINT

  /// string value channel
  class StringChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;
    typedef PersistentParams inheritedParams;

    string mStringValue;

  public:
    StringChannel(OutputBehaviour &aOutput, const string aChannelId) : inherited(aOutput, aChannelId) { mResolution = 0; }
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_default; } ///< no real dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_none, unitScaling_1); }
    virtual const char *getName() P44_OVERRIDE { return "stringChannel"; }
    virtual double getMin() P44_OVERRIDE { return 0; }
    virtual double getMax() P44_OVERRIDE { return 0; }
    virtual double getStdDimPerMS() P44_OVERRIDE { return 0; } // not dimmable
    virtual ApiValueType getChannelValueType() P44_OVERRIDE { return apivalue_string; }

    virtual bool setChannelValueIfNotDontCare(DsScenePtr aScene, const string& aNewValue, bool aAlwaysApply);
    virtual void setChannelValueString(const string& aValue, bool aAlwaysSync=false);
    virtual void syncChannelValueString(const string& aValue, bool aAlwaysSync=false);
    virtual string getChannelValueString();
    virtual void setValueOptions(const vector<const char*>& aValues);

    // string value property access implementation
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE P44_FINAL;

    // persistence implementation
    virtual size_t numFieldDefs() P44_OVERRIDE P44_FINAL;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE P44_FINAL;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE P44_FINAL;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE P44_FINAL;

  };
  typedef boost::intrusive_ptr<StringChannel> StringChannelPtr;

  #endif // !REDUCED_FOOTPRINT

} // namespace p44

#endif /* defined(__p44vdc__channelbehaviour__) */
