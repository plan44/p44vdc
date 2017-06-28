//
//  Copyright (c) 2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "ventilationbehaviour.hpp"

using namespace p44;


// MARK: ===== VentilationScene


VentilationScene::VentilationScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
  inherited(aSceneDeviceSettings, aSceneNo),
  airflowIntensity(0),
  airflowDirection(dsVentilationDirection_undefined),
  louverPosition(0)
{
}


#define DONT_CARE 0x80 // mask for don't care
#define VALUE_MASK 0x7F // mask for value
#define AUTO 0x7F // special value for setting auto channel

typedef struct {
  uint8_t airflow; ///< air flow intensity value for this scene, uint_8 to save footprint
  uint8_t direction; ///< air flow direction
  uint8_t louverpos; ///< louver position value, uint_8 to save footprint
  SceneCmd sceneCmd; ///< command for this scene
} DefaultVentilationSceneParams;

#define NUMGROUPSCENES 38 ///< Number of group scenes

static const DefaultVentilationSceneParams defaultGroupScenes[NUMGROUPSCENES+1] = {
  // airflow    direction  louver     cmd
  {  0,         DONT_CARE, DONT_CARE, scene_cmd_off       }, // 0 : stage 0
  {  0,         DONT_CARE, DONT_CARE, scene_cmd_off       }, // 1 : stage 10
  {  0,         DONT_CARE, DONT_CARE, scene_cmd_off       }, // 2 : stage 20
  {  0,         DONT_CARE, DONT_CARE, scene_cmd_off       }, // 3 : stage 30
  {  0,         DONT_CARE, DONT_CARE, scene_cmd_off       }, // 4 : stage 40
  {  25,        DONT_CARE, DONT_CARE, scene_cmd_invoke    }, // 5 : stage 1
  {  25,        0,         100,       scene_cmd_invoke    }, // 6 : stage 11
  {  25,        0,         100,       scene_cmd_invoke    }, // 7 : stage 21
  {  25,        1,         100,       scene_cmd_invoke    }, // 8 : stage 31
  {  25,        2,         100,       scene_cmd_invoke    }, // 9 : stage 41
  {  DONT_CARE, DONT_CARE, DONT_CARE, scene_cmd_none      }, // 10 : none (area stepping continue)
  {  DONT_CARE, DONT_CARE, DONT_CARE, scene_cmd_decrement }, // 11 : decrement main channel (airflow intensity)
  {  DONT_CARE, DONT_CARE, DONT_CARE, scene_cmd_increment }, // 12 : increment main channel (airflow intensity)
  {  5,         DONT_CARE, DONT_CARE, scene_cmd_min       }, // 13 : set minimum (airflow intensity)
  {  100,       DONT_CARE, DONT_CARE, scene_cmd_max       }, // 14 : set maximum (airflow intensity)
  {  DONT_CARE, DONT_CARE, DONT_CARE, scene_cmd_stop      }, // 15 : stop dimming / changes / movement
  {  DONT_CARE, DONT_CARE, DONT_CARE, scene_cmd_none      }, // 16 : reserved
  {  50,        DONT_CARE, DONT_CARE, scene_cmd_invoke    }, // 17 : stage 2
  {  75,        DONT_CARE, DONT_CARE, scene_cmd_invoke    }, // 18 : stage 3
  {  100,       DONT_CARE, DONT_CARE, scene_cmd_invoke    }, // 19 : stage 4
  {  50,        0,         100,       scene_cmd_invoke    }, // 20 : stage 12
  {  75,        0,         100,       scene_cmd_invoke    }, // 21 : stage 13
  {  100,       0,         100,       scene_cmd_invoke    }, // 22 : stage 14
  {  50,        0,         100,       scene_cmd_invoke    }, // 23 : stage 22
  {  75,        0,         100,       scene_cmd_invoke    }, // 24 : stage 23
  {  100,       0,         100,       scene_cmd_invoke    }, // 25 : stage 24
  {  50,        1,         100,       scene_cmd_invoke    }, // 26 : stage 32
  {  75,        1,         100,       scene_cmd_invoke    }, // 27 : stage 33
  {  100,       1,         100,       scene_cmd_invoke    }, // 28 : stage 34
  {  50,        2,         100,       scene_cmd_invoke    }, // 29 : stage 42
  {  75,        2,         100,       scene_cmd_invoke    }, // 30 : stage 43
  {  100,       2,         100,       scene_cmd_invoke    }, // 31 : stage 44
  {  DONT_CARE, DONT_CARE, DONT_CARE, scene_cmd_none      }, // 32 : reserved
  {  AUTO,      DONT_CARE, DONT_CARE, scene_cmd_invoke    }, // 33 : stage auto flow intensity
  {  DONT_CARE, DONT_CARE, DONT_CARE, scene_cmd_none      }, // 34 : reserved
  {  DONT_CARE, DONT_CARE, AUTO,      scene_cmd_invoke    }, // 35 : stage auto louver position (swing mode)
  {  25,        DONT_CARE, DONT_CARE, scene_cmd_invoke    }, // 36 : noise reduction
  {  100,       DONT_CARE, DONT_CARE, scene_cmd_invoke    }, // 37 : boost
  // all other group scenes equal or higher
  {  DONT_CARE, DONT_CARE, DONT_CARE, scene_cmd_invoke    }, // 38..63 : reserved
};


// flags in globalSceneFlags
enum {
  // parent uses bit 0 and 1 (globalflags_sceneLevelMask) and bits 8..23
  // ventilation scene global
  ventilationflags_airflowauto = 0x0004, ///< automatic air flow intensity
  ventilationflags_louverauto = 0x0008, ///< automatic louver position
};




void VentilationScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // set the base class scene defaults
  inherited::setDefaultSceneValues(aSceneNo);
  VentilationBehaviourPtr vb = boost::dynamic_pointer_cast<VentilationBehaviour>(getOutputBehaviour());
  if (vb) {
    // get defaults from table
    if (aSceneNo>NUMGROUPSCENES)
      aSceneNo = NUMGROUPSCENES; // last entry in the table is the default for all higher scene numbers
    const DefaultVentilationSceneParams &p = defaultGroupScenes[aSceneNo];
    // init values from it
    sceneCmd = p.sceneCmd;
    bool afi = false;
    bool adi = false;
    bool lpi = false;
    // - intensity
    airflowIntensity = p.airflow & VALUE_MASK;
    if (airflowIntensity==AUTO) {
      airflowIntensity = 50;
      setGlobalSceneFlag(ventilationflags_airflowauto, true);
      setSceneValueFlags(vb->airflowIntensity->getChannelIndex(), valueflags_dontCare, true); // auto -> do not apply intensity
    }
    afi = p.airflow & DONT_CARE;
    // - direction
    airflowDirection = (DsVentilationAirflowDirection)(p.direction & VALUE_MASK);
    adi = p.direction & DONT_CARE;
    // - louver position
    louverPosition = p.louverpos & VALUE_MASK;
    if (louverPosition==AUTO) {
      louverPosition = 50;
      setGlobalSceneFlag(ventilationflags_louverauto, true);
      setSceneValueFlags(vb->louverPosition->getChannelIndex(), valueflags_dontCare, true); // auto -> do not apply louver pos
    }
    lpi = p.louverpos & DONT_CARE;
    // adjust values for global scenes
    switch (aSceneNo) {
      case FIRE:
      case SMOKE:
      case GAS:
        // fan off, no automatic activity
        airflowIntensity = 0;
        setGlobalSceneFlag(ventilationflags_airflowauto, false);
        setGlobalSceneFlag(ventilationflags_louverauto, false);
        afi = false;
        break;
      default:
        break;
    }
    // adjust per-channel dontcare
    if (afi) {
      setSceneValueFlags(vb->airflowIntensity->getChannelIndex(), valueflags_dontCare, true);
      setSceneValueFlags(vb->airflowAuto->getChannelIndex(), valueflags_dontCare, true);
    }
    if (adi) setSceneValueFlags(vb->airflowDirection->getChannelIndex(), valueflags_dontCare, true);
    if (lpi) {
      setSceneValueFlags(vb->louverPosition->getChannelIndex(), valueflags_dontCare, true);
      setSceneValueFlags(vb->louverAuto->getChannelIndex(), valueflags_dontCare, true);
    }
  }
  markClean(); // default values are always clean
}


