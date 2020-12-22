CC := gcc
SRCD := src
BLDD := build
BIND := bin
INCD := include
TESTD := tests

CFLAGS := -O2 -Wall -Werror
SERVER_BIN := server
CLIENT_BIN := client

INC := -I $(INCD)

C_SRC_FILES := $(SRCD)/io.c $(SRCD)/server.c $(SRCD)/jobs.c $(SRCD)/parse.c $(SRCD)/proto.c $(SRCD)/client.c $(SRCD)/conn.c
C_OBJ_FILES := $(patsubst $(SRCD)/%,$(BLDD)/%,$(C_SRC_FILES:.c=.o))
S_SRC_FILES := $(SRCD)/io.c $(SRCD)/server.c $(SRCD)/jobs.c $(SRCD)/parse.c $(SRCD)/conn.c $(SRCD)/proto.c $(SRCD)/client.c
S_OBJ_FILES := $(patsubst $(SRCD)/%,$(BLDD)/%,$(S_SRC_FILES:.c=.o))

HDR_FILES := $(shell find $(INCD) -type f -name *.h)
TST_FILES := $(shell find $(TSTD) -type f -name test*.sh)

.PHONY: clean all setup

all: setup $(BIND)/$(SERVER_BIN) $(BIND)/$(CLIENT_BIN)

debug: CFLAGS += -g
debug: all

setup: $(BIND) $(BLDD)
$(BIND):
	mkdir -p $(BIND)
$(BLDD):
	mkdir -p $(BLDD)

$(BIND)/$(SERVER_BIN): $(BLDD)/server_main.o $(S_OBJ_FILES) 
	$(CC) $^ -o $@ 

$(BIND)/$(CLIENT_BIN): $(BLDD)/client_main.o $(C_OBJ_FILES)
	$(CC) $^ -o $@

$(BLDD)/%.o: $(SRCD)/%.c $(HDR_FILES)
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

tests: $(BIND)/$(SERVER_BIN) $(BIND)/$(CLIENT_BIN)
	@for x in tests/test*.sh; do sh $$x; done

clean:
	rm -rf $(BLDD) $(BIND)
	rm -rf *.out *.err

doc:
	pandoc README.md -V geometry:margin=1in -V 'mainfont:Times New Roman' -V fontsize=11pt -o design.pdf
