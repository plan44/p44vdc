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

#ifndef __p44vdc__dsbehaviour__
#define __p44vdc__dsbehaviour__

#include "vdc.hpp"

#include "dsuid.hpp"
#include "dsdefs.h"

#include "valuesource.hpp"
#include "dsscene.hpp"

using namespace std;

namespace p44 {

  // offset to differentiate containers and property keys for descriptions, settings and states
  enum {
    descriptions_key_offset = 1000,
    settings_key_offset = 2000,
    states_key_offset = 3000
  };


  typedef enum {
    behaviour_undefined,
    behaviour_button,
    behaviour_binaryinput,
    behaviour_sensor,
    behaviour_output,
    behaviour_actionOutput,
  } BehaviourType;


  class Device;

  class DsBehaviour;

  class DsScene;

  class ButtonBehaviour;
  class OutputBehaviour;
  class BinaryInputBehaviour;
  class SensorBehaviour;


  /// a DsBehaviour represents and implements a device behaviour according to dS specs
  /// (for example: the dS Light state machine). The interface of a DsBehaviour is generic
  /// such that it can be used by different physical implementations (e.g. both DALI devices
  /// and hue devices will make use of the dS light state machine behaviour.
  class DsBehaviour : public PropertyContainer, public PersistentParams
  {
    typedef PropertyContainer inheritedProps;
    typedef PersistentParams inheritedParams;

    friend class Device;
    friend class DsScene;

  protected:

    /// the device this behaviour belongs to
    Device &mDevice;

    /// the index of this behaviour in the device's vector
    /// @note the indices of behaviours are relevant for dS backwards compatibility, because
    ///   older dS systems used the indices for idenitification of buttons/inputs/sensors
    size_t mIndex;

    /// the ID of the behaviour
    /// @note If string is empty, getApiId() will return decimal string representation of getIndex(), for backwards compatibility
    string mBehaviourId;


    /// @name behaviour description, constants or variables
    ///   set by device implementations when adding a Behaviour.
    /// @{
    string mHardwareName; ///< name that identifies this behaviour among others for the human user (terminal label text etc)
    /// @}

    /// @name persistent settings
    /// @{
    DsClass mColorClass; 
    /// @}

    /// @name internal volatile state
    /// @{
    VdcHardwareError mHardwareError; ///< hardware error
    MLMicroSeconds mHardwareErrorUpdated; ///< when was hardware error last updated
    /// @}


  public:
    DsBehaviour(Device &aDevice, const string aBehaviourId);
    virtual ~DsBehaviour();

    /// device type identifier
    /// @return constant identifier for this type of behaviour
    /// @note default is the basic behaviour type name (output/sensor/binaryInput/buttonInput/actionOutput).
    ///   Specific output behaviour subclasses will return identifiers such as "light", "shadow"
    ///   (but not any more variants like "colorlight" or "rgblight").
    virtual const char *behaviourTypeIdentifier() { return getTypeName(); }

    /// initialisation of hardware-specific constants for this behaviour
    /// @param aHardwareName name to identify this functionality in hardware (like input terminal label, button label or kind etc.)
    /// @note this must be called once before the device gets added to the device container.
    void setHardwareName(const string &aHardwareName) { mHardwareName = aHardwareName; };

    /// @return hardware name
    /// @note if no specific name was set with setHardwareName(), this returns the behaviourId
    string getHardwareName() { return mHardwareName.empty() ? mBehaviourId : mHardwareName; };

    /// update of hardware status
    void setHardwareError(VdcHardwareError aHardwareError);

    /// set group
    virtual void setGroup(DsGroup aGroup) { /* NOP in base class */ };

    /// get group
    virtual DsGroup getGroup() { return group_undefined; /* not defined in base class */ };

    /// get color class
    /// @note if no colorClass is explicitly set (`colorClass` property), this
    ///   returns the color class derived from the behaviour's group, and if that does not
    ///   exist or equals `group_undefined`, the device's colorClass
    ///   (property named `primaryGroup` for historical reasons)
    /// @return color class of this behaviour (useful for coloring UI elements)
    virtual DsClass getColorClass();

    /// push state
    /// @param aDS push to dS (vDSM)
    /// @param aDS push to bridges
    /// @return true if selected API was connected and push could be sent
    bool pushBehaviourState(bool aDS, bool aBridges);

