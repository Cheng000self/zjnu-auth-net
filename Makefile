CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -pthread
LDFLAGS ?=
LDLIBS ?= -lcurl
PKG_CONFIG ?= pkg-config

TARGET = zjnu_auth_lit
STATIC_TARGET = zjnu_auth_lit_static
SRC = zjnu_auth_lit.cpp

STATIC_CXXFLAGS ?= $(CXXFLAGS) -DZJNU_AUTH_NO_CURL
STATIC_LDFLAGS ?= -static
STATIC_LDLIBS ?= -pthread

.PHONY: all static verify-static clean check

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

static: $(STATIC_TARGET)

$(STATIC_TARGET): $(SRC)
	$(CXX) $(STATIC_CXXFLAGS) $(STATIC_LDFLAGS) -o $@ $< $(STATIC_LDLIBS)

verify-static: $(STATIC_TARGET)
	@file $(STATIC_TARGET)
	@ldd $(STATIC_TARGET) 2>&1 | grep -q "not a dynamic executable" && echo "OK: fully static binary" || (echo "FAIL: binary is still dynamically linked"; ldd $(STATIC_TARGET); exit 1)

check:
	python3 -m py_compile zjnu_auth_lit.py
	bash -n zjnu_auth_lit.sh
	$(CXX) $(CXXFLAGS) -c $(SRC) -o /tmp/zjnu_auth_lit.o

clean:
	rm -f $(TARGET) $(STATIC_TARGET) *.o
