# ACC y Cartpole baseline x86-64 / RV64GC. Las bibliotecas originales permanecen en external/.
.DEFAULT_GOAL := acc
.DELETE_ON_ERROR:

RISCV_PREFIX ?= riscv64-unknown-elf
SPIKE ?= spike
CMAKE ?= cmake
PATCH ?= patch
PROFILE ?= spike
CARTPOLE_PROFILE_GROUP ?= B
ifeq ($(filter $(CARTPOLE_PROFILE_GROUP),A B),)
$(error CARTPOLE_PROFILE_GROUP debe ser A o B)
endif
PROFILE_GROUP_ID := $(if $(filter A,$(CARTPOLE_PROFILE_GROUP)),1,2)
CARTPOLE_PROFILE_REGIONS ?= full
ifeq ($(filter $(CARTPOLE_PROFILE_REGIONS),off cycles full),)
$(error CARTPOLE_PROFILE_REGIONS debe ser off, cycles o full)
endif
REGION_MODE := $(if $(filter off,$(CARTPOLE_PROFILE_REGIONS)),0,$(if $(filter cycles,$(CARTPOLE_PROFILE_REGIONS)),1,2))
ACC_STEPS ?= 64
HOST_CC ?= gcc
HOST_CXX ?= g++
PYTHON ?= $(if $(wildcard .venv/bin/python),$(CURDIR)/.venv/bin/python,python3)
CVA6_RUNNER ?=
MAX_CYCLES ?= 4000000000
HOST_TIMEOUT ?= 86400
SEED ?= 475092611
CVA6_TB ?=
CVA6_BUILD ?=
CVA6_RUNNER_DIR ?= $(CURDIR)/build/cva6-mpc-channel
REFERENCE ?= $(CURDIR)/build/x86/acc/64/results/capture.bin
VALID_STEPS := $(shell seq 1 64)
ifeq ($(filter $(ACC_STEPS),$(VALID_STEPS)),)
$(error ACC_STEPS debe estar entre 1 y 64)
endif
ifeq ($(filter $(PROFILE),spike x86 verilator),)
$(error PROFILE debe ser spike, x86 o verilator)
endif
ifeq ($(PROFILE),spike)
BUILD_DIR ?= build/rv64
else
BUILD_DIR ?= build/$(PROFILE)
endif
V ?= 0

BUILD := $(abspath $(BUILD_DIR))
ifeq ($(filter $(CURDIR)/build/%,$(BUILD)),)
$(error BUILD_DIR debe ser un subdirectorio de $(CURDIR)/build/)
endif

RV_CC := $(RISCV_PREFIX)-gcc
RV_CXX := $(RISCV_PREFIX)-g++
ARCH_FLAGS := -march=rv64gc -mabi=lp64d -mcmodel=medany
ifeq ($(PROFILE),x86)
RV_CC := $(HOST_CC)
RV_CXX := $(HOST_CXX)
ARCH_FLAGS :=
OSQP_PLATFORM := -DCMAKE_SYSTEM_NAME=Linux
else
OSQP_PLATFORM := -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_SYSTEM_PROCESSOR=riscv64 -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
endif
FP_FLAGS := -fno-fast-math -ffp-contract=off
OSQP_CFLAGS := $(ARCH_FLAGS) $(FP_FLAGS)
ACC_CXXFLAGS := -std=c++20 -O2 -DNDEBUG $(ARCH_FLAGS) $(FP_FLAGS) \
                 -ffunction-sections -fdata-sections
ACC_DEFINES := -DEIGEN_DONT_VECTORIZE \
               -DTNX=3 -DTNU=2 -DTNDU=2 -DTNY=1 \
               -DPREDICTION_HORIZON=5 -DCONTROL_HORIZON=5 \
               -DRSHARC_ACC_CLOSED_LOOP_MAX_STEPS=$(ACC_STEPS)

ifneq ($(PROFILE),x86)
ACC_DEFINES += -DRSHARC_ENABLE_UNWIND=1
endif
ifeq ($(PROFILE),verilator)
ACC_DEFINES += -DRSHARC_BAREMETAL_DISABLE_CONSOLE -DRSHARC_ACC_CVA6_CHANNEL
endif

