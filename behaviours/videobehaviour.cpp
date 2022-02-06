//
//  Copyright (c) 2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "videobehaviour.hpp"

using namespace p44;


// MARK: - video scene values/channels

VideoScene::VideoScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
  inherited(aSceneDeviceSettings, aSceneNo),
  station(0),
  inputSource(0),
  powerState(powerState_off)
{
}


double VideoScene::sceneValue(int aChannelIndex)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
//    case channeltype_p44_audio_content_source: return contentSource;
    case channeltype_video_station: return station;
    case channeltype_video_input_source: return inputSource;
    case channeltype_power_state: return powerState;
    default: return inherited::sceneValue(aChannelIndex);
  }
  return 0;
}


void VideoScene::setSceneValue(int aChannelIndex, double aValue)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
//    case channeltype_p44_audio_content_source: setPVar(contentSource, (uint32_t)aValue); break;
    case channeltype_video_station: setPVar(station, (uint)aValue); break;
    case channeltype_video_input_source: setPVar(inputSource, (uint)aValue); break;
    case channeltype_power_state: setPVar(powerState, (DsPowerState)aValue); break;
    default: inherited::setSceneValue(aChannelIndex, aValue); break;
  }
}


// MARK: - Video Scene persistence

const char *VideoScene::tableName()
{
  return "VideoScenes";
}

// data field definitions

static const size_t numVideoSceneFields = 3;

size_t VideoScene::numFieldDefs()
{
  return inherited::numFieldDefs()+numVideoSceneFields;
}


const FieldDefinition *VideoScene::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numVideoSceneFields] = {
    { "station", SQLITE_INTEGER },
    { "inputSource", SQLITE_INTEGER },
    { "powerState", SQLITE_INTEGER },
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numVideoSceneFields)
    return &dataDefs[aIndex];
  return NULL;
}


// flags in globalSceneFlags
enum {
  // parent uses bit 0 and 1 (globalflags_sceneLevelMask) and bits 8..23
  // video scene global (same bits as in audio)
  videoflags_fixvol = 0x0004, ///< fixed (always recalled) volume
  videoflags_message = 0x0008, ///< is a message
};



/// load values from passed row
void VideoScene::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the fields
  station = aRow->get<int>(aIndex++);
  inputSource = aRow->get<int>(aIndex++);
  powerState = (DsPowerState)aRow->get<int>(aIndex++);
}


/// bind values to passed statement
void VideoScene::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, (int)station);
  aStatement.bind(aIndex++, (int)inputSource);
  aStatement.bind(aIndex++, (int)powerState);
}


// MARK: - Video Scene properties

enum {
  fixvol_key,
  message_key,
  numSceneProperties
};

static char videoscene_key;


int VideoScene::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return inherited::numProps(aDomain, aParentDescriptor)+numSceneProperties;
}



PropertyDescriptorPtr VideoScene::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // scene level properties
  static const PropertyDescription sceneproperties[numSceneProperties] = {
    { "fixvol", apivalue_bool, fixvol_key, OKEY(videoscene_key) },
    { "message", apivalue_bool, message_key, OKEY(videoscene_key) },
  };
  int n = inherited::numProps(aDomain, aParentDescriptor);
  if (aPropIndex<n)
    return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
  aPropIndex -= n; // rebase to 0 for my own first property
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&sceneproperties[aPropIndex], aParentDescriptor));
}


bool VideoScene::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(videoscene_key)) {
    // global scene level
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case fixvol_key: aPropValue->setBoolValue(hasFixVol()); return true;
        case message_key: aPropValue->setBoolValue(isMessage()); return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        case fixvol_key: setFixVol(aPropValue->boolValue()); return true;
        case message_key: setMessage(aPropValue->boolValue()); return true;
      }
    }
  }
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: - default video scene

void VideoScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // set the common simple scene defaults
  inherited::setDefaultSceneValues(aSceneNo);
  // Add special video scene behaviour
  mEffect = scene_effect_none; // no smooth transitions
  bool psi = true; // default: ignore power state
  bool sci = true; // default: ignore content source
  bool voli = true; // default:ignore volume
  // adjust volume default setting
  if (value>0) {
    value=30; // all non-zero volume presets are 30%
    voli = false;
  }
  else if (value==0){
    voli = true; // in general, volume 0 means no volume change
  }
  switch (aSceneNo) {
    case AUTO_OFF :
      voli = false; // apply zero volume
      mEffect = scene_effect_transition; // transition...
      mEffectParam = 30*Minute; // ...of 30min
      break;
    case ROOM_OFF :
    case PRESET_OFF_10:
    case PRESET_OFF_20:
    case PRESET_OFF_30:
    case PRESET_OFF_40:
      voli = true; // do not change volume...
      powerState = powerState_standby; // ...but switch to standby
      psi = false;
      break;
    case AREA_1_ON:
    case AREA_2_ON:
    case AREA_3_ON:
    case AREA_4_ON:
    case T1234_CONT:
    case LOCAL_ON:
    case WAKE_UP:
    case PRESENT:
    case WATER:
    case ZONE_ACTIVE:
      // reserved in this behaviour (but active in standard scene -> disable)
      voli = true;
      break;
    case MIN_S :
    case MAX_S :
      voli = false;
      mGlobalSceneFlags |= videoflags_fixvol;
      break;
    // group related scenes
    case AUDIO_PREV_TITLE: voli = true; mSceneCmd = scene_cmd_audio_previous_title; break;
    case AUDIO_NEXT_TITLE: voli = true; mSceneCmd = scene_cmd_audio_next_title; break;
    case AUDIO_PREV_CHANNEL: voli = true; mSceneCmd = scene_cmd_audio_previous_channel; break;
    case AUDIO_NEXT_CHANNEL: voli = true; mSceneCmd = scene_cmd_audio_next_channel; break;
    case AUDIO_MUTE:
      mSceneCmd = scene_cmd_audio_mute;
      value = 0;
      voli = false;
      mGlobalSceneFlags |= videoflags_fixvol;
      psi = true;
      break;
    case AUDIO_UNMUTE: mSceneCmd = scene_cmd_audio_unmute; break;
    case AUDIO_PLAY: mSceneCmd = scene_cmd_audio_play; break;
    case AUDIO_PAUSE: mSceneCmd = scene_cmd_audio_pause; break;
    case AUDIO_SHUFFLE_OFF: mSceneCmd = scene_cmd_audio_shuffle_off; break;
    case AUDIO_SHUFFLE_ON: mSceneCmd = scene_cmd_audio_shuffle_on; break;
    case AUDIO_RESUME_OFF: mSceneCmd = scene_cmd_audio_resume_off; break;
    case AUDIO_RESUME_ON: mSceneCmd = scene_cmd_audio_resume_on; break;
    // group independent scenes
    case PANIC:
      mGlobalSceneFlags |= videoflags_message;
      powerState = powerState_on;
      psi = false;
      voli = true;
      break;
    case STANDBY:
    case SLEEPING:
    case ABSENT:
      powerState = powerState_standby;
      psi = false;
      break;
    case DEEP_OFF:
      powerState = powerState_off;
      psi = false;
      break;
    case ALARM1:
    case ALARM2:
    case ALARM3:
    case ALARM4:
    case FIRE:
    case SMOKE:
      powerState = powerState_on;
      psi = false;
      // fall through
    case GAS:
    case HAIL:
      voli = true; // messages (if possible) are visual, so no volume change!
      mGlobalSceneFlags |= videoflags_message;
      break;
  }
  // in general, power state follows actively set volume
  if (!voli && aSceneNo!=AUDIO_MUTE) {
    powerState = value>0 ? powerState_on : powerState_standby;
    psi = false;
    // fixvol for mute scenes
    if (value==0) {
      mGlobalSceneFlags |= videoflags_fixvol;
    }
  }
  // adjust per-channel dontcare
  VideoBehaviourPtr vb = boost::dynamic_pointer_cast<VideoBehaviour>(getOutputBehaviour());
  if (vb) {
    if (voli) setSceneValueFlags(vb->volume->getChannelIndex(), valueflags_dontCare, true);
    if (psi) setSceneValueFlags(vb->powerState->getChannelIndex(), valueflags_dontCare, true);
    if (sci) {
      setSceneValueFlags(vb->station->getChannelIndex(), valueflags_dontCare, true);
      setSceneValueFlags(vb->inputSource->getChannelIndex(), valueflags_dontCare, true);
    }
  }
  markClean(); // default values are always clean
}


#if DEBUG

void VideoDeviceSettings::dumpDefaultScenes()
{
  printf("SC\tSCI\tPS\tPSI\tVol\tVolI\tVolF\tMM\tTT\tCS\tCSI\n");
  for (SceneNo sn = START_ZONE_SCENES; sn<MAX_SCENE_NO; sn++) {
    VideoScenePtr videoScene = VideoScenePtr(new VideoScene(*this, sn));
    videoScene->setDefaultSceneValues(sn);
    string s;
    bool voli = videoScene->sceneValueFlags(0)&valueflags_dontCare;
    bool psi = videoScene->sceneValueFlags(1)&valueflags_dontCare;
    bool csi = videoScene->sceneValueFlags(2)&valueflags_dontCare;
    s = string_format("%d\t", sn); // scene number
    string_format_append(s, "%s\t", videoScene->isDontCare() ? "1" : "-"); // SCI = global ignore flag
    if (psi) s+="-\t"; else string_format_append(s, "%d\t", (int)videoScene->sceneValue(1)); // PS = power state
    string_format_append(s, "%s\t", psi ? "1" : "-"); // PSI = power state ignore flag
    if (voli) s+="-\t"; else string_format_append(s, "%d\t", (int)videoScene->sceneValue(0)); // Vol = audio volume
    string_format_append(s, "%s\t", voli ? "1" : "-"); // VolI = volume ignore flag
    string_format_append(s, "%s\t", videoScene->hasFixVol() ? "1" : "-"); // VolF = fixvol
    string_format_append(s, "%s\t", videoScene->isMessage() ? "1" : "-"); // MM = message
    string_format_append(s, "%.3f\t", (double)mDevice.getOutput()->transitionTimeFromScene(videoScene, true)/Second);
    if (csi) s+="-\t"; else string_format_append(s, "%d\t", (int)videoScene->sceneValue(2)); // CS = content source
    string_format_append(s, "%s\t", csi ? "1" : "-"); // CSI = content source ignore flag
    printf("%s\n", s.c_str());
  }
  printf("\n\n");
}

#endif // DEBUG



// MARK: - FixVol

bool VideoScene::hasFixVol()
{
  return (mGlobalSceneFlags & videoflags_fixvol)!=0;
}

void VideoScene::setFixVol(bool aNewValue)
{
  setGlobalSceneFlag(videoflags_fixvol, aNewValue);
}


bool VideoScene::isMessage()
{
  return (mGlobalSceneFlags & videoflags_message)!=0;
}

void VideoScene::setMessage(bool aNewValue)
{
  setGlobalSceneFlag(videoflags_message, aNewValue);
}


// MARK: - VideoDeviceSettings with default video scenes factory


VideoDeviceSettings::VideoDeviceSettings(Device &aDevice) :
  inherited(aDevice)
{
};


DsScenePtr VideoDeviceSettings::newDefaultScene(SceneNo aSceneNo)
{
  VideoScenePtr videoScene = VideoScenePtr(new VideoScene(*this, aSceneNo));
  videoScene->setDefaultSceneValues(aSceneNo);
  // return it
  return videoScene;
}


DsScenePtr VideoDeviceSettings::newUndoStateScene()
{
  // get standard undo state scene
  VideoScenePtr undoStateScene =  boost::dynamic_pointer_cast<VideoScene>(inherited::newUndoStateScene());
  // adjust flags for restoring a state
  if (undoStateScene) {
    undoStateScene->setFixVol(true);
  }
  return undoStateScene;
}





// MARK: - VideoBehaviour

#define STANDARD_DIM_CURVE_EXPONENT 4 // standard exponent, usually ok for PWM for LEDs

VideoBehaviour::VideoBehaviour(Device &aDevice) :
  inherited(aDevice),
  // hardware derived parameters
  // persistent settings
  // volatile state
  unmuteVolume(0),
  knownPaused(false),
  stateRestoreCmdValid(false)
{
  // make it member of the video group
  setGroupMembership(group_magenta_video, true);
  // primary output controls volume
  setHardwareName("volume");
  // add the video device channels
  // - volume (default channel, comes first)
  volume = AudioVolumeChannelPtr(new AudioVolumeChannel(*this));
  addChannel(volume);
  // - power state
  powerState = PowerStateChannelPtr(new PowerStateChannel(*this));
  addChannel(powerState);
  // - tv station
  station = VideoStationChannelPtr(new VideoStationChannel(*this));
  addChannel(station);
  // - tv input source
  inputSource = VideoInputSourceChannelPtr(new VideoInputSourceChannel(*this));
  addChannel(inputSource);
}


Tristate VideoBehaviour::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // now check for video behaviour level features
  switch (aFeatureIndex) {
    case modelFeature_outmodegeneric:
      // wants generic output mode
      return yes;
    default:
      // not available at this level, ask base class
      return inherited::hasModelFeature(aFeatureIndex);
  }
}



// MARK: - behaviour interaction with digitalSTROM system


#define AUTO_OFF_FADE_TIME (1800*Second)
#define AUTO_OFF_FADE_STEPSIZE 5

