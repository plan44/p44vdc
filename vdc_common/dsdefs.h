//
//  Copyright (c) 1-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef p44vdc_dsdefs_h
#define p44vdc_dsdefs_h

// MARK: - generic dS constants in dS global scope

/// Constants which are used in the entire dS system
/// @{

/// scene numbers
typedef enum {
  START_ZONE_SCENES = 0,  ///< first zone scene
  ROOM_OFF = 0,           ///< preset 0 - room off
  CLIMATE_HEAT_TEMP_OFF = 0,        ///< climate control: temperature off, heating mode
  AREA_1_OFF = 1,                   ///< area 1 off / audio: reserved
  CLIMATE_HEAT_TEMP_COMFORT = 1,    ///< climate control: temperature comfort, heating mode
  AREA_2_OFF = 2,                   ///< area 2 off / audio: reserved
  CLIMATE_HEAT_TEMP_ECO = 2,        ///< climate control: temperature eco, heating mode
  AREA_3_OFF = 3,                   ///< area 3 off / audio: reserved
  CLIMATE_HEAT_TEMP_NOTUSED = 3,    ///< climate control: temperature notused/cool/setback, heating mode
  AREA_4_OFF = 4,                   ///< area 4 off / audio: reserved
  CLIMATE_HEAT_TEMP_NIGHT = 4,      ///< climate control: temperature night, heating mode
  ROOM_ON = 5,                      ///< preset 1 - main room on
  CLIMATE_HEAT_TEMP_HOLIDAY = 5,    ///< climate control: temperature holiday/vacation, heating mode
  AREA_1_ON = 6,                    ///< area 1 on / audio: reserved
  CLIMATE_COOL_PASSIVE_ON = 6,      ///< climate control: passive cooling mode, on
  VENTILATION_BOOST = 6,            ///< ventilation: boost (full power for limited time)
  AREA_2_ON = 7,                    ///< area 2 on
  VENTILATION_CALM = 7,             ///< ventilation: calm (noise reduction)
  AUDIO_REPEAT_OFF = 7,             ///< audio: Repeat off
  CLIMATE_COOL_PASSIVE_OFF = 7,     ///< climate control: passive cooling mode, off
  AREA_3_ON = 8,                    ///< area 3 on
  VENTILATION_AUTO_FLOW = 8,        ///< ventilation: automatic air flow intensity
  AUDIO_REPEAT_1 = 8,               ///< audio: Repeat 1
  CLIMATE_RESERVED = 8,             ///< climate control: reserved (was once used as MANUAL mode)
  AREA_4_ON = 9,                    ///< area 4 on
  VENTILATION_AUTO_LOUVER = 9,      ///< ventilation: automatic louver position / swing mode
  AUDIO_REPEAT_ALL = 9,             ///< audio: Repeat all
  CLIMATE_COOL_TEMP_OFF = 9,        ///< climate control: temperature off, cooling mode
  T1234_CONT = 10,                  ///< area 1-4 increment/decrement continue
  CLIMATE_COOL_TEMP_COMFORT = 10,   ///< climate control: temperature comfort, cooling mode
  DEC_S = 11,                       ///< decrement value
  CLIMATE_COOL_TEMP_ECO = 11,       ///< climate control: temperature eco, cooling mode
  INC_S = 12,                       ///< increment value
  CLIMATE_COOL_TEMP_NOTUSED = 12,   ///< climate control: temperature notused/cool/setback, cooling mode
  MIN_S = 13,                       ///< minimum value
  CLIMATE_COOL_TEMP_NIGHT = 13,     ///< climate control: temperature night, cooling mode
  MAX_S = 14,                       ///< maximum value
  CLIMATE_COOL_TEMP_HOLIDAY = 14,   ///< climate control: temperature holiday/vacation, cooling mode
  STOP_S = 15,                      ///< stop
  ///< 16 reserved
  PRESET_2 = 17,          ///< preset 2
  PRESET_3 = 18,          ///< preset 3
  PRESET_4 = 19,          ///< preset 4
  PRESET_12 = 20,         ///< preset 12
  PRESET_13 = 21,         ///< preset 13
  PRESET_14 = 22,         ///< preset 14
  PRESET_22 = 23,         ///< preset 22
  PRESET_23 = 24,         ///< preset 23
  PRESET_24 = 25,         ///< preset 24
  PRESET_32 = 26,         ///< preset 32
  PRESET_33 = 27,         ///< preset 33
  PRESET_34 = 28,         ///< preset 34
  PRESET_42 = 29,         ///< preset 42
  CLIMATE_ENABLE = 29,    ///< climate control: enable
  PRESET_43 = 30,         ///< preset 43
  CLIMATE_DISABLE = 30,   ///< climate control: disable
  PRESET_44 = 31,         ///< preset 44
  CLIMATE_VALVE_PROPHYLAXIS = 31, ///< climate control: valve prophylaxis
  PRESET_OFF_10 = 32,     ///< preset 10
  CLIMATE_VALVE_OPEN = 32, ///< climate control: fully open valve (service, prophylaxis)
  PRESET_11 = 33,         ///< preset 11
  CLIMATE_VALVE_CLOSE = 33, ///< climate control: fully close valve (service, prophylaxis)
  PRESET_OFF_20 = 34,     ///< preset 20
  PRESET_21 = 35,         ///< preset 21
  PRESET_OFF_30 = 36,     ///< preset 30
  PRESET_31 = 37,         ///< preset 31
  PRESET_OFF_40 = 38,     ///< preset 40
  PRESET_41 = 39,         ///< preset 41
  AUTO_OFF = 40,          ///< slow motion off (1 minute down to 0)
  CLIMATE_FAN_ONLY = 40,  ///< climate control: Fan only mode
  ALERT_S = 41,           ///< Alert scene (blinking on, but don't care new from dSS 1.11.0 onwards)
  CLIMATE_DRY = 41,       ///< climate control: dry only mode (reduce humidity, to temperature change)
  AREA_1_DEC = 42,        ///< area 1 decrement value
  CLIMATE_AUTOMATIC = 42, ///< climate control: automatic mode (FCU device decides about operating mode)
  AUDIO_PREV_TITLE = 42,  ///< audio: Previous Title
  AREA_1_INC = 43,        ///< area 1 increment value
  AUDIO_NEXT_TITLE = 43,  ///< audio: Next Title
  AREA_2_DEC = 44,        ///< area 2 decrement value
  AUDIO_PREV_CHANNEL = 44,///< audio: Previous Channel
  AREA_2_INC = 45,        ///< area 2 increment value
  AUDIO_NEXT_CHANNEL = 45,///< audio: Next Channel
  AREA_3_DEC = 46,        ///< area 3 decrement value
  AUDIO_MUTE = 46,        ///< audio: Mute
  AREA_3_INC = 47,        ///< area 3 increment value
  AUDIO_UNMUTE = 47,      ///< audio: Unmute
  AREA_4_DEC = 48,        ///< area 4 decrement value
  AUDIO_PLAY = 48,        ///< audio: Play
  AREA_4_INC = 49,        ///< area 4 increment value
  AUDIO_PAUSE = 49,       ///< audio: Pause
  LOCAL_OFF = 50,         ///< local button off scene
  LOCAL_ON = 51,          ///< local button on scene
  AREA_1_STOP_S = 52,     ///< area 1 stop
  AUDIO_SHUFFLE_OFF = 52, ///< audio: Shuffle Off
  AREA_2_STOP_S = 53,     ///< area 2 stop
  AUDIO_SHUFFLE_ON = 53,  ///< audio: Shuffle On
  AREA_3_STOP_S = 54,     ///< area 3 stop
  AUDIO_RESUME_OFF = 54,  ///< audio: Resume Off
  AREA_4_STOP_S = 55,     ///< area 4 stop
  AUDIO_RESUME_ON = 55,   ///< audio: Resume On
  ///< 56..63 - reserved
  START_APARTMENT_SCENES = 64,                    ///< 64 - first apartment scene
  AUTO_STANDBY = (START_APARTMENT_SCENES + 0),    ///< 64 - auto-standby scene
  PANIC = (START_APARTMENT_SCENES + 1),           ///< 65 - panic
  ENERGY_OL = (START_APARTMENT_SCENES + 2),       ///< 66 - overload energy consumption dSM
  STANDBY = (START_APARTMENT_SCENES + 3),         ///< 67 - standby scene
  DEEP_OFF = (START_APARTMENT_SCENES + 4),        ///< 68 - deep off scene
  SLEEPING = (START_APARTMENT_SCENES + 5),        ///< 69 - sleeping
  WAKE_UP = (START_APARTMENT_SCENES + 6),         ///< 70 - wakeup
  PRESENT = (START_APARTMENT_SCENES + 7),         ///< 71 - at home
  ABSENT = (START_APARTMENT_SCENES + 8),          ///< 72 - not at home
  BELL1 = (START_APARTMENT_SCENES + 9),           ///< 73 - Bell1
  ALARM1 = (START_APARTMENT_SCENES + 10),         ///< 74 - Alarm1
  ZONE_ACTIVE = (START_APARTMENT_SCENES + 11),    ///< 75 - Zone active
  FIRE = (START_APARTMENT_SCENES + 12),           ///< 76 - Fire
  SMOKE = (START_APARTMENT_SCENES + 13),          ///< 77 - Smoke
  WATER = (START_APARTMENT_SCENES + 14),          ///< 78 - Water
  GAS = (START_APARTMENT_SCENES + 15),            ///< 79 - Gas
  BELL2 = (START_APARTMENT_SCENES + 16),          ///< 80 - Bell2
  BELL3 = (START_APARTMENT_SCENES + 17),          ///< 81 - Bell3
  BELL4 = (START_APARTMENT_SCENES + 18),          ///< 82 - Bell4
  ALARM2 = (START_APARTMENT_SCENES + 19),         ///< 83 - Alarm2
  ALARM3 = (START_APARTMENT_SCENES + 20),         ///< 84 - Alarm3
  ALARM4 = (START_APARTMENT_SCENES + 21),         ///< 85 - Alarm4
  WIND = (START_APARTMENT_SCENES + 22),           ///< 86 - Wind
  NO_WIND = (START_APARTMENT_SCENES + 23),        ///< 87 - No Wind
  RAIN = (START_APARTMENT_SCENES + 24),           ///< 88 - Rain
  NO_RAIN = (START_APARTMENT_SCENES + 25),        ///< 89 - No Rain
  HAIL = (START_APARTMENT_SCENES + 26),           ///< 90 - Hail
  NO_HAIL = (START_APARTMENT_SCENES + 27),        ///< 91 - No Hail
  POLLUTION = (START_APARTMENT_SCENES + 28),      ///< 92 - Pollution
  MAX_SCENE_NO,                                   ///< currently known number of scenes
  INVALID_SCENE_NO = MAX_SCENE_NO                 ///< marker for invalid scene
} DsSceneNumber;

