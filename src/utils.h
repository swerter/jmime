gboolean gc_contains_c(const gchar *str, const gchar c);

gchar *gc_strip(const gchar *text);
gchar *gc_lstrip(const gchar* text);
gchar *gc_rstrip(const gchar* text);

GString *gstr_strip(GString *text);
GString *gstr_replace_all(GString *text, const gchar* old, const gchar *new);
GString *gstr_substitute_xml_entities_into_attributes(gchar quote, const gchar *text);
GString *gstr_substitute_xml_entities_into_text(const gchar *text);