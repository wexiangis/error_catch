
CFLAGS += -Wall
# CFLAGS += -rdynamic
# CFLAGS += -L./ -lext -DLIBEXT

target: lib
	@gcc -o out main.c ecapi.c $(CFLAGS)

lib:
	@gcc -shared -fPIC -o libext.so libext.c $(CFLAGS)

clean:
	@rm -rf out* *.so
