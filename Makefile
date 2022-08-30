GCC = gcc

CFLAGS += -Wall
# CFLAGS += -L./ -lext -DLIBEXT
# CFLAGS += -rdynamic
# CFLAGS += -g

target: lib
	@$(GCC) -o out main.cpp ecapi.cpp $(CFLAGS)

lib:
	@$(GCC) -shared -fPIC -o libext.so libext.c $(CFLAGS)

clean:
	@rm -rf out* *.so
