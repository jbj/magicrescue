all: magicrescue

magicrescue: magicrescue.c
	cc -Wall -O3 -o $@ $<

clean:
	rm -f magicrescue
