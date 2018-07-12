//
//  Copyright (c) 2015-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__audiobehaviour__
#define __p44vdc__audiobehaviour__

#include "device.hpp"
#include "simplescene.hpp"
#include "outputbehaviour.hpp"

using namespace std;

namespace p44 {

  /// Audio volume channel, 0..100%
  class AudioVolumeChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;
    double dimPerMS;

  public:
    AudioVolumeChannel(OutputBehaviour &aOutput) : inherited(aOutput, "audioVolume")
    {
      resolution = 0.1; // arbitrary, 1:1000 seems ok
      dimPerMS = (getMax()-getMin())/7000; // standard 7 seconds for full scale by default
    };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_audio_volume; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "volume"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // dS volume goes from 0 to 100%
    virtual double getMax() P44_OVERRIDE { return 100; };
    virtual double getDimPerMS() P44_OVERRIDE { return dimPerMS; }; ///< value to step up or down per Millisecond

    virtual void setDimPerMS(double aDimPerMS) { dimPerMS = aDimPerMS; }; ///< set dimming per MS to make actual audio steps and dimming steps align better than with standard step 

  };
  typedef boost::intrusive_ptr<AudioVolumeChannel> AudioVolumeChannelPtr;


  /// Audio content source channel
  class AudioContentSourceChannel : public IndexChannel
  {
    typedef IndexChannel inherited;

  public:
    AudioContentSourceChannel(OutputBehaviour &aOutput) : inherited(aOutput, "contentSource") {};

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_p44_audio_content_source; }; ///< the dS channel type
    virtual const char *getName() P44_OVERRIDE { return "contentsource"; };

  };
  typedef boost::intrusive_ptr<AudioContentSourceChannel> AudioContentSourceChannelPtr;



  /// A concrete class implementing the Scene object for a audio device, having a volume channel plus a index value (for specific song/sound effects)
  /// @note subclasses can implement more parameters
  class AudioScene : public SimpleCmdScene
  {
    typedef SimpleCmdScene inherited;

  public:
    AudioScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo);

    /// @name audio scene specific values
    /// @{

    uint32_t contentSource; ///< the index of a content source, e.g. a song/sound effect from a list
    DsPowerState powerState; ///< the power state of the audio device

    /// @}

    /// Set default scene values for a specified scene number
    /// @param aSceneNo the scene number to set default values
    virtual void setDefaultSceneValues(SceneNo aSceneNo) P44_OVERRIDE;

    // scene values implementation
    virtual double sceneValue(int aChannelIndex) P44_OVERRIDE;
    virtual void setSceneValue(int aChannelIndex, double aValue) P44_OVERRIDE;

    // query flags
    bool hasFixVol();
    bool isMessage();
    bool hasPriority();
    bool isInterruptible();
    bool hasPausedRestore();

    // set flags
    void setFixVol(bool aNewValue);
    void setMessage(bool aNewValue);
    void setPriority(bool aNewValue);
    void setInterruptible(bool aNewValue);
    void setPausedRestore(bool aNewValue);


  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

    // persistence implementation
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<AudioScene> AudioScenePtr;



  /// the persistent parameters of a audio scene device (including scene table)
  /// @note subclasses can implement more parameters
  class AudioDeviceSettings : public CmdSceneDeviceSettings
  {
    typedef CmdSceneDeviceSettings inherited;

  public:
    AudioDeviceSettings(Device &aDevice);

    /// factory method to create the correct subclass type of DsScene
    /// @param aSceneNo the scene number to create a scene object for.
    /// @note setDefaultSceneValues() must be called to set default scene values
    virtual DsScenePtr newDefaultScene(SceneNo aSceneNo) P44_OVERRIDE;

    /// factory method to create the correct subclass type of DsScene
    /// suitable for storing current state for later undo.
    virtual DsScenePtr newUndoStateScene() P44_OVERRIDE;

  };


  /// Implements the behaviour of a digitalSTROM Audio device
  class AudioBehaviour : public OutputBehaviour
  {
    typedef OutputBehaviour inherited;


    /// @name hardware derived parameters (constant during operation)
    /// @{
    /// @}


    /// @name persistent settings
    /// @{
    /// @}


    /// @name internal volatile state
    /// @{
    double unmuteVolume; ///< volume that was present when last "mute" command was found, will be restored at "unmute"
    /// @}


  public:

    AudioBehaviour(Device &aDevice);

    /// device type identifier
    /// @return constant identifier for this type of behaviour
    virtual const char *behaviourTypeIdentifier() P44_OVERRIDE P44_FINAL { return "audio"; };

    /// the volume channel
    AudioVolumeChannelPtr volume;
    /// the power state channel
    PowerStateChannelPtr powerState;
    /// the content source channel
    AudioContentSourceChannelPtr contentSource;

    /// the current state command
    bool stateRestoreCmdValid; ///< set if state restore command is valid
    string stateRestoreCmd; ///< scene command that will restore current state (beyond what is stored in the channels)

    bool knownPaused; ///< paused


    /// @name interaction with digitalSTROM system
    /// @{

    /// check for presence of model feature (flag in dSS visibility matrix)
    /// @param aFeatureIndex the feature to check for
    /// @return yes if this output behaviour has the feature, no if (explicitly) not, undefined if asked entity does not know
    virtual Tristate hasModelFeature(DsModelFeatures aFeatureIndex) P44_OVERRIDE;

    /// apply scene to output channels
    /// @param aScene the scene to apply to output channels
    /// @param aSceneCmd This will be used instead of the scenecommand stored in the scene. This
    /// @return true if apply is complete, i.e. everything ready to apply to hardware outputs.
    ///   false if scene cannot yet be applied to hardware, and will be performed later
    /// @note this derived class' performApplySceneToChannels() only implements special hard-wired behaviour specific scenes
    ///   basic scene apply functionality is provided by base class' implementation already.
    virtual bool performApplySceneToChannels(DsScenePtr aScene, SceneCmd aSceneCmd) P44_OVERRIDE;

    /// perform special scene actions (like flashing) which are independent of dontCare flag.
    /// @param aScene the scene that was called (if not dontCare, performApplySceneToChannels() has already been called)
    /// @param aDoneCB will be called when scene actions have completed (but not necessarily when stopped by stopSceneActions())
    virtual void performSceneActions(DsScenePtr aScene, SimpleCB aDoneCB) P44_OVERRIDE;

    /// will be called to stop all ongoing actions before next callScene etc. is issued.
    /// @note this must stop all ongoing actions such that applying another scene or action right afterwards
    ///   cannot mess up things.
    virtual void stopSceneActions() P44_OVERRIDE;

    /// check if this channel of this device is allowed to dim now (for lights, this will prevent dimming lights that are off)
    /// @param aChannel the channel to check
    virtual bool canDim(ChannelBehaviourPtr aChannel) P44_OVERRIDE;

    /// identify the device to the user in a behaviour-specific way
    /// @note implemented as blinking for LightBehaviour
    virtual void identifyToUser() P44_OVERRIDE;

    /// @}


    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description() P44_OVERRIDE;

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc() P44_OVERRIDE;

  protected:

    /// called by performApplySceneToChannels() to load channel values from a scene.
    /// @param aScene the scene to load channel values from
    /// @note Scenes don't have 1:1 representation of all channel values for footprint and logic reasons, so this method
    ///   is implemented in the specific behaviours according to the scene layout for that behaviour.
    virtual void loadChannelsFromScene(DsScenePtr aScene) P44_OVERRIDE;

    /// called by captureScene to save channel values to a scene.
    /// @param aScene the scene to save channel values to
    /// @note Scenes don't have 1:1 representation of all channel values for footprint and logic reasons, so this method
    ///   is implemented in the specific behaviours according to the scene layout for that behaviour.
    /// @note call markDirty on aScene in case it is changed (otherwise captured values will not be saved)
    virtual void saveChannelsToScene(DsScenePtr aScene) P44_OVERRIDE;

  };

  typedef boost::intrusive_ptr<AudioBehaviour> AudioBehaviourPtr;

} // namespace p44

#endif /* defined(__p44vdc__audiobehaviour__) */
