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

#ifndef __p44vdc__sensorbehaviour__
#define __p44vdc__sensorbehaviour__

#include "device.hpp"
#include "valueunits.hpp"
#include "extutils.hpp"

#include <math.h>

using namespace std;

namespace p44 {

  /// Requirement: Evaluated values are sent on every internal evaluation interval if they differ from the previously sent value.
  /// Solution (existing in vDCs since ever):
  /// - "minPushInterval" : approximately represents the "evaluation interval" - values are not delivered more often than this
  ///   The default for this was 30 seconds for all sensors until now, will now be set depending on the sensor type/usage

  /// Requirement: An evaluated value is sent after the longest update interval is reached, even if the value does not differ from the one previously sent.
  ///   This serves as a heartbeat signal of the sensor besides providing measurement values.
  /// Solution (existing in vDCs since ever):
  /// - "aliveSignInterval" : represents the "longest update interval" in the hartbeat sense. As long as the sensor is operating
  ///   correctly, it will push updates at least in this interval.
  ///   This cannot be statically defined per sensor type, because this is a property of the actual sensor hardware that
  ///   cannot be changed. vdcs do *report* this interval for the sensor, if it is known and does apply (there are many
  ///   energy harvesting sensors which do not report anything regularly).
  /// - "changesOnlyInterval" : also represents the "longest update interval" in the sense that a *unchanged* sensor report
  ///   from the hardware is NOT pushed again during this interval, even if the "minPushInterval" would allow so.
  ///   This is a setting with a default value of 0.

  /// Requirement: To be able to catch fast changes, some sensor types are allowed to report a value immediately
  ///   if a certain delta to the previously sent value is reached.
  /// Solution (being implemented June 2018):
  ///   the sensor will be automatically assigned a profile depending on the sensor type, defining three parameters
  ///   - trigDelta the difference between current value and last pushed value (absolute or relative, see TrigMode)
  ///   - trigMin the minimum value the current value or the delta (in relative mode) must reach to trigger push
  ///   - trigIntvl the minimum interval between the last push and a delta-triggered push


  enum {
    tr_absolute = 0x00, ///< trigDelta is absolute value, trigMin is minimal current value needed for trigger to be active
    tr_relative = 0x01, ///< trigDelta is relative to current value, trigMin is minimal delta value needed for trigger to be active
    tr_unipolar = 0x02, ///< trigDelta positive means trigger is active only for increasing value, negative only for decreasing value
  };
  typedef uint8_t TrigMode;


  /// the parameter profile how to evaluate and report a particular sensor type
  typedef struct {
    // identification
    VdcSensorType type; ///< the sensor type these parameters apply to
    VdcUsageHint usage; ///< the sensor usage these parameters apply to
    // parameters
    // - evaluation
    MLMicroSeconds evalWin; ///< evaluation window size (time)
    MLMicroSeconds collWin; ///< subdatapoint collection time
    WinEvalMode evalType; ///< type of evaluation

    // - defaults for settings
    MLMicroSeconds pushIntvl; ///< default setting for minPushInterval, 0 = use global default
    MLMicroSeconds chgOnlyIntvl; ///< default setting for changesOnlyInterval, 0 = none
    // - SOD (send on delta) push delivery
    double trigDelta; ///< the minimal absolute or relative change to trigger a out-of-period push. 0=disabled
    TrigMode trigMode;  ///< SOD trigger mode
    double trigMin; ///< for tr_absolute: minimal absolute value needed to activate delta triggering; for tr_relative: the minimal absolute delta needed
    MLMicroSeconds trigIntvl; ///< how soon after a previous push a extra trigger may occur
  } SensorBehaviourProfile;



