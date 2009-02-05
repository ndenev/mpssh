CC = gcc
LD = gcc
CFLAGS = -Wall
LDFLAGS =
RM = /bin/rm -f
SSHPATH = `which ssh`

LIBS =

OBJS = pslot.o host.o mpssh.o
PROG = mpssh

all: $(PROG)

$(PROG): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) $(LIBS) $(FLAGS) -o $(PROG)

%.o: %.c
	$(CC) $(CFLAGS) $(FLAGS) -c $<

clean:
	$(RM) $(PROG) $(OBJS) $(PROG).core

install: all
	strip $(PROG)
	install -m 751 -o root $(PROG) /usr/local/bin