// apply scene
bool VideoBehaviour::performApplySceneToChannels(DsScenePtr aScene, SceneCmd aSceneCmd)
{
  // check special actions (commands) for video scenes
  VideoScenePtr videoScene = boost::dynamic_pointer_cast<VideoScene>(aScene);
  if (videoScene) {
    // any scene call cancels actions (such as fade down)
    stopSceneActions();
    // Note: some of the video special commands are handled at the applyChannelValues() level
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
  } // if video scene
  // perform standard apply (loading channels)
  return inherited::performApplySceneToChannels(aScene, aSceneCmd);
}


void VideoBehaviour::loadChannelsFromScene(DsScenePtr aScene)
{
  VideoScenePtr videoScene = boost::dynamic_pointer_cast<VideoScene>(aScene);
  if (videoScene) {
    // load channels from scene
    // - volume: ds-audio says: "If the flag is not set, the volume setting of the previously set scene
    //   will be taken over unchanged unless the device was off before the scene call."
    if ((powerState->getChannelValue()!=powerState_on) || knownPaused || videoScene->hasFixVol()) {
      // device was off or paused before, or fixvol is set
      volume->setChannelValueIfNotDontCare(aScene, videoScene->value, 0, 0, true); // always apply
    }
    // - powerstate
    powerState->setChannelValueIfNotDontCare(aScene, videoScene->powerState, 0, 0, false);
    // - tv station
    station->setChannelValueIfNotDontCare(aScene, videoScene->station, 0, 0, !videoScene->mCommand.empty()); // always apply if there is a command
    // - tv input source
    inputSource->setChannelValueIfNotDontCare(aScene, videoScene->inputSource, 0, 0, !videoScene->mCommand.empty()); // always apply if there is a command
    // - state restore command
    stateRestoreCmd = videoScene->mCommand;
    stateRestoreCmdValid = !videoScene->mCommand.empty(); // only non-empty command is considered valid
  }
}


void VideoBehaviour::saveChannelsToScene(DsScenePtr aScene)
{
  VideoScenePtr videoScene = boost::dynamic_pointer_cast<VideoScene>(aScene);
  if (videoScene) {
    // save channels from scene
    videoScene->setPVar(videoScene->value, volume->getChannelValue());
    videoScene->setSceneValueFlags(volume->getChannelIndex(), valueflags_dontCare, false);
    videoScene->setPVar(videoScene->powerState, (DsPowerState)powerState->getChannelValue());
    videoScene->setSceneValueFlags(powerState->getChannelIndex(), valueflags_dontCare, false);
    videoScene->setPVar(videoScene->station, (uint32_t)station->getChannelValue());
    videoScene->setSceneValueFlags(station->getChannelIndex(), valueflags_dontCare, false);
    videoScene->setPVar(videoScene->inputSource, (uint32_t)inputSource->getChannelValue());
    videoScene->setSceneValueFlags(inputSource->getChannelIndex(), valueflags_dontCare, false);
    // save command from scene if there is one
    if (stateRestoreCmdValid) {
      videoScene->setPVar(videoScene->mCommand, stateRestoreCmd);
    }
  }
}


// dS Dimming rule for audio (which I think makes sense for video, too):
//  "All selected devices which are turned on and in play state take part in the dimming process."

bool VideoBehaviour::canDim(ChannelBehaviourPtr aChannel)
{
  // only devices that are on can be dimmed (volume changed)
  if (aChannel->getChannelType()==channeltype_audio_volume) {
    return powerState->getChannelValue()==powerState_on; // dimmable if on
  }
  else {
    // other video channels cannot be dimmed anyway
    return false;
  }
}


void VideoBehaviour::performSceneActions(DsScenePtr aScene, SimpleCB aDoneCB)
{
  // we can only handle video scenes
  VideoScenePtr videoScene = boost::dynamic_pointer_cast<VideoScene>(aScene);
  if (videoScene) {
    // TODO: check for blink effect?
  }
  // none of my effects, let inherited check
  inherited::performSceneActions(aScene, aDoneCB);
}


void VideoBehaviour::stopSceneActions()
{
  // let inherited stop as well
  inherited::stopSceneActions();
}



void VideoBehaviour::identifyToUser()
{
  // blink effect?
  // TODO: %%% implement it, and change canIdentifyToUser() result when done
}


// MARK: - description/shortDesc


string VideoBehaviour::shortDesc()
{
  return string("Video");
}


string VideoBehaviour::description()
{
  string s = string_format("%s behaviour\n", shortDesc().c_str());
  string_format_append(s,
    "\n- volume = %.1f, powerstate = %d, station = %u, inputSource = %u",
    volume->getChannelValue(),
    (int)powerState->getChannelValue(),
    (unsigned int)station->getChannelValue(),
    (unsigned int)inputSource->getChannelValue()
  );
  s.append(inherited::description());
  return s;
}







