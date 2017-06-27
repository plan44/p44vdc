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

namespace p44 {

HomeConnectDeviceCoffeMaker::HomeConnectDeviceCoffeMaker(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
    standalone(false), inherited(aVdcP, aHomeApplicanceInfoRecord)
{
  hcDevType = homeconnect_coffeemaker;
}

bool HomeConnectDeviceCoffeMaker::configureDevice()
{
  HomeConnectActionPtr a;
  // FIXME: ugly direct model match
//    standalone = (modelGuid=="TI909701HC/03") || (modelGuid=="TI909701HC/00");
  // FIXME: got even uglier:
  standalone = (modelGuid.substr(0,10)=="TI909701HC");
  // Create standard actions
  // - enums that can be shared between actions
  EnumValueDescriptorPtr tempLevel = EnumValueDescriptorPtr(new EnumValueDescriptor("temperatureLevel", true));
  tempLevel->addEnum("Normal", 0, true); // default
  tempLevel->addEnum("High", 1);
  tempLevel->addEnum("VeryHigh", 2);
  EnumValueDescriptorPtr beanAmount = EnumValueDescriptorPtr(new EnumValueDescriptor("beanAmount", true));
  beanAmount->addEnum("VeryMild", 0);
  beanAmount->addEnum("Mild", 1);
  beanAmount->addEnum("Normal", 2, true); // default
  beanAmount->addEnum("Strong", 3);
  beanAmount->addEnum("VeryStrong", 4);
  beanAmount->addEnum("DoubleShot", 5);
  beanAmount->addEnum("DoubleShotPlus", 6);
  beanAmount->addEnum("DoubleShotPlusPlus", 7);
  // - command template
  string cmdTemplate =
    "PUT:programs/active:{\"data\":{\"key\":\"ConsumerProducts.CoffeeMaker.Program.Beverage.%s\","
    "\"options\":[";
  if (!standalone) {
    cmdTemplate +=
      "{ \"key\":\"ConsumerProducts.CoffeeMaker.Option.CoffeeTemperature\",\"value\":\"ConsumerProducts.CoffeeMaker.EnumType.CoffeeTemperature.@{temperatureLevel}\"},";
  }
  cmdTemplate +=
      "{ \"key\":\"ConsumerProducts.CoffeeMaker.Option.BeanAmount\",\"value\":\"ConsumerProducts.CoffeeMaker.EnumType.BeanAmount.@{beanAmount}\"},"
      "{ \"key\":\"ConsumerProducts.CoffeeMaker.Option.FillQuantity\",\"value\":@{fillQuantity%%0}}"
    "]}}";
  // - espresso
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.espresso", "Espresso", string_format(cmdTemplate.c_str(),"Espresso")));
  a->addParameter(tempLevel);
  a->addParameter(beanAmount);
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("fillQuantity", valueType_numeric, VALUE_UNIT(valueUnit_liter, unitScaling_milli), 35, 60, 5, true, 40)));
  deviceActions->addAction(a);
  // - espresso macciato
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.espressoMacchiato", "Espresso Macchiato", string_format(cmdTemplate.c_str(),"EspressoMacchiato")));
  a->addParameter(tempLevel);
  a->addParameter(beanAmount);
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("fillQuantity", valueType_numeric, VALUE_UNIT(valueUnit_liter, unitScaling_milli), 40, 60, 10, true, 50)));
  deviceActions->addAction(a);
  // - (plain) coffee
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.coffee", "Coffee", string_format(cmdTemplate.c_str(),"Coffee")));
  a->addParameter(tempLevel);
  a->addParameter(beanAmount);
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("fillQuantity", valueType_numeric, VALUE_UNIT(valueUnit_liter, unitScaling_milli), 60, 250, 10, true, 120)));
  deviceActions->addAction(a);
  // - Cappuccino
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.cappuccino", "Cappuccino", string_format(cmdTemplate.c_str(),"Cappuccino")));
  a->addParameter(tempLevel);
  a->addParameter(beanAmount);
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("fillQuantity", valueType_numeric, VALUE_UNIT(valueUnit_liter, unitScaling_milli), 100, 250, 10, true, 180)));
  deviceActions->addAction(a);
  // - latte macchiato
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.latteMacchiato", "Latte Macchiato", string_format(cmdTemplate.c_str(),"LatteMacchiato")));
  a->addParameter(tempLevel);
  a->addParameter(beanAmount);
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("fillQuantity", valueType_numeric, VALUE_UNIT(valueUnit_liter, unitScaling_milli), 200, 400, 20, true, 300)));
  deviceActions->addAction(a);
  // - latte macchiato
  a = HomeConnectActionPtr(new HomeConnectAction(*this, "std.caffeLatte", "Caffe Latte", string_format(cmdTemplate.c_str(),"CaffeLatte")));
  a->addParameter(tempLevel);
  a->addParameter(beanAmount);
  a->addParameter(ValueDescriptorPtr(new NumericValueDescriptor("fillQuantity", valueType_numeric, VALUE_UNIT(valueUnit_liter, unitScaling_milli), 100, 400, 20, true, 250)));
  deviceActions->addAction(a);

  return inherited::configureDevice();
}

HomeConnectDeviceCoffeMaker::~HomeConnectDeviceCoffeMaker()
{
  // TODO Auto-generated destructor stub
}



} /* namespace p44 */
