TARGET = ctx_switch
CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c17 -Isrc/asclib/include -g -m64


# --- C files ---
SRC = main.c asclib/worker.c asclib/dispatcher.c asclib/task_control.c
OBJ_DIR = build/obj
BIN_DIR = build/bin
SRC_DIR = src
OBJ = $(SRC:%.c=$(OBJ_DIR)/%.o)
BIN = $(BIN_DIR)/$(TARGET)

# --- ASM ----
ASMS = asclib/x86_64/switch.asm


# -- Language objects ----
OBJ_ASM = $(ASMS:%.asm=$(OBJ_DIR)/%.o)
OBJ_C = $(SRC:%.c=$(OBJ_DIR)/%.o)

# -- All objetcs
OBJ = $(OBJ_C) $(OBJ_ASM)


# --- Default target ---
all: $(BIN)

# --- Assemble ---
$(OBJ_DIR)/asclib/x86_64/%.o: $(SRC_DIR)/asclib/x86_64/%.asm
	@mkdir -p $(dir $@)
	@echo "Assembling -> $<"
	nasm -f elf64 $< -o $@

# --- Build final executable ---
$(BIN): $(OBJ) | $(BIN_DIR) $(OBJ_DIR)/asclib $(OBJ_DIR)/asclib/x86_64
	@echo "Linking -> $@"
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIB_FLAG)

# --- Compile each .c file into build/obj ---
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling -> $<"
	$(CC) $(CFLAGS) -c $< -o $@

# --- Create required directories ---
$(OBJ_DIR) $(BIN_DIR):
	@mkdir -p $@

$(OBJ_DIR)/asclib: | $(OBJ_DIR)
	@mkdir -p $@

$(OBJ_DIR)/asclib/x86_64: | $(OBJ_DIR)/asclib
	@mkdir -p $@

# --- Run ---
run: all
	@echo "Running $(TARGET)..."
	@./$(BIN)

# --- Debug ---
debug: all
	gdb ./$(BIN)

# --- Clean ---
clean:
	rm -rf build

.PHONY: all run clean debug