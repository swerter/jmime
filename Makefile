CC?=clang
NOOUT=2>&1 >/dev/null
CFLAGS=-O3 -fPIC -Wall -g
CPPFLAGS=-O3 -fPIC -Wall -g
LDFLAGS=

tools: version check-cc jmime-tools

version:
	@cat VERSION

jmime-tools:
	@mkdir -p _build $(NOOUT)

	gcc $(CFLAGS)   -c src/parson/parson.c 	-o _build/parson.o
	g++ $(CPPFLAGS) -c src/jxapian.cc 			-o _build/jxapian.o `xapian-config --cxxflags`
	gcc $(CFLAGS)   -c src/jmime.c 					-o _build/jmime.o   `pkg-config --cflags glib-2.0 gmime-2.6 gumbo`

	gcc $(CFLAGS) -c tools/jmime_index_maildir.c  -o _build/jmime_index_maildir.o `pkg-config --cflags glib-2.0`
	gcc $(CFLAGS) -c tools/jmime_index_message.c  -o _build/jmime_index_message.o `pkg-config --cflags glib-2.0`
	gcc $(CFLAGS) -c tools/jmime_search.c 				-o _build/jmime_search.o        `pkg-config --cflags glib-2.0`
	gcc $(CFLAGS) -c tools/jmime_attachment.c 		-o _build/jmime_attachment.o    `pkg-config --cflags glib-2.0`
	gcc $(CFLAGS) -c tools/jmime_json.c 					-o _build/jmime_json.o          `pkg-config --cflags glib-2.0`

	g++ $(CPPFLAGS) `pkg-config --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --libs` _build/parson.o _build/jxapian.o _build/jmime.o _build/jmime_index_maildir.o 	-o _build/jmime_index_maildir
	g++ $(CPPFLAGS) `pkg-config --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --libs` _build/parson.o _build/jxapian.o _build/jmime.o _build/jmime_index_message.o 	-o _build/jmime_index_message
	g++ $(CPPFLAGS) `pkg-config --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --libs` _build/parson.o _build/jxapian.o _build/jmime.o _build/jmime_search.o 				-o _build/jmime_search
	g++ $(CPPFLAGS) `pkg-config --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --libs` _build/parson.o _build/jxapian.o _build/jmime.o _build/jmime_json.o 					-o _build/jmime_json
	g++ $(CPPFLAGS) `pkg-config --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --libs` _build/parson.o _build/jxapian.o _build/jmime.o _build/jmime_attachment.o 		-o _build/jmime_attachment

check-cc:
	@hash clang 2>/dev/null || \
	hash gcc 2>/dev/null || ( \
	echo '`clang` or `gcc` seem not to be installed or in your PATH.' && \
	echo 'Maybe you need to install one of it first.' && \
	exit 1)

clean:
	rm -rf _build
