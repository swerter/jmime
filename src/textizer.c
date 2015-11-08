#include <glib.h>
#include <string.h>
#include <gumbo.h>


// The stripping functions from glib do not remove tabs, newlines etc.,
// so we define our owns that remove all whitespace.
static gchar *lstrip(const gchar* text) {
  GRegex *regex = g_regex_new ("\\A\\s+", 0, 0, NULL);
  gchar *stripped =  g_regex_replace_literal(regex, text, -1, 0, "", 0, NULL);
  g_regex_unref(regex);
  return stripped;
}

static gchar *rstrip(const gchar* text) {
  GRegex *regex = g_regex_new ("\\s+$", 0, 0, NULL);
  gchar *stripped =  g_regex_replace_literal(regex, text, -1, 0, "", 0, NULL);
  g_regex_unref(regex);
  return stripped;
}

static gchar *strip(gchar *text) {
  gchar *lstripped = lstrip(text);
  gchar *stripped = rstrip(lstripped);
  g_free(lstripped);
  return stripped;
}

/*
 *
 *
 */
GString *textize(const GumboNode* node) {
  if (node->type == GUMBO_NODE_TEXT) {
    return g_string_new(node->v.text.text);

  } else if (node->type == GUMBO_NODE_ELEMENT &&
             node->v.element.tag != GUMBO_TAG_SCRIPT &&
             node->v.element.tag != GUMBO_TAG_STYLE) {

    const GumboVector* children = &node->v.element.children;
    GString *contents = g_string_new(NULL);

    for (unsigned int i = 0; i < children->length; ++i) {
      GString *text = textize((GumboNode*) children->data[i]);
      gchar *stripped_text = strip(text->str);
      g_string_free(text, TRUE);

      if (i && strlen(stripped_text) && contents->len)
        g_string_append_c(contents, ' ');

      g_string_append(contents, stripped_text);
      g_free(stripped_text);
    }
    return contents;
  } else {
    return g_string_new(NULL);
  }
}


