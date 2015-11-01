#include <glib/gprintf.h>
#include <gmime/gmime.h>
#include <locale.h>
#include "../jmime.h"

int main (int argc, char *argv[]) {

  if (argc < 4) {
    g_printerr ("usage: %s <MIME-Message-path> <filename> <partId>\n", argv[0]);
    return 1;
  }

  // Use the locales specified by the environment
  setlocale (LC_ALL, "");

  g_mime_init(GMIME_ENABLE_RFC2047_WORKAROUNDS);

  FILE *msg_file = fopen (argv[1], "r");
  if (!msg_file) {
    g_printerr("cannot open file '%s': %s\r\n", argv[1], g_strerror(errno));
    return 1;
  }

  GMimeMessage *message = mime_email_file_to_message(msg_file);
  if (!message) {
    g_printerr("failed to construct message\r\n");
    return 1;
  }

  int part_id = g_ascii_strtoll(argv[3], NULL, 10);
  if (!part_id) {
    g_printerr("partId could not be parsed\r\n");
    return 1;
  }
  GByteArray *attachment = get_attachment(message, argv[2], part_id);

  if (attachment) {
    FILE *fout = fopen(argv[2], "wb");
    if (fout) {
      fwrite(attachment->data, attachment->len, 1, fout);
      fclose(fout);
    }
    g_byte_array_free(attachment, TRUE);
  }

  g_object_unref(message);
  g_mime_shutdown ();

  return 0;
}
