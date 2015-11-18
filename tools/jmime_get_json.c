#include <stdlib.h>
#include <glib/gprintf.h>
#include "../src/jmime.h"

int main(int argc, char *argv[]) {

  if (argc < 2) {
    g_printerr ("usage: %s <MIME-Message-path>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  jmime_init();
  if (argc == 2) {

  GString *json_message = NULL;
  json_message = jmime_get_json(argv[1], TRUE);
  if (!json_message)
    exit(EXIT_FAILURE);

  setbuf(stdout, NULL);
  g_printf("%s\n", json_message->str);
  g_string_free(json_message, TRUE);
  } else {
    g_printf("[");
    int x;
    for ( x = 1; x < argc; x++ ) {
      /* printf("File: %s\n", argv[x]); */
      GString *json_message = NULL;
      json_message = jmime_get_json(argv[1], TRUE);
      if (!json_message)
        exit(EXIT_FAILURE);

      setbuf(stdout, NULL);
      g_printf("%s\n", json_message->str);
      g_string_free(json_message, TRUE);
    }
    g_printf("]");
  }

  jmime_shutdown();

  return 0;
}
