#include <glib/gprintf.h>
#include <gmime/gmime.h>
#include "../jmime.h"

int main (int argc, char *argv[]) {

  if (argc < 2) {
    g_printerr ("usage: %s <MIME-Message-path>\n", argv[0]);
    return 1;
  }

  g_mime_init(GMIME_ENABLE_RFC2047_WORKAROUNDS);

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

  g_mime_shutdown ();

  return 0;
}