  /// Implements the behaviour of a Digital Strom Sensor. In particular it manages and throttles
  /// pushing updates to the dS upstream, to avoid jitter in hardware reported values to flood
  /// the system with unneded update messages
  class SensorBehaviour :
    public DsBehaviour
    #if ENABLE_P44SCRIPT
    ,public ValueSource
    #endif
  {
    typedef DsBehaviour inherited;
    friend class Device;
    friend class VdcHost;
    friend class LocalController; // for full standalone mode
    friend class ProxyDevice; // for proxy behaviour setup

    MLTicket mInvalidatorTicket;
    MLTicket mUpdateTicket;
    MLTicket mReEvaluationTicket;

  protected:

    /// @name behaviour description, constants or variables
    ///   set by device implementations when adding a Behaviour.
    /// @{
    VdcSensorType mSensorType; ///< type and physical unit of sensor
    VdcUsageHint mSensorUsage; ///< usage for sensor (if known)
    double mMin; ///< minimum value (corresponding to aEngineeringValue==0). If min==max, range is not known, and min is invalid
    double mMax; ///< max value.  If min==max, range is not known, and max is invalid
    double mResolution; ///< change per LSB of sensor engineering value. If resolution==0, resolution is not known
    MLMicroSeconds mUpdateInterval; ///< approximate time resolution of the sensor (how fast the sensor can track values)
    MLMicroSeconds mAliveSignInterval; ///< how often the sensor reports a value minimally (if it does not report for longer than that, it can be considered out of order). Can be 0 for sensors from which no regular update can be expected at all
    const SensorBehaviourProfile *mProfileP; ///< the sensor behaviour profile, can be NULL for simple forwarding without special processing
    MLMicroSeconds mMaxPushInterval; ///< max push interval (after that, value gets re-pushed even if no sensor update has occurred)
    bool mLimitToMinMax; ///< if set, sensor values are limited (clamped) to mMin..mMax even if engineering value is outside
    /// @}

    /// @name persistent settings
    /// @{
    DsGroup mSensorGroup; ///< group this sensor belongs to
    MLMicroSeconds mMinPushInterval; ///< minimum time between pushes (even if we have more frequent hardware sensor updates)
    MLMicroSeconds mChangesOnlyInterval; ///< time span during which only actual value changes are reported. After this interval, next hardware sensor update, even without value change, will cause a push)
    #if !REDUCED_FOOTPRINT
    VdcSensorFunc mSensorFunc; ///< the sensor function (P44 extension, for user dial "sensors" like dimmer wheels etc.)
    DsChannelType mSensorChannel; ///< the channel the sensor is supposed to control (P44 extension)
    VdcDialSyncMode mDialSyncMode; ///< how this dial syncs with actual output values (P44 extension)
    #endif
    #if ENABLE_RRDB
    string mRRDBpath; ///< the rrd path to log into. If it does not start with a slash, it is considered relative to Application::dataPath(). If it ends with a slash, the rrd file name is autogenerated as a unique sensor id
    string mRRDBconfig; ///< the rrd config for creating a rrdb file to log sensor data into
    #endif
    /// @}


    /// @name internal volatile state
    /// @{
    double mCurrentValue; ///< current sensor value
    double mLastPushedValue; ///< last pushed value (for delta triggering)
    MLMicroSeconds mLastUpdate; ///< time of last update from hardware
    MLMicroSeconds mLastPush; ///< time of last push
    int32_t mContextId; ///< context ID for the value - <0 = none
    string mContextMsg; ///< context message for the value - empty=none
    WindowEvaluatorPtr mFilter; ///< filter
    #if ENABLE_JSONBRIDGEAPI
    bool mBridgeExclusive; ///< if set, non-default sensor functions are only forwarded to bridges (if any is connected)
    #endif
    #if ENABLE_RRDB
    bool mLoggingReady; ///< if set, logging is ready
    MLMicroSeconds mLastRRDBupdate; ///< set when rrd was last updated
    string mRRDBfile; ///< if set, this is a currently active and initialized rrd database file and can be written to
    string mRRDBupdate; ///< the update string with %R, %F and %P placeholders for raw, filtered and pushed values
    #endif
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
    /// @param aMin minimum value
    /// @param aMax maximum value
    /// @param aResolution resolution (smallest step) of this sensor's value
    /// @param aUpdateInterval approximate time resolution of the sensor (how fast the sensor can track values).
    ///   Note that this does not mean the sensor actually pushes values this fast - e.g. when value does not change,
    ///   there might be no updates at all.
    ///   If set to 0, this means we have no information about time resolution
    /// @param aAliveSignInterval how often the sensor will send an update in all cases.
    ///   If set to 0, this means that no regular update interval can be expected.
    /// @param aDefaultChangesOnlyInterval the minimum time between two pushes with the same value. If the sensor hardware
    ///   sends updates more frequently, these are only pushed when the value has actually changed.
    /// @param aLimitToMinMax if set, reported sensor values are always limited to be within aMin and aMax
    /// @note this must be called once before the device gets added to the device container. Implementation might
    ///   also derive default values for settings from this information.
    void setHardwareSensorConfig(
      VdcSensorType aType, VdcUsageHint aUsage,
      double aMin, double aMax, double aResolution,
      MLMicroSeconds aUpdateInterval, MLMicroSeconds aAliveSignInterval, MLMicroSeconds aDefaultChangesOnlyInterval=0,
      bool aLimitToMinMax=false
    );

    /// set group
    virtual void setGroup(DsGroup aGroup) P44_OVERRIDE { mSensorGroup = aGroup; };

    /// get group
    virtual DsGroup getGroup() P44_OVERRIDE { return mSensorGroup; };

    #if !REDUCED_FOOTPRINT
    /// set sensor function (P44 extensions for dimmer sensors)
    void setSensorFunc(VdcSensorFunc aSensorFunc) { mSensorFunc = aSensorFunc; };

    /// set sensor channel (P44 extensions for dimmer sensors)
    void setSensorChannel(DsChannelType aSensorChannel) { mSensorChannel = aSensorChannel; };
    #endif

    /// creates a name of the form "<name>, <range><unit>"
    /// @param aName the name (function)
    void setSensorNameWithRange(const char *aName);

    /// current value and range
    double getCurrentValue() { return mCurrentValue; };
    MLMicroSeconds getLastUpdateTimestamp() { return mLastUpdate; };
    double getMax() { return mMax; };
    double getMin() { return mMin; };
    double getResolution() { return mResolution; };

    /// get sensor type
    /// @return the sensor type
    VdcSensorType getSensorType() { return mSensorType; };

    /// get the update interval
    /// @return the update interval
    MLMicroSeconds getUpdateInterval() { return mUpdateInterval; };

    /// get sensor type
    /// @return the value unit of the sensor
    ValueUnit getSensorUnit();

    /// @return sensor unit as text (symbol)
    string getSensorUnitText();

    /// @return sensor type as text
    string getSensorTypeText();

    /// @return formatted sensor value range as string
    string getSensorRange();

    /// set (custom) filter for sensor
    /// @param aEvalType the type of evaluation the filter should perform (specify eval_none to remove custom filter)
    /// @param aWindowTime width (timespan) of evaluation window
    /// @param aDataPointCollTime within that timespan, new values reported will be collected into a single datapoint
    /// @note this overrides any filtering that may have been set via sensor profiles at instantiation of the sensor
    ///   (setting eval_none restores sensor-type specific filtering, if any)
    void setFilter(WinEvalMode aEvalType, MLMicroSeconds aWindowTime, MLMicroSeconds aDataPointCollTime);

    /// set or disable default filtering, reporting, averaging (moderation) rules according to sensor type
    /// @param aEnable set to enable moderation
    /// @note moderation is on by default, and needs to be in order to conform to dS specifications.
    ///   Other non-DS use cases (e.g. bridging) might want to disable type specific moderation.
    /// @note disabling moderation does not reset push interval or changesOnlyInterval that might have been adjusted
    ///   by the default moderation that was active before.
    /// @note for sensor types that have no default moderation, trying to enable does nothing.
    void defaultModeration(bool aEnable);

    /// @return true when sensor changes should be forwarded to bridge clients only, and NOT get processed locally
    bool isBridgeExclusive();

    /// @name interface towards actual device hardware (or simulation)
    /// @{

    /// invalidate sensor value, i.e. indicate that current value is not known
    /// @param aPush set when change should be pushed. Can be set to false to use manual pushSensor() later
    void invalidateSensorValue(bool aPush = true);

    /// update sensor value (when new value received from hardware)
    /// @param aValue the new value from the sensor, in physical units according to sensorType (VdcSensorType)
    /// @param aMinChange what minimum change the new value must have compared to last reported value
    ///   to be treated as a change. Default is -1, which means half the declared resolution.
    /// @param aPush set when change should be pushed. Can be set to false to use manual pushSensor() later
    /// @param aContextId -1 for none, or positive integer identifying a context
    /// @param aContextMsg empty for none, or string describing a context
    void updateSensorValue(double aValue, double aMinChange = -1, bool aPush = true, int32_t aContextId = -1, const char *aContextMsg = NULL);

    /// sensor value change occurred
    /// @param aEngineeringValue the engineering value from the sensor.
    ///   The state value will be adjusted and scaled according to min/resolution
    /// @note this call only works correctly if resolution relates to 1 LSB of aEngineeringValue
    ///   Use updateSensorValue if relation between engineering value and physical unit value is more complicated
    void updateEngineeringValue(long aEngineeringValue, bool aPush = true, int32_t aContextId = -1, const char *aContextMsg = NULL);

    /// pushes the current sensor state
    /// @param aAlways if set, the state even pushed when last push is more recent than minPushInterval
    /// @note this can be used for sensor values that are more often updated than is of interest for upstream,
    ///   to only occasionally push an update. For that, use updateSensorValue() with aPush==false
    /// @return true if state was actually pushed (all conditions met and vDC API connected)
    bool pushSensor(bool aAlways = false);

    /// @}

    /// check if we have a recent value
    /// @param aMaxAge how old a value we consider still "valid"
    /// @return true if the sensor has a value not older than aMaxAge
    bool hasCurrentValue(MLMicroSeconds aMaxAge);

    /// check for defined state
    /// @return true if behaviour has a defined (non-NULL) state
    virtual bool hasDefinedState() P44_OVERRIDE;

    /// re-validate current sensor value (i.e. prevent it from expiring and getting invalid)
    virtual void revalidateState() P44_OVERRIDE;

    /// Get short text for a "first glance" status of the behaviour
    /// @return string, really short, intended to be shown as a narrow column in a list
    virtual string getStatusText() P44_OVERRIDE;


    #if ENABLE_P44SCRIPT
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
    #endif // ENABLE_P44SCRIPT

    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description() P44_OVERRIDE;

    #if ENABLE_JSONBRIDGEAPI
    /// instruction for bridges to bridge this behaviour or not
    /// @return true if the behaviour is meant to be bridged
    virtual bool wantsBridging() P44_OVERRIDE { return mDevice.bridgingFlags() & DeviceSettings::bridge_sensors; };
    #endif

  protected:

    /// the behaviour type
    virtual BehaviourType getType() const P44_OVERRIDE { return behaviour_sensor; };

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
    void reportFinalValue();
    void reEvaluateSensorValue(double aValue, double aMinChange);
    #if ENABLE_RRDB
    void prepareLogging();
    void logSensorValue(MLMicroSeconds aTimeStamp, double aRawValue, double aProcessedValue, double aPushedValue);
    #endif

  };
  typedef boost::intrusive_ptr<SensorBehaviour> SensorBehaviourPtr;



} // namespace p44

#endif /* defined(__p44vdc__sensorbehaviour__) */
