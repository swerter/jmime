CC?=clang
NOOUT=2>&1 >/dev/null
INCLUDES=-I`pkg-config --cflags --libs gmime-2.6 gumbo`
OPTFLAGS?=-O3 -fPIC -std=c99 -Wall -g
CFLAGS=$(OPTFLAGS) $(INCLUDES)
LDFLAGS=

examples: version check-cc jmime-examples

version:
	@cat VERSION

jmime-examples:
	@mkdir -p examples/bin $(NOOUT)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTIONS)  src/parson/parson.c src/*.c  examples/jmime_json.c        -o examples/bin/jmime_json
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTIONS)  src/parson/parson.c src/*.c  examples/jmime_attachment.c  -o examples/bin/jmime_attachment
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTFLAGS) `pkg-config --cflags --libs gmime-2.6 gumbo` src/utils.c  src/sanitizer.c  examples/sanitizer.c  -o examples/bin/sanitizer
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTFLAGS) `pkg-config --cflags --libs gmime-2.6 gumbo` src/utils.c  src/textizer.c   examples/textizer.c   -o examples/bin/textizer

check-cc:
	@hash clang 2>/dev/null || \
	hash gcc 2>/dev/null || ( \
	echo '`clang` or `gcc` seem not to be installed or in your PATH.' && \
	echo 'Maybe you need to install one of it first.' && \
	exit 1)

clean:
	rm -rf examples/bin
