#include <glib/gprintf.h>
#include <gmime/gmime.h>
#include "../src/jmime.h"

int main (int argc, char *argv[]) {

  if (argc < 5) {
    g_printerr ("usage: %s message_file [inline |attachment] name part_id\n", argv[0]);
    return 1;
  }

  jmime_init();

  GMimeMessage *message = jmime_message_from_path(argv[1]);
  if (!message)
    return 1;

  int part_id = g_ascii_strtoll(argv[4], NULL, 10);
  if (!part_id) {
    g_printerr("part_id could not be parsed\r\n");
    return 1;
  }

  GByteArray *attachment = jmime_message_get_attachment_data(message, argv[3], part_id, argv[2]);
  g_object_unref(message);
  if (!attachment)
    return 1;

  FILE *fout = fopen(argv[3], "wb");
  if (!fout) {
    g_printerr("file could not be opened for writing: %s\r\n", argv[3]);
    return 1;
  }

  g_printf("Written %d bytes to file %s\r\n", attachment->len, argv[3]);
  fwrite(attachment->data, attachment->len, 1, fout);
  fclose(fout);

  g_byte_array_free(attachment, TRUE);

  jmime_shutdown();

  return 0;
}