typedef uint8_t SceneNo; ///< scene number

typedef uint8_t SceneArea; ///< area number, 0=no area
typedef enum {
  no_area = 0,
  area_1 = 1,
  area_2 = 2,
  area_3 = 3,
  area_4 = 4,
  num_areas = 4 ///< number of areas (excluding global)
} DsArea;

typedef uint16_t DsZoneID; ///< digitalSTROM Zone ID (= room ID)
typedef enum {
  zoneId_global = 0 ///< global (appartment, all rooms) zone
} DsZones;

/// color/class
typedef enum {
  class_undefined = 0,
  class_yellow_light = 1,
  class_grey_shadow = 2,
  class_blue_climate = 3,
  class_cyan_audio = 4,
  class_magenta_video = 5,
  class_red_security = 6,
  class_green_access = 7,
  class_black_joker = 8,
  class_white_singledevices = 9,
  numColorClasses = 10,
} DsClass;

/// color/group
typedef enum {
  group_undefined = 0, ///< formerly "variable", but now 8 is called "variable"
  group_yellow_light = 1,
  group_grey_shadow = 2,
  group_blue_heating = 3, ///< heating - formerly "climate"
  group_cyan_audio = 4,
  group_magenta_video = 5,
  group_red_security = 6,  ///< no group!
  group_green_access = 7,  ///< no group!
  group_black_variable = 8,
  group_blue_cooling = 9, ///< cooling - formerly just "white" (is it still white? snow?)
  group_blue_ventilation = 10, ///< ventilation - formerly "display"?!
  group_blue_windows = 11, ///< windows (not the OS, holes in the wall..)
  group_blue_air_recirculation = 12, ///< air recirculation for fan coil units
  group_roomtemperature_control = 48, ///< room temperature control
  group_ventilation_control = 49, ///< room ventilation control
} DsGroup;

