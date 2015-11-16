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

	gcc $(CFLAGS) -c tools/jmime_index_message.c  -o _build/jmime_index_message.o `pkg-config --cflags glib-2.0`
	gcc $(CFLAGS) -c tools/jmime_index_mailbox.c  -o _build/jmime_index_mailbox.o `pkg-config --cflags glib-2.0`
	gcc $(CFLAGS) -c tools/jmime_search_mailbox.c 				-o _build/jmime_search_mailbox.o        `pkg-config --cflags glib-2.0`
	gcc $(CFLAGS) -c tools/jmime_get_part.c 		-o _build/jmime_get_part.o    `pkg-config --cflags glib-2.0`
	gcc $(CFLAGS) -c tools/jmime_get_json.c 					-o _build/jmime_get_json.o          `pkg-config --cflags glib-2.0`

	g++ $(CPPFLAGS) `pkg-config --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --libs` _build/parson.o _build/jxapian.o _build/jmime.o _build/jmime_index_mailbox.o 	-o _build/jmime_index_mailbox
	g++ $(CPPFLAGS) `pkg-config --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --libs` _build/parson.o _build/jxapian.o _build/jmime.o _build/jmime_index_message.o 	-o _build/jmime_index_message
	g++ $(CPPFLAGS) `pkg-config --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --libs` _build/parson.o _build/jxapian.o _build/jmime.o _build/jmime_search_mailbox.o -o _build/jmime_search_mailbox
	g++ $(CPPFLAGS) `pkg-config --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --libs` _build/parson.o _build/jxapian.o _build/jmime.o _build/jmime_get_json.o 			-o _build/jmime_get_json
	g++ $(CPPFLAGS) `pkg-config --libs glib-2.0 gmime-2.6 gumbo` `xapian-config --libs` _build/parson.o _build/jxapian.o _build/jmime.o _build/jmime_get_part.o 		  -o _build/jmime_get_part

check-cc:
	@hash clang 2>/dev/null || \
	hash gcc 2>/dev/null || ( \
	echo '`clang` or `gcc` seem not to be installed or in your PATH.' && \
	echo 'Maybe you need to install one of it first.' && \
	exit 1)

clean:
	rm -rf _build
