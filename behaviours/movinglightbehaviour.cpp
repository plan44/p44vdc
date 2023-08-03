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

#include "movinglightbehaviour.hpp"


using namespace p44;


// MARK: - MovingLightScene


MovingLightScene::MovingLightScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
  inherited(aSceneDeviceSettings, aSceneNo),
  hPos(0),
  vPos(0)
{
}


double MovingLightScene::sceneValue(int aChannelIndex)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_p44_position_h : return hPos;
    case channeltype_p44_position_v : return vPos;
    default: return inherited::sceneValue(aChannelIndex);
  }
  return 0;
}


void MovingLightScene::setSceneValue(int aChannelIndex, double aValue)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_p44_position_h : setPVar(hPos, aValue); break;
    case channeltype_p44_position_v : setPVar(vPos, aValue); break;
    default: inherited::setSceneValue(aChannelIndex, aValue); break;
  }
}


// MARK: - Moving Light Scene persistence

const char *MovingLightScene::tableName()
{
  return "MovingLightScenes";
}

// data field definitions

static const size_t numMovingLightSceneFields = 2;

size_t MovingLightScene::numFieldDefs()
{
  return inherited::numFieldDefs()+numMovingLightSceneFields;
}


const FieldDefinition *MovingLightScene::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numMovingLightSceneFields] = {
    { "hPos", SQLITE_FLOAT },
    { "vPos", SQLITE_FLOAT }
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numMovingLightSceneFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void MovingLightScene::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the fields
  hPos = aRow->get<double>(aIndex++);
  vPos = aRow->get<double>(aIndex++);
}


/// bind values to passed statement
void MovingLightScene::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, hPos);
  aStatement.bind(aIndex++, vPos);
}



// MARK: - default moving light scene

void MovingLightScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // set the common moving light scene defaults
  inherited::setDefaultSceneValues(aSceneNo);
  // No default value for position
  hPos = 0;
  vPos = 0;
  // set dontcare flags for position by default
  MovingLightBehaviourPtr movingLightBehaviour = boost::dynamic_pointer_cast<MovingLightBehaviour>(getOutputBehaviour());
  if (movingLightBehaviour) {
    setSceneValueFlags(movingLightBehaviour->horizontalPosition->getChannelIndex(), valueflags_dontCare, true);
    setSceneValueFlags(movingLightBehaviour->verticalPosition->getChannelIndex(), valueflags_dontCare, true);
  }
  markClean(); // default values are always clean (but setSceneValueFlags sets dirty)
}


// MARK: - MovingLightDeviceSettings with default light scenes factory


MovingLightDeviceSettings::MovingLightDeviceSettings(Device &aDevice) :
  inherited(aDevice)
{
};


DsScenePtr MovingLightDeviceSettings::newDefaultScene(SceneNo aSceneNo)
{
  MovingLightScenePtr movingLightScene = MovingLightScenePtr(new MovingLightScene(*this, aSceneNo));
  movingLightScene->setDefaultSceneValues(aSceneNo);
  // return it
  return movingLightScene;
}



// MARK: - MovingLightBehaviour


MovingLightBehaviour::MovingLightBehaviour(Device &aDevice, bool aCtOnly) :
  inherited(aDevice, aCtOnly)
{
  // Create and add auxiliary channels to the device for horizontal and vertical position
  // - horizontal position
  horizontalPosition = ChannelBehaviourPtr(new HPosChannel(*this));
  addChannel(horizontalPosition);
  // - vertical position
  verticalPosition = ChannelBehaviourPtr(new VPosChannel(*this));
  addChannel(verticalPosition);
}


Tristate MovingLightBehaviour::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // now check for light behaviour level features
  switch (aFeatureIndex) {
//    case modelFeature_positionControls: //%%% does not exist yet...
//      // Assumption: all moving light output devices need a UI for position
//      return yes;
    default:
      // not available at this level, ask base class
      return inherited::hasModelFeature(aFeatureIndex);
  }
}



void MovingLightBehaviour::loadChannelsFromScene(DsScenePtr aScene)
{
  // load color light scene info
  inherited::loadChannelsFromScene(aScene);
  // now load moving light specific scene information
  MovingLightScenePtr movingLightScene = boost::dynamic_pointer_cast<MovingLightScene>(aScene);
  if (movingLightScene) {
    MLMicroSeconds ttUp = transitionTimeFromScene(movingLightScene, true);
    MLMicroSeconds ttDown = transitionTimeFromScene(movingLightScene, false);
    // prepare next position values in channels
    horizontalPosition->setChannelValueIfNotDontCare(movingLightScene, movingLightScene->hPos, ttUp, ttDown, true);
    verticalPosition->setChannelValueIfNotDontCare(movingLightScene, movingLightScene->vPos, ttUp, ttDown, true);
  }
}


