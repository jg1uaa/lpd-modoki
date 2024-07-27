ALL = lpd-modoki

all:	$(ALL)

lpd-modoki: lpd-modoki.c
	$(CC) -O2 -Wall -Wno-pointer-sign $< -o $@

clean:
	rm -f $(ALL)
