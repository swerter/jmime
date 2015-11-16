#include <stdlib.h>
#include <glib/gprintf.h>
#include "../src/jmime.h"

int main(int argc, char *argv[]) {

  if (argc < 3) {
    g_printerr ("usage: %s <Index-Path> <Mailbox-Path>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  jmime_init();
  jmime_index_message(argv[1], argv[2]);
  jmime_shutdown();

  return 0;
}


