#include <stdlib.h>
#include <gumbo.h>
#include <glib/gprintf.h>
#include "../src/textizer.c"


int main(int argc, char *argv[]) {

  if (argc < 2) {
    g_printerr ("usage: %s <input-file-path> <output-file-path>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  char* input;
  gsize input_length;

  if (!g_file_get_contents (argv[1], &input, &input_length, NULL)) {
    g_printf("File %s could not be opened!\n", argv[1]);
    exit(EXIT_FAILURE);
  }

  g_print("%lu bytes read from %s\n", input_length, argv[1]);

  GumboOutput* output = gumbo_parse_with_options(&kGumboDefaultOptions, input, input_length);


  GString *textized_content = textize(output->root);
  gumbo_destroy_output(&kGumboDefaultOptions, output);
  // Do not destroy input before destroying Gumbo output, as it's being pointed at by Gumbo
  g_free(input);

  if (!g_file_set_contents(argv[2], textized_content->str, -1, NULL)) {
    g_printf("File %s could not be written!\n", argv[2]);
  }
  g_print("%lu bytes written to %s\n", textized_content->len, argv[2]);
  g_string_free(textized_content, TRUE);

  return 0;
}
