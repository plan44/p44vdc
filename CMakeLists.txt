set(P44VDC_PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/pbuf/gen")
file(MAKE_DIRECTORY "${P44VDC_PROTO_GEN_DIR}")

set(P44VDC_PROTO_GENERATED
  "${P44VDC_PROTO_GEN_DIR}/messages.pb-c.h"
  "${P44VDC_PROTO_GEN_DIR}/messages.pb-c.c"
  "${P44VDC_PROTO_GEN_DIR}/vdcapi.pb-c.h"
  "${P44VDC_PROTO_GEN_DIR}/vdcapi.pb-c.c"
)

add_custom_command(
  OUTPUT "${P44VDC_PROTO_GEN_DIR}/messages.pb-c.h" "${P44VDC_PROTO_GEN_DIR}/messages.pb-c.c"
  COMMAND "${PROTOC_C}" --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/pbuf --c_out=${P44VDC_PROTO_GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/pbuf/messages.proto
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/pbuf/messages.proto"
  VERBATIM
)

add_custom_command(
  OUTPUT "${P44VDC_PROTO_GEN_DIR}/vdcapi.pb-c.h" "${P44VDC_PROTO_GEN_DIR}/vdcapi.pb-c.c"
  COMMAND "${PROTOC_C}" --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/pbuf --c_out=${P44VDC_PROTO_GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/pbuf/vdcapi.proto
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/pbuf/vdcapi.proto"
  VERBATIM
)

add_library(p44vdc STATIC
  ${P44VDC_PROTO_GENERATED}
  vdc_common/dsbehaviour.cpp
  vdc_common/outputbehaviour.cpp
  vdc_common/channelbehaviour.cpp
  vdc_common/dsscene.cpp
  vdc_common/simplescene.cpp
  vdc_common/device.cpp
  vdc_common/singledevice.cpp
  vdc_common/devicesettings.cpp
  vdc_common/valuesource.cpp
  vdc_common/valuedescriptor.cpp
  vdc_common/propertycontainer.cpp
  vdc_common/pbufvdcapi.cpp
  vdc_common/jsonvdcapi.cpp
  vdc_common/vdcapi.cpp
  vdc_common/apivalue.cpp
  vdc_common/dsaddressable.cpp
  vdc_common/vdc.cpp
  vdc_common/vdchost.cpp
  vdc_common/localcontroller.cpp
  vdc_common/discovery.cpp
  vdc_common/dsuid.cpp
  vdc_common/p44_vdcd_host.cpp
  behaviours/climatecontrolbehaviour.cpp
  behaviours/ventilationbehaviour.cpp
  behaviours/audiobehaviour.cpp
  behaviours/videobehaviour.cpp
  behaviours/shadowbehaviour.cpp
  behaviours/buttonbehaviour.cpp
  behaviours/sensorbehaviour.cpp
  behaviours/binaryinputbehaviour.cpp
  behaviours/lightbehaviour.cpp
  behaviours/colorlightbehaviour.cpp
  behaviours/movinglightbehaviour.cpp
  deviceclasses/simpleio/consoledevice.cpp
  deviceclasses/simpleio/mystromdevice.cpp
  deviceclasses/simpleio/digitaliodevice.cpp
  deviceclasses/simpleio/analogiodevice.cpp
  deviceclasses/simpleio/staticvdc.cpp
  deviceclasses/custom/customdevice.cpp
  deviceclasses/custom/externalvdc.cpp
  deviceclasses/custom/mattervdc.cpp
  deviceclasses/custom/scriptedvdc.cpp
  deviceclasses/evaluator/evaluatorvdc.cpp
  deviceclasses/evaluator/evaluatordevice.cpp
  deviceclasses/bridge/bridgevdc.cpp
  deviceclasses/bridge/bridgedevice.cpp
  deviceclasses/dmx/dmxdevice.cpp
  deviceclasses/dmx/dmxvdc.cpp
  deviceclasses/ds485/ds485comm.cpp
  deviceclasses/ds485/ds485device.cpp
  deviceclasses/ds485/ds485vdc.cpp
  deviceclasses/wbf/wbfcomm.cpp
  deviceclasses/wbf/wbfdevice.cpp
  deviceclasses/wbf/wbfvdc.cpp
  deviceclasses/proxy/proxyvdc.cpp
  deviceclasses/proxy/proxydevice.cpp
  deviceclasses/proxy/p44bridgeapi.cpp
  deviceclasses/enocean/enoceancomm.cpp
  deviceclasses/enocean/enoceandevice.cpp
  deviceclasses/enocean/enoceanremotecontrol.cpp
  deviceclasses/enocean/enoceaninputhandler.cpp
  deviceclasses/enocean/enoceanrps.cpp
  deviceclasses/enocean/enocean4bs.cpp
  deviceclasses/enocean/enocean1bs.cpp
  deviceclasses/enocean/enoceanvld.cpp
  deviceclasses/enocean/enoceanvdc.cpp
  deviceclasses/eldat/eldatcomm.cpp
  deviceclasses/eldat/eldatdevice.cpp
  deviceclasses/eldat/eldatvdc.cpp
  deviceclasses/zf/zfcomm.cpp
  deviceclasses/zf/zfdevice.cpp
  deviceclasses/zf/zfvdc.cpp
  deviceclasses/dali/dalicomm.cpp
  deviceclasses/dali/dalidevice.cpp
  deviceclasses/dali/dalivdc.cpp
  deviceclasses/hue/huecomm.cpp
  deviceclasses/hue/huevdc.cpp
  deviceclasses/hue/huedevice.cpp
  deviceclasses/ledchain/ledchaindevice.cpp
  deviceclasses/ledchain/ledchainvdc.cpp
)

set(P44VDC_INCLUDE_DIRS
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${P44VDC_PROTO_GEN_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/vdc_common"
  "${CMAKE_CURRENT_SOURCE_DIR}/behaviours"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/simpleio"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/bridge"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/proxy"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/custom"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/evaluator"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/enocean"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/dmx"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/ds485"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/wbf"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/eldat"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/zf"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/dali"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/hue"
  "${CMAKE_CURRENT_SOURCE_DIR}/deviceclasses/ledchain"
)

if(ENABLE_DS485)
  set(P44VDC_DS485_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include/digitalSTROM")
  file(MAKE_DIRECTORY "${P44VDC_DS485_INCLUDE_DIR}")
  file(GLOB P44VDC_DS485_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ds485-stack/ds485-core/include/digitalSTROM/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ds485-stack/ds485-netlib/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ds485-stack/ds485-client/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ds485-stack/libdsuid/src/*.h"
  )
  foreach(ds485_header IN LISTS P44VDC_DS485_HEADERS)
    configure_file("${ds485_header}" "${P44VDC_DS485_INCLUDE_DIR}" COPYONLY)
  endforeach()
  target_sources(p44vdc PRIVATE
    thirdparty/ds485-stack/libdsuid/src/dsuid.c
    thirdparty/ds485-stack/ds485-client/base/log.c
    thirdparty/ds485-stack/ds485-client/base/socket_client.c
    thirdparty/ds485-stack/ds485-client/base/socket_send.c
    thirdparty/ds485-stack/ds485-client/src/client-socket.c
    thirdparty/ds485-stack/ds485-client/src/ds485-client.c
    thirdparty/ds485-stack/ds485-client/src/shim.c
    thirdparty/ds485-stack/ds485-client/src/util.c
    thirdparty/ds485-stack/ds485-client/src/waitqueue.c
  )
  list(APPEND P44VDC_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ds485-stack/libdsuid/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ds485-stack/ds485-client"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ds485-stack/ds485-client/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ds485-stack/prebuilt/dsm-api-v2"
    "${P44VDC_DS485_INCLUDE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/include"
  )
endif()

target_include_directories(p44vdc PUBLIC ${P44VDC_INCLUDE_DIRS})
target_link_libraries(p44vdc PUBLIC p44utils p44lrgraphics vdcd_config)

if(ENABLE_P44FEATURES)
  target_link_libraries(p44vdc PUBLIC p44features)
endif()

target_compile_options(p44vdc PRIVATE -Wno-reorder -Wno-missing-field-initializers)
