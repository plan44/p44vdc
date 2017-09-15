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

#if ENABLE_HOMECONNECT

namespace p44 {

HomeConnectDeviceCoffeMaker::HomeConnectDeviceCoffeMaker(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
    standalone(false),
    inherited(aVdcP, aHomeApplicanceInfoRecord)
{
  HomeConnectDeviceSettingsPtr settings = new HomeConnectDeviceSettings(*this);
  settings->fireAction = "std.StandBy";
  settings->leaveHomeAction = "std.StandBy";
  settings->deepOffAction = "std.StandBy";
  settings->sleepAction = "std.StandBy";

  installSettings(settings);
}

HomeConnectDeviceCoffeMaker::~HomeConnectDeviceCoffeMaker()
{
  // TODO Auto-generated destructor stub
}

bool HomeConnectDeviceCoffeMaker::configureDevice()
{
  bool ret = inherited::configureDevice();

  addDefaultStandByAction();
  addDefaultPowerOnAction();

  // configure operation mode
  OperationModeConfiguration omConfig = { 0 };
  omConfig.hasInactive = true;
  omConfig.hasReady = true;
  omConfig.hasDelayedStart = false;
  omConfig.hasRun = true;
  omConfig.hasPause = false;
  omConfig.hasActionrequired = true;
  omConfig.hasFinished = true;
  omConfig.hasError = true;
  omConfig.hasAborting = true;
  configureOperationModeState(omConfig);

  // configure remote control
  RemoteControlConfiguration rcConfig = { 0 };
  rcConfig.hasControlInactive = true;
  rcConfig.hasControlActive = false;  // coffee machine do not have BSH.Common.Status.RemoteControlActive so it is either inactive or start Allowed
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

  // FIXME: ugly direct model match
//    standalone = (modelGuid=="TI909701HC/03") || (modelGuid=="TI909701HC/00");
  // FIXME: got even uglier:
  standalone = (modelGuid.substr(0,10)=="TI909701HC");

  addAction("std.Espresso",          "Espresso",            "Espresso",          35,  60,  5,  40);
  addAction("std.EspressoMacchiato", "Espresso Macchiato",  "EspressoMacchiato", 40,  60,  10, 50);
  addAction("std.Coffee",            "Coffee",              "Coffee",            60,  250, 10, 120);
  addAction("std.Cappuccino",        "Cappuccino",          "Cappuccino",        100, 250, 10, 180);
  addAction("std.LatteMacchiato",    "Latte Macchiato",     "LatteMacchiato",    200, 400, 20, 300);
  addAction("std.CaffeLatte",        "Caffe Latte",         "CaffeLatte",        100, 400, 20, 250);

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

  fillQuantityProp = ValueDescriptorPtr(new NumericValueDescriptor("FillQuantity", valueType_numeric, VALUE_UNIT(valueUnit_liter, unitScaling_milli), 0, 400, 1));

  deviceProperties->addProperty(beanAmountProp);
  deviceProperties->addProperty(fillQuantityProp);

  return ret;
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


void HomeConnectDeviceCoffeMaker::handleEventTypeStatus(const string& aKey, JsonObjectPtr aValue)
{
  if ((aKey == "BSH.Common.Status.RemoteControlStartAllowed") && (remoteControl != NULL)) {

    if (aValue) {
      string remoteStartValue;
      bool value = aValue->boolValue();

      if (value) {
        remoteStartValue = "RemoteStartActive";
      }
      else {
        remoteStartValue = "RemoteControlInactive";
      }

      if (remoteControlDescriptor->setStringValueCaseInsensitive(remoteStartValue)) {
        ALOG(LOG_NOTICE, "New Remote Start Allowed State: '%s'", remoteStartValue.c_str());
        remoteControl->push();
      }
    }
    return;
  }

  inherited::handleEventTypeStatus(aKey, aValue);
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

  if(!standalone) {
    builder.addOption("ConsumerProducts.CoffeeMaker.Option.CoffeeTemperature", "\"ConsumerProducts.CoffeeMaker.EnumType.CoffeeTemperature.@{TemperatureLevel}\"");
  }

  builder.addOption("ConsumerProducts.CoffeeMaker.Option.BeanAmount", "\"ConsumerProducts.CoffeeMaker.EnumType.BeanAmount.@{BeanAmount}\"");
  builder.addOption("ConsumerProducts.CoffeeMaker.Option.FillQuantity", "@{FillQuantity%%0}");


  builder.selectMode(HomeConnectProgramBuilder::Mode_Activate);
  string runProgramCommand = builder.build();

  builder.selectMode(HomeConnectProgramBuilder::Mode_Select);
  string selectProgramCommand = builder.build();

  EnumValueDescriptorPtr tempLevel = EnumValueDescriptorPtr(new EnumValueDescriptor("TemperatureLevel", true));
  int i = 0;
  tempLevel->addEnum("Normal", i++, true); // default
  tempLevel->addEnum("High", i++);
  tempLevel->addEnum("VeryHigh", i++);

  EnumValueDescriptorPtr beanAmount = EnumValueDescriptorPtr(new EnumValueDescriptor("BeanAmount", true));
  i = 0;
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
                                                        *powerState,
                                                        *operationMode));
  action->addParameter(tempLevel);
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