void MovingLightBehaviour::saveChannelsToScene(DsScenePtr aScene)
{
  // save basic light scene info
  inherited::saveChannelsToScene(aScene);
  // now save moving light specific scene information
  MovingLightScenePtr movingLightScene = boost::dynamic_pointer_cast<MovingLightScene>(aScene);
  if (movingLightScene) {
    // when saved, position values are no longer dontcare
    movingLightScene->setSceneValueFlags(horizontalPosition->getChannelIndex(), valueflags_dontCare, false);
    movingLightScene->setPVar(movingLightScene->hPos, horizontalPosition->getChannelValue());
    movingLightScene->setSceneValueFlags(verticalPosition->getChannelIndex(), valueflags_dontCare, false);
    movingLightScene->setPVar(movingLightScene->vPos, verticalPosition->getChannelValue());
  }
}



bool MovingLightBehaviour::updatePositionTransition(MLMicroSeconds aNow)
{
  bool moreSteps = horizontalPosition->updateTimedTransition(aNow);
  if (verticalPosition->updateTimedTransition(aNow)) moreSteps = true;
  return moreSteps;
}



void MovingLightBehaviour::appliedPosition()
{
  horizontalPosition->channelValueApplied();
  verticalPosition->channelValueApplied();
}


string MovingLightBehaviour::shortDesc()
{
  return string("MovingColorLight");
}


// MARK: - FeatureLightScene

FeatureLightScene::FeatureLightScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
  inherited(aSceneDeviceSettings, aSceneNo),
  hZoom(DEFAULT_ZOOM),
  vZoom(DEFAULT_ZOOM),
  rotation(0),
  brightnessGradient(DEFAULT_BRIGHTNESS_GRADIENT),
  hueGradient(DEFAULT_HUE_GRADIENT),
  saturationGradient(DEFAULT_SATURATION_GRADIENT),
  featureMode(DEFAULT_FEATURE_MODE)
{
}


double FeatureLightScene::sceneValue(int aChannelIndex)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_p44_zoom_h : return hZoom;
    case channeltype_p44_zoom_v : return vZoom;
    case channeltype_p44_rotation : return rotation;
    case channeltype_p44_brightness_gradient : return brightnessGradient;
    case channeltype_p44_hue_gradient : return hueGradient;
    case channeltype_p44_saturation_gradient : return saturationGradient;
    case channeltype_p44_feature_mode : return featureMode;
    default: return inherited::sceneValue(aChannelIndex);
  }
  return 0;
}


void FeatureLightScene::setSceneValue(int aChannelIndex, double aValue)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_p44_zoom_h : setPVar(hZoom, aValue); break;
    case channeltype_p44_zoom_v : setPVar(vZoom, aValue); break;
    case channeltype_p44_rotation : setPVar(rotation, aValue); break;
    case channeltype_p44_brightness_gradient : setPVar(brightnessGradient, aValue); break;
    case channeltype_p44_hue_gradient : setPVar(hueGradient, aValue); break;
    case channeltype_p44_saturation_gradient : setPVar(saturationGradient, aValue); break;
    case channeltype_p44_feature_mode : setPVar(featureMode, (uint32_t)aValue); break;
    default: inherited::setSceneValue(aChannelIndex, aValue); break;
  }
}




// MARK: - Feature Light Scene persistence

const char *FeatureLightScene::tableName()
{
  return "FeatureLightScenes";
}

// data field definitions

static const size_t numFeatureLightSceneFields = 7;

size_t FeatureLightScene::numFieldDefs()
{
  return inherited::numFieldDefs()+numFeatureLightSceneFields;
}


const FieldDefinition *FeatureLightScene::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFeatureLightSceneFields] = {
    { "hZoom", SQLITE_FLOAT },
    { "vZoom", SQLITE_FLOAT },
    { "rotation", SQLITE_FLOAT },
    { "briGradient", SQLITE_FLOAT },
    { "hueGradient", SQLITE_FLOAT },
    { "satGradient", SQLITE_FLOAT },
    { "featureMode", SQLITE_INTEGER }
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFeatureLightSceneFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void FeatureLightScene::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the fields
  hZoom = aRow->get<double>(aIndex++);
  vZoom = aRow->get<double>(aIndex++);
  rotation = aRow->get<double>(aIndex++);
  brightnessGradient = aRow->get<double>(aIndex++);
  hueGradient = aRow->get<double>(aIndex++);
  saturationGradient = aRow->get<double>(aIndex++);
  featureMode = aRow->get<int>(aIndex++);
}


/// bind values to passed statement
void FeatureLightScene::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, hZoom);
  aStatement.bind(aIndex++, vZoom);
  aStatement.bind(aIndex++, rotation);
  aStatement.bind(aIndex++, brightnessGradient);
  aStatement.bind(aIndex++, hueGradient);
  aStatement.bind(aIndex++, saturationGradient);
  aStatement.bind(aIndex++, (int)featureMode);
}


// MARK: - default feature light scene

void FeatureLightScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // set the common moving light scene defaults
  inherited::setDefaultSceneValues(aSceneNo);
  // half size, non rotated by default
  hZoom = 50;
  vZoom = 50;
  rotation = 0;
  brightnessGradient = DEFAULT_BRIGHTNESS_GRADIENT;
  hueGradient = DEFAULT_HUE_GRADIENT;
  saturationGradient = DEFAULT_SATURATION_GRADIENT;
  featureMode = DEFAULT_FEATURE_MODE;
  // set dontcare flags
  FeatureLightBehaviourPtr featureLightBehaviour = boost::dynamic_pointer_cast<FeatureLightBehaviour>(getOutputBehaviour());
  if (featureLightBehaviour) {
    setSceneValueFlags(featureLightBehaviour->horizontalZoom->getChannelIndex(), valueflags_dontCare, true);
    setSceneValueFlags(featureLightBehaviour->verticalZoom->getChannelIndex(), valueflags_dontCare, true);
    setSceneValueFlags(featureLightBehaviour->rotation->getChannelIndex(), valueflags_dontCare, true);
    setSceneValueFlags(featureLightBehaviour->brightnessGradient->getChannelIndex(), valueflags_dontCare, true);
    setSceneValueFlags(featureLightBehaviour->hueGradient->getChannelIndex(), valueflags_dontCare, true);
    setSceneValueFlags(featureLightBehaviour->saturationGradient->getChannelIndex(), valueflags_dontCare, true);
    setSceneValueFlags(featureLightBehaviour->featureMode->getChannelIndex(), valueflags_dontCare, true);
  }
  markClean(); // default values are always clean (but setSceneValueFlags sets dirty)
}


// MARK: - FeatureLightDeviceSettings with default light scenes factory


FeatureLightDeviceSettings::FeatureLightDeviceSettings(Device &aDevice) :
  inherited(aDevice)
{
};


DsScenePtr FeatureLightDeviceSettings::newDefaultScene(SceneNo aSceneNo)
{
  FeatureLightScenePtr featureLightScene = FeatureLightScenePtr(new FeatureLightScene(*this, aSceneNo));
  featureLightScene->setDefaultSceneValues(aSceneNo);
  // return it
  return featureLightScene;
}



// MARK: - FeatureLightBehaviour


FeatureLightBehaviour::FeatureLightBehaviour(Device &aDevice, bool aCtOnly) :
  inherited(aDevice, aCtOnly)
{
  // Create and add auxiliary channels to the device for horizontal and vertical position
  // - horizontal zoom
  horizontalZoom = ChannelBehaviourPtr(new HZoomChannel(*this));
  addChannel(horizontalZoom);
  // - vertical zoom
  verticalZoom = ChannelBehaviourPtr(new VZoomChannel(*this));
  addChannel(verticalZoom);
  // - rotation
  rotation = ChannelBehaviourPtr(new RotationChannel(*this));
  addChannel(rotation);
  // - brightness gradient
  brightnessGradient = ChannelBehaviourPtr(new BrightnessGradientChannel(*this));
  addChannel(brightnessGradient);
  // - hue gradient
  hueGradient = ChannelBehaviourPtr(new HueGradientChannel(*this));
  addChannel(hueGradient);
  // - saturation gradient
  saturationGradient = ChannelBehaviourPtr(new SaturationGradientChannel(*this));
  addChannel(saturationGradient);
  // - feature kode
  featureMode = ChannelBehaviourPtr(new FeatureModeChannel(*this));
  addChannel(featureMode);
}


Tristate FeatureLightBehaviour::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // now check for light behaviour level features
  switch (aFeatureIndex) {
//    case modelFeature_positionControls: //%%% does not exist yet...
//      // Assumption: all moving light output devices need a UI for position
//      return yes;
    default:
      // not available at this level, ask base class
      return inherited::hasModelFeature(aFeatureIndex);
  }
}



