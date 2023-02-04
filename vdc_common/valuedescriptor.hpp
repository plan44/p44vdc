//  SPDX-License-Identifier: GPL-3.0-or-later
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

#ifndef __p44vdc__valuedescriptor__
#define __p44vdc__valuedescriptor__

#include "p44vdc_common.hpp"
#include "dsdefs.h"
#include "propertycontainer.hpp"
#include "valueunits.hpp"

using namespace std;

namespace p44 {


  /// value descriptor / validator / value extractor
  /// The value descriptor can describe a parameter via read-only properties,
  /// can check values for conforming to the description and convert values for using in code.
  /// This is an abstract base class, actual validation/conversion is implemented in subclasses
  class ValueDescriptor : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    friend class ValueList;

  protected:

    /// base class constructor for subclasses
    /// @param aName the name of this value
    /// @param aValueType the value type of this value (describing computing type)
    /// @param aValueUnit the value unit if this value (describing physical unit type and scaling)
    /// @param aHasDefault true if the parameter has a non-null default value
    ValueDescriptor(const string aName, VdcValueType aValueType, ValueUnit aValueUnit, bool aHasDefault);

  public:

    /// Using external values as action parameter - checks and conversions
    /// @{

    /// checks if aApiValue conforms to the parameter definition
    /// @param aApiValue API value containing a value to be used for this value. Passing no value (not a NULL-value-object!) is always conformant
    /// @param aMakeInternal if set, the value is converted to internal format (relevant for enums, to get them as numeric value)
    /// @return NULL if the value conforms, API error describing what's wrong if not
    virtual ErrorPtr conforms(ApiValuePtr aApiValue, bool aMakeInternal = false) = 0;

    /// get the name
    /// @return name of this value
    string getName() const { return valueName; }
    /// @return name of this value as CStr
    const char *getNameCStr() const { return valueName.c_str(); }

    /// get the time of last update (even if no change)
    /// @return the time of last update or Never if value has never been set so far
    MLMicroSeconds getLastUpdate() { return lastUpdate; };

    /// get the time of last change
    /// @return the time of last change or Never if value has never been set so far
    MLMicroSeconds getLastChange() { return lastChange; };


    /// get the (default) value into an ApiValue
    /// @param aApiValue the API value to write the value to
    /// @param aAsInternal if set, the value is returned in internal format (relevant for enums, to get them as numeric value)
    /// @param aPrevious if set, the previous value is returned
    /// @return true if there is a (default) value that could be assigned to aApiValue, false otherwise (aApiValue will be untouched)
    virtual bool getValue(ApiValuePtr aApiValue, bool aAsInternal = false, bool aPrevious = false) = 0;

    /// get the value as string
    /// @param aAsInternal if set, the value is returned in internal format (relevant for enums, to get them as numeric value)
    /// @param aPrevious if set, the previous value is returned
    /// @return string representation of the current value
    string getStringValue(bool aAsInternal = false, bool aPrevious = false);

    /// get a double value
    /// @param aAsInternal if set, the value is returned in internal format (relevant for enums, to get them as numeric value)
    /// @param aPrevious if set, the previous value is returned
    double getDoubleValue(bool aAsInternal = false, bool aPrevious = false);

    /// get a 32 bit integer value
    /// @param aAsInternal if set, the value is returned in internal format (relevant for enums, to get them as numeric value)
    /// @param aPrevious if set, the previous value is returned
    /// @note boolean values return 0 for false, 1 for true
    int32_t getInt32Value(bool aAsInternal = false, bool aPrevious = false);

    /// get a boolean value
    /// @param aAsInternal if set, the value is returned in internal format (relevant for enums, to get them as numeric value)
    /// @param aPrevious if set, the previous value is returned
    /// @note also works for non-boolean numbers or internal enum values (0=false, everything else=true)
    bool getBoolValue(bool aAsInternal = false, bool aPrevious = false);


    /// @}

    /// Setting state and state parameter value to allow query via API and property pushing
    /// @{

