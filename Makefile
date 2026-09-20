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

PKG_CONFIG_CFLAGS := $(shell pkg-config --cflags pixman-1 libdrm hyprland $(LUA_PKG))
PKG_CONFIG_LIBS := $(shell pkg-config --libs $(LUA_PKG))

CXX ?= c++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++2b -fPIC -MMD -MP
CPPFLAGS += -DWLR_USE_UNSTABLE $(PKG_CONFIG_CFLAGS)
LDFLAGS += -shared --no-gnu-unique
LDLIBS += $(PKG_CONFIG_LIBS)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEP)
