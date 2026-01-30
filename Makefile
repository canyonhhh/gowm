CC      = cc
CFLAGS  = -Wall -Wextra -O2
LDFLAGS = -lX11

TARGET  = gowm
SRC     = gowm.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean install

install: all
	install -m 0755 $(TARGET) /usr/local/bin