CONFIG := $(BUILD)/build-config.txt
LIBMPC_DIR := $(BUILD)/libmpc
LIBMPC_STAMP := $(LIBMPC_DIR)/.prepared
OSQP_SRC := $(BUILD)/osqp-src
OSQP_BUILD := $(BUILD)/osqp
OSQP_STAMP := $(OSQP_SRC)/.prepared
OSQP_CONFIGURED := $(OSQP_BUILD)/.configured
OSQP_LIB := $(OSQP_BUILD)/out/libosqp.a
ACC_DIR := $(BUILD)/acc/$(ACC_STEPS)
ACC_CONFIG := $(ACC_DIR)/build-config.txt
RESULTS := $(ACC_DIR)/results
ACC_ELF := $(ACC_DIR)/acc.elf
ACC_MAP := $(ACC_DIR)/acc.map
ACC_CONFIG_HEADER := $(BUILD)/include/acc_config.hpp

ACC_INCLUDES := -Iexternal/eigen -Iexternal/json \
                -I"$(LIBMPC_DIR)/include" -I"$(BUILD)/include" \
                -Isrc/common -Isrc/acc
ACC_LDFLAGS := $(ARCH_FLAGS) -nostdlib -static -Wl,--gc-sections \
               -Wl,-T,src/baremetal/linker.ld -Wl,-Map,"$(ACC_MAP)"
ACC_LIBS := -Wl,--start-group "$(OSQP_LIB)" \
            -lstdc++ -lsupc++ -lm -lc -lgcc -Wl,--end-group
ACC_SOURCES := src/baremetal/start.S src/baremetal/runtime.cpp \
               src/baremetal/heap.cpp src/common/controller.cpp \
               src/acc/ACC_Controller.cpp src/acc/main.cpp
ifeq ($(PROFILE),x86)
ACC_SOURCES := src/common/controller.cpp src/acc/ACC_Controller.cpp src/acc/main.cpp
ACC_LDFLAGS := -Wl,--gc-sections -Wl,-Map,"$(ACC_MAP)"
ACC_LIBS := -Wl,--start-group "$(OSQP_LIB)" -lm -ldl -lrt -Wl,--end-group
endif
ACC_OBJECTS := $(addprefix $(ACC_DIR)/obj/,$(addsuffix .o,$(basename $(ACC_SOURCES))))
LIBMPC_INPUTS := $(shell find external/libmpc/include -type f)
OSQP_INPUTS := $(shell find external/osqp -type f)

ifeq ($(V),1)
Q :=
else
Q := @
endif

.PHONY: acc run-acc acc-x86 run-acc-x86 acc-verilator run-acc-verilator run-current validate-acc clean help prepare-acc-runner FORCE
acc: $(ACC_ELF)

run-acc:
	+$(Q)$(MAKE) PROFILE=spike run-current

acc-x86:
	+$(Q)$(MAKE) PROFILE=x86 acc

run-acc-x86:
	+$(Q)$(MAKE) PROFILE=x86 run-current

acc-verilator:
	+$(Q)$(MAKE) PROFILE=verilator acc

run-acc-verilator:
	+$(Q)$(MAKE) PROFILE=x86 ACC_STEPS=64 run-current
	+$(Q)$(MAKE) PROFILE=verilator run-current

run-current: acc
	$(Q)$(PYTHON) scripts/run_closed_loop.py run --mode "$(PROFILE)" --elf "$(ACC_ELF)" \
	  --steps $(ACC_STEPS) --out "$(RESULTS)" --prefix "$(RISCV_PREFIX)" \
	  --spike "$(SPIKE)" --runner "$(CVA6_RUNNER)" --max-cycles $(MAX_CYCLES) --timeout $(HOST_TIMEOUT) \
	  --seed $(SEED) $(if $(filter verilator,$(PROFILE)),--reference "$(REFERENCE)",)

validate-acc:
	$(Q)$(PYTHON) scripts/run_closed_loop.py compare \
	  --observed "$(CURDIR)/build/verilator/acc/$(ACC_STEPS)/results/capture.bin" \
	  --reference "$(REFERENCE)" --steps $(ACC_STEPS)

.PHONY: prepare-runner prepare-cartpole-runner
prepare-acc-runner prepare-cartpole-runner: prepare-runner

prepare-runner:
	$(Q)$(PYTHON) scripts/run_closed_loop.py prepare-runner --source "$(CVA6_TB)" \
	  --runner-build "$(CVA6_BUILD)" --out "$(CVA6_RUNNER_DIR)" \
	  --profile-group "$(CARTPOLE_PROFILE_GROUP)"

