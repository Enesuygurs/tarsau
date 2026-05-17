.PHONY: all clean

all:
	gcc tarsau.c -o tarsau

clean:
	rm -f tarsau
