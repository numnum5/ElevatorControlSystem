# Variables
CC = gcc              # Compiler
CFLAGS = -Wall -g     # Compiler flags for warnings and debugging
OBJ = car.o controller.o call.o internal.o safety.o  # Object files

# Default target: build all components
all: car controller call internal safety

# Rule for building the car component
car: car.o
	$(CC) $(CFLAGS) -o car car.o

# Rule for building the controller component
controller: controller.o
	$(CC) $(CFLAGS) -o controller controller.o

# Rule for building the call component
call: call.o
	$(CC) $(CFLAGS) -o call call.o

# Rule for building the internal controls component
internal: internal.o
	$(CC) $(CFLAGS) -o internal internal.o

# Rule for building the safety critical component
safety: safety.o
	$(CC) $(CFLAGS) -o safety safety.o

# Compile source files to object files
car.o: car.c
	$(CC) $(CFLAGS) -c car.c

controller.o: controller.c
	$(CC) $(CFLAGS) -c controller.c

call.o: call.c
	$(CC) $(CFLAGS) -c call.c

internal.o: internal.c
	$(CC) $(CFLAGS) -c internal.c

safety.o: safety.c
	$(CC) $(CFLAGS) -c safety.c

# Clean up
clean:
	rm -f *.o car controller call internal safety
