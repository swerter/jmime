#include <string.h>
#include <glib.h>

gboolean gc_contains_c(const gchar *str, const gchar c) {
  for(int i = 0; i < strlen(str); i++)
    if (str[i] == c)
      return TRUE;
  return FALSE;
}

// The stripping functions from glib do not remove tabs, newlines etc.,
// so we define our owns that remove all whitespace.
gchar *gc_lstrip(const gchar* text) {
  GRegex *regex = g_regex_new ("\\A\\s+", 0, 0, NULL);
  gchar *stripped = g_regex_replace_literal(regex, text, -1, 0, "", 0, NULL);
  g_regex_unref(regex);
  return stripped;
}


gchar *gc_rstrip(const gchar* text) {
  GRegex *regex = g_regex_new ("\\s+$", 0, 0, NULL);
  gchar *stripped = g_regex_replace_literal(regex, text, -1, 0, "", 0, NULL);
  g_regex_unref(regex);
  return stripped;
}


gchar *gc_strip(const gchar *text) {
  gchar *lstripped = gc_lstrip(text);
  gchar *stripped = gc_rstrip(lstripped);
  g_free(lstripped);
  return stripped;
}


GString *gstr_strip(GString *text) {
  gchar *stripped_str = gc_strip(text->str);
  g_string_assign(text, stripped_str);
  return text;
}


GString *gstr_replace_all(GString *text, const gchar* old_str, const gchar *new_str) {
  gchar *escaped_s1 = g_regex_escape_string (old_str, -1);
  GRegex *regex = g_regex_new (escaped_s1, 0, 0, NULL);
  gchar *new_string =  g_regex_replace_literal(regex, text->str, -1, 0, new_str, 0, NULL);
  g_regex_unref(regex);
  g_free(escaped_s1);
  g_string_assign(text, new_string);
  g_free(new_string);
  return text;
}


GString *gstr_substitute_xml_entities_into_text(const gchar *text) {
  GString *result = g_string_new(text);
  gstr_replace_all(result, "&", "&amp;"); // replacing of & must come first
  gstr_replace_all(result, "<", "&lt;");
  gstr_replace_all(result, ">", "&gt;");
  return result;
}


GString *gstr_substitute_xml_entities_into_attributes(const gchar quote, const gchar *text) {
  GString *result = gstr_substitute_xml_entities_into_text(text);
  if (quote == '"') {
    gstr_replace_all(result, "\"", "&quot;");
  } else if (quote == '\'') {
    gstr_replace_all(result, "'", "&apos;");
  }
  return result;
}
