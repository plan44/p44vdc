## SPDX-License-Identifier: GPL-3.0-or-later
##
## Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
##
## WLED device class build configuration
##

if ENABLE_WLED

# Source files
WLED_SOURCES = \
  wledcomm.cpp \
  wledvdc.cpp \
  wleddevice.cpp

# Header files
WLED_HEADERS = \
  wledcomm.hpp \
  wledvdc.hpp \
  wleddevice.hpp

# Conditionally add to main build
vdc_sources += $(addprefix deviceclasses/wled/, $(WLED_SOURCES))
nobase_include_HEADERS += $(addprefix deviceclasses/wled/, $(WLED_HEADERS))

endif