help:
	@printf '%s\n' \
	  'make acc / run-acc                 Compilar / ejecutar Spike.' \
	  'make acc-x86 / run-acc-x86         Compilar / ejecutar x86 y guardar captura.' \
	  'make acc-verilator                Compilar para CVA6 con consola desactivada.' \
	  'make run-acc-verilator ACC_STEPS=1 CVA6_RUNNER=/ruta/al/runner' \
	  'make prepare-acc-runner CVA6_TB=/ruta/ariane_tb.cpp CVA6_BUILD=/ruta/build' \
	  'make validate-acc ACC_STEPS=64     Comparar capturas actuales, sin lanzar RTL.' \
	  'make cartpole / cartpole-x86       Compilar Cartpole para Spike / x86.' \
	  'make cartpole-verilator            Compilar Cartpole para CVA6.' \
	  'make run-cartpole-x86              Bucle cerrado Cartpole en x86.' \
	  'make run-cartpole                  x86 + Spike y comparacion numerica.' \
	  'make run-cartpole-verilator CARTPOLE_STEPS=1 CVA6_RUNNER=/ruta/al/runner' \
	  'make prepare-cartpole-runner CVA6_TB=/ruta/ariane_tb.cpp CVA6_BUILD=/ruta/build' \
	  'make clean                        Borrar el BUILD_DIR seleccionado.' \
	  'Opciones: RISCV_PREFIX, HOST_CC, HOST_CXX, ACC_STEPS=1..64, SPIKE, V=1.'

# cmp conserva la fecha si no cambian compilador u opciones.
$(CONFIG): FORCE
	$(Q)mkdir -p "$(BUILD)"
	$(Q)set -eu; \
	  command -v "$(RV_CC)" >/dev/null || { printf '%s\n' 'No se encuentra $(RV_CC). Configura RISCV_PREFIX; consulta README.md.' >&2; exit 1; }; \
	  command -v "$(RV_CXX)" >/dev/null; \
	  { printf '%s\n' \
	      'CC=$(RV_CC)' 'CXX=$(RV_CXX)' \
	      'CC_PATH='"$$(command -v "$(RV_CC)")" \
	      'CXX_PATH='"$$(command -v "$(RV_CXX)")" \
	      'OSQP_CFLAGS=$(OSQP_CFLAGS)' \
	      'OSQP_RELEASE_FLAGS=-O2 -DNDEBUG' \
	      'CARTPOLE_PROFILE_GROUP=$(CARTPOLE_PROFILE_GROUP)' \
	      'CARTPOLE_PROFILE_REGIONS=$(CARTPOLE_PROFILE_REGIONS)' \
	      'PROFILE=$(PROFILE)'; \
	    "$(RV_CC)" --version; \
	    "$(RV_CXX)" --version; \
	  } > "$@.tmp"; \
	  if cmp -s "$@.tmp" "$@"; then rm -f "$@.tmp"; else mv "$@.tmp" "$@"; fi

# Las opciones del escenario no obligan a reconstruir OSQP.
$(ACC_CONFIG): $(CONFIG) FORCE
	$(Q)mkdir -p "$(ACC_DIR)"
	$(Q){ cat "$(CONFIG)"; printf '%s\n' 'ACC_CXXFLAGS=$(ACC_CXXFLAGS)' 'ACC_DEFINES=$(ACC_DEFINES)' 'ACC_LDFLAGS=$(ACC_LDFLAGS)' 'ACC_LIBS=$(ACC_LIBS)'; } > "$@.tmp"
	$(Q)if cmp -s "$@.tmp" "$@"; then rm -f "$@.tmp"; else mv "$@.tmp" "$@"; fi

# Cada parche se aplica una vez sobre una copia nueva, nunca sobre external/.
$(LIBMPC_STAMP): $(LIBMPC_INPUTS) scripts/patches/libmpc-baremetal.patch scripts/patches/libmpc-regions.patch Makefile
	@printf '%s\n' 'PREPARE libmpc'
	$(Q)rm -rf -- "$(LIBMPC_DIR)"
	$(Q)mkdir -p "$(LIBMPC_DIR)"
	$(Q)cp -R external/libmpc/include "$(LIBMPC_DIR)/"
	$(Q)"$(PATCH)" --batch --fuzz=0 -p1 -d "$(LIBMPC_DIR)" -i "$(CURDIR)/scripts/patches/libmpc-baremetal.patch"
	$(Q)"$(PATCH)" --batch --fuzz=0 -p1 -d "$(LIBMPC_DIR)" -i "$(CURDIR)/scripts/patches/libmpc-regions.patch"
	$(Q)touch "$@"

