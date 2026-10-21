#
# Makefile for the unit tests of vdr-plugin-vdrsternelec
#
# The tested parts do not depend on VDR or libcurl.
#

CXX      ?= g++
CXXFLAGS ?= -g -O2 -Wall -Wextra -Werror -Wno-maybe-uninitialized -fsanitize=address,undefined

SRCS = test_parsers.c ../json.c ../parsers.c ../sha256.c

all: test

test_parsers: $(SRCS) ../json.h ../parsers.h ../sha256.h
	$(CXX) $(CXXFLAGS) -x c++ $(SRCS) -o $@

test: test_parsers
	./test_parsers

clean:
	@-rm -f test_parsers

.PHONY: all test clean
