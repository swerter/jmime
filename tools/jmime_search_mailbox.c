#include <stdlib.h>
#include <glib/gprintf.h>
#include "../src/jmime.h"

int main(int argc, char *argv[]) {

  if (argc < 3) {
    g_printerr ("usage: %s <Mailbox-Path> \"<Query-String>\"\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  jmime_init();
  gchar **result = jmime_search_mailbox(argv[1], argv[2], 1000);

  int i = 0;
  while (result[i])
    g_printf("%s\n", result[i++]);

  jmime_shutdown();

  return 0;
}