$(OSQP_STAMP): $(OSQP_INPUTS) scripts/patches/osqp-baremetal.patch Makefile
	@printf '%s\n' 'PREPARE OSQP'
	$(Q)rm -rf -- "$(OSQP_SRC)"
	$(Q)mkdir -p "$(OSQP_SRC)"
	$(Q)cp -R external/osqp/. "$(OSQP_SRC)/"
	$(Q)"$(PATCH)" --batch --fuzz=0 -p1 -d "$(OSQP_SRC)" -i "$(CURDIR)/scripts/patches/osqp-baremetal.patch"
	$(Q)touch "$@"

$(OSQP_CONFIGURED): $(OSQP_STAMP) $(CONFIG) Makefile
	@printf '%s\n' 'CONFIGURE OSQP ($(PROFILE), double, QDLDL)'
	$(Q)rm -rf -- "$(OSQP_BUILD)"
	$(Q)"$(CMAKE)" -S "$(OSQP_SRC)" -B "$(OSQP_BUILD)" -G "Unix Makefiles" \
	  $(OSQP_PLATFORM) \
	  -DCMAKE_C_COMPILER="$(RV_CC)" -DCMAKE_CXX_COMPILER="$(RV_CXX)" \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_C_FLAGS="$(OSQP_CFLAGS)" -DCMAKE_CXX_FLAGS="$(OSQP_CFLAGS)" \
	  -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" \
	  -DUNITTESTS=OFF -DPRINTING=OFF -DPROFILING=OFF -DCTRLC=OFF \
	  -DENABLE_MKL_PARDISO=OFF -DDFLOAT=OFF -DDLONG=ON
	$(Q)mkdir -p "$(BUILD)/include"
	$(Q)ln -sfn ../osqp-src/include "$(BUILD)/include/osqp"
	$(Q)touch "$@"

$(OSQP_LIB): $(OSQP_CONFIGURED)
	+$(Q)"$(CMAKE)" --build "$(OSQP_BUILD)" --target osqpstatic

# Una sola configuración JSON para la planta Python y el controlador C++.
$(ACC_CONFIG_HEADER): src/acc/config.json Makefile
	$(Q)mkdir -p "$(@D)"
	$(Q)$(PYTHON) -c 'import json,sys; print("#pragma once\nnamespace rsharc_acc { inline constexpr char config_json[] = " + json.dumps(sys.stdin.read()) + "; }")' < "$<" > "$@.tmp"
	$(Q)mv "$@.tmp" "$@"

$(ACC_OBJECTS): $(ACC_CONFIG_HEADER) $(ACC_CONFIG) $(LIBMPC_STAMP) $(OSQP_CONFIGURED) Makefile

$(ACC_DIR)/obj/%.o: %.cpp
	@printf 'CXX %s\n' "$<"
	$(Q)mkdir -p "$(@D)"
	$(Q)"$(RV_CXX)" $(ACC_CXXFLAGS) $(ACC_DEFINES) $(ACC_INCLUDES) -MMD -MP -c "$<" -o "$@"

$(ACC_DIR)/obj/%.o: %.S
	@printf 'ASM %s\n' "$<"
	$(Q)mkdir -p "$(@D)"
	$(Q)"$(RV_CXX)" $(ACC_CXXFLAGS) $(ACC_DEFINES) $(ACC_INCLUDES) -MMD -MP -c "$<" -o "$@"

$(ACC_ELF): $(ACC_OBJECTS) $(OSQP_LIB) $(if $(filter-out x86,$(PROFILE)),src/baremetal/linker.ld,) Makefile
	@printf 'LINK %s\n' "$@"
	$(Q)"$(RV_CXX)" $(ACC_LDFLAGS) $(ACC_OBJECTS) -o "$@" $(ACC_LIBS)

clean:
	$(Q)rm -rf -- "$(BUILD)"

-include $(ACC_OBJECTS:.o=.d)

