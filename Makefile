CC?=clang
NOOUT=2>&1 >/dev/null
INCLUDES=-I`pkg-config --cflags --libs gmime-2.6 gumbo`
OPTFLAGS?=-O3 -fPIC -std=c99 -Wall
CFLAGS=$(OPTFLAGS) $(INCLUDES)
LDFLAGS=

all: version check-cc jmime-examples

version:
	@cat VERSION

jmime-examples:
	@mkdir -p examples/bin $(NOOUT)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTIONS) parson/parson.c jmime.c examples/jmime_json.c -o examples/bin/jmime_json
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTIONS) parson/parson.c jmime.c examples/jmime_attachment.c -o examples/bin/jmime_attachment

check-cc:
	@hash clang 2>/dev/null || \
	hash gcc 2>/dev/null || ( \
	echo '`clang` or `gcc` seem not to be installed or in your PATH.' && \
	echo 'Maybe you need to install one of it first.' && \
	exit 1)

clean:
	rm -rf examples/bin