typedef uint64_t DsGroupMask; ///< 64 bit mask, Bit0 = group 0, Bit63 = group 63


/// button click types
typedef enum {
  ct_tip_1x = 0, ///< first tip
  ct_tip_2x = 1, ///< second tip
  ct_tip_3x = 2, ///< third tip
  ct_tip_4x = 3, ///< fourth tip
  ct_hold_start = 4, ///< hold start
  ct_hold_repeat = 5, ///< hold repeat
  ct_hold_end = 6, ///< hold end
  ct_click_1x = 7, ///< short click
  ct_click_2x = 8, ///< double click
  ct_click_3x = 9, ///< triple click
  ct_short_long = 10, ///< short/long = programming mode
  ct_local_off = 11, ///< local button has turned device off
  ct_local_on = 12, ///< local button has turned device on
  ct_short_short_long = 13, ///< short/short/long = local programming mode
  ct_local_stop = 14, ///< local stop
  ct_none = 255 ///< no click (for state)
} DsClickType;


/// button mode aka "LTMODE"
typedef enum {
  buttonMode_standard = 0, ///< standard pushbutton with full state machine
  buttonMode_turbo = 1, ///< only tips, no hold (dimming) or double/triple clicks
  buttonMode_presence = 2,
  buttonMode_switch = 3,
  buttonMode_reserved1 = 4,
  buttonMode_rockerDown_pairWith0 = 5, ///< down-Button, paired with buttonInput[0] (aka "Eingang 1" in dS 1.0)
  buttonMode_rockerDown_pairWith1 = 6, ///< down-Button, paired with buttonInput[1] (aka "Eingang 2" in dS 1.0)
  buttonMode_rockerDown_pairWith2 = 7, ///< down-Button, paired with buttonInput[2] (aka "Eingang 3" in dS 1.0)
  buttonMode_rockerDown_pairWith3 = 8, ///< down-Button, paired with buttonInput[3] (aka "Eingang 4" in dS 1.0)
  buttonMode_rockerUp_pairWith0 = 9, ///< up-Button, paired with buttonInput[0] (aka "Eingang 1" in dS 1.0)
  buttonMode_rockerUp_pairWith1 = 10, ///< up-Button, paired with buttonInput[1] (aka "Eingang 2" in dS 1.0)
  buttonMode_rockerUp_pairWith2 = 11, ///< up-Button, paired with buttonInput[2] (aka "Eingang 3" in dS 1.0)
  buttonMode_rockerUp_pairWith3 = 12, ///< up-Button, paired with buttonInput[3] (aka "Eingang 4" in dS 1.0)
  buttonMode_rockerUpDown = 13, ///< up/down Button, without separately identified inputs (Note: only exists in dS 1.0 adpation. vdSDs always identify all inputs)
  buttonMode_standard_multi = 14,
  buttonMode_reserved2 = 15,
  buttonMode_akm_rising1_falling0 = 16,
  buttonMode_akm_rising0_falling1 = 17,
  buttonMode_akm_rising1 = 18,
  buttonMode_akm_falling1 = 19,
  buttonMode_akm_rising0 = 20,
  buttonMode_akm_falling0 = 21,
  buttonMode_akm_risingToggle = 22,
  buttonMode_akm_fallingToggle = 23,
  buttonMode_inactive = 255
} DsButtonMode;


