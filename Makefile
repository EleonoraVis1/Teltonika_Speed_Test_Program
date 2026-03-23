CC = gcc
CFLAGS = -Wall -O2
LIBS = -lcurl -lm

SRC = program.c cJSON.c getopt.c
OBJ = $(SRC:.c=.o)

TARGET = program

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)