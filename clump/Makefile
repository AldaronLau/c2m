#DEBUG = -pg -ggdb
DEBUG = -DNDEBUG -O3 -flto
CFLAGS = -std=c99 -Wall -Werror -pedantic -fPIC $(DEBUG)
CC = $(CROSS_COMPILE)gcc
AR = gcc-ar

SRC = src
BUILD = build
MODULES = clump pool array list hash rhash tree bitarray hcodec
OBJS = $(addprefix $(BUILD)/, $(addsuffix .o,$(MODULES)))
SHARED = $(BUILD)/libclump.so
STATIC = $(BUILD)/libclump.a

all:  $(STATIC) $(SHARED)

$(BUILD):
	mkdir $(BUILD)

$(BUILD)/hcodec.o: $(SRC)/hcodec.h

$(BUILD)/%.o: $(SRC)/%.c $(SRC)/clump.h
	$(CC) $(CFLAGS) -o $@ -c $<

$(STATIC): $(BUILD) $(OBJS)
	$(AR) csr $(STATIC) $(OBJS)

$(SHARED): $(BUILD) $(OBJS)
	$(CC) $(CFLAGS) -shared -o $(SHARED) $(OBJS)

install: $(STATIC)
	cp $(SRC)/clump.h /usr/local/include/
	cp $(STATIC) /usr/local/lib64/

clean:
	rm -rf $(BUILD)
