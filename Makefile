##
# BEGIN_COPYRIGHT
#
# This file is part of SciDB.
# Copyright (C) 2008-2013 SciDB, Inc.
#
# SciDB is free software: you can redistribute it and/or modify
# it under the terms of the AFFERO GNU General Public License as published by
# the Free Software Foundation.
#
# SciDB is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
# INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
# NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
# the AFFERO GNU General Public License for the complete license terms.
#
# You should have received a copy of the AFFERO GNU General Public License
# along with SciDB.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
#
# END_COPYRIGHT
##
# You need SciDB installed with its development packages.
ifeq ($(SCIDB),) 
  X := $(shell which scidb 2>/dev/null)
  ifneq ($(X),)
    X := $(shell dirname ${X})
    SCIDB := $(shell dirname ${X})
  endif
  $(info SciDB installed at $(SCIDB))
endif

# A development environment will have SCIDB_VER defined, and SCIDB
# will not be in the same place... but the 3rd party directory *will*
# be, so build it using SCIDB_VER .
ifeq ($(SCIDB_VER),)
  SCIDB_3RDPARTY = $(SCIDB)/3rdparty
else
  SCIDB_3RDPARTY = /opt/scidb/$(SCIDB_VER)/3rdparty
endif

# Include the OPTIMIZED flags for non-debug use
OPTIMIZED=-O2 -DNDEBUG
DEBUG=-g -ggdb3
CFLAGS = -pedantic -W -Wextra -Wall -Wno-variadic-macros -Wno-strict-aliasing \
         -Wno-long-long -Wno-unused-parameter -fPIC $(OPTIMIZED) -std=c++11
INC = -I. -DPROJECT_ROOT="\"$(SCIDB)\"" -I"$(SCIDB_3RDPARTY)/boost/include/" \
      -I"$(SCIDB)/include" -I./extern

LIBS = -shared -Wl,-soname,libr_exec.so -ldl -lm -lcrypt -L. \
       -L"$(SCIDB)/3rdparty/boost/lib" -L"$(SCIDB)/lib" \
       -Wl,-rpath,$(SCIDB)/lib:$(RPATH)

all: Rcon
	@if test ! -d "$(SCIDB)"; then echo  "Error. Try:\n\nmake SCIDB=<PATH TO SCIDB INSTALLATION>\n\nBe sure to have the SciDB development packages installed on your system."; exit 1; fi 
	$(CXX) $(CFLAGS) $(INC) -o plugin.cpp.o -c plugin.cpp

	$(CXX) $(CFLAGS) $(INC) -o LogicalRExec.cpp.o -c LogicalRExec.cpp
	$(CXX) $(CFLAGS) $(INC) -o PhysicalRExec.cpp.o -c PhysicalRExec.cpp

	$(CXX) $(CFLAGS) $(INC) -o libr_exec.so \
	                           plugin.cpp.o \
	                           LogicalRExec.cpp.o \
	                           PhysicalRExec.cpp.o \
	                           Rconnection.cpp.o \
	                           $(LIBS)
	@echo
	@echo "Now run"
	@echo "  cp *.so $(SCIDB)/lib/scidb/plugins"
	@echo "on *all* your SciDB nodes and restart SciDB."

Rcon:
	$(CXX) $(CFLAGS) $(INC) -o Rconnection.cpp.o -c Rconnection.cpp

clean:
	rm -f *.o *.so
