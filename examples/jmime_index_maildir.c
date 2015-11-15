#include <stdlib.h>
#include <glib/gprintf.h>
#include "../src/jmime.h"

int main(int argc, char *argv[]) {

  if (argc < 2) {
    g_printerr ("usage: %s <Maildir-Path>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  jmime_init();
  jmime_index_maildir(argv[1]);
  jmime_shutdown();

  return 0;
}