double VentilationScene::sceneValue(size_t aChannelIndex)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_airflow_intensity: return airflowIntensity;
    case channeltype_airflow_direction: return airflowDirection;
    case channeltype_airflow_louver_position: return louverPosition;
    case channeltype_airflow_louver_auto: return globalSceneFlags & ventilationflags_louverauto ? 1 : 0;
    case channeltype_airflow_intensity_auto: return globalSceneFlags & ventilationflags_airflowauto ? 1 : 0;
  }
  return 0;
}


void VentilationScene::setSceneValue(size_t aChannelIndex, double aValue)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  VentilationBehaviourPtr vb = boost::dynamic_pointer_cast<VentilationBehaviour>(getOutputBehaviour());
  switch (cb->getChannelType()) {
    case channeltype_airflow_intensity:
      setPVar(airflowIntensity, aValue);
      break;
    case channeltype_airflow_direction:
      setPVar(airflowDirection, (DsVentilationAirflowDirection)aValue);
      break;
    case channeltype_airflow_louver_position:
      setPVar(louverPosition, aValue);
      break;
    case channeltype_airflow_louver_auto:
      setGlobalSceneFlag(ventilationflags_louverauto, (bool)aValue);
      break;
    case channeltype_airflow_intensity_auto:
      setGlobalSceneFlag(ventilationflags_airflowauto, (bool)aValue);
      break;
  }
}


// MARK: ===== Ventilation Scene persistence

const char *VentilationScene::tableName()
{
  return "VentilationScenes";
}

// data field definitions

static const size_t numVentilationSceneFields = 3;

size_t VentilationScene::numFieldDefs()
{
  return inherited::numFieldDefs()+numVentilationSceneFields;
}


const FieldDefinition *VentilationScene::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numVentilationSceneFields] = {
    { "airflowintensity", SQLITE_FLOAT },
    { "airflowdirection", SQLITE_INTEGER },
    { "louverposition", SQLITE_FLOAT }
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numVentilationSceneFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void VentilationScene::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the fields
  aRow->getIfNotNull<double>(aIndex++, airflowIntensity);
  aRow->getCastedIfNotNull<DsVentilationAirflowDirection, int>(aIndex++, airflowDirection);
  aRow->getIfNotNull<double>(aIndex++, louverPosition);
}


/// bind values to passed statement
void VentilationScene::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, airflowIntensity);
  aStatement.bind(aIndex++, (int)airflowDirection);
  aStatement.bind(aIndex++, louverPosition);
}




// MARK: ===== VentilationDeviceSettings with default shadow scenes factory


VentilationDeviceSettings::VentilationDeviceSettings(Device &aDevice) :
  inherited(aDevice)
{
}


DsScenePtr VentilationDeviceSettings::newDefaultScene(SceneNo aSceneNo)
{
  VentilationScenePtr ventilationScene = VentilationScenePtr(new VentilationScene(*this, aSceneNo));
  ventilationScene->setDefaultSceneValues(aSceneNo);
  // return it
  return ventilationScene;
}



// MARK: ===== VentilationBehaviour


VentilationBehaviour::VentilationBehaviour(Device &aDevice, VentilationDeviceKind aKind) :
  inherited(aDevice),
  ventilationDeviceKind(aKind)
{
  // Note: there is no default group for ventilation, depends on application and must be set when instantiating the behaviour
  // add the output channels
  // - air flow intensity
  airflowIntensity = AirflowIntensityChannelPtr(new AirflowIntensityChannel(*this));
  addChannel(airflowIntensity);
  // - air flow direction
  airflowDirection = AirflowDirectionChannelPtr(new AirflowDirectionChannel(*this));
  addChannel(airflowDirection);
  // - louver position
  louverPosition = LouverPositionChannelPtr(new LouverPositionChannel(*this));
  addChannel(louverPosition);
  // - louver automatic
  louverAuto = FlagChannelPtr(new LouverAutoChannel(*this));
  addChannel(louverAuto);
  // - louver position
  airflowAuto = FlagChannelPtr(new AirflowAutoChannel(*this));
  addChannel(airflowAuto);
}



bool VentilationBehaviour::processControlValue(const string &aName, double aValue)
{
  // TODO: evaluate room broadcasts like temperature, humidity, CO2 - NOP for now
  return inherited::processControlValue(aName, aValue);
}




Tristate VentilationBehaviour::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // now check for climate control behaviour level features
  switch (aFeatureIndex) {
    case modelFeature_blink:
      // ventilation outputs can't blink
      return no;
    default:
      // not available at this level, ask base class
      return inherited::hasModelFeature(aFeatureIndex);
  }
}


void VentilationBehaviour::loadChannelsFromScene(DsScenePtr aScene)
{
  VentilationScenePtr ventilationScene = boost::dynamic_pointer_cast<VentilationScene>(aScene);
  if (ventilationScene) {
    // load channels from scene
    // - air flow intensity
    airflowIntensity->setChannelValueIfNotDontCare(aScene, ventilationScene->airflowIntensity, 0, 0, true);
    // - air flow intensity automatic
    airflowAuto->setChannelValueIfNotDontCare(aScene, ventilationScene->globalSceneFlags & ventilationflags_airflowauto ? 1 : 0, 0, 0, true);
    // - air flow direction
    airflowDirection->setChannelValueIfNotDontCare(aScene, ventilationScene->airflowDirection, 0, 0, true);
    // - louver position
    louverPosition->setChannelValueIfNotDontCare(aScene, ventilationScene->louverPosition, 0, 0, true);
    // - louver position automatic
    louverAuto->setChannelValueIfNotDontCare(aScene, ventilationScene->globalSceneFlags & ventilationflags_louverauto ? 1 : 0, 0, 0, true);
  }
}


void VentilationBehaviour::saveChannelsToScene(DsScenePtr aScene)
{
  VentilationScenePtr ventilationScene = boost::dynamic_pointer_cast<VentilationScene>(aScene);
  if (ventilationScene) {
    // save position and angle to scene
    // - air flow intensity
    ventilationScene->setPVar(ventilationScene->airflowIntensity, airflowIntensity->getChannelValue());
    ventilationScene->setSceneValueFlags(airflowIntensity->getChannelIndex(), valueflags_dontCare, false);
    // - air flow intensity automatic
    ventilationScene->setGlobalSceneFlag(ventilationflags_airflowauto, airflowAuto->getFlag());
    ventilationScene->setSceneValueFlags(airflowAuto->getChannelIndex(), valueflags_dontCare, false);
    // - air flow direction
    ventilationScene->setPVar(ventilationScene->airflowDirection, (DsVentilationAirflowDirection)airflowDirection->getChannelValue());
    ventilationScene->setSceneValueFlags(airflowDirection->getChannelIndex(), valueflags_dontCare, false);
    // - louver position
    ventilationScene->setPVar(ventilationScene->louverPosition, louverPosition->getChannelValue());
    ventilationScene->setSceneValueFlags(louverPosition->getChannelIndex(), valueflags_dontCare, false);
    // - louver position automatic
    ventilationScene->setGlobalSceneFlag(ventilationflags_louverauto, airflowAuto->getFlag());
    ventilationScene->setSceneValueFlags(louverAuto->getChannelIndex(), valueflags_dontCare, false);
  }
}



// apply scene
// - execute special climate commands
bool VentilationBehaviour::applyScene(DsScenePtr aScene)
{
  // check the special hardwired scenes
  SceneCmd sceneCmd = aScene->sceneCmd;
  switch (sceneCmd) {
    case scene_cmd_off:
    case scene_cmd_min:
    case scene_cmd_max:
    case scene_cmd_increment:
    case scene_cmd_decrement:
      // these always end automatic airflow intensity mode
      airflowAuto->setChannelValue(0);
      break;
    default:
      // all other scene calls are processed normally
      break;
  }
  // other type of scene, let base class handle it
  return inherited::applyScene(aScene);
}



// MARK: ===== description


string VentilationBehaviour::shortDesc()
{
  return string("Ventilation");
}


string VentilationBehaviour::description()
{
  string s = string_format("%s behaviour (%s)", shortDesc().c_str(), ventilationDeviceKind==ventilationdevice_recirculation ? "recirculation" : "ventilation");
  s.append(inherited::description());
  return s;
}

