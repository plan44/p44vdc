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

#ifndef __p44vdc__buttonbehaviour__
#define __p44vdc__buttonbehaviour__

#include "device.hpp"

using namespace std;

namespace p44 {

  class LocalController;


  #if ENABLE_LOCALCONTROLLER || ENABLE_JSONBRIDGEAPI
  class ButtonScenesMap
  {
  public:
    int mArea; ///< which area the button affects
    SceneNo mSceneClick[5]; ///< scene to call for off, 1..4 clicks
    ButtonScenesMap(DsButtonFunc aButtonFunc, bool aGlobal);
  };
  #endif // ENABLE_LOCALCONTROLLER || ENABLE_JSONBRIDGEAPI

  /// Implements the behaviour of a Digital Strom button, in particular the
  /// state machine which generates the different click types for the dS upstream
  /// from button press + button release events.
  /// This class should be used as-is for any virtual device which represents
  /// a user button or rocker switch.
  class ButtonBehaviour :
    public DsBehaviour
    #if ENABLE_LOCALCONTROLLER && ENABLE_P44SCRIPT
    ,public ValueSource
    #endif
  {
    typedef DsBehaviour inherited;

    friend class Device;
    friend class VdcHost; // for simple local mode
    friend class LocalController; // for full standalone mode
    friend class ProxyDevice; // for proxy behaviour setup

  public:

    typedef enum {
      statemachine_standard, ///< Digital Strom standard button state machine
      statemachine_simple, ///< no dimming, only tips, tolerant to missing a pressed/released event sometimes
      statemachine_dimmer ///< primarily dimming, does not generate single clicks (e.g. audio volume button)
    } ButtonStateMachineMode;


  protected:

    /// @name hardware derived parameters (constant during operation)
    ///   fixed constants or values set ONCE by device implementations when adding a ButtonBehaviour.
    ///   These need to remain constant after device initialisation is complete!
    /// @{
    bool mSupportsLocalKeyMode; ///< set if this button can act as local button
    int mButtonID; ///< the ID grouping all inputs of a hardware button (which can have multiple elements)
    VdcButtonType mButtonType; ///< type of button
    VdcButtonElement mButtonElementID; ///< identifies element of a multi-input button hardware-button
    DsButtonMode mFixedButtonMode; ///< if not buttonMode_inactive, then this is the only mode that can be set
    int mCombinables; ///< number of other device's submodules in total (including this device) that upstream might combine to form two-way buttons.
    /// @}

    /// @name persistent settings
    /// @{
    DsGroup mButtonGroup; ///< the group this button belongs to
    DsButtonFunc mButtonFunc; ///< the button function (LTNUM)
    DsButtonMode mButtonMode; ///< the button mode (LTMODE)
    DsChannelType mButtonChannel; ///< the channel the button is supposed to control
    bool mSetsLocalPriority; ///< button should set local priority
    bool mCallsPresent; ///< button should call "present" scene
    VdcButtonActionMode mButtonActionMode; ///< if set, button clicks directly issue action
    uint8_t mButtonActionId; ///< action Id (aka scene number) to trigger when button is clicked
    ButtonStateMachineMode mStateMachineMode; ///< state machine to use
    MLMicroSeconds mLongFunctionDelay; ///< delay to be used to differentiate "long" press
    /// @}


    /// @name internal volatile state
    /// @{
    #if ENABLE_JSONBRIDGEAPI
    bool mBridgeExclusive; ///< if set, button actions are only forwarded to bridges (if any is connected)
    #endif

    bool mButtonPressed; ///< set if button is currently pressed
    MLMicroSeconds mLastAction; ///< time of last clickType or actionMode/actionScene update

    DsClickType mClickType; ///< set to last click type of button

    VdcButtonActionMode mActionMode; ///< last triggered action mode
    uint8_t mActionId; ///< last triggered action Id (aka scene number)

    /// @}

  public:
  
    /// constructor
    /// @param aDevice the device the behaviour belongs to
    /// @param aId the string ID for that button.
    ///   If empty string is passed, an id will be auto-generated
    ButtonBehaviour(Device &aDevice, const string aId);

    /// initialisation of hardware-specific constants for this button input
    /// @param aButtonID the ID of the physical button (all inputs belonging to a single physical button
    ///   like a 2-way rocker or a 4-way navigation button must have the same buttonID. Multiple physical buttons must have distinct IDs)
    /// @param aType the physical button's type.
    /// @param aElement the element of the physical button this input represents (like: up or down for a 2-way rocker)
    /// @param aSupportsLocalKeyMode true if this button can be local key
    /// @param aCounterPartIndex for 2-way buttons, this identifies the index of the counterpart input (needed for dS 1.0 LTMODE compatibility only)
    /// @param aNumCombinables determine if and how this button input's device can be combined with other submodules of the same dSUID
    ///   - aNumCombinables==0 -> no cross-device combinations possible, button mode is fixed (one-way, two-way, whatever).
    ///   - aNumCombinables==1 -> no cross-device combinations possible, but button mode can be changed.
    //    - if aNumCombinables>1 this is the number of devices (including this one) with same basse dSUID but different submodule indices
    ///     that may be combined to form two-way buttons externally by assigning them respective DsButtonModes.
    ///     Note that this automatically implies the button mode is NOT fixed. The device containing such buttonInputs will report
    ///     modelfeature "twowayconfig", to enable upstream UI to offer forming two-way buttons out of combinable members.
    ///     The range of submodule indices available for combinations calculates as follows:
    ///     - baseIndex = subModuleIndex MOD aNumCombinables
    ///     - range: baseIndex..baseIndex+aNumCombinables-1
    /// @note this must be called once before the device gets added to the device container. Implementation might
    ///   also derive default values for settings from this information.
    void setHardwareButtonConfig(int aButtonID, VdcButtonType aType, VdcButtonElement aElement, bool aSupportsLocalKeyMode, int aCounterPartIndex, int aNumCombinables);

    /// get function
    DsButtonFunc getButtonFunction() { return mButtonFunc; };

    /// set group
    virtual void setGroup(DsGroup aGroup) P44_OVERRIDE;

    /// get group
    virtual DsGroup getGroup() P44_OVERRIDE { return mButtonGroup; };

    /// set channel
    virtual void setChannel(DsChannelType aChannel);

    /// set function
    virtual void setFunction(DsButtonFunc aFunc) { setPVar(mButtonFunc, aFunc); };

    /// set state machine mode
    void setStateMachineMode(ButtonStateMachineMode aStateMachineMode) { mStateMachineMode = aStateMachineMode; }

    /// set long function delay (how long button must be held to trigger "dimming" (press-and-hold) type operation
    void setLongFunctionDelay(MLMicroSeconds aLongFunctionDelay) { mLongFunctionDelay = aLongFunctionDelay; }

    /// @return true when button actions should be forwarded to bridge clients only, and NOT get processed locally
    bool isBridgeExclusive();

    /// make button bridge exclusive, i.e. not causing any local or DS actions
    void setBridgeExclusive() { mBridgeExclusive = true; };

    /// @name interface towards actual device hardware (or simulation)
    /// @{

    /// report current button state (can be repeated with same state)
    /// Do NOT use this mixed with injectClick()!
    /// @param aPressed true if button is currently pressed, false if button is currently released
    void updateButtonState(bool aPressed);

    /// send direct action
    /// @param aActionMode the mode how to send the action
    /// @param aActionId the scene number to send
    void sendAction(VdcButtonActionMode aActionMode, uint8_t aActionId);

    /// send click
    /// @param aClickType the click to send
    /// @note Normally this is not directly used, but automatically called by the button state machine
    void sendClick(DsClickType aClickType);

    /// inject already detected clicks or hold/release operations
    /// @note this is for devices which do click/hold disambiguation themselves.
    /// Do NOT use this mixed with updateButtonState()!
    /// @param aClickType the click or hold/release to inject
    void injectClick(DsClickType aClickType);

    /// inject (set) current button state without causing evaluation
    /// @note this is for devices which do click/hold disambiguation themselves.
    /// Do NOT use this mixed with updateButtonState()!
    /// @param aButtonPressed current pressed/released state of the button
    void injectState(bool aButtonPressed);

    /// @}


    /// check for defined state
    /// @return true if behaviour has a defined (non-NULL) state
    virtual bool hasDefinedState() P44_OVERRIDE;

    /// @return button element that defines the function of this button in local operation modes
    VdcButtonElement localFunctionElement();


    #if ENABLE_LOCALCONTROLLER && ENABLE_P44SCRIPT

    /// @name ValueSource interface
    /// @{

    /// check if enabled for use
    virtual bool isEnabled() P44_OVERRIDE;

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

    #endif // ENABLE_LOCALCONTROLLER

    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description() P44_OVERRIDE;

  protected:

    /// the behaviour type
    virtual BehaviourType getType() const P44_OVERRIDE { return behaviour_button; };

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
    enum {
      buttonflag_firstflag = 0x0001,
      buttonflag_setsLocalPriority = buttonflag_firstflag<<0,
      buttonflag_callsPresent = buttonflag_firstflag<<1,
      buttonflag_OBSOLETE_simpleStateMachine = buttonflag_firstflag<<2, // legacy, was used only from 1.5.3.2 .. 1.5.3.5
      buttonflag_nextflag = buttonflag_firstflag<<3
    };
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

  private:

    /// @name button state machine v2.01
    /// @{

    /// button states
    typedef enum {
      S0_idle,
      S1_initialpress,
      S2_holdOrTip,
      S3_hold,
      S4_nextTipWait,
      S5_nextPauseWait,
      S6_2ClickWait,
      S7_progModeWait,
      S8_awaitrelease,
      S9_2pauseWait,
      // S10 missing
      S11_localdim,
      S12_3clickWait,
      S13_3pauseWait,
      S14_awaitrelease_timedout, // await release of a hold that timed-out (button stuck more than 30 secs)
    } ButtonState;

    // state machine vars
    ButtonState mState;
    int mClickCounter;
    int mHoldRepeats;
    bool mDimmingUp;
    MLMicroSeconds mTimerRef;
    MLTicket mButtonStateMachineTicket;

    // state machine params
    static const int t_long_function_delay = 500*MilliSecond;
    static const int t_dim_repeat_time = 1000*MilliSecond;
    static const int t_click_length = 140*MilliSecond;
    static const int t_click_pause = 140*MilliSecond;
    static const int t_tip_timeout = 800*MilliSecond;
    static const int t_local_dim_timeout = 160*MilliSecond;
    static const int max_hold_repeats = 30;

    // methods
    bool isLocalButtonEnabled();
    bool isOutputOn();
    VdcDimMode twoWayDirection();
    void resetStateMachine();
    void checkStandardStateMachine(bool aStateChanged, MLMicroSeconds aNow);
    void checkCustomStateMachine(bool aStateChanged, MLMicroSeconds aNow);
    void clickSequenceComplete();
    void holdRepeat();
    void localSwitchOutput();
    void localDim(bool aStart);
    void injectedOpComplete(bool aSequence);

    /// @}

  };
  typedef boost::intrusive_ptr<ButtonBehaviour> ButtonBehaviourPtr;

}


#endif /* defined(__p44vdc__buttonbehaviour__) */
