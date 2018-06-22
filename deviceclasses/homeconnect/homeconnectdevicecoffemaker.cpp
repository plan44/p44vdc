//
//  Copyright (c) 2017 digitalSTROM.org, Zurich, Switzerland
//
//  Author: Pawel Kochanowski <pawel.kochanowski@digitalstrom.com>
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

#include "homeconnectdevicecoffemaker.hpp"
#include "homeconnectaction.hpp"

#if ENABLE_HOMECONNECT

#include "homeconnectaction.hpp"

namespace p44 {

namespace
{
  static const string COFFEEMAKER_CONFIG_FILE_NAME = HOMECONNECT_CONFIG_FILE_NAME_BASE + "CoffeeMaker";
}

HomeConnectDeviceCoffeMaker::HomeConnectDeviceCoffeMaker(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
    inherited(aVdcP, aHomeApplicanceInfoRecord, COFFEEMAKER_CONFIG_FILE_NAME)
{
  HomeConnectDeviceSettingsPtr settings = new HomeConnectDeviceSettings(*this);
  settings->fireAction = "StandBy";
  settings->leaveHomeAction = "StandBy";
  settings->deepOffAction = "StandBy";
  settings->sleepAction = "StandBy";

  installSettings(settings);
}

HomeConnectDeviceCoffeMaker::~HomeConnectDeviceCoffeMaker()
{
  // TODO Auto-generated destructor stub
}

void HomeConnectDeviceCoffeMaker::configureDevice(StatusCB aStatusCB)
{
  addProgramNameProperty();
  // configure operation mode
  OperationModeConfiguration omConfig = { 0 };
  omConfig.hasInactive = true;
  omConfig.hasReady = true;
  omConfig.hasDelayedStart = false;
  omConfig.hasRun = true;
  omConfig.hasPause = false;
  omConfig.hasActionrequired = true;
  omConfig.hasFinished = false;
  omConfig.hasError = true;
  omConfig.hasAborting = true;
  configureOperationModeState(omConfig);

  // configure remote control
  RemoteControlConfiguration rcConfig = { 0 };
  rcConfig.hasControlInactive = false;
  rcConfig.hasControlActive = true;
  rcConfig.hasStartActive = true;
  configureRemoteControlState(rcConfig);

  // configure power state
  PowerStateConfiguration psConfig = { 0 };
  psConfig.hasOff = false;
  psConfig.hasOn = true;
  psConfig.hasStandby = true;
  configurePowerState(psConfig);

  // configure program status properties
  ProgramStatusConfiguration progStatusConfig = { 0 };
  progStatusConfig.hasElapsedTime = false;
  progStatusConfig.hasRemainingTime = true;
  progStatusConfig.hasProgres = true;
  configureProgramStatus(progStatusConfig);

  EventConfiguration eventConfig = { 0 };
  eventConfig.hasAlarmClockElapsed = false;
  eventConfig.hasLocallyOperated = true;
  eventConfig.hasProgramAborted = false;
  eventConfig.hasProgramFinished = true;
  eventConfig.hasProgramStarted = true;
  configureEvents(eventConfig);

  HomeConnectGoToStandbyActionPtr action =
      HomeConnectGoToStandbyActionPtr(new HomeConnectGoToStandbyAction(*this, *powerStateDescriptor, *operationModeDescriptor));
  deviceActions->addAction(action);
  addDefaultPowerOnAction();
  addDefaultStopAction();

  addAction("Espresso",          "Espresso",            "Espresso",          35,  60,  5,  40);
  addAction("EspressoMacchiato", "Espresso Macchiato",  "EspressoMacchiato", 40,  60,  10, 50);
  addAction("Coffee",            "Coffee",              "Coffee",            60,  250, 10, 100);
  addAction("Cappuccino",        "Cappuccino",          "Cappuccino",        100, 300, 20, 180);
  addAction("LatteMacchiato",    "Latte Macchiato",     "LatteMacchiato",    200, 400, 20, 250);
  addAction("CaffeLatte",        "Caffe Latte",         "CaffeLatte",        100, 400, 20, 200);

  beanAmountProp = EnumValueDescriptorPtr(new EnumValueDescriptor("BeanAmount", true));
  int i = 0;
  beanAmountProp->addEnum("VeryMild", i++);
  beanAmountProp->addEnum("Mild", i++);
  beanAmountProp->addEnum("Normal", i++, true);
  beanAmountProp->addEnum("Strong", i++);
  beanAmountProp->addEnum("VeryStrong", i++);
  beanAmountProp->addEnum("DoubleShot", i++);
  beanAmountProp->addEnum("DoubleShotPlus", i++);
  beanAmountProp->addEnum("DoubleShotPlusPlus", i++);

  fillQuantityProp = ValueDescriptorPtr(new NumericValueDescriptor("FillQuantity", valueType_numeric, VALUE_UNIT(valueUnit_liter, unitScaling_milli), 0, 400, 1, true, 0));

  deviceProperties->addProperty(beanAmountProp);
  deviceProperties->addProperty(fillQuantityProp);

  aStatusCB(Error::ok());
}

void HomeConnectDeviceCoffeMaker::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  inherited::stateChanged(aChangedState, aEventsToPush);
}

void HomeConnectDeviceCoffeMaker::handleEventTypeNotify(const string& aKey, JsonObjectPtr aValue)
{
  ALOG(LOG_INFO, "CoffeMaker Event 'NOTIFY' - item: %s, %s", aKey.c_str(), aValue ? aValue->c_strValue() : "<none>");

  if (aKey == "ConsumerProducts.CoffeeMaker.Option.BeanAmount") {
    string value = (aValue != NULL) ? aValue->stringValue() : "";
    beanAmountProp->setStringValueCaseInsensitive(removeNamespace(value));
    return;
  }

  if (aKey == "ConsumerProducts.CoffeeMaker.Option.FillQuantity") {
    int32_t value = (aValue != NULL) ? aValue->int32Value() : 0;
    fillQuantityProp->setInt32Value(value);
    return;
  }

  inherited::handleEventTypeNotify(aKey, aValue);
}

void HomeConnectDeviceCoffeMaker::handleRemoteStartAllowedChange(JsonObjectPtr aNewValue)
{
  if (aNewValue == NULL) {
    return;
  }

  string remoteStartValue;
  bool value = aNewValue->boolValue();

  if (value) {
    remoteStartValue = "RemoteStartActive";
  }
  else {
    remoteStartValue = "RemoteControlActive";
  }

  if (remoteControlDescriptor->setStringValueCaseInsensitive(remoteStartValue)) {
    ALOG(LOG_NOTICE, "New Remote Start Allowed State: '%s'", remoteStartValue.c_str());
    remoteControl->push();
  }
}

void HomeConnectDeviceCoffeMaker::handleOperationStateChange(const string& aNewValue)
{
  if (aNewValue == "BSH.Common.EnumType.OperationState.Finished") {
    if (operationModeDescriptor->getStringValue() == "ModeRun") {
      deviceEvents->pushEvent("ProgramFinished");
    }
  }
  else {
    inherited::handleOperationStateChange(aNewValue);
  }

}

void HomeConnectDeviceCoffeMaker::addAction(const string& aActionName,
                                            const string& aDescription,
                                            const string& aProgramName,
                                            double aFillAmountMin,
                                            double aFillAmountMax,
                                            double aFillAmountResolution,
                                            double aFillAmountDefault)
{

  HomeConnectProgramBuilder builder("ConsumerProducts.CoffeeMaker.Program.Beverage." + aProgramName);

  builder.addOption("ConsumerProducts.CoffeeMaker.Option.BeanAmount", "\"ConsumerProducts.CoffeeMaker.EnumType.BeanAmount.@{BeanAmount}\"");
  builder.addOption("ConsumerProducts.CoffeeMaker.Option.FillQuantity", "@{FillQuantity%%0}");


  builder.selectMode(HomeConnectProgramBuilder::Mode_Activate);
  string runProgramCommand = builder.build();

  builder.selectMode(HomeConnectProgramBuilder::Mode_Select);
  string selectProgramCommand = builder.build();


  EnumValueDescriptorPtr beanAmount = EnumValueDescriptorPtr(new EnumValueDescriptor("BeanAmount", true));
  int i = 0;
  beanAmount->addEnum("VeryMild", i++);
  beanAmount->addEnum("Mild", i++);
  beanAmount->addEnum("Normal", i++, true); // default
  beanAmount->addEnum("Strong", i++);
  beanAmount->addEnum("VeryStrong", i++);
  beanAmount->addEnum("DoubleShot", i++);
  beanAmount->addEnum("DoubleShotPlus", i++);
  beanAmount->addEnum("DoubleShotPlusPlus", i++);

  ValueDescriptorPtr fillAmount =
      ValueDescriptorPtr(new NumericValueDescriptor("FillQuantity",
                                                    valueType_numeric,
                                                    VALUE_UNIT(valueUnit_liter, unitScaling_milli),
                                                    aFillAmountMin,
                                                    aFillAmountMax,
                                                    aFillAmountResolution,
                                                    true,
                                                    aFillAmountDefault));

  HomeConnectActionPtr action =
      HomeConnectActionPtr(new HomeConnectPowerOnAction(*this,
                                                        aActionName,
                                                        aDescription,
                                                        runProgramCommand,
                                                        selectProgramCommand,
                                                        *powerStateDescriptor,
                                                        *operationModeDescriptor));
  action->addParameter(beanAmount);
  action->addParameter(fillAmount);
  deviceActions->addAction(action);
}

bool HomeConnectDeviceCoffeMaker::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("homeconnect_coffee", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT
