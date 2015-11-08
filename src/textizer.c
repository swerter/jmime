#include <glib.h>
#include <string.h>
#include <gumbo.h>

#include "utils.h"

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
      gstr_strip(text);

      if (i && text->len && contents->len)
        g_string_append_c(contents, ' ');

      g_string_append(contents, text->str);
      g_string_free(text, TRUE);
    }
    return contents;
  } else {
    return g_string_new(NULL);
  }
}


