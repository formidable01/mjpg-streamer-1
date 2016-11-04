###############################################################
#
# Purpose: Makefile for "M-JPEG Streamer"
# Author.: Tom Stoeveken (TST)
# Version: 0.3
# License: GPL
#
###############################################################

CC = g++

OTHER_HEADERS = ../../mjpg_streamer.h ../../utils.h ../output.h ../input.h

CXXFLAGS += -O2 -DLINUX -D_GNU_SOURCE -Wall -shared -std=c++11 -fPIC -I/usr/local/lib
#CFLAGS += -DDEBUG
LFLAGS += -L/usr/local/lib -luWS -luv -lz -lpthread 

all: output_ws.so

clean:
	rm -f *.a *.o core *~ *.so *.lo

output_ws.so: $(OTHER_HEADERS) output_ws.cpp
	$(CC) $(CXXFLAGS) -o $@ output_ws.cpp $(LFLAGS) 
