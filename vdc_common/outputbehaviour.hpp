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

#ifndef __p44vdc__outputbehaviour__
#define __p44vdc__outputbehaviour__

#include "device.hpp"
#include "channelbehaviour.hpp"

using namespace std;

namespace p44 {

  /// Implements the basic behaviour of an output with one or multiple output channels
  class OutputBehaviour : public DsBehaviour
  {
    typedef DsBehaviour inherited;
    friend class ChannelBehaviour;
    friend class Device;

    /// channels
    ChannelBehaviourVector channels;

  protected:

    /// @name hardware derived parameters (constant during operation)
    /// @{
    VdcOutputFunction outputFunction; ///< the function of the output
    VdcUsageHint outputUsage; ///< the input type when device has hardwired functions
    VdcOutputMode defaultOutputMode; ///< the default mode of the output - this mode ist used when outputMode is set to outputmode_default
    bool variableRamp; ///< output has variable ramp times
    double maxPower; ///< max power in Watts the output can control
    /// @}


    /// @name persistent settings
    /// @{
    VdcOutputMode outputMode; ///< the mode of the output. Can be outputmode_default to have device to use its preferred (or only possible) mode
    bool pushChanges; ///< if set, local changes to output will be pushed upstreams
    uint64_t outputGroups; ///< mask for group memberships (0..63)
    /// @}


    /// @name internal volatile state
    /// @{
    bool localPriority; ///< if set device is in local priority mode
    MLMicroSeconds transitionTime; ///< default transition time when changing this output
    /// @}

  public:

    OutputBehaviour(Device &aDevice);

    /// @name Access to channels
    /// @{

    /// get number of channels
    /// @return number of channels
    size_t numChannels();

    /// get channel by index
    /// @param aChannelIndex the channel index (0=primary channel, 1..n other channels)
    /// @param aPendingApplyOnly if set, a channel is only returned when its value is pending to be applied
    /// @return NULL for unknown channel
    ChannelBehaviourPtr getChannelByIndex(int aChannelIndex, bool aPendingApplyOnly = false);

    /// get channel by channelType
    /// @param aChannelType the channel type, can be channeltype_default to get primary/default channel
    /// @param aPendingApplyOnly if set, a channel is only returned when its value is pending to be applied
    /// @return NULL for unknown channel
    ChannelBehaviourPtr getChannelByType(DsChannelType aChannelType, bool aPendingApplyOnly = false);

    /// get channel by channel ID
    /// @param aChannelId the channel ID
    /// @param aPendingApplyOnly if set, a channel is only returned when its value is pending to be applied
    /// @return NULL for unknown channel
    ChannelBehaviourPtr getChannelById(const string aChannelId, bool aPendingApplyOnly = false);


    /// add a channel to the output
    /// @param aChannel the channel to add
    /// @note this is usually called by initialisation code of classes derived from OutputBehaviour to
    ///   add the behaviour specific channels.
    void addChannel(ChannelBehaviourPtr aChannel);

    /// get the actual output mode
    /// @return the actual output mode, never returns outputmode_default
    VdcOutputMode actualOutputMode();

    /// @}


    /// @name interface towards actual device hardware (or simulation)
    /// @{

    /// Configure hardware parameters of the output
    void setHardwareOutputConfig(VdcOutputFunction aOutputFunction, VdcOutputMode aDefaultOutputMode, VdcUsageHint aUsage, bool aVariableRamp, double aMaxPower);

    /// @param aLocalPriority true to set local priority mode, false to clear it
    void setLocalPriority(bool aLocalPriority) { localPriority = aLocalPriority; };

    /// @return true if device is in local priority mode
    bool hasLocalPriority() { return localPriority; };

    /// @return true if output is enabled
    bool isEnabled() { return outputMode!=outputmode_disabled; };

    /// set new output mode
    /// @param aOutputMode new output mode (including outputmode_disabled, and outputmode_default to generically enable)
    /// @note a change in output mode might trigger (re-)applying channel values
    virtual void setOutputMode(VdcOutputMode aOutputMode);

    /// @return output functionality the hardware provides
    VdcOutputFunction getOutputFunction() { return outputFunction; };

    /// Apply output-mode specific output value transformation
    /// @param aChannelValue channel value
    /// @param aChannelIndex channel index (might be different transformation depending on type)
    /// @return output value limited/transformed according to outputMode
    /// @note subclasses might implement behaviour-specific output transformations
    virtual double outputValueAccordingToMode(double aChannelValue, int aChannelIndex);

    /// Convert actual output value back to channel value according to output-mode (for syncing back channel values)
    /// @param aOutputValue actual output value
    /// @param aChannelIndex channel index (might be different transformation depending on type)
    /// @return channel value converted back from actual output value according to outputMode
    /// @note subclasses might implement behaviour-specific output transformations
    virtual double channelValueAccordingToMode(double aOutputValue, int aChannelIndex);

    /// @}


    /// @name interaction with digitalSTROM system
    /// @{

    /// check group membership
    /// @param aGroup color number to check
    /// @return true if device is member of this group
    bool isMember(DsGroup aGroup);

    /// set group membership
    /// @param aGroup group number to set or remove
    /// @param aIsMember true to make device member of this group
    void setGroupMembership(DsGroup aGroup, bool aIsMember);

    /// remove all group memberships
    void resetGroupMembership();

    /// check for presence of model feature (flag in dSS visibility matrix)
    /// @param aFeatureIndex the feature to check for
    /// @return yes if this output behaviour has the feature, no if (explicitly) not, undefined if asked entity does not know
    virtual Tristate hasModelFeature(DsModelFeatures aFeatureIndex);