# NLopt para Cartpole
NLOPT_BUILD := $(BUILD)/nlopt
NLOPT_LIB := $(NLOPT_BUILD)/libnlopt.a
NLOPT_CONFIGURED := $(NLOPT_BUILD)/.configured
NLOPT_INPUTS := $(shell find external/nlopt -type f)
NLOPT_SOURCE := $(CURDIR)/external/nlopt
NLOPT_CFLAGS := $(ARCH_FLAGS) $(FP_FLAGS) -funroll-loops
ifeq ($(PROFILE),verilator)
NLOPT_SOURCE := $(BUILD)/nlopt-src
NLOPT_CFLAGS += -I$(CURDIR)/src/common -DRSHARC_CVA6_PROFILE_ENABLE=1
NLOPT_CFLAGS += -DRSHARC_CVA6_REGION_MODE=$(REGION_MODE)
NLOPT_CFLAGS += -DRSHARC_CVA6_PROFILE_GROUP=$(PROFILE_GROUP_ID)
endif

NLOPT_PLATFORM := -DCMAKE_SYSTEM_NAME=Linux
ifneq ($(PROFILE),x86)
NLOPT_PLATFORM := -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_SYSTEM_PROCESSOR=riscv64 -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
endif

.PHONY: nlopt
nlopt: $(NLOPT_LIB)

$(NLOPT_CONFIGURED): $(CONFIG) Makefile external/nlopt/CMakeLists.txt $(if $(filter verilator,$(PROFILE)),$(NLOPT_INPUTS) scripts/patches/nlopt-regions.patch src/common/cva6_profile.h,)
	@printf 'CONFIGURE NLopt (%s)\n' "$(PROFILE)"
	$(Q)rm -rf -- "$(NLOPT_BUILD)"
ifeq ($(PROFILE),verilator)
	$(Q)rm -rf -- "$(NLOPT_SOURCE)"
	$(Q)mkdir -p "$(NLOPT_SOURCE)"
	$(Q)cp -R external/nlopt/. "$(NLOPT_SOURCE)/"
	$(Q)"$(PATCH)" --batch --fuzz=0 -p1 -d "$(NLOPT_SOURCE)" -i "$(CURDIR)/scripts/patches/nlopt-regions.patch"
endif
	$(Q)"$(CMAKE)" -S "$(NLOPT_SOURCE)" -B "$(NLOPT_BUILD)" -G "Unix Makefiles" \
	  $(NLOPT_PLATFORM) \
	  -DCMAKE_C_COMPILER="$(RV_CC)" -DCMAKE_CXX_COMPILER="$(RV_CXX)" \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_C_FLAGS="$(NLOPT_CFLAGS)" \
	  -DCMAKE_CXX_FLAGS="$(ARCH_FLAGS) $(FP_FLAGS)" \
	  -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" \
	  -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" \
	  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
	  -DM_LIBRARY:FILEPATH="$$("$(RV_CC)" -print-file-name=libm.a)" \
	  -DBUILD_SHARED_LIBS=OFF \
	  -DWITH_THREADLOCAL=OFF -DTHREADLOCAL:STRING= \
	  -DNLOPT_CXX=OFF -DNLOPT_FORTRAN=OFF \
	  -DNLOPT_PYTHON=OFF -DNLOPT_OCTAVE=OFF -DNLOPT_MATLAB=OFF \
	  -DNLOPT_GUILE=OFF -DNLOPT_JAVA=OFF -DNLOPT_SWIG=OFF \
	  -DNLOPT_LUKSAN=OFF -DNLOPT_TESTS=OFF \
	  -DHAVE_GETOPT_H:BOOL=OFF -DHAVE_GETOPT:BOOL=OFF \
	  -DHAVE_UNISTD_H:BOOL=OFF -DHAVE_GETPID:BOOL=OFF \
	  -DHAVE_GETTID_SYSCALL:BOOL=OFF -DHAVE_QSORT_R:BOOL=OFF \
	  -DHAVE_STDINT_H:BOOL=ON -DHAVE_TIME_H:BOOL=ON \
	  -DHAVE_SYS_TIME_H:BOOL=ON -DHAVE_GETTIMEOFDAY:BOOL=ON \
	  -DHAVE_TIME:BOOL=OFF -DHAVE_COPYSIGN:BOOL=ON \
	  -DHAVE_ISINF:BOOL=ON -DHAVE_ISNAN:BOOL=ON \
	  -DHAVE_FPCLASSIFY:BOOL=ON
	$(Q)touch "$@"

$(NLOPT_LIB): $(NLOPT_CONFIGURED) $(NLOPT_INPUTS)
	+$(Q)"$(CMAKE)" --build "$(NLOPT_BUILD)" --target nlopt

