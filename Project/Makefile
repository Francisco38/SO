CC=gcc
FLAGS=-Wall -pthread
PROG=proj
OBJS=projecto.o


all:			${PROG}

clean:
					rm ${OBJS} *~ ${PROG}

${PROG}:	${OBJS}
					${CC} ${FLAGS} ${OBJS} -o $@

.c.o:
					${CC} ${FLAGS} $< -c -o $@

###################################
projecto.o: projecto.c