    /// perform special scene actions (like flashing) which are independent of dontCare flag.
    /// @param aScene the scene that was called (if not dontCare, applyScene() has already been called)
    /// @param aDoneCB will be called when scene actions have completed (but not necessarily when stopped by stopSceneActions())
    virtual void performSceneActions(DsScenePtr aScene, SimpleCB aDoneCB) { if (aDoneCB) aDoneCB(); /* NOP in base class */ };

    /// will be called to stop all ongoing actions before next callScene etc. is issued.
    /// @note this must stop all ongoing actions such that applying another scene or action right afterwards
    ///   cannot mess up things.
    virtual void stopSceneActions() { /* NOP in base class */ };

    /// perform applying Scene
    /// @param aScene the scene to apply
    /// @return true if apply is complete, i.e. everything ready to apply to hardware outputs.
    ///   false if scene cannot be applied to hardware (not yet, or maybe not at all); applying to hardware, if
    ///   needed at all, will be triggered otherwise.
    /// @note this is a OutputBehaviour level wrapper and preparator for behaviour-specific applyScene().
    bool performApplyScene(DsScenePtr aScene);

    /// capture current state into passed scene object
    /// @param aScene the scene object to update
    /// @param aFromDevice true to request real values read back from device hardware (if possible), false to
    ///   just capture the currently cached channel values
    /// @param aDoneCB will be called when capture is complete
    virtual void captureScene(DsScenePtr aScene, bool aFromDevice, SimpleCB aDoneCB);

    /// switch on at minimum brightness if not already on (needed for callSceneMin), only relevant for lights
    /// @param aScene the scene to take all other channel values from, except brightness which is set to light's minDim
    virtual void onAtMinBrightness(DsScenePtr aScene) { /* NOP in base class, only relevant for lights */ };

    /// check if this channel of this device is allowed to dim now (for lights, this will prevent dimming lights that are off)
    /// @param aChannel the channel to check
    virtual bool canDim(ChannelBehaviourPtr aChannel) { return true; /* in base class, nothing prevents dimming */ };


    /// Process a named control value. The type, group membership and settings of the device determine if at all,
    /// and if, how the value affects physical outputs of the device or general device operation
    /// @note if this method adjusts channel values, it must not directly update the hardware, but just
    ///   prepare channel values such that these can be applied using requestApplyingChannels().
    /// @param aName the name of the control value, which describes the purpose
    /// @param aValue the control value to process
    /// @note base class by default forwards the control value to all of its output behaviours.
    /// @return true if value processing caused channel changes so channel values should be applied.
    virtual bool processControlValue(const string &aName, double aValue) { return false; /* NOP in base class, no channels changed */ };

    /// identify the device to the user in a behaviour-specific way
    /// @note this is usually called by device's identifyToUser(), unless device has hardware (rather than behaviour)
    ///   specific implementation
    virtual void identifyToUser() { /* NOP in base class */ };

    /// @}

    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description() P44_OVERRIDE;

    /// Get short text for a "first glance" status of the behaviour
    /// @return string, really short, intended to be shown as a narrow column in a list
    virtual string getStatusText() P44_OVERRIDE;


  protected:

    /// apply scene to output channels and other state variables
    /// @param aScene the scene to apply to output channels
    /// @return true if apply is complete, i.e. everything ready to apply to hardware outputs.
    ///   false if scene cannot be applied to hardware (not yet, or maybe not at all); applying to hardware, if
    ///   needed at all, will be triggered otherwise.
    /// @note This method must NOT call device level applyChannelValues() to actually apply values to hardware for
    ///   a one-step scene value change.
    ///   It MAY cause subsequent applyChannelValues() calls AFTER returning to perform special effects
    /// @note this method does not handle dimming, and must not be called with dimming specific scenes. For dimming,
    ///   only dimChannel method must be used.
    /// @note base class' implementation provides applying the scene values to channels.
    ///   Derived classes may implement handling of hard-wired behaviour specific scenes.
    virtual bool applyScene(DsScenePtr aScene);

    /// called by applyScene to load channel values from a scene.
    /// @param aScene the scene to load channel values from
    /// @note Scenes don't have 1:1 representation of all channel values for footprint and logic reasons, so this method
    ///   is implemented in the specific behaviours according to the scene layout for that behaviour.
    virtual void loadChannelsFromScene(DsScenePtr aScene);

    /// called by captureScene to save channel values to a scene.
    /// @param aScene the scene to save channel values to
    /// @note Scenes don't have 1:1 representation of all channel values for footprint and logic reasons, so this method
    ///   is implemented in the specific behaviours according to the scene layout for that behaviour.
    /// @note call markDirty on aScene in case it is changed (otherwise captured values will not be saved)
    virtual void saveChannelsToScene(DsScenePtr aScene);

    // the behaviour type
    virtual BehaviourType getType() P44_OVERRIDE { return behaviour_output; };

    // for groups property
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;

    // property access implementation for descriptor/settings/states
    virtual int numDescProps() P44_OVERRIDE;
    virtual const PropertyDescriptorPtr getDescDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual int numSettingsProps() P44_OVERRIDE;
    virtual const PropertyDescriptorPtr getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual int numStateProps() P44_OVERRIDE;
    virtual const PropertyDescriptorPtr getStateDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    // combined field access for all types of properties
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

    // persistence implementation
    enum {
      outputflag_pushChanges = 0x0001,
      outputflag_nextflag = 1<<1
    };
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

  private:

    void channelValuesCaptured(DsScenePtr aScene, bool aFromDevice, SimpleCB aDoneCB);

  };
  
  typedef boost::intrusive_ptr<OutputBehaviour> OutputBehaviourPtr;

} // namespace p44

#endif /* defined(__p44vdc__outputbehaviour__) */
