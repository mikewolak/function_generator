# Project name
PROJECT = waveform_generator

# Compiler
CC = gcc

# Directories
SRCDIR = src
INCDIR = include
OBJDIR = obj
BINDIR = bin

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Get compiler and linker flags from pkg-config
PKGCONFIG_DEPS = gtk+-3.0 gl portaudio-2.0 sndfile
CFLAGS += $(shell pkg-config --cflags $(PKGCONFIG_DEPS))
LIBS += $(shell pkg-config --libs $(PKGCONFIG_DEPS))

# Additional compiler flags
CFLAGS += -Wall -Wextra -O2 -g
CFLAGS += -I$(INCDIR)

# Additional linker flags
LIBS += -lm -lpthread

# Default target
all: directories $(BINDIR)/$(PROJECT)

# Create necessary directories
directories:
	@mkdir -p $(OBJDIR)
	@mkdir -p $(BINDIR)

# Compile source files
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link the executable
$(BINDIR)/$(PROJECT): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LIBS)

# Clean build files
clean:
	rm -rf $(OBJDIR) $(BINDIR)

# Install target (optional)
install: $(BINDIR)/$(PROJECT)
	install -m 755 $(BINDIR)/$(PROJECT) /usr/local/bin/

# Uninstall target (optional)
uninstall:
	rm -f /usr/local/bin/$(PROJECT)

.PHONY: all directories clean install uninstall