# Servicio Cartpole escalar: misma configuracion en x86, Spike y CVA6.
CARTPOLE_STEPS ?= 64
CARTPOLE_MAX_CYCLES ?= 100000000000
CARTPOLE_HOST_TIMEOUT ?= 1209600
ifeq ($(filter $(CARTPOLE_STEPS),$(VALID_STEPS)),)
$(error CARTPOLE_STEPS debe estar entre 1 y 64)
endif

CARTPOLE_DIR := $(BUILD)/cartpole/$(CARTPOLE_STEPS)
CARTPOLE_ELF := $(CARTPOLE_DIR)/cartpole.elf
CARTPOLE_MAP := $(CARTPOLE_DIR)/cartpole.map
CARTPOLE_CONFIG := $(CARTPOLE_DIR)/build-config.txt
CARTPOLE_CONFIG_HEADER := $(BUILD)/include/cartpole_config.hpp
CARTPOLE_CXXFLAGS := -std=c++20 -O2 -DNDEBUG $(ARCH_FLAGS) $(FP_FLAGS) \
                     -ffunction-sections -fdata-sections
CARTPOLE_DEFINES := -DEIGEN_DONT_VECTORIZE -DEIGEN_STACK_ALLOCATION_LIMIT=0 \
                   -DTNX=4 -DTNU=1 -DTNDU=1 -DTNY=1 \
                   -DPREDICTION_HORIZON=20 -DCONTROL_HORIZON=4 \
                   -DRSHARC_CARTPOLE_MAX_STEPS=$(CARTPOLE_STEPS)
CARTPOLE_INCLUDES := -Iexternal/eigen -Iexternal/json -Iexternal/libmpc/include \
                    -Iexternal/nlopt/src/api -I"$(NLOPT_BUILD)" \
                    -I"$(BUILD)/include" -Isrc/common -Isrc/cartpole
CARTPOLE_SOURCES := src/common/controller.cpp src/cartpole/NLMPCController.cpp \
                   src/cartpole/main.cpp
CARTPOLE_LDFLAGS := -Wl,--gc-sections -Wl,-Map,"$(CARTPOLE_MAP)"
CARTPOLE_LIBS := -Wl,--start-group "$(NLOPT_LIB)" -lm -Wl,--end-group
ifneq ($(PROFILE),x86)
CARTPOLE_SOURCES += src/baremetal/start.S src/baremetal/runtime.cpp src/baremetal/heap.cpp
CARTPOLE_DEFINES += -DRSHARC_ENABLE_UNWIND=1
CARTPOLE_LDFLAGS += $(ARCH_FLAGS) -nostdlib -static \
                    -Wl,--defsym,RSHARC_STACK_SIZE=0x40000 -Wl,-T,src/baremetal/linker.ld
CARTPOLE_LIBS := -Wl,--start-group "$(NLOPT_LIB)" \
                -lstdc++ -lsupc++ -lm -lc -lgcc -Wl,--end-group
endif
ifeq ($(PROFILE),verilator)
CARTPOLE_DEFINES += -DRSHARC_BAREMETAL_DISABLE_CONSOLE -DRSHARC_CARTPOLE_CVA6_CHANNEL
CARTPOLE_DEFINES += -DRSHARC_CVA6_PROFILE_ENABLE=1
CARTPOLE_DEFINES += -DRSHARC_CVA6_REGION_MODE=$(REGION_MODE)
CARTPOLE_DEFINES += -DRSHARC_CVA6_PROFILE_GROUP=$(PROFILE_GROUP_ID)
CARTPOLE_INCLUDES := $(filter-out -Iexternal/libmpc/include,$(CARTPOLE_INCLUDES)) -I"$(LIBMPC_DIR)/include"
endif
CARTPOLE_OBJECTS := $(addprefix $(CARTPOLE_DIR)/obj/,$(addsuffix .o,$(basename $(CARTPOLE_SOURCES))))

.PHONY: cartpole cartpole-x86 cartpole-verilator
cartpole: $(CARTPOLE_ELF)

cartpole-x86:
	+$(Q)$(MAKE) PROFILE=x86 cartpole

cartpole-verilator:
	+$(Q)$(MAKE) PROFILE=verilator cartpole

