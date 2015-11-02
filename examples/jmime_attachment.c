#include <glib/gprintf.h>
#include <gmime/gmime.h>
#include "../jmime.h"

int main (int argc, char *argv[]) {

  if (argc < 4) {
    g_printerr ("usage: %s <MIME-Message-path> <filename> <part-Id>\n", argv[0]);
    return 1;
  }

  jmime_init();

  GMimeMessage *message = jmime_message_from_path(argv[1]);
  if (!message)
    return 1;

  int part_id = g_ascii_strtoll(argv[3], NULL, 10);
  if (!part_id) {
    g_printerr("part-Id could not be parsed\r\n");
    return 1;
  }

  GByteArray *attachment = jmime_message_get_attachment_data(message, argv[2], part_id);
  g_object_unref(message);

  if (!attachment)
    return 1;

  FILE *fout = fopen(argv[2], "wb");
  if (!fout) {
    g_printerr("file could not be opened for writting: %s\r\n", argv[2]);
    return 1;
  }

  fwrite(attachment->data, attachment->len, 1, fout);
  fclose(fout);

  g_byte_array_free(attachment, TRUE);

  jmime_shutdown();

  return 0;
}
