
CFLAGS = $(shell pkg-config --cflags check)
CFLAGS += -DUPSGI_PCRE2
LDFLAGS = $(shell pkg-config --libs check)
LDFLAGS += -ldl -lz
LDFLAGS += $(shell xml2-config --libs)
LDFLAGS += $(shell pkg-config --libs openssl)
LDFLAGS += $(shell pcre2-config --libs8)
LDFLAGS += $(shell pkg-config --libs jansson)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	LDFLAGS += -lcap
endif


objects = check_core check_regexp

all: $(objects)

$(objects): %: %.c ../libupsgi.a
	$(CC) $(CFLAGS) -o $@ $< ../libupsgi.a $(LDFLAGS)

test: all
	@for file in $(objects); do ./$$file; done

clean:
	rm -f $(objects)
