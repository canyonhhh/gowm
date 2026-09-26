CC      = cc
CFLAGS  = -Wall -Wextra -O2
CPPFLAGS += $(shell pkg-config --cflags xft)
LDFLAGS = -lX11 -lXinerama -lXrandr
LDLIBS += $(shell pkg-config --libs xft)

TARGET  = gowm
SRC     = gowm.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)

test:
	./tests/run-overview.sh

.PHONY: all clean install test

install: all
	install -m 0755 $(TARGET) /usr/local/bin
