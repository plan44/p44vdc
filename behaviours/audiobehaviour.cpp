//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2015-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "audiobehaviour.hpp"

#include <math.h>

using namespace p44;


// MARK: - audio scene values/channels

AudioScene::AudioScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
  inherited(aSceneDeviceSettings, aSceneNo),
  powerState(powerState_off)
{
}


string AudioScene::sceneValueString(int aChannelIndex)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_p44_audio_content_source: return contentSource;
    default: return inherited::sceneValueString(aChannelIndex);
  }
}

void AudioScene::setSceneValueString(int aChannelIndex, const string& aValue)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_p44_audio_content_source: setPVar(contentSource, aValue); break;
    default: inherited::setSceneValueString(aChannelIndex, aValue); break;
  }
}


double AudioScene::sceneValue(int aChannelIndex)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_power_state: return powerState;
    default: return inherited::sceneValue(aChannelIndex);
  }
}


void AudioScene::setSceneValue(int aChannelIndex, double aValue)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_power_state: setPVar(powerState, (DsPowerState)aValue); break;
    default: inherited::setSceneValue(aChannelIndex, aValue); break;
  }
}



// MARK: - Audio Scene persistence

const char *AudioScene::tableName()
{
  return "AudioScenes";
}

// data field definitions

static const size_t numAudioSceneFields = 2;

size_t AudioScene::numFieldDefs()
{
  return inherited::numFieldDefs()+numAudioSceneFields;
}


const FieldDefinition *AudioScene::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numAudioSceneFields] = {
    { "contentSourceStr", SQLITE_TEXT },
    { "powerState", SQLITE_INTEGER },
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numAudioSceneFields)
    return &dataDefs[aIndex];
  return NULL;
}


// flags in globalSceneFlags
enum {
  // parent uses bit 0 and 1 (globalflags_sceneLevelMask) and bits 8..23
  // audio scene global
  audioflags_fixvol = 0x0004, ///< fixed (always recalled) volume
  audioflags_message = 0x0008, ///< is a message
  audioflags_priority = 0x0010, ///< is a priority message
  audioflags_interruptible = 0x0020, ///< is an interruptible message
  audioflags_paused_restore = 0x0040, ///< paused restore after message
};



/// load values from passed row
void AudioScene::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the fields
  contentSource = nonNullCStr(aRow->get<const char *>(aIndex++));
  powerState = (DsPowerState)aRow->get<int>(aIndex++);
}


/// bind values to passed statement
void AudioScene::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, contentSource.c_str(), false);
  aStatement.bind(aIndex++, (int)powerState);
}


// MARK: - Audio Scene properties

enum {
  fixvol_key,
  message_key,
  priority_key,
  interruptible_key,
  pausedRestore_key,
  numSceneProperties
};

static char audioscene_key;


int AudioScene::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return inherited::numProps(aDomain, aParentDescriptor)+numSceneProperties;
}



PropertyDescriptorPtr AudioScene::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // scene level properties
  static const PropertyDescription sceneproperties[numSceneProperties] = {
    { "fixvol", apivalue_bool, fixvol_key, OKEY(audioscene_key) },
    { "message", apivalue_bool, message_key, OKEY(audioscene_key) },
    { "priority", apivalue_bool, priority_key, OKEY(audioscene_key) },
    { "interruptible", apivalue_bool, interruptible_key, OKEY(audioscene_key) },
    { "pausedRestore", apivalue_bool, pausedRestore_key, OKEY(audioscene_key) },
  };
  int n = inherited::numProps(aDomain, aParentDescriptor);
  if (aPropIndex<n)
    return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
  aPropIndex -= n; // rebase to 0 for my own first property
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&sceneproperties[aPropIndex], aParentDescriptor));
}


bool AudioScene::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(audioscene_key)) {
    // global scene level
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case fixvol_key: aPropValue->setBoolValue(hasFixVol()); return true;
        case message_key: aPropValue->setBoolValue(isMessage()); return true;
        case priority_key: aPropValue->setBoolValue(hasPriority()); return true;
        case interruptible_key: aPropValue->setBoolValue(isInterruptible()); return true;
        case pausedRestore_key: aPropValue->setBoolValue(hasPausedRestore()); return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        case fixvol_key: setFixVol(aPropValue->boolValue()); return true;
        case message_key: setMessage(aPropValue->boolValue()); return true;
        case priority_key: setPriority(aPropValue->boolValue()); return true;
        case interruptible_key: setInterruptible(aPropValue->boolValue()); return true;
        case pausedRestore_key: setPausedRestore(aPropValue->boolValue()); return true;
      }
    }
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: - default audio scene

void AudioScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // set the common simple scene defaults
  inherited::setDefaultSceneValues(aSceneNo);
  // Add special audio scene behaviour
  bool psi = false; // default: dont ignore power state
  bool sci = false; // default: dont ignore content source
  switch (aSceneNo) {
    // group related scenes
    case AUDIO_REPEAT_OFF: mSceneCmd = scene_cmd_audio_repeat_off; break;
    case AUDIO_REPEAT_1: mSceneCmd = scene_cmd_audio_repeat_1; break;
    case AUDIO_REPEAT_ALL: mSceneCmd = scene_cmd_audio_repeat_all; break;
    case AUDIO_PREV_TITLE: mSceneCmd = scene_cmd_audio_previous_title; break;
    case AUDIO_NEXT_TITLE: mSceneCmd = scene_cmd_audio_next_title; break;
    case AUDIO_PREV_CHANNEL: mSceneCmd = scene_cmd_audio_previous_channel; break;
    case AUDIO_NEXT_CHANNEL: mSceneCmd = scene_cmd_audio_next_channel; break;
    case AUDIO_MUTE: mSceneCmd = scene_cmd_audio_mute; break;
    case AUDIO_UNMUTE: mSceneCmd = scene_cmd_audio_unmute; break;
    case AUDIO_PLAY: mSceneCmd = scene_cmd_audio_play; break;
    case AUDIO_PAUSE: mSceneCmd = scene_cmd_audio_pause; break;
    case AUDIO_SHUFFLE_OFF: mSceneCmd = scene_cmd_audio_shuffle_off; break;
    case AUDIO_SHUFFLE_ON: mSceneCmd = scene_cmd_audio_shuffle_on; break;
    case AUDIO_RESUME_OFF: mSceneCmd = scene_cmd_audio_resume_off; break;
    case AUDIO_RESUME_ON: mSceneCmd = scene_cmd_audio_resume_on; break;
    // group independent scenes
    case BELL1:
    case BELL2:
    case BELL3:
    case BELL4:
      // Non-Standard: simple messages
      mGlobalSceneFlags |= audioflags_fixvol|audioflags_message;
      value = 30;
      break;
    case PANIC:
      value = 0; // silent on panic
      mGlobalSceneFlags |= audioflags_fixvol;
      sci = true;
      psi = true;
      break;
    case STANDBY:
      powerState = powerState_standby;
      sci = true;
      break;
    case DEEP_OFF:
      powerState = powerState_off;
      sci = true;
      break;
    case SLEEPING:
    case ABSENT:
      powerState = powerState_standby;
      sci = true;
      break;
    case GAS:
      psi = true;
      // fall through
    case FIRE:
    case SMOKE:
    case WATER:
      mGlobalSceneFlags |= audioflags_paused_restore;
      // fall through
    case ALARM1:
    case ALARM2:
    case ALARM3:
    case ALARM4:
      mGlobalSceneFlags |= audioflags_priority;
      // fall through
    case HAIL:
      value = 30;
      mGlobalSceneFlags |= audioflags_fixvol|audioflags_message;
      break;
  }
  // adjust volume default setting
  if (value>0) {
    value=30; // all non-zero volume presets are 30%
  }
  if (
    (aSceneNo>=PRESET_2 && aSceneNo<=PRESET_41) || // standard invoke scenes
    (aSceneNo==ROOM_OFF) || // main off
    (aSceneNo==ROOM_ON) // main on
  ) {
    // powerstate follows volume
    powerState = value>0 ? powerState_on : powerState_off;
    // fixvol for mute scenes
    if (value==0) {
      mGlobalSceneFlags |= audioflags_fixvol;
    }
  }
  // adjust per-channel dontcare
  AudioBehaviourPtr ab = boost::dynamic_pointer_cast<AudioBehaviour>(getOutputBehaviour());
  if (ab) {
    if (psi) setSceneValueFlags(ab->powerState->getChannelIndex(), valueflags_dontCare, true);
    if (sci) setSceneValueFlags(ab->contentSource->getChannelIndex(), valueflags_dontCare, true);
  }
  markClean(); // default values are always clean
}

// MARK: - FixVol


bool AudioScene::hasFixVol()
{
  return (mGlobalSceneFlags & audioflags_fixvol)!=0;
}

void AudioScene::setFixVol(bool aNewValue)
{
  setGlobalSceneFlag(audioflags_fixvol, aNewValue);
}


bool AudioScene::isMessage()
{
  return (mGlobalSceneFlags & audioflags_message)!=0;
}

void AudioScene::setMessage(bool aNewValue)
{
  setGlobalSceneFlag(audioflags_message, aNewValue);
}


bool AudioScene::hasPriority()
{
  return (mGlobalSceneFlags & audioflags_priority)!=0;
}

void AudioScene::setPriority(bool aNewValue)
{
  setGlobalSceneFlag(audioflags_priority, aNewValue);
}


bool AudioScene::isInterruptible()
{
  return (mGlobalSceneFlags & audioflags_interruptible)!=0;
}

void AudioScene::setInterruptible(bool aNewValue)
{
  setGlobalSceneFlag(audioflags_interruptible, aNewValue);
}


bool AudioScene::hasPausedRestore()
{
  return (mGlobalSceneFlags & audioflags_paused_restore)!=0;
}

void AudioScene::setPausedRestore(bool aNewValue)
{
  setGlobalSceneFlag(audioflags_paused_restore, aNewValue);
}



// MARK: - AudioDeviceSettings with default audio scenes factory


AudioDeviceSettings::AudioDeviceSettings(Device &aDevice) :
  inherited(aDevice)
{
};


DsScenePtr AudioDeviceSettings::newDefaultScene(SceneNo aSceneNo)
{
  AudioScenePtr audioScene = AudioScenePtr(new AudioScene(*this, aSceneNo));
  audioScene->setDefaultSceneValues(aSceneNo);
  // return it
  return audioScene;
}


DsScenePtr AudioDeviceSettings::newUndoStateScene()
{
  // get standard undo state scene
  AudioScenePtr undoStateScene =  boost::dynamic_pointer_cast<AudioScene>(inherited::newUndoStateScene());
  // adjust flags for restoring a state
  if (undoStateScene) {
    undoStateScene->setFixVol(true);
  }
  return undoStateScene;
}





// MARK: - AudioBehaviour

AudioBehaviour::AudioBehaviour(Device &aDevice) :
  inherited(aDevice),
  // hardware derived parameters
  // persistent settings
  // volatile state
  unmuteVolume(0),
  knownPaused(false),
  stateRestoreCmdValid(false)
{
  // make it member of the audio group
  setGroupMembership(group_cyan_audio, true);
  // primary output controls volume
  setHardwareName("volume");
  // add the audio device channels
  // - volume (default channel, comes first)
  volume = AudioVolumeChannelPtr(new AudioVolumeChannel(*this));
  addChannel(volume);
  // - power state
  powerState = PowerStateChannelPtr(new PowerStateChannel(*this));
  addChannel(powerState);
  // - content source
  contentSource = AudioContentSourceChannelPtr(new AudioContentSourceChannel(*this));
  addChannel(contentSource);
}


Tristate AudioBehaviour::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // now check for audio behaviour level features
  switch (aFeatureIndex) {
    case modelFeature_outmodegeneric:
      // wants generic output mode
      return yes;
    default:
      // not available at this level, ask base class
      return inherited::hasModelFeature(aFeatureIndex);
  }
}



// MARK: - behaviour interaction with Digital Strom system


#define AUTO_OFF_FADE_TIME (1800*Second)
#define AUTO_OFF_FADE_STEPSIZE 5

// apply scene
bool AudioBehaviour::performApplySceneToChannels(DsScenePtr aScene, SceneCmd aSceneCmd)
{
  // check special actions (commands) for audio scenes
  AudioScenePtr audioScene = boost::dynamic_pointer_cast<AudioScene>(aScene);
  if (audioScene) {
    // any scene call cancels actions (such as fade down)
    stopSceneActions();
    // Note: some of the audio special commands are handled at the applyChannelValues() level
    //   in the device, using sceneContextForApply().
    // Now check for the commands that can be handled at the behaviour level
    switch (aSceneCmd) {
      case scene_cmd_audio_mute:
        unmuteVolume = volume->getChannelValue(); ///< save current volume
        volume->setChannelValue(0); // mute
        return true; // don't let inherited load channels, just request apply
      case scene_cmd_audio_unmute:
        volume->setChannelValue(unmuteVolume>0 ? unmuteVolume : 1); // restore value known before last mute, but at least non-zero
        return true; // don't let inherited load channels, just request apply
      case scene_cmd_slow_off:
        // TODO: %%% implement it. For now, just invoke
        aSceneCmd = scene_cmd_invoke;
        break;
      default:
        break;
    }
  } // if audio scene
  // perform standard apply (loading channels)
  return inherited::performApplySceneToChannels(aScene, aSceneCmd);
}


