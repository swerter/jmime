#include <glib/gprintf.h>
#include <gmime/gmime.h>
#include <locale.h>
#include "../jmime.h"

int main (int argc, char *argv[]) {

  if (argc < 2) {
    g_printerr ("usage: %s <MIME-Message-path>\n", argv[0]);
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


  // We get JSON like this
  char *json_message = mime_message_to_json(message);
  if (json_message) {
    setbuf(stdout, NULL);
    g_printf("%s\n", json_message);
    g_free(json_message);
  }


  // // We get attachment like this
  // GByteArray *attachment = get_inline_content(message, "profilephoto", 6);

  // if (attachment) {
  //   FILE *fout = fopen("image.jpg", "wb");
  //   if (fout) {
  //     fwrite(attachment->data, attachment->len, 1, fout);
  //     fclose(fout);
  //   }
  //   g_byte_array_free(attachment, TRUE);
  // }

  g_object_unref(message);
  g_mime_shutdown ();

  return 0;
}
