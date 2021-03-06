SHELL = /bin/bash

.PHONY: all
all:

# Make these default for now
ARCH            ?= arm32
PLATFORM        ?= orly2
O		?= out/$(ARCH)-plat-$(PLATFORM)

arch_$(ARCH)	:= y

cmd-fixdep	:= ./scripts/fixdep

ifneq ($O,)
out-dir := $O/
endif

ifneq ($V,1)
q := @
cmd-echo := true
else
q :=
cmd-echo := echo
endif

include core/core.mk

include ta/ta.mk

.PHONY: clean
clean:
	@echo Cleaning
	${q}rm -f $(cleanfiles)

.PHONY: cscope
cscope:
	@echo Creating cscope database
	${q}rm -f cscope.*
	${q}find $(PWD) -name "*.[chSs]" > cscope.files
	${q}cscope -b -q -k
