CC      = cc
CFLAGS  = -Wall -Wextra -O2
CPPFLAGS += $(shell pkg-config --cflags x11 xinerama xrandr xft)
LDLIBS += $(shell pkg-config --libs x11 xinerama xrandr xft)

TARGET  = gowm
SRC     = gowm.c workspace.c monitors.c ui.c
HEADERS = workspace.h monitors.h ui.h config.h bindings.h
STATE_TEST = tests/test-workspace

all: $(TARGET)

$(TARGET): $(SRC) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS) $(LDLIBS)

$(STATE_TEST): tests/test_workspace.c workspace.c workspace.h config.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_workspace.c workspace.c -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(STATE_TEST)

test-state: $(STATE_TEST)
	./$(STATE_TEST)

test-x11: $(TARGET)
	./tests/run-overview.sh ./$(TARGET)

test: test-state test-x11

.PHONY: all clean install test test-state test-x11

install: all
	install -m 0755 $(TARGET) /usr/local/bin
