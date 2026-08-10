# FastNote C/RayGUI Edition — Build system

CC = gcc
CFLAGS = -Wall -Wextra $(shell pkg-config --cflags raylib 2>/dev/null || echo "-I/usr/include")
LDFLAGS = -lraylib -lm -lpthread -ldl -lrt

SRCDIR = src
BUILDDIR = build
TARGET = fastnote-c-raygui

SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

test: all
	./$(TARGET) --version
	./$(TARGET) --headless --selftest
