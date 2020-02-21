# these are system specific, override them when invoking make if you are on
# another platform
ARDUINO_DIR   ?= /usr/share/arduino
ARDMK_DIR     ?= /usr/share/arduino
AVR_TOOLS_DIR ?= /usr

# fixes missing config issue
AVRDUDE_CONF = /etc/avrdude.conf

BOARD_TAG    = uno

# add the liberries
#CXXFLAGS += -ITalkie/Talkie -Wno-narrowing
#LOCAL_CPP_SRCS += $(wildcard Talkie/Talkie/*.cpp)

include ${ARDUINO_DIR}/Arduino.mk

