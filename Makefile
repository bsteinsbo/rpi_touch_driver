DEBUG = -g
CFLAGS = -O ${DEBUG}
LDFLAGS = ${DEBUG}
OBJS = rpi_touch_driver.o
PROG = rpi_touch_driver
LIBS = -ludev

${PROG}: ${OBJS}
	${CC} ${LDFLAGS} -o ${PROG} ${OBJS} ${LIBS}

clean:
	rm -f ${PROG} ${OBJS}

install: ${PROG}
	cp ${PROG} /usr/local/bin && chmod 755 /usr/local/bin/${PROG}

systemd-install:
	cp rpi-touch-driver.service /etc/systemd/system && chmod 644 /etc/systemd/system/rpi-touch-driver.service
	systemctl daemon-reload
	-systemctl enable rpi-touch-driver
	systemctl start rpi-touch-driver