void FeatureLightBehaviour::loadChannelsFromScene(DsScenePtr aScene)
{
  // load color light scene info
  inherited::loadChannelsFromScene(aScene);
  // now load moving light specific scene information
  FeatureLightScenePtr featureLightScene = boost::dynamic_pointer_cast<FeatureLightScene>(aScene);
  if (featureLightScene) {
    MLMicroSeconds ttUp = transitionTimeFromScene(featureLightScene, true);
    MLMicroSeconds ttDown = transitionTimeFromScene(featureLightScene, false);
    // prepare next position values in channels
    horizontalZoom->setChannelValueIfNotDontCare(featureLightScene, featureLightScene->hZoom, ttUp, ttDown, true);
    verticalZoom->setChannelValueIfNotDontCare(featureLightScene, featureLightScene->vZoom, ttUp, ttDown, true);
    rotation->setChannelValueIfNotDontCare(featureLightScene, featureLightScene->rotation, ttUp, ttDown, true);
    brightnessGradient->setChannelValueIfNotDontCare(featureLightScene, featureLightScene->brightnessGradient, ttUp, ttDown, true);
    hueGradient->setChannelValueIfNotDontCare(featureLightScene, featureLightScene->hueGradient, ttUp, ttDown, true);
    saturationGradient->setChannelValueIfNotDontCare(featureLightScene, featureLightScene->saturationGradient, ttUp, ttDown, true);
    featureMode->setChannelValueIfNotDontCare(featureLightScene, featureLightScene->featureMode, ttUp, ttDown, true);
  }
}


void FeatureLightBehaviour::saveChannelsToScene(DsScenePtr aScene)
{
  // save basic light scene info
  inherited::saveChannelsToScene(aScene);
  // now save moving light specific scene information
  FeatureLightScenePtr featureLightScene = boost::dynamic_pointer_cast<FeatureLightScene>(aScene);
  if (featureLightScene) {
    // when saved, feature are no longer dontcare
    featureLightScene->setSceneValueFlags(horizontalZoom->getChannelIndex(), valueflags_dontCare, false);
    featureLightScene->setPVar(featureLightScene->hZoom, horizontalZoom->getChannelValue());
    featureLightScene->setSceneValueFlags(verticalZoom->getChannelIndex(), valueflags_dontCare, false);
    featureLightScene->setPVar(featureLightScene->vZoom, verticalZoom->getChannelValue());
    featureLightScene->setSceneValueFlags(rotation->getChannelIndex(), valueflags_dontCare, false);
    featureLightScene->setPVar(featureLightScene->rotation, rotation->getChannelValue());
    featureLightScene->setSceneValueFlags(brightnessGradient->getChannelIndex(), valueflags_dontCare, false);
    featureLightScene->setPVar(featureLightScene->brightnessGradient, brightnessGradient->getChannelValue());
    featureLightScene->setSceneValueFlags(hueGradient->getChannelIndex(), valueflags_dontCare, false);
    featureLightScene->setPVar(featureLightScene->hueGradient, hueGradient->getChannelValue());
    featureLightScene->setSceneValueFlags(saturationGradient->getChannelIndex(), valueflags_dontCare, false);
    featureLightScene->setPVar(featureLightScene->saturationGradient, saturationGradient->getChannelValue());
    featureLightScene->setSceneValueFlags(featureMode->getChannelIndex(), valueflags_dontCare, false);
    featureLightScene->setPVar(featureLightScene->featureMode, (uint32_t)featureMode->getChannelValue());
  }
}


bool FeatureLightBehaviour::updateFeatureTransition(MLMicroSeconds aNow)
{
  bool moreSteps = horizontalZoom->updateTimedTransition(aNow);
  if (verticalZoom->updateTimedTransition(aNow)) moreSteps = true;
  if (rotation->updateTimedTransition(aNow)) moreSteps = true;
  if (brightnessGradient->updateTimedTransition(aNow)) moreSteps = true;
  if (hueGradient->updateTimedTransition(aNow)) moreSteps = true;
  if (saturationGradient->updateTimedTransition(aNow)) moreSteps = true;
  return moreSteps;
}



void FeatureLightBehaviour::appliedFeatures()
{
  horizontalZoom->channelValueApplied();
  verticalZoom->channelValueApplied();
  rotation->channelValueApplied();
  brightnessGradient->channelValueApplied();
  hueGradient->channelValueApplied();
  saturationGradient->channelValueApplied();
  featureMode->channelValueApplied();
}



string FeatureLightBehaviour::shortDesc()
{
  return string("FeatureSpotLight");
}


