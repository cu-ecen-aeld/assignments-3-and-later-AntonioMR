# Compiler target
TARGET	= writer

# Folders
SRC_DIR		= .
INC_DIR		= .
OBJ_DIR		= ./obj
BIN_DIR		= .

# Preprocessor DEFINITIONS
CDEFS	?= 
# Compiler Flags
CFLAGS	?= -O3 -g -Wall
# Linker Flags
LDFLAGS	?= 
# Compiler
CROSS_COMPILE ?= 
CC		?= $(CROSS_COMPILE)gcc

# Headers routes
INCLUDES ?= $(INC_DIR)

# Files
SOURCE_FILES	= writer.c

#########################################################
#                 Makefile objetives                    #
#########################################################
EXECUTABLE_FILES = $(TARGET:%=$(BIN_DIR)/%)
OBJECT_FILES     = $(SOURCE_FILES:%.c=$(OBJ_DIR)/%.o)

build: $(EXECUTABLE_FILES)

clean:
	@rm -r -f $(OBJ_DIR)/
	@rm -f $(BIN_DIR)/$(TARGET)
	@echo Done!

all: $(EXECUTABLE_FILES)

$(EXECUTABLE_FILES): $(OBJECT_FILES)
	@echo Generating $@
	@mkdir -p $(BIN_DIR)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Build successful!"

$(OBJECT_FILES): $(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(INCLUDES) 
	@echo Compiling $<
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDES:%=-I %) -o $@ -c $< 