    /// check for defined state
    /// @return true if behaviour has a defined (non-NULL) state
    virtual bool hasDefinedState() { return false; };

    /// re-validate current sensor value (i.e. prevent it from expiring and getting invalid)
    virtual void revalidateState() { /* NOP in base class */ };

    /// Get short text for a "first glance" status of the behaviour
    /// @return string, really short, intended to be shown as a narrow column in a list
    virtual string getStatusText() { return ""; };

    virtual Device& getDevice() { return mDevice; };



    /// @name persistent settings management
    /// @{

    /// load behaviour parameters from persistent DB
    ErrorPtr load();

    /// save unsaved behaviour parameters to persistent DB
    ErrorPtr save();

    /// forget any parameters stored in persistent DB
    ErrorPtr forget();

    /// @}

    /// get the index value
    /// @return index of this behaviour in one of the owning device's behaviour lists
    size_t getIndex() const { return mIndex; };

    /// get the identifier (unique within this device instance)
    /// @return behaviour id string
    string getId() const { return mBehaviourId; };

    /// get the behaviour ID
    /// @param aApiVersion the API version to get the ID for. APIs before v3 always return the behaviour index as a numeric string
    /// @return the behaviour ID, which must be unique within the device and must always allow to re-find the same behaviour
    string getApiId(int aApiVersion) const;

    /// textual representation of getType()
    /// @return type string, which is the string used to prefix the xxxDescriptions, xxxSettings and xxxStates properties
    /// @note this only identifies the basic behaviour type. Subclassed behaviours can only be identified using behaviourTypeIdentifier()
    const char *getTypeName() const;

    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description();

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc();

    /// @return a prefix for log messages from this addressable
    virtual string logContextPrefix() P44_OVERRIDE;

    /// @return log level offset (overridden to use something other than the P44LoggingObj's)
    virtual int getLogLevelOffset() P44_OVERRIDE;

    /// @return name (usually user-defined) of the context object
    virtual string contextName() const P44_OVERRIDE;

    /// @return type (such as: device, element, vdc, trigger) of the context object
    virtual string contextType() const P44_OVERRIDE;

    /// @return id identifying the context object
    virtual string contextId() const P44_OVERRIDE;

  protected:

    /// type of behaviour
    virtual BehaviourType getType() const = 0;

    /// automatic id for this behaviour
    /// @return returns a ID for the behaviour.
    /// @note this is only valid for a fully configured behaviour, as it is derived from configured parameters
    virtual string getAutoId() { return getTypeName(); } // base class just returns the behaviour type (sensor, button, binaryInput)


    // persistence implementation
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;


    /// @name property access implementation for descriptor/settings/states
    /// @{

    /// @return number of description (readonly) properties
    virtual int numDescProps() { return 0; };

    /// @param aPropIndex the description property index
    /// @return description (readonly) property descriptor
    virtual const PropertyDescriptorPtr getDescDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor) { return NULL; };

    /// @return number of settings (read/write) properties
    virtual int numSettingsProps() { return 0; };

    /// @param aPropIndex the settings property index
    /// @return settings (read/write) property descriptor
    virtual const PropertyDescriptorPtr getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor) { return NULL; };

    /// @return number of states (read/write) properties
    virtual int numStateProps() { return 0; };

    /// @param aPropIndex the states property index
    /// @return states (read/write) property descriptor
    virtual const PropertyDescriptorPtr getStateDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor) { return NULL; };


    /// access single field in this behaviour
    /// @param aMode access mode (see PropertyAccessMode: read, write or write preload)
    /// @param aPropValue JsonObject with a single value
    /// @param aPropertyDescriptor decriptor for a single value field/array in this behaviour.
    /// @return false if value could not be accessed
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

    /// @}

    /// only for deeper levels
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;

  private:

    // key for saving this behaviour in the DB
    string getDbKey();

    // property access basic dispatcher implementation
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    int numLocalProps(PropertyDescriptorPtr aParentDescriptor);

  };
  typedef boost::intrusive_ptr<DsBehaviour> DsBehaviourPtr;

} // namespace p44


#endif /* defined(__p44vdc__dsbehaviour__) */
