TARGET = micarrayd
CC = gcc

SOURCELIST = $(wildcard src/*.c)
OBJECTLIST = \
	$(filter %.o, $(patsubst \
		%.c, \
		%.o, \
		$(SOURCELIST) \
	))

CFLAGS = \
	-Iinclude \
	-c \
	-Wall \
	-std=gnu99 \
	-O2

LDFLAGS = -lpulse-simple -lpulse -lcjson -lspeexdsp -lrnnoise

all: $(TARGET)

$(TARGET): $(OBJECTLIST)
	$(CC) $(OBJECTLIST) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm $(OBJECTLIST) $(TARGET)

install:
	cp $(TARGET) /usr/local/bin/
	cp $(TARGET).service /etc/systemd/system/

uninstall:
	rm /usr/local/bin/$(TARGET)
	rm /etc/systemd/system/$(TARGET).service
