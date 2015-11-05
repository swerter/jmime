#include <glib/gprintf.h>
#include <gmime/gmime.h>
#include "../src/jmime.h"

int main(int argc, char *argv[]) {

  if (argc < 2) {
    g_printerr ("usage: %s <MIME-Message-path>\n", argv[0]);
    return 1;
  }

  jmime_init();

  GMimeMessage *message = jmime_message_from_path(argv[1]);
  if (!message)
    return 1;

  char *json_message = jmime_message_to_json(message, TRUE);
  g_object_unref(message);

  if (!json_message)
    return 1;

  setbuf(stdout, NULL);
  g_printf("%s\n", json_message);
  g_free(json_message);

  jmime_shutdown();

  return 0;
}
