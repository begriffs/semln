CFLAGS = -g -std=c99 -pedantic -Wall -Wextra
CFLAGS  += -I/usr/local/include
LDFLAGS += -L/usr/local/lib
LDLIBS  +=  -lcmark -licuio -licui18n -licuuc -licudata

mdline : mdline.c