/// button function aka "LTNUM" (lower 4 bits in LTNUMGRP0)
typedef enum {
  // all colored buttons
  buttonFunc_device = 0, ///< device button (and preset 2-4)
  buttonFunc_area1_preset0x = 1, ///< area1 button (and preset 2-4)
  buttonFunc_area2_preset0x = 2, ///< area2 button (and preset 2-4)
  buttonFunc_area3_preset0x = 3, ///< area3 button (and preset 2-4)
  buttonFunc_area4_preset0x = 4, ///< area4 button (and preset 2-4)
  buttonFunc_room_preset0x = 5, ///< room button (and preset 1-4)
  buttonFunc_room_preset1x = 6, ///< room button (and preset 10-14)
  buttonFunc_room_preset2x = 7, ///< room button (and preset 20-24)
  buttonFunc_room_preset3x = 8, ///< room button (and preset 30-34)
  buttonFunc_room_preset4x = 9, ///< room button (and preset 40-44)
  buttonFunc_area1_preset1x = 10, ///< area1 button (and preset 12-14)
  buttonFunc_area2_preset2x = 11, ///< area2 button (and preset 22-24)
  buttonFunc_area3_preset3x = 12, ///< area3 button (and preset 32-34)
  buttonFunc_area4_preset4x = 13, ///< area4 button (and preset 42-44)
  // black buttons
  buttonFunc_alarm = 1, ///< alarm
  buttonFunc_panic = 2, ///< panic
  buttonFunc_leave = 3, ///< leaving home
  buttonFunc_doorbell = 5, ///< door bell
  buttonFunc_apartment = 14, ///< appartment button
  buttonFunc_app = 15, ///< application specific button
} DsButtonFunc;


/// output channel types
typedef enum {
  channeltype_default = 0, ///< default channel (main output value, e.g. brightness for lights)
  channeltype_brightness = 1, ///< brightness for lights
  channeltype_hue = 2, ///< hue for color lights
  channeltype_saturation = 3, ///< saturation for color lights
  channeltype_colortemp = 4, ///< color temperature for lights with variable white point
  channeltype_cie_x = 5, ///< X in CIE Color Model for color lights
  channeltype_cie_y = 6, ///< Y in CIE Color Model for color lights
  channeltype_shade_position_outside = 7, ///< shade position outside (blinds)
  channeltype_shade_position_inside = 8, ///< shade position inside (curtains)
  channeltype_shade_angle_outside = 9, ///< shade opening angle outside (blinds)
  channeltype_shade_angle_inside = 10, ///< shade opening angle inside (curtains)
  channeltype_permeability = 11, ///< permeability (smart glass)
  channeltype_airflow_intensity = 12, ///< airflow intensity channel
  channeltype_airflow_direction = 13, ///< airflow direction (DsVentilationDirectionState)
  channeltype_airflow_flap_position = 14, ///< airflow flap position (angle), 0..100 of device's available range
  channeltype_airflow_louver_position = 15, ///< louver position (angle), 0..100 of device's available range
  channeltype_heating_power = 16, ///< power level for simple heating or cooling device (such as valve)
  channeltype_cooling_capacity = 17, ///< cooling capacity
  channeltype_audio_volume = 18, /// audio volume
  channeltype_power_state = 19, ///< FCU custom channel: power state
  channeltype_airflow_louver_auto = 20, ///< louver automatic mode (0=off, >0=on)
  channeltype_airflow_intensity_auto = 21, ///< airflow intensity automatic mode (0=off, >0=on)
  channeltype_water_temperature = 22, ///< water temperature
  channeltype_water_flow = 23, ///< water flow rate
  channeltype_video_station = 24, ///< video tv station (channel number)
  channeltype_video_input_source = 25, ///< video input source (TV, HDMI etc.)

  channeltype_custom_first = 192, ///< first device-specific channel
  channeltype_custom_last = 239, ///< last device-specific channel

  channeltype_fcu_operation_mode = channeltype_custom_first+0, ///< FCU custom channel: operating mode

  channeltype_p44_position_v = channeltype_custom_first+0, ///< vertical position (e.g for moving lights)
  channeltype_p44_position_h = channeltype_custom_first+1, ///< horizontal position (e.g for moving lights)
  channeltype_p44_zoom_v = channeltype_custom_first+2, ///< vertical zoom (for extended functionality moving lights)
  channeltype_p44_zoom_h = channeltype_custom_first+3, ///< horizontal zoom (for extended functionality moving lights)
  channeltype_p44_rotation = channeltype_custom_first+4, ///< rotation (for extended functionality moving lights)
  channeltype_p44_brightness_gradient = channeltype_custom_first+5, ///< gradient for brightness
  channeltype_p44_hue_gradient = channeltype_custom_first+6, ///< gradient for hue
  channeltype_p44_saturation_gradient = channeltype_custom_first+7, ///< gradient for saturation
  channeltype_p44_feature_mode = channeltype_custom_first+8, ///< feature mode

  channeltype_p44_audio_content_source = channeltype_custom_first+22, ///< audio content source // FIXME: p44-specific channel type for audio content source until dS specifies one

  numChannelTypes = 240 // 0..239 are channel types
} DsChannelTypeEnum;
typedef uint8_t DsChannelType;


/// Power state channel values (audio, FCU...)
typedef enum {
  powerState_off = 0, ///< "normal" off (no standby, just off)
  powerState_on = 1, ///< "normal" on
  powerState_forcedOff = 2, ///< also "local off" (climate: turned off locally/by user e.g. to silence a device, but will turn on when global building protection requires it)
  powerState_standby = 3, ///< also "power save" (audio: off, but ready for quick start)
  numDsPowerStates
} DsPowerState;


/// ventilation airflow direction channel states
typedef enum {
  dsVentilationDirection_undefined = 0,
  dsVentilationDirection_supply_or_down = 1,
  dsVentilationDirection_exhaust_or_up = 2,
  numDsVentilationDirectionStates
} DsVentilationAirflowDirection;




/// binary input types (sensor functions)
typedef enum {
  binInpType_none = 0, ///< no system function
  binInpType_presence = 1, ///< Presence
  binInpType_light = 2, ///< Light
  binInpType_presenceInDarkness = 3, ///< Presence in darkness
  binInpType_twilight = 4, ///< twilight
  binInpType_motion = 5, ///< motion
  binInpType_motionInDarkness = 6, ///< motion in darkness
  binInpType_smoke = 7, ///< smoke
  binInpType_wind = 8, ///< wind
  binInpType_rain = 9, ///< rain
  binInpType_sun = 10, ///< solar radiation (sun light above threshold)
  binInpType_thermostat = 11, ///< thermostat (temperature below user-adjusted threshold)
  binInpType_lowBattery = 12, ///< device has low battery
  binInpType_windowOpen = 13, ///< window is open
  binInpType_doorOpen = 14, ///< door is open
  binInpType_windowHandle = 15, ///< TRI-STATE! Window handle, has extendedValue showing closed/open/tilted, bool value is just closed/open
  binInpType_garageDoorOpen = 16, ///< garage door is open
  binInpType_sunProtection = 17, ///< protect against too much sunlight
  binInpType_frost = 18, ///< frost detector
  binInpType_heatingActivated = 19, ///< heating system activated
  binInpType_heatingChangeOver = 20, ///< heating system change over (active=warm water, non active=cold water)
  binInpType_initStatus = 21, ///< can indicate when not all functions are ready yet
  binInpType_malfunction = 22, ///< malfunction, device needs maintainance, cannot operate
  binInpType_service = 23, ///< device needs service, but can still operate normally at the moment
} DsBinaryInputType;


/// model features (column from dSS visibility Matrix)
/// Full documentation see: http://redmine.digitalstrom.org/projects/dss/wiki/Model_Features
/// @note while modelfeatures have dS system scope, this enum as such has not, because model features only appear by name in the API. The enum is vDC implementation specific
typedef enum {
  modelFeature_dontcare, ///< Show "Retain output when calling scene X" check box in scene properties device configuration.
  modelFeature_blink, ///< Show "Blink when calling scene X" check box in scene properties device configuration.
  modelFeature_ledauto, ///< Radiogroup "LED mode" in advanced scene properties device configuration supports "auto" mode
  modelFeature_leddark, ///< Radiogroup "LED mode" in advanced scene properties device configuration supports "dark" mode.
  modelFeature_transt, ///< Show "Transition time" radio group in advanced scene properties device configuration dialog.
  modelFeature_outmode, ///< Show "Output mode" radio group in device properties dialog with "switched", "dimmed" and "disabled" selections. The "switched" parameter for this configuration has a value of 16.
  modelFeature_outmodeswitch, ///< Show "Output mode" radio group in device properties dialog with only "switched" and "disabled" selections. The "switched" parameter for this configuration has a value of 35.
  modelFeature_outmodegeneric, ///< Show "Output mode" radio group in device properties dialog with only "enabled" and "disabled" selections.
  modelFeature_outvalue8, ///< Enables UI slider for 8-bit output value (basically, see details in Wiki)
  modelFeature_pushbutton, ///< Show push button settings in "Device properties" dialog. Also check if multi-button settings for device pairing must be shown (depends on various other parameters).
  modelFeature_pushbdevice, ///< This flag influences the contents of the "Push-button" drop down list, it makes sure that a "device pushbutton" entry is present.
  modelFeature_pushbsensor, ///< This flag influences the contents of the "Push-button" drop down list, it makes sure that a "sensor" entry is present.
  modelFeature_pushbarea, ///< This flag influences the contents of the "Push-button" drop down list, it makes sure that a "Area-pushbutton" entry is present. It also enables the area selection drop down.
  modelFeature_pushbadvanced, ///< Enables the advanced push button configuration in the "Device Properties" dialog.
  modelFeature_pushbcombined, ///< Enabled is for combined up/down buttons (basically, see details in Wiki)
  modelFeature_shadeprops, ///< Enables the "Shade Device Properties" dialog for the given device in the "Hardware" tab.
  modelFeature_shadeposition, ///< When set, the device values are assumed to have a 16bit resolution, also some labels will show "Position" instead of "Value" (basically, see details in Wiki)
  modelFeature_motiontimefins, ///< Shows "Turn time blades" and "Calibrate turn times" options in the "Device Properties shade" dialog.
  modelFeature_optypeconfig, ///< Show "Operation Type" settings in the "Device Properties" dialog.
  modelFeature_shadebladeang, ///< Show "Set Blade Angle" option in the "Device Settings" dialog.
  modelFeature_highlevel, ///< This flag influences the contents of the "Push-button" dropdown list, it makes sure that a "App button" entry is present.
  modelFeature_consumption, ///< Enables the "Configure Consumption Event" dialog for the given device in the "Hardware" tab.
  modelFeature_jokerconfig, ///< Show "Joker" configuration settings in "Device Properties" dialog.
  modelFeature_akmsensor, ///< Show "Sensor function" settings in "Device Properties" dialog.
  modelFeature_akminput, ///< Show AKM "Input" settings in "Device Properties" dialog.
  modelFeature_akmdelay, ///< Show AKM "Delay" settings in "Device Properties" dialog.
  modelFeature_twowayconfig, ///< Shows the "Button function" settings in "Device Properties" dialog, depends on "pushbutton" parameter.
  modelFeature_outputchannels, ///< Display "Hue" and "Saturation" setting in the "Device Settings" dialog.
  modelFeature_heatinggroup, ///< Shows "Heating group" settings in "Device Properties" dialog.
  modelFeature_heatingoutmode, ///< Enables the "Output mode" radio group in "Device Properties" dialog and influences its contents. The presented options will be: "switched" (65), "pulse width modulation (PWM)" (64) and "disabled" (0).
  modelFeature_heatingprops, ///< Enables the "Device Properties climate" dialog for the given device in the "Hardware" tab.
  modelFeature_pwmvalue, ///< Read out and display "Operation mode" in the "Device Settings" dialog.
  modelFeature_valvetype, ///< Shows "Attached terminal device" settings in "Device Properties" dialog.
  modelFeature_extradimmer, ///< Enables the "Output mode" radio group in "Device Properties" dialog and influences its contents. The presented options will be: "switched" (16), "dimmed 0-10V", "dimmed 1-10V" and "disabled".
  modelFeature_umvrelay, ///< Shows "Relay Function" settings in "Device Properties" dialog.
  modelFeature_blinkconfig, ///< Shows "Blink behavior for output on scene calls" settings in the advanced "Device Properties" dialog.
  modelFeature_umroutmode, ///< Enables the "Output mode" radio group in "Device Properties" dialog and influences its contents. The presented options will be: "single switched" (35) for all dSUIDs and "combined switched" (43), "combined two stage switched" (34), "combined three stage switched" (38) and "disabled" (0).
  modelFeature_fcu, ///< enables FCU specific UI bits such as "automatic" flags
  modelFeature_extendedvalvetypes, ///< Rehau specific modelFeature, extends list of available valve types in "Device Properties" dialog.
  modelFeature_identification, ///< device has a non-dummy identifyToUser() implementation (so light bulb button will be active)
  numModelFeatures
} DsModelFeatures;

