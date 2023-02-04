//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2014-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__simplescene__
#define __p44vdc__simplescene__

#include "dsscene.hpp"

using namespace std;

namespace p44 {

  /// concrete implementation of a single-value scene
  class SimpleScene : public DsScene
  {
    typedef DsScene inherited;

  public:
    SimpleScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo); ///< constructor, sets values according to dS specs' default values

    /// @name SimpleScene specific values
    /// @{

    double value; ///< scene value
    VdcSceneEffect mEffect; ///< scene effect (transition or alert)
    uint32_t mEffectParam; ///< parameter for the effect (such as per-scene transition time)

    /// @}

    /// Set default scene values for a specified scene number
    /// @param aSceneNo the scene number to set default values
    virtual void setDefaultSceneValues(SceneNo aSceneNo) P44_OVERRIDE;

    // scene values implementation
    virtual double sceneValue(int aOutputIndex) P44_OVERRIDE;
    virtual void setSceneValue(int aOutputIndex, double aValue) P44_OVERRIDE;

    /// scene contents hash
    /// @return a hash value of the scene contents (NOT including the scene number!)
    /// @note is allowed to return different values for same scene contents on different platforms
    virtual uint64_t sceneHash() P44_OVERRIDE;

    /// get default area number for a scene number
    /// @param aSceneNo scene number
    /// @return 0 if not an area scene, 1..4 if it is a area scene
    /// @note only the hard coded default scene table is consulted!
    static int areaForScene(SceneNo aSceneNo);

  protected:

    // persistence implementation
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<SimpleScene> SimpleScenePtr;



  /// concrete implementation of a single-value scene including a command value string for device-specific actions
  /// like audio titles, source selection etc.
  class SimpleCmdScene : public SimpleScene
  {
    typedef SimpleScene inherited;

  public:
    SimpleCmdScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo); ///< constructor, sets values according to dS specs' default values

    /// @name SimpleCmdScene specific values
    /// @{

    string mCommand;

    /// @}


    /// Substitute channel values for placeholders
    /// @param aCommandStr string in which placeholders are substituted
    /// @return number of substitutions applied
    int substitutePlaceholders(string &aCommandStr);

  protected:

    // persistence implementation
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<SimpleCmdScene> SimpleCmdScenePtr;


  /// the persistent parameters of a simple scene with an extra command string field
  /// @note subclasses can implement more parameters
  class CmdSceneDeviceSettings : public SceneDeviceSettings
  {
    typedef SceneDeviceSettings inherited;

  public:
    CmdSceneDeviceSettings(Device &aDevice);

    /// factory method to create the correct subclass type of DsScene
    /// @param aSceneNo the scene number to create a scene object for.
    /// @note setDefaultSceneValues() must be called to set default scene values
    virtual DsScenePtr newDefaultScene(SceneNo aSceneNo) P44_OVERRIDE;
    
  };
  
  



}


#endif /* defined(__p44vdc__simplescene__) */
