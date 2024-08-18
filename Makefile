ALL = lpd-modoki

all:	$(ALL)

lpd-modoki: lpd-modoki.c
	$(CC) -O2 -Wall $< -o $@

clean:
	rm -f $(ALL)