/// @}



// MARK: - vDC API constants - scope is vDC API only

/// Constants which are used in the vDC API and have a direct meaning only for vDC API clients (such as vdSM)
/// @{


/// sensor types
/// @note these are used in numeric enum form in sensorDescriptions[].sensorType since vDC API 1.0
///   but are not 1:1 mapped to dS sensor types (dS sensor types are constructed from these + VdcUsageHint)
typedef enum {
  sensorType_none = 0,
  // physical double values
  sensorType_temperature = 1, ///< temperature in degrees celsius
  sensorType_humidity = 2, ///< relative humidity in %
  sensorType_illumination = 3, ///< illumination in lux
  sensorType_supplyVoltage = 4, ///< supply voltage level in Volts
  sensorType_gas_CO = 5, ///< CO (carbon monoxide) concentration in ppm
  sensorType_gas_radon = 6, ///< Radon activity in Bq/m3
  sensorType_gas_type = 7, ///< gas type sensor
  sensorType_dust_PM10 = 8, ///< particles <10µm in μg/m3
  sensorType_dust_PM2_5 = 9, ///< particles <2.5µm in μg/m3
  sensorType_dust_PM1 = 10, ///< particles <1µm in μg/m3
  sensorType_set_point = 11, ///< room operating panel set point, 0..1
  sensorType_fan_speed = 12, ///< fan speed, 0..1 (0=off, <0=auto)
  sensorType_wind_speed = 13, ///< wind speed in m/s
  sensorType_power = 14, ///< Power in W
  sensorType_current = 15, ///< Electric current in A
  sensorType_energy = 16, ///< Energy in kWh
  sensorType_apparent_power = 17, ///< Apparent electric power in VA
  sensorType_air_pressure = 18, ///< Air pressure in hPa
  sensorType_wind_direction = 19, ///< Wind direction in degrees
  sensorType_sound_volume = 20, ///< Sound pressure level in dB
  sensorType_precipitation = 21, ///< Precipitation in mm/m2
  sensorType_gas_CO2 = 22, ///< CO2 (carbon dioxide) concentration in ppm
  sensorType_gust_speed = 23, ///< gust speed in m/S
  sensorType_gust_direction = 24, ///< gust direction in degrees
  sensorType_generated_power = 25, ///< Generated power in W
  sensorType_generated_energy = 26, ///< Generated energy in kWh
  sensorType_water_quantity = 27, ///< Water quantity in liters
  sensorType_water_flowrate = 28, ///< Water flow rate in liters/minute
  sensorType_length = 29, ///< Length in meters
  sensorType_mass = 30, ///< mass in grams
  sensorType_duration = 31, ///< time in seconds
  numVdcSensorTypes
} VdcSensorType;


/// technical value types
/// @note these are used to describe single device properties and parameter values, along with VdcSiUnit
typedef enum {
  valueType_unknown,
  valueType_numeric,
  valueType_integer,
  valueType_boolean,
  valueType_enumeration,
  valueType_string,
  numValueTypes
} VdcValueType;


/// Scene Effects (transition and alerting)
typedef enum {
  scene_effect_none = 0, ///< no effect, immediate transition
  scene_effect_smooth = 1, ///< smooth (default: 100mS) normal transition
  scene_effect_slow = 2, ///< slow (default: 1min) transition
  scene_effect_custom = 3, ///< custom (default: 5sec) transition
  scene_effect_alert = 4, ///< blink (for light devices, effectParam!=0 allows detail control) / alerting (in general: an effect that draws the user’s attention)
  scene_effect_transition = 5, ///< transition time according to scene-level effectParam (milliseconds)
  scene_effect_script = 6, ///< run scene script
} VdcSceneEffect;


/// Dim mode
typedef enum {
  dimmode_down = -1,
  dimmode_stop = 0,
  dimmode_up = 1
} VdcDimMode;


