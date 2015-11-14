CC?=clang
NOOUT=2>&1 >/dev/null
CFLAGS=-O3 -fPIC -std=c99 -Wall -g
CPPFLAGS=-O3 -fPIC -Wall -g
LDFLAGS=

examples: version check-cc jmime-examples

version:
	@cat VERSION

jmime-examples:
	@mkdir -p _build $(NOOUT)

	gcc $(CFLAGS)   -c src/parson/parson.c -o _build/parson.o
	gcc $(CFLAGS)   `pkg-config --cflags glib-2.0 gmime-2.6 gumbo` -c src/jmime.c -o _build/jmime.o
	g++ $(CPPFLAGS) -c src/xapian.cc -o _build/xapian.o
	gcc $(CFLAGS)   `pkg-config --cflags glib-2.0` -c examples/jmime_indexer.c -o _build/jmime_indexer.o
	gcc $(CFLAGS)   `pkg-config --cflags glib-2.0` -c examples/jmime_search.c -o _build/jmime_search.o
	gcc $(CFLAGS)   `pkg-config --cflags glib-2.0` -c examples/jmime_attachment.c -o _build/jmime_attachment.o
	gcc $(CFLAGS)   `pkg-config --cflags glib-2.0` -c examples/jmime_json.c -o _build/jmime_json.o

	g++ $(CPPFLAGS) `pkg-config --cflags --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --cxxflags --libs` _build/parson.o _build/xapian.o _build/jmime.o _build/jmime_indexer.o -o _build/jmime_indexer
	g++ $(CPPFLAGS) `pkg-config --cflags --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --cxxflags --libs` _build/parson.o _build/xapian.o _build/jmime.o _build/jmime_search.o -o _build/jmime_search
	g++ $(CPPFLAGS) `pkg-config --cflags --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --cxxflags --libs` _build/parson.o _build/xapian.o _build/jmime.o _build/jmime_json.o -o _build/jmime_json
	g++ $(CPPFLAGS) `pkg-config --cflags --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --cxxflags --libs` _build/parson.o _build/xapian.o _build/jmime.o _build/jmime_attachment.o -o _build/jmime_attachment

check-cc:
	@hash clang 2>/dev/null || \
	hash gcc 2>/dev/null || ( \
	echo '`clang` or `gcc` seem not to be installed or in your PATH.' && \
	echo 'Maybe you need to install one of it first.' && \
	exit 1)

clean:
	rm -rf _build
