# C11 Vulkan workspace.
#
# Common commands:
#   make
#   make run
#   make check
#   make shaders
#   make EXAMPLE=00_enumerate
#   make EXAMPLE=01_window
#   make clean
#
# On Windows, "make CC=clang VULKAN_SDK=C:/path/to/VulkanSDK/x.y.z" can be
# used to override the defaults.

CC       := gcc
GLSLC    := glslc
CMAKE    := cmake
BUILD_DIR ?= build
EXAMPLE   ?= 01_window
MODEL     ?= assets/models/african_head.obj

ifeq ($(OS),Windows_NT)
	VULKAN_SDK ?= C:/VulkanSDK/1.4.363.0
	VULKAN_INCLUDE := $(VULKAN_SDK)/Include
	VULKAN_LIB_DIR := $(VULKAN_SDK)/Lib
	VULKAN_LIBS := -lvulkan-1
	GLFW_ROOT ?= C:/vcpkg/installed/x64-windows
	GLFW_CFLAGS := -I"$(GLFW_ROOT)/include"
	GLFW_LIBS := -L"$(GLFW_ROOT)/lib" -lglfw3dll
	GLFW_RUNTIME := $(BUILD_DIR)/glfw3.dll
	TARGET := $(BUILD_DIR)/$(EXAMPLE).exe
else
	VULKAN_SDK ?= /usr
	VULKAN_INCLUDE := $(VULKAN_SDK)/include
	VULKAN_LIB_DIR := $(VULKAN_SDK)/lib
	VULKAN_LIBS := -lvulkan
	PKG_CONFIG ?= pkg-config
	GLFW_CFLAGS := $(shell $(PKG_CONFIG) --cflags glfw3)
	GLFW_LIBS := $(shell $(PKG_CONFIG) --libs glfw3)
	MATH_LIBS := -lm
	GLFW_RUNTIME :=
	TARGET := $(BUILD_DIR)/$(EXAMPLE)
endif

CPPFLAGS += -Isrc -I"$(VULKAN_INCLUDE)" $(GLFW_CFLAGS)
CFLAGS   += -std=c11 -O0 -g3 -Wall -Wextra -Wpedantic -Wshadow
CFLAGS   += -Wconversion -Wsign-conversion -Wformat=2 -Wundef -Werror
LDFLAGS  += -L"$(VULKAN_LIB_DIR)"
LDLIBS   += $(VULKAN_LIBS) $(GLFW_LIBS) $(MATH_LIBS)

EXAMPLE_SOURCES := $(wildcard examples/$(EXAMPLE)/*.c)
COMMON_SOURCES  := src/vk_helpers.c
SOURCES         := $(EXAMPLE_SOURCES) $(COMMON_SOURCES)
OBJECTS         := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(SOURCES))

SHADER_SOURCES := $(wildcard shaders/*.vert shaders/*.frag)
SHADER_TARGETS := $(patsubst shaders/%.vert,$(BUILD_DIR)/shaders/%.vert.spv,$(SHADER_SOURCES))
SHADER_TARGETS := $(patsubst shaders/%.frag,$(BUILD_DIR)/shaders/%.frag.spv,$(SHADER_TARGETS))

.PHONY: all run check shaders clean info

all: $(TARGET) $(GLFW_RUNTIME) $(SHADER_TARGETS)

$(TARGET): $(OBJECTS)
	@$(CMAKE) -E make_directory "$(@D)"
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

$(BUILD_DIR)/glfw3.dll: $(GLFW_ROOT)/bin/glfw3.dll
	@$(CMAKE) -E make_directory "$(@D)"
	$(CMAKE) -E copy_if_different "$<" "$@"

$(BUILD_DIR)/obj/%.o: %.c
	@$(CMAKE) -E make_directory "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD_DIR)/shaders/%.vert.spv: shaders/%.vert
	@$(CMAKE) -E make_directory "$(@D)"
	$(GLSLC) -fshader-stage=vert "$<" -o "$@"

$(BUILD_DIR)/shaders/%.frag.spv: shaders/%.frag
	@$(CMAKE) -E make_directory "$(@D)"
	$(GLSLC) -fshader-stage=frag "$<" -o "$@"

run: all
	"$(TARGET)" --model "$(MODEL)"

check: all
	"$(TARGET)" --smoke-test --model "$(MODEL)"

shaders: $(SHADER_TARGETS)

clean:
	@$(CMAKE) -E rm -rf "$(BUILD_DIR)"

info:
	@echo "Compiler:     $(CC)"
	@$(CC) --version
	@echo "Shader tool:  $(GLSLC)"
	@$(GLSLC) --version
	@echo "Vulkan SDK:   $(VULKAN_SDK)"
	@echo "GLFW root:    $(GLFW_ROOT)"
	@echo "Example:      $(EXAMPLE)"
	@echo "Executable:   $(TARGET)"

-include $(OBJECTS:.o=.d)
