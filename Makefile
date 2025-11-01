# --- Compiler and flags ---
CC      = gcc
LIBS    = 
CFLAGS  = -g -Wall -Wextra -Werror -Iinclude -MMD -MP

ifeq ($(MODE),debug)
	CFLAGS += -g -O0
	OUTDIR := build/debug
else
	OUTDIR := build/release
endif

# --- Source and object files ---
SRC     = main.c $(wildcard src/*.c)
OBJ     = $(OUTDIR)/main.o $(patsubst src/%.c, $(OUTDIR)/%.o, $(wildcard src/*.c))
TARGET  = $(OUTDIR)/ssn-1
DEP     = $(OBJ:.o=.d)

# --- Default rule ---
all: $(TARGET)

# --- Link rule ---
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@echo "Build complete: $(TARGET)"

# --- Compile rules ---
$(OUTDIR)/%.o: src/%.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/%.o: %.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Directory rule ---
$(OUTDIR):
	mkdir -p $(OUTDIR)

# --- Cleanup rule ---
clean:
	rm -rf build

# --- Dependencies ---
-include $(DEP)

.PHONY: all clean
