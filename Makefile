TARGET = ctx_switch
CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c17 -Isrc/asclib/include -g -m64 -MMD -MP

SRC_DIR = src
OBJ_DIR = build/obj
BIN_DIR = build/bin

# --- Source Discovery ---
# This looks for ALL .c files in your src directory automatically.
# If you prefer manual listing, ensure the path matches the folder exactly.
C_FILES := $(shell find $(SRC_DIR) -name "*.c")
ASM_FILES := $(shell find $(SRC_DIR) -name "*.asm")

# --- Object Mapping ---
# This takes src/folder/file.c and turns it into build/obj/folder/file.o
OBJ_C   := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(C_FILES))
OBJ_ASM := $(patsubst $(SRC_DIR)/%.asm, $(OBJ_DIR)/%.o, $(ASM_FILES))
OBJ     := $(OBJ_C) $(OBJ_ASM)
DEPS    := $(OBJ:.o=.d)

BIN = $(BIN_DIR)/$(TARGET)

# --- Rules ---

all: $(BIN)

$(BIN): $(OBJ) | $(BIN_DIR)
	@echo "Linking -> $@"
	$(CC) $(CFLAGS) -o $@ $(OBJ)

# Rule for C files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling -> $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Rule for ASM files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "Assembling -> $<"
	nasm -f elf64 $< -o $@

-include $(DEPS)

$(BIN_DIR):
	@mkdir -p $@

clean:
	rm -rf build

.PHONY: all clean