#include <stdlib.h>
#include <glib/gprintf.h>
#include <gmime/gmime.h>
#include "../src/jmime.h"

int main(int argc, char *argv[]) {

  if (argc < 2) {
    g_printerr ("usage: %s <MIME-Message-path>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  jmime_init();

  char *json_message = jmime_get_json(argv[1], TRUE);
  if (!json_message)
    exit(EXIT_FAILURE);

  setbuf(stdout, NULL);
  g_printf("%s\n", json_message);
  g_free(json_message);

  jmime_shutdown();

  return 0;
}
