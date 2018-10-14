all: magicrescue subdirs
 
magicrescue: magicrescue.c
	cc -Wall -O3 -o $@ $<

subdirs:
	@make -k -C commands

clean:
	rm -f magicrescue
	
.PHONY: subdirs clean