$(CARTPOLE_CONFIG): $(CONFIG) FORCE
	$(Q)mkdir -p "$(CARTPOLE_DIR)"
	$(Q){ cat "$(CONFIG)"; printf '%s\n' 'CARTPOLE_CXXFLAGS=$(CARTPOLE_CXXFLAGS)' 'CARTPOLE_DEFINES=$(CARTPOLE_DEFINES)' 'CARTPOLE_INCLUDES=$(CARTPOLE_INCLUDES)' 'CARTPOLE_LDFLAGS=$(CARTPOLE_LDFLAGS)' 'CARTPOLE_LIBS=$(CARTPOLE_LIBS)'; } > "$@.tmp"
	$(Q)if cmp -s "$@.tmp" "$@"; then rm -f "$@.tmp"; else mv "$@.tmp" "$@"; fi

$(CARTPOLE_CONFIG_HEADER): src/cartpole/config.json Makefile
	$(Q)mkdir -p "$(@D)"
	$(Q)$(PYTHON) -c 'import json,sys; print("#pragma once\nnamespace rsharc_cartpole { inline constexpr char config_json[] = " + json.dumps(sys.stdin.read()) + "; }")' < "$<" > "$@.tmp"
	$(Q)mv "$@.tmp" "$@"

$(CARTPOLE_OBJECTS): $(CARTPOLE_CONFIG_HEADER) $(CARTPOLE_CONFIG) $(NLOPT_LIB) $(if $(filter verilator,$(PROFILE)),$(LIBMPC_STAMP),) Makefile

$(CARTPOLE_DIR)/obj/%.o: %.cpp
	@printf 'CXX %s\n' "$<"
	$(Q)mkdir -p "$(@D)"
	$(Q)"$(RV_CXX)" $(CARTPOLE_CXXFLAGS) $(CARTPOLE_DEFINES) $(CARTPOLE_INCLUDES) -MMD -MP -c "$<" -o "$@"

$(CARTPOLE_DIR)/obj/%.o: %.S
	@printf 'ASM %s\n' "$<"
	$(Q)mkdir -p "$(@D)"
	$(Q)"$(RV_CXX)" $(CARTPOLE_CXXFLAGS) $(CARTPOLE_DEFINES) $(CARTPOLE_INCLUDES) -MMD -MP -c "$<" -o "$@"

$(CARTPOLE_ELF): $(CARTPOLE_OBJECTS) $(NLOPT_LIB) $(if $(filter-out x86,$(PROFILE)),src/baremetal/linker.ld,) Makefile
	@printf 'LINK %s\n' "$@"
	$(Q)"$(RV_CXX)" $(CARTPOLE_LDFLAGS) $(CARTPOLE_OBJECTS) -o "$@" $(CARTPOLE_LIBS)

-include $(CARTPOLE_OBJECTS:.o=.d)

# Planta Python externa y servicio persistente Cartpole.
CARTPOLE_RESULTS := $(CARTPOLE_DIR)/results
CARTPOLE_REFERENCE ?= $(CURDIR)/build/x86/cartpole/$(CARTPOLE_STEPS)/results/capture.json

.PHONY: run-cartpole run-cartpole-x86 run-cartpole-current run-cartpole-verilator
run-cartpole:
	+$(Q)$(MAKE) PROFILE=x86 run-cartpole-current
	+$(Q)$(MAKE) PROFILE=spike run-cartpole-current

run-cartpole-x86:
	+$(Q)$(MAKE) PROFILE=x86 run-cartpole-current

run-cartpole-verilator:
	+$(Q)$(MAKE) PROFILE=x86 run-cartpole-current
	+$(Q)$(MAKE) PROFILE=verilator run-cartpole-current

run-cartpole-current: cartpole
	$(Q)$(PYTHON) scripts/run_closed_loop.py run --controller cartpole \
	  --mode "$(PROFILE)" --elf "$(CARTPOLE_ELF)" --steps $(CARTPOLE_STEPS) \
	  --out "$(CARTPOLE_RESULTS)" --spike "$(SPIKE)" \
	  --prefix "$(RISCV_PREFIX)" --runner "$(CVA6_RUNNER)" --seed $(SEED) \
	  --max-cycles $(CARTPOLE_MAX_CYCLES) \
	  --timeout $(if $(filter verilator,$(PROFILE)),$(CARTPOLE_HOST_TIMEOUT),$(HOST_TIMEOUT)) \
	  $(if $(and $(filter spike verilator,$(PROFILE)),$(strip $(CARTPOLE_REFERENCE))),--reference "$(CARTPOLE_REFERENCE)",)
