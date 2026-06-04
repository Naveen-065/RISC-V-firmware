SIM ?= icarus
TOPLEVEL_LANG = verilog

CLKS_PER_BIT ?= 8

PWD := $(shell pwd)
PARTCL_RTL_DIR ?= $(PWD)/../external/partcl_neuromorphic_compute/verilog/rtl
VERILOG_SOURCES = $(PWD)/rtl/tmr_caravel_uart_system.v
VERILOG_SOURCES += $(PARTCL_RTL_DIR)/mat_mult_wb.v
TOPLEVEL = tmr_caravel_uart_system
MODULE = test_tmr_caravel_uart

COMPILE_ARGS += -g2012
COMPILE_ARGS += -P$(TOPLEVEL).CLKS_PER_BIT=$(CLKS_PER_BIT)

export PYTHONPATH := $(PWD)/tests
export CLKS_PER_BIT

include $(shell cocotb-config --makefiles)/Makefile.sim
