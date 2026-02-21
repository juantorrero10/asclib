TARGET = asclib_demo
LIB_NAME = libasclib.a
CC = gcc

CFLAGS_RELEASE = -Wall -Wextra -Werror -std=c17 -Isrc/asclib/include -O2 -m64 -MMD -MP
CFLAGS_DEBUG   = -Wall -Wextra -std=c17 -Isrc/asclib/include -g -O0 -MMD -MP
CFLAGS = $(CFLAGS_RELEASE)

SRC_DIR = src
OBJ_DIR = build/obj
BIN_DIR = build/bin
LIB_DIR = build/lib

LIB_C := $(shell find $(SRC_DIR) -name "*.c" ! -path "$(SRC_DIR)/demo/*")
DEMO_C := $(shell find $(SRC_DIR)/demo -name "*.c")
ASM_FILES := $(shell find $(SRC_DIR) -name "*.asm")

OBJ_LIB := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(LIB_C)) \
           $(patsubst $(SRC_DIR)/%.asm,$(OBJ_DIR)/%.o,$(ASM_FILES))
OBJ_DEMO := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(DEMO_C))

DEPS := $(OBJ_LIB:.o=.d) $(OBJ_DEMO:.o=.d)

BIN := $(BIN_DIR)/$(TARGET)
STATIC_LIB := $(LIB_DIR)/$(LIB_NAME)

all: $(STATIC_LIB)

$(STATIC_LIB): $(OBJ_LIB) | $(LIB_DIR)
	@echo "Archiving -> $@"
	ar rcs $@ $^

$(BIN): $(OBJ_LIB) $(OBJ_DEMO) | $(BIN_DIR)
	@echo "Linking -> $@"
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling -> $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "Assembling -> $<"
	nasm -f elf64 $< -o $@

-include $(DEPS)

$(BIN_DIR) $(LIB_DIR):
	@mkdir -p $@

run: CFLAGS = $(CFLAGS_DEBUG)
run: $(BIN)
	@echo "Running $(TARGET) (Debug Mode)..."
	@./$(BIN)

clean:
	rm -rf build

.PHONY: all clean run
