TARGET := hyprtrizoneLayout.so
BUILD_DIR := build

SRC := \
	src/main.cpp \
	src/pluginConfig.cpp \
	src/hyprtrizoneLayout.cpp \
	src/StackIndicatorPassElement.cpp

OBJ := $(SRC:%.cpp=$(BUILD_DIR)/%.o)
DEP := $(OBJ:.o=.d)

# Lua 5.5 has a different pkg-config name on every distro; same candidates as Hyprland's own build.
LUA_PKG ?= $(firstword $(foreach p,lua55 lua5.5 lua-55 lua-5.5 lua,$(shell pkg-config --exists $(p) 2>/dev/null && echo $(p))))

# Hyprland's input capture header includes libeis.h, which hyprland.pc does not list.
PKG_CONFIG_CFLAGS := $(shell pkg-config --cflags pixman-1 libdrm hyprland libeis-1.0 $(LUA_PKG))
PKG_CONFIG_LIBS := $(shell pkg-config --libs $(LUA_PKG))

CXX ?= c++
CXXFLAGS ?= -O2 -g
# Unique symbols are shared across every loaded copy and keep dlclose from unloading the plugin;
# the flag has to reach the compile step, not only the link.
CXXFLAGS += -std=c++2b -fPIC -fno-gnu-unique -MMD -MP
CPPFLAGS += -DWLR_USE_UNSTABLE $(PKG_CONFIG_CFLAGS)
LDFLAGS += -shared
LDLIBS += $(PKG_CONFIG_LIBS)

# Each branch targets one Hyprland minor release. HYPRLAND_VERSION names the exact patch release the Nix build
# and VM tests pin, and any patch release of that minor builds. Headers of another minor release can compile
# and still misbehave, so the build stops unless IGNORE_HYPRLAND_VERSION=1 (for porting).
HYPRLAND_TARGET := $(shell cut -d. -f1,2 HYPRLAND_VERSION)
HYPRLAND_FOUND_VERSION := $(shell pkg-config --modversion hyprland 2>/dev/null)
HYPRLAND_FOUND := $(shell echo '$(HYPRLAND_FOUND_VERSION)' | cut -d. -f1,2)

.PHONY: all clean check-hyprland

all: $(TARGET)

check-hyprland:
	@pkg-config --print-errors --exists pixman-1 libdrm hyprland libeis-1.0 $(LUA_PKG) || \
		{ echo "error: pkg-config cannot resolve Hyprland's build dependencies (see above)." >&2; exit 1; }
ifneq ($(IGNORE_HYPRLAND_VERSION),1)
	@if [ "$(HYPRLAND_FOUND)" != "$(HYPRLAND_TARGET)" ]; then \
		echo "error: this branch targets Hyprland $(HYPRLAND_TARGET), pkg-config finds '$(HYPRLAND_FOUND_VERSION)'." >&2; \
		echo "Build the hyprland-$(HYPRLAND_FOUND) branch (or master, if it targets $(HYPRLAND_FOUND))," >&2; \
		echo "or set IGNORE_HYPRLAND_VERSION=1 to port." >&2; \
		exit 1; \
	fi
endif

$(TARGET): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(BUILD_DIR)/%.o: %.cpp | check-hyprland
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEP)
