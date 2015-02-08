CC = gcc
LD = gcc
SSHPATH = `which ssh`
SCPPATH = `which scp`
CFLAGS = -Wall -DSSHPATH=\"$(SSHPATH)\" -DSCPPATH=\"$(SCPPATH)\" -I/opt/local/include -L/opt/local/lib -llua -lm
LDFLAGS = -I/opt/local/include -L/opt/local/lib -llua -lm
RM = /bin/rm -f
BIN=/usr/local/bin

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
	install -m 775 -d $(BIN)
	install -m 751 $(PROG) $(BIN)

test: $(PROG)
	./mpssh -f hosts "uname -a"
