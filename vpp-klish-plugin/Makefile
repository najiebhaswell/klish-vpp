CC = gcc
CFLAGS = -Wall -Wextra -fPIC -g -O2
LDFLAGS = -shared
INCLUDES = -I/usr/local/include
LIBS = -L/usr/local/lib -lklish -lfaux
TARGET = libklish-plugin-vpp.so

all: $(TARGET)

$(TARGET): src/vpp_plugin.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

src/vpp_plugin.o: src/vpp_plugin.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f src/*.o $(TARGET)

install: $(TARGET)
	sudo install -m 755 $(TARGET) /usr/local/lib/
	sudo ldconfig
