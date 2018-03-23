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

#ifndef __p44vdc__sensorbehaviour__
#define __p44vdc__sensorbehaviour__

#include "device.hpp"
#include "valueunits.hpp"

#include <math.h>

using namespace std;

namespace p44 {


  /// Implements the behaviour of a digitalSTROM Sensor. In particular it manages and throttles
  /// pushing updates to the dS upstream, to avoid jitter in hardware reported values to flood
  /// the system with unneded update messages
  class SensorBehaviour : public DsBehaviour, public ValueSource
  {
    typedef DsBehaviour inherited;
    friend class Device;

    MLTicket invalidatorTicket;

  protected:

    /// @name behaviour description, constants or variables
    ///   set by device implementations when adding a Behaviour.
    /// @{
    VdcSensorType sensorType; ///< type and physical unit of sensor
    VdcUsageHint sensorUsage; ///< usage for sensor (if known)
    double min; ///< minimum value (corresponding to aEngineeringValue==0). If min==max, range is not known, and min is invalid
    double max; ///< max value.  If min==max, range is not known, and max is invalid
    double resolution; ///< change per LSB of sensor engineering value. If resolution==0, resolution is not known
    MLMicroSeconds updateInterval; ///< approximate time resolution of the sensor (how fast the sensor can track values)
    MLMicroSeconds aliveSignInterval; ///< how often the sensor reports a value minimally (if it does not report for longer than that, it can be considered out of order). Can be 0 for sensors from which no regular update can be expected at all
    /// @}

    /// @name persistent settings
    /// @{
    DsGroup sensorGroup; ///< group this sensor belongs to
    MLMicroSeconds minPushInterval; ///< minimum time between pushes (even if we have more frequent hardware sensor updates)
    MLMicroSeconds changesOnlyInterval; ///< time span during which only actual value changes are reported. After this interval, next hardware sensor update, even without value change, will cause a push)
    /// @}


    /// @name internal volatile state
    /// @{
    double currentValue; ///< current sensor value
    MLMicroSeconds lastUpdate; ///< time of last update from hardware
    MLMicroSeconds lastPush; ///< time of last push
    int32_t contextId; ///< context ID for the value - <0 = none
    string contextMsg; ///< context message for the value - empty=none
    /// @}


  public:

    /// constructor
    /// @param aDevice the device the behaviour belongs to
    /// @param aId the string ID for that sensor.
    ///   If empty string is passed, an id will be auto-generated from the sensor type (after setHardwareSensorConfig() is called)
    SensorBehaviour(Device &aDevice, const string aId);

    virtual ~SensorBehaviour();

    /// initialisation of hardware-specific constants for this sensor
    /// @param aType the sensor type (Note: not the same as dS sensor types, needs mapping)
    /// @param aUsage how this input is normally used (indoor/outdoor etc.)
    /// @param aMin minimum value)
    /// @param aMax maximum value)
    /// @param aResolution resolution (smallest step) of this sensor's value
    /// @param aUpdateInterval how often an update can be expected from this sensor normally. If 0, this means that no
    ///   regular updates can be expected.
    /// @param aAliveSignInterval how often the sensor will send an update in all cases. If 0, this means that no regular
    ///   update interval can be expected.
    /// @param aDefaultChangesOnlyInterval the minimum time between two pushes with the same value. If the sensor hardware
    ///   sends updates more frequently, these are only pushed when the value has actually changed.
    /// @note this must be called once before the device gets added to the device container. Implementation might
    ///   also derive default values for settings from this information.
    void setHardwareSensorConfig(VdcSensorType aType, VdcUsageHint aUsage, double aMin, double aMax, double aResolution, MLMicroSeconds aUpdateInterval, MLMicroSeconds aAliveSignInterval, MLMicroSeconds aDefaultChangesOnlyInterval=0);

    /// set group
    virtual void setGroup(DsGroup aGroup) P44_OVERRIDE { sensorGroup = aGroup; };

