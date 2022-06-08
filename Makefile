
CFLAGS += -Wall
# CFLAGS += -L./ -lext -DLIBEXT
# CFLAGS += -rdynamic

target: lib
	@gcc -o out main.c ecapi.c $(CFLAGS)

lib:
	@gcc -shared -fPIC -o libext.so libext.c $(CFLAGS)

clean:
	@rm -rf out* *.so
