.PHONY: all clean

CC = gcc

CFLAGS = -g -Wall -Wextra -Wpedantic

NAME = sarch
SRC = sarch.c 
OBJ = $(SRC:.c=.o)

CLN = *.mk *.o $(NAME)

all: $(NAME)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(NAME): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

deps.mk: $(SRC)
	$(CC) -MM $^ > $@

ifneq (clean, $(MAKECMDGOALS))
-include deps.mk
endif

clean:
	rm -f $(CLN)
