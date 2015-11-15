#include <stdlib.h>
#include <glib/gprintf.h>
#include "../src/jmime.h"

int main (int argc, char *argv[]) {

  if (argc < 4) {
    g_printerr ("usage: %s message_file part_id out_file\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  jmime_init();


  int part_id = g_ascii_strtoll(argv[2], NULL, 10);
  if (!part_id) {
    g_printerr("part_id could not be parsed\r\n");
    exit(EXIT_FAILURE);
  }

  GByteArray *attachment = jmime_get_part_data(argv[1], part_id);
  if (!attachment)
    exit(EXIT_FAILURE);

  FILE *fout = fopen(argv[4], "wb");
  if (!fout) {
    g_printerr("file could not be opened for writing: %s\r\n", argv[4]);
    exit(EXIT_FAILURE);
  }

  g_printf("Written %d bytes to file %s\r\n", attachment->len, argv[4]);
  fwrite(attachment->data, attachment->len, 1, fout);
  fclose(fout);

  g_byte_array_free(attachment, TRUE);

  jmime_shutdown();

  return 0;
}
