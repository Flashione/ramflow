CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -pthread
TARGET = ramflow-c
SRC = ramflow.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
