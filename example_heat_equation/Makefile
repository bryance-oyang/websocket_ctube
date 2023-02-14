FINAL=a.out
srcdir=

SHELL=/bin/sh
CC=gcc -pipe -mtune=native -march=native -pthread
OFLAGS=-Ofast -flto -ffat-lto-objects
CFLAGS+=-std=gnu17 -Wall
LDFLAGS=-lc -lm
PFLAGS=-ggdb3
DFLAGS=$(CFLAGS) -MM -MT

# debug and sanitize code
CDEBUG=-ggdb3
SANITIZE=-ggdb3 -fno-omit-frame-pointer -fno-common -fsanitize=address -fsanitize=pointer-compare -fsanitize=pointer-subtract -fsanitize=leak -fsanitize=undefined -fsanitize-address-use-after-scope
TSANITIZE=-ggdb3 -fsanitize=thread

# env vars for sanitizers
# https://github.com/google/sanitizers/wiki/SanitizerCommonFlags
# https://github.com/google/sanitizers/wiki/AddressSanitizerFlags
# https://developers.redhat.com/blog/2021/05/05/memory-error-checking-in-c-and-c-comparing-sanitizers-and-valgrind
# export ASAN_OPTIONS=check_initialization_order=true:detect_stack_use_after_return=true:print_stats=true:atexit=true

# Fix false memory leak reporting when using glib2
#export G_SLICE=always-malloc G_DEBUG=gc-friendly

ifdef srcdir
VPATH=$(srcdir)
SRCS=$(wildcard $(srcdir)/*.c)
HDRS=$(wildcard $(srcdir)/*.h)
CFLAGS+=-I. -I$(srcdir)
else
SRCS=$(wildcard *.c)
HDRS=$(wildcard *.h)
endif
OBJS=$(SRCS:.c=.o)
DEPS=$(SRCS:.c=.d)
ASMS=$(SRCS:.c=.s)

ifeq ($(MAKECMDGOALS), debug)
CFLAGS+=$(CDEBUG)
LDFLAGS+=$(CDEBUG)
else ifeq ($(MAKECMDGOALS), sanitize)
CFLAGS+=$(SANITIZE)
LDFLAGS+=$(SANITIZE)
else ifeq ($(MAKECMDGOALS), tsanitize)
CFLAGS+=$(TSANITIZE)
LDFLAGS+=$(TSANITIZE)
else ifeq ($(MAKECMDGOALS), profile)
CFLAGS+=$(OFLAGS) $(PFLAGS)
LDFLAGS+=$(OFLAGS) $(PFLAGS)
else
CFLAGS+=$(OFLAGS)
LDFLAGS+=$(OFLAGS)
endif

ifneq ($(MAKECMDGOALS), clean)
-include $(DEPS)
endif

.DEFAULT_GOAL=all
.PHONY: all
all: $(DEPS) $(FINAL)
	@echo done

.PHONY: clean
clean:
	-rm -f $(OBJS) $(ASMS) $(DEPS) $(HDRS:.h=.h.gch) $(FINAL) *.out
	@echo done

.PHONY: profile
profile: $(DEPS) $(FINAL)
	@echo done

.PHONY: debug
debug: $(DEPS) $(FINAL)
	@echo done

.PHONY: sanitize
sanitize: $(DEPS) $(FINAL)
	@echo done

.PHONY: tsanitize
tsanitize: $(DEPS) $(FINAL)
	@echo done

.PHONY: asm
asm: $(DEPS) $(ASMS)
	@echo done

.PHONY: depend
depend: $(DEPS)
	@echo done

.PHONY: headers
headers: $(HDRS:.h=.h.gch)
	@echo done

.PHONY: dox
dox: Doxyfile
	doxygen Doxyfile

$(FINAL): $(OBJS) ws_ctube.a
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

%.s: %.c
	$(CC) -S -fverbose-asm $(CFLAGS) -o $@ $<

%.d: %.c
	$(CC) $(DFLAGS) $*.o $< >$*.d

%.h.gch: %.h
	$(CC) -c $(CFLAGS) -o $@ $<

Doxyfile:
	doxygen -g