    /// creates a name of the form "<name>, <range><unit>"
    /// @param aName the name (function)
    void setSensorNameWithRange(const char *aName);

    /// current value and range
    double getCurrentValue() { return currentValue; };
    MLMicroSeconds getLastUpdateTimestamp() { return lastUpdate; };
    double getMax() { return max; };
    double getMin() { return min; };
    double getResolution() { return resolution; };

    /// get sensor type
    /// @return the sensor type
    VdcSensorType getSensorType() { return sensorType; };

    /// get the update interval
    /// @return the update interval
    MLMicroSeconds getUpdateInterval() { return updateInterval; };

    /// get sensor type
    /// @return the value unit of the sensor
    ValueUnit getSensorUnit();

    /// @return sensor unit as text (symbol)
    string getSensorUnitText();

    /// @return sensor type as text
    string getSensorTypeText();

    /// @return formatted sensor value range as string
    string getSensorRange();

    /// @name interface towards actual device hardware (or simulation)
    /// @{

    /// invalidate sensor value, i.e. indicate that current value is not known
    /// @param aPush set when change should be pushed. Can be set to false to use manual pushSensor() later
    void invalidateSensorValue(bool aPush = true);

    /// update sensor value (when new value received from hardware)
    /// @param aValue the new value from the sensor, in physical units according to sensorType (VdcSensorType)
    /// @param aMinChange what minimum change the new value must have compared to last reported value
    /// @param aPush set when change should be pushed. Can be set to false to use manual pushSensor() later
    /// @param aContextId -1 for none, or positive integer identifying a context
    /// @param aContextMsg empty for none, or string describing a context
    ///   to be treated as a change. Default is -1, which means half the declared resolution.
    void updateSensorValue(double aValue, double aMinChange = -1, bool aPush = true, int32_t aContextId = -1, const char *aContextMsg = NULL);

    /// sensor value change occurred
    /// @param aEngineeringValue the engineering value from the sensor.
    ///   The state value will be adjusted and scaled according to min/resolution
    /// @note this call only works correctly if resolution relates to 1 LSB of aEngineeringValue
    ///   Use updateSensorValue if relation between engineering value and physical unit value is more complicated
    void updateEngineeringValue(long aEngineeringValue, bool aPush = true, int32_t aContextId = -1, const char *aContextMsg = NULL);

    /// pushes the current sensor state
    /// @param aChanged if set, the state is considered changed, which means it is always pushed, unless last push was less than minPushInterval ago
    /// @param aAlways if set, the state even pushed when last push is more recent than minPushInterval
    /// @note this can be used for sensor values that are more often updated than is of interest for upstream,
    ///   to only occasionally push an update. For that, use updateSensorValue() with aPush==false
    /// @return true if state was actually pushed (all conditions met and vDC API connected)
    bool pushSensor(bool aChanged, bool aAlways = false);

    /// @}

    /// check if we have a recent value
    /// @param aMaxAge how old a value we consider still "valid"
    /// @return true if the sensor has a value not older than aMaxAge
    bool hasCurrentValue(MLMicroSeconds aMaxAge);

    /// check for defined state
    /// @return true if behaviour has a defined (non-NULL) state
    virtual bool hasDefinedState() P44_OVERRIDE;

    /// Get short text for a "first glance" status of the behaviour
    /// @return string, really short, intended to be shown as a narrow column in a list
    virtual string getStatusText() P44_OVERRIDE;


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



    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description() P44_OVERRIDE;

  protected:

    /// the behaviour type
    virtual BehaviourType getType() P44_OVERRIDE { return behaviour_sensor; };

    /// automatic id for this behaviour
    /// @return returns a ID for the behaviour.
    /// @note this is only valid for a fully configured behaviour, as it is derived from configured parameters
    virtual string getAutoId() P44_OVERRIDE;

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
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

  private:

    void armInvalidator();

  };
  typedef boost::intrusive_ptr<SensorBehaviour> SensorBehaviourPtr;



} // namespace p44

#endif /* defined(__p44vdc__sensorbehaviour__) */
