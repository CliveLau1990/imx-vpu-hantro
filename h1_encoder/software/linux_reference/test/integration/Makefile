# this is for versatile platform
CROSS_COMPILE=arm-none-linux-gnueabi-
CC = $(CROSS_COMPILE)gcc
ARCH = -mcpu=arm926ej-s -mtune=arm926ej-s
INC = -I../../../inc
CFLAGS = -O -g -Wall $(ARCH) $(INC)

LIB = ../../libh1enc.a

TESTBENCH=integration_testbench

all: IntegrationTestRegister.o
	$(CC) $(CFLAGS) $^ -o $(TESTBENCH) $(LIB) -lpthread
         
lib:
	make -C ../.. versatile

clean:
	-rm -f *.o $(TESTBENCH)
        
        
