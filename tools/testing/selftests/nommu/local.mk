# detect if users request NOMMU build or not
# User can set NOMMU to 1 to build/test for NOMMU platforms
NOMMU ?= 0
ifeq ($(NOMMU),1)
CFLAGS += -DNOMMU
export NOMMU
endif