/// button types (for buttonDescriptions[].buttonType)
typedef enum {
  buttonType_undefined = 0, ///< kind of button not defined by device hardware
  buttonType_single = 1, ///< single pushbutton
  buttonType_2way = 2, ///< two-way pushbutton or rocker
  buttonType_4way = 3, ///< 4-way navigation button
  buttonType_4wayWithCenter = 4, ///< 4-way navigation with center button
  buttonType_8wayWithCenter = 5, ///< 8-way navigation with center button
  buttonType_onOffSwitch = 6, ///< On-Off switch
} VdcButtonType;


/// button element IDs (for buttonDescriptions[].buttonElementID)
typedef enum {
  buttonElement_center = 0, ///< center element / single button
  buttonElement_down = 1, ///< down, for 2,4,8-way
  buttonElement_up = 2, ///< up, for 2,4,8-way
  buttonElement_left = 3, ///< left, for 2,4,8-way
  buttonElement_right = 4, ///< right, for 2,4,8-way
  buttonElement_upperLeft = 5, ///< upper left, for 8-way
  buttonElement_lowerLeft = 6, ///< lower left, for 8-way
  buttonElement_upperRight = 7, ///< upper right, for 8-way
  buttonElement_lowerRight = 8, ///< lower right, for 8-way
} VdcButtonElement;



/// direct scene call action mode for buttons
typedef enum {
  buttonActionMode_normal = 0, ///< normal scene call
  buttonActionMode_force = 1, ///< forced scene call
  buttonActionMode_undo = 2, ///< undo scene
  buttonActionMode_none = 255, ///< no action
} VdcButtonActionMode;


/// output functions (describes capability of output)
typedef enum {
  outputFunction_switch = 0, ///< switch output - single channel 0..100
  outputFunction_dimmer = 1, ///< effective value dimmer - single channel 0..100
  outputFunction_positional = 2, ///< positional (servo, unipolar valves, blinds - single channel 0..n, usually n=100)
  outputFunction_ctdimmer = 3, ///< dimmer with color temperature - channels 1 and 4
  outputFunction_colordimmer = 4, ///< full color dimmer - channels 1..6
  outputFunction_bipolar_positional = 5, ///< bipolar valves, dual direction fan control etc. - single channel -n...0...n, usually n=100
  outputFunction_internallyControlled = 6, ///< output values(s) mostly internally controlled, e.g. FCU
  outputFunction_custom = 0x7F ///< custom output/channel configuration, none of the well-known functions above
} VdcOutputFunction;

/// output modes
typedef enum {
  outputmode_disabled = 0, ///< disabled
  outputmode_binary = 1, ///< binary ON/OFF mode
  outputmode_gradual = 2, ///< gradual output value (dimmer, valve, positional etc.)
  outputmode_default = 0x7F ///< use device in its default (or only) mode, without further specification
} VdcOutputMode;


/// heatingSystemCapability modes
typedef enum {
  hscapability_heatingOnly = 1, ///< only positive "heatingLevel" will be applied to the output
  hscapability_coolingOnly = 2, ///< only negative "heatingLevel" will be applied as positive values to the output
  hscapability_heatingAndCooling = 3 ///< absolute value of "heatingLevel" will be applied to the output
} VdcHeatingSystemCapability;


typedef enum {
  hstype_unknown = 0,
  hstype_floor = 1,
  hstype_radiator = 2,
  hstype_wall = 3,
  hstype_convectorPassive = 4,
  hstype_convectorActive = 5,
  hstype_floorLowEnergy = 6
} VdcHeatingSystemType;


/// hardware error status
typedef enum {
  hardwareError_none = 0, ///< hardware is ok
  hardwareError_openCircuit = 1, ///< input or output open circuit  (eg. bulb burnt)
  hardwareError_shortCircuit = 2, ///< input or output short circuit
  hardwareError_overload = 3, ///< output overload, including mechanical overload (e.g. heating valve actuator obstructed)
  hardwareError_busConnection = 4, ///< third party device bus problem (such as DALI short-circuit)
  hardwareError_lowBattery = 5, ///< third party device has low battery
  hardwareError_deviceError = 6, ///< other device error
} VdcHardwareError;


/// usage hints for inputs and outputs
typedef enum {
  usage_undefined = 0, ///< usage not defined
  usage_room = 1, ///< room related (e.g. indoor sensors and controllers)
  usage_outdoors = 2, ///< outdoors related (e.g. outdoor sensors)
  usage_user = 3, ///< user interaction (e.g. indicators, displays, dials, sliders)
  usage_total = 4, ///< total
  usage_lastrun = 5, ///< last run (of an activity like a washing machine program)
  usage_average = 6, ///< average (per run activity)
} VdcUsageHint;


/// @}

#endif