    /// set double value
    /// @param aValue the double value to set
    /// @return true if set value differs from previous value
    virtual bool setDoubleValue(double aValue) { return false; /* NOP in base class */ };

    /// update double value for significant changes only
    /// @param aValue the new value from the sensor, in physical units according to siUnit
    /// @param aMinChange what minimum change the new value must have compared to last reported value
    ///   to be treated as a change. Default is -1, which means half the declared resolution.
    /// @return true if set value differs enough from previous value and was actually updated
    virtual bool updateDoubleValue(double aValue, double aMinChange = -1)  { return false; /* NOP in base class */ };

    /// set int value
    /// @param aValue the int value to set
    /// @return true if set value differs from previous value
    virtual bool setInt32Value(int32_t aValue) { return false; /* NOP in base class */ };

    /// set string value
    /// @param aValue the string value to set
    /// @return true if set value differs from previous value
    virtual bool setStringValue(const string aValue) { return false; /* NOP in base class */ };

    /// set boolean value
    /// @param aValue the int value to set
    /// @return true if set value differs from previous value
    virtual bool setBoolValue(bool aValue) { return false; /* NOP in base class */ };

    /// set API value
    /// @param aValue the value to set, already converted to internal format (for text enums)
    /// @return true if set value differs from previous value
    /// @note conforms() should be applied to aValue first to make sure value is ok to set and is converted to internal value
    bool setValue(ApiValuePtr aValue);

    /// make value invalid, reported as NULL when accessed via properties
    /// @return true if value was valid before (i.e. became invalid now)
    bool invalidate();

    /// set "defaultvalue" flag
    void setIsDefault(bool aIsDefault) { isDefaultValue = aIsDefault; };

    /// set "defaultvalue" flag
    void setIsOptional(bool aIsOptional) { isOptionalValue = aIsOptional; };

    /// set "readonly" flag
    void setReadOnly(bool aReadOnly) { readOnly = aReadOnly; };

    /// set "needsFetch" flag
    void setNeedsFetch(bool aNeedsFetch) { needsFetch = aNeedsFetch; };

    /// check readonly flag
    bool isReadOnly() { return readOnly; }

    /// check default value flag (value itself can still be NULL)
    bool isDefault() { return isDefaultValue; }

    /// check optional value flag (value itself can still be non-NULL)
    bool isOptional() { return isOptionalValue; }

    /// check readonly flag
    bool doesNeedFetch() { return needsFetch; }

    /// get the value unit
    ValueUnit getValueUnit() { return valueUnit; }


    /// @}

    /// Utilities
    /// @{

    /// get name of a given VdcValueType
    /// @param aValueType the value type to get the name for
    /// @return value type name
    static string valueTypeName(VdcValueType aValueType);

    /// get value type from a given string
    /// @param aValueTypeName a value type name string
    /// @return value type (valueType_unknown when string does not match)
    static VdcValueType stringToValueType(const string aValueTypeName);

    /// @}

  protected:

    string valueName; ///< the name of the value
    bool hasValue; ///< set if there is a stored value. For action params, this is the default value. For state/states params this is the actual value
    bool isDefaultValue; ///< set if the value stored is the default value
    bool isOptionalValue; ///< set if "null" is a conformant value
    bool readOnly; ///< set if the value cannot be written
    bool needsFetch; ///< set if property needs a fetch callback before it can be read
    VdcValueType valueType; ///< the technical type of the value
    ValueUnit valueUnit; ///< the unit+scaling of the value
    MLMicroSeconds lastUpdate; ///< when the value was last updated
    MLMicroSeconds lastChange; ///< when the value was last changed

    /// set last update
    /// @param aLastUpdate time of last update, can be Never (or Infinite to use current now())
    /// @return true if this update caused hasValue to be changed from false to true
    bool setLastUpdate(MLMicroSeconds aLastUpdate=Infinite);

    /// report if changed
    /// @param aChanged true if value has changed
    /// @return just returns aChanged
    bool setChanged(bool aChanged);

