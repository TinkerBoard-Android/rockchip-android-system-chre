#
# WWAN World Nanoapp Makefile
#

# Environment Checks ###########################################################

ifeq ($(CHRE_PREFIX),)
$(error "The CHRE_PREFIX environment variable must be set to a path to the \
         CHRE project root. Example: export CHRE_PREFIX=$$HOME/chre")
endif

# Nanoapp Configuration ########################################################

NANOAPP_NAME = wwan_world

# Common Compiler Flags ########################################################

COMMON_CFLAGS += -I.

# Common Source Files ##########################################################

COMMON_SRCS += wwan_world.cc

# Permission declarations ######################################################

CHRE_NANOAPP_USES_WWAN = true

# Makefile Includes ############################################################

include $(CHRE_PREFIX)/build/nanoapp/app.mk
