UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS
    CFLAGS = -Wall -O2 -g -I/opt/homebrew/include
    LDFLAGS = -L/opt/homebrew/lib
    LIBS = -lcmark -lyaml
else
    # Linux
    CFLAGS = -Wall -O2 -g
    LDFLAGS =
    LIBS = -lcmark -lyaml
endif

CC = cc
TARGET = mdlinks
SRCS = mdlinks.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