    /// checks if aApiValue needs further conformance check
    /// @param aApiValue API value containing a value to be used for this value. Passing no value (not a NULL-value-object!) is always conformant
    /// @return true if type specific conformance check is needed and can be done
    bool needsConformanceCheck(ApiValuePtr aApiValue, ErrorPtr &aError);

    /// notify that this value has just updated
    /// @note this must be called by setters just after updating update time, current and previous values.
    ///   it is used to possibly trigger events and property pushes
    void notifyUpdate();

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;
  };
  typedef boost::intrusive_ptr<ValueDescriptor> ValueDescriptorPtr;


  /// parameter descriptor subclass for numeric parameters, describing parameter via min/max/resolution
  class NumericValueDescriptor : public ValueDescriptor
  {
    typedef ValueDescriptor inherited;

    double min; ///< maximum allowed value
    double max; ///< minimum allowed value
    double resolution; ///< resolution
    double value; ///< the (default) value
    double previousValue; ///< the previous value

  public:

    /// constructor for a numeric parameter, which can be any of the physical unit types, bool, int, numeric enum or generic double
    NumericValueDescriptor(const string aName, VdcValueType aValueType, ValueUnit aValueUnit, double aMin, double aMax, double aResolution, bool aHasDefault = false, double aDefaultValue = 0) :
      inherited(aName, aValueType, aValueUnit, aHasDefault), min(aMin), max(aMax), resolution(aResolution), value(aDefaultValue) {};

    virtual ErrorPtr conforms(ApiValuePtr aApiValue, bool aMakeInternal = false) P44_FINAL P44_OVERRIDE;
    virtual bool getValue(ApiValuePtr aApiValue, bool aAsInternal = false, bool aPrevious = false) P44_FINAL P44_OVERRIDE;

    virtual bool setDoubleValue(double aValue) P44_FINAL P44_OVERRIDE;
    virtual bool updateDoubleValue(double aValue, double aMinChange = -1) P44_FINAL P44_OVERRIDE;

    virtual bool setInt32Value(int32_t aValue) P44_FINAL P44_OVERRIDE;

    virtual bool setBoolValue(bool aValue) P44_FINAL P44_OVERRIDE;

    void setMinValue(double aValue) { min = aValue; }
    void setMaxValue(double aValue) { max = aValue; }

  protected:

    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_FINAL P44_OVERRIDE;

  };


  /// parameter descriptor subclass for text parameters
  class TextValueDescriptor : public ValueDescriptor
  {
    typedef ValueDescriptor inherited;

    string value; ///< the (default) value
    string previousValue; ///< the previous value

  public:

    /// constructor for a text string parameter
    TextValueDescriptor(const string aName, bool aHasDefault = false, const string aDefaultValue = "") :
      inherited(aName, valueType_string, valueUnit_none, aHasDefault), value(aDefaultValue) {};

    virtual ErrorPtr conforms(ApiValuePtr aApiValue, bool aMakeInternal = false) P44_FINAL P44_OVERRIDE;
    virtual bool getValue(ApiValuePtr aApiValue, bool aAsInternal = false, bool aPrevious = false) P44_FINAL P44_OVERRIDE;

    virtual bool setStringValue(const string aValue) P44_FINAL P44_OVERRIDE;

  };



  class EnumList : public P44Obj
  {
  public:
    typedef uint32_t EnumValue;
    static const EnumValue unknownEnum = 0xFFFFFFFF;

  private:
    typedef pair<string, EnumValue> EnumDesc;
    typedef vector<EnumDesc> EnumVector;
    EnumVector enumDescs; ///< text to enum value mapping pairs
    bool valuesInDescription; ///< if set, numeric values are shown in the description

  public:
    EnumList(bool aWithValuesInDescription);

    /// add enum (text to value mapping)
    /// @param aEnumText the text value
    /// @param aEnumValue the integer value corresponding to the text
    void addMapping(const char *aEnumText, EnumValue aEnumValue);

    /// add enum texts and map them to values 0,1,2...
    /// @param aTexts vector of texts
    void addEnumTexts(std::vector<const char*> aTexts);

    /// get text for
    /// @param aValue value to get text for
    /// @param aDefaultText text to return when aValue is unknown (defaults to NULL)
    /// @return text corresponding to specified aValue, aDefaultText if no mapping exists
    const char *textForValue(EnumValue aValue, const char *aDefaultText = NULL) const;

    /// get value for specified text
    /// @param aValue text to get value for
    /// @param aDefaultValue value to return when aText is unknown (defaults to EnumList::unknownEnum)
    /// @return value corresponding to specified aText, aDefaultValue if no mapping exists
    EnumValue valueForText(const char *aText, bool aCaseSensitive = false, EnumValue aDefaultValue = unknownEnum) const;
    EnumValue valueForText(const string &aText, bool aCaseSensitive = false, EnumValue aDefaultValue = unknownEnum) const;

    /// @name property interface.
    /// @note these methods have the same signatures like PropertyContainer methods, but are only helpers to
    ///    implement enums in an actual PropertyContainer
    /// @{

    int numProps();
    PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

    /// @}
  };
  typedef boost::intrusive_ptr<EnumList> EnumListPtr;



  class EnumValueDescriptor;
  typedef boost::intrusive_ptr<EnumValueDescriptor> EnumValueDescriptorPtr;

  /// parameter descriptor subclass for enumeration parameters, describing parameter via a list of possible values
  class EnumValueDescriptor : public ValueDescriptor
  {
    typedef ValueDescriptor inherited;

    EnumListPtr enumList; ///< the enum value/text mapping
    uint32_t value; ///< the (default) enum value
    uint32_t previousValue; ///< the previous value
    bool noInternalValue; ///< the internal value is not exposed, getValue() always returns external (text) value

  public:

    /// constructor for a text enumeration parameter
    EnumValueDescriptor(const string aName, bool aNoInternalValue=false);

    /// add a enum value
    /// @param aEnumText the text
    /// @param aEnumValue the numeric value corresponding to the text.
    /// @param aIsDefault if set, this is considered the default value
    void addEnum(const char *aEnumText, int aEnumValue, bool aIsDefault = false);

    virtual ErrorPtr conforms(ApiValuePtr aApiValue, bool aMakeInternal = false) P44_FINAL P44_OVERRIDE;
    virtual bool getValue(ApiValuePtr aApiValue, bool aAsInternal = false, bool aPrevious = false) P44_FINAL P44_OVERRIDE;

    virtual bool setBoolValue(bool aValue) P44_FINAL P44_OVERRIDE;
    virtual bool setInt32Value(int32_t aValue) P44_FINAL P44_OVERRIDE;
    virtual bool setDoubleValue(double aValue) P44_FINAL P44_OVERRIDE;
    virtual bool setStringValue(const string aValue) P44_FINAL P44_OVERRIDE;
    bool setStringValueCaseInsensitive(const string& aValue);

    /// static factory method: create a EnumValueDescriptor and add list of strings as simple enums
    /// (first string corresponds to enum value 0, next to 1, etc.)
    /// @param aName name of the EnumValue
    /// @param aValues vector of strings representing the enum values in order 0...n
    static EnumValueDescriptorPtr create(const char* aName, std::vector<const char*> aValues);

  protected:

    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_FINAL P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_FINAL P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_FINAL P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_FINAL P44_OVERRIDE;

  };


  class ValueList : public PropertyContainer
  {
    typedef PropertyContainer inherited;

  public:

    typedef vector<ValueDescriptorPtr> ValuesVector;

    ValuesVector values;

    /// add a value (descriptor)
    /// @param aValueDesc a value descriptor object.
    void addValue(ValueDescriptorPtr aValueDesc);

    /// get value (for applying updates)
    /// @param aName name of the value(descriptor) to get
    ValueDescriptorPtr getValue(const string aName);


  protected:

    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_FINAL P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_FINAL P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<ValueList> ValueListPtr;


} // namespace p44


#endif /* defined(__p44vdc__valuedescriptor__) */
