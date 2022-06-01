
CFLAGS += -Wall
# CFLAGS += -rdynamic
# CFLAGS += -fsanitize=address

target: lib
	@gcc -o out main.c ecapi.c -L./ -lext $(CFLAGS)

lib:
	@gcc -shared -fPIC -o libext.so libext.c $(CFLAGS)

clean:
	@rm -rf out* *.so