void AudioBehaviour::loadChannelsFromScene(DsScenePtr aScene)
{
  AudioScenePtr audioScene = boost::dynamic_pointer_cast<AudioScene>(aScene);
  if (audioScene) {
    // load channels from scene
    // - volume: ds-audio says: "If the flag is not set, the volume setting of the previously set scene
    //   will be taken over unchanged unless the device was off before the scene call."
    if ((powerState->getChannelValue()!=powerState_on) || knownPaused || audioScene->hasFixVol()) {
      // device was off or paused before, or fixvol is set
      volume->setChannelValueIfNotDontCare(aScene, audioScene->value, 0, 0, true); // always apply
    }
    // - powerstate
    powerState->setChannelValueIfNotDontCare(aScene, audioScene->powerState, 0, 0, false);
    // - content source
    contentSource->setChannelValueIfNotDontCare(aScene, audioScene->contentSource, !audioScene->mCommand.empty()); // always apply if there is a command
    // - state restore command
    stateRestoreCmd = audioScene->mCommand;
    stateRestoreCmdValid = !audioScene->mCommand.empty(); // only non-empty command is considered valid
  }
}


void AudioBehaviour::saveChannelsToScene(DsScenePtr aScene)
{
  AudioScenePtr audioScene = boost::dynamic_pointer_cast<AudioScene>(aScene);
  if (audioScene) {
    // save channels from scene
    audioScene->setPVar(audioScene->value, volume->getChannelValue());
    audioScene->setSceneValueFlags(volume->getChannelIndex(), valueflags_dontCare, false);
    audioScene->setPVar(audioScene->powerState, (DsPowerState)powerState->getChannelValue());
    audioScene->setSceneValueFlags(powerState->getChannelIndex(), valueflags_dontCare, false);
    audioScene->setPVar(audioScene->contentSource, contentSource->getChannelValueString());
    audioScene->setSceneValueFlags(contentSource->getChannelIndex(), valueflags_dontCare, false);
    // save command from scene if there is one
    if (stateRestoreCmdValid) {
      audioScene->setPVar(audioScene->mCommand, stateRestoreCmd);
    }
  }
}


// dS Dimming rule for Audio:
//  "All selected devices which are turned on and in play state take part in the dimming process."

bool AudioBehaviour::canDim(ChannelBehaviourPtr aChannel)
{
  // only devices that are on can be dimmed (volume changed)
  if (aChannel->getChannelType()==channeltype_audio_volume) {
    return powerState->getChannelValue()==powerState_on; // dimmable if on
  }
  else {
    // other audio channels cannot be dimmed anyway
    return false;
  }
}


void AudioBehaviour::performSceneActions(DsScenePtr aScene, SimpleCB aDoneCB)
{
  // we can only handle audio scenes
  AudioScenePtr audioScene = boost::dynamic_pointer_cast<AudioScene>(aScene);
  if (audioScene) {
    // TODO: check for blink effect?
  }
  // none of my effects, let inherited check
  inherited::performSceneActions(aScene, aDoneCB);
}


void AudioBehaviour::stopSceneActions()
{
  // let inherited stop as well
  inherited::stopSceneActions();
}



void AudioBehaviour::identifyToUser(MLMicroSeconds aDuration)
{
  // blink effect?
  // TODO: %%% implement it, and change canIdentifyToUser() result when done
}


// MARK: - description/shortDesc


string AudioBehaviour::shortDesc()
{
  return string("Audio");
}


string AudioBehaviour::description()
{
  string s = string_format("%s behaviour\n", shortDesc().c_str());
  string_format_append(s,
    "\n- volume = %.1f, powerstate = %d, contentsource = %u",
    volume->getChannelValue(),
    (int)powerState->getChannelValue(),
    (unsigned int)contentSource->getChannelValue()
  );
  s.append(inherited::description());
  return s;
}







