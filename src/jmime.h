void jmime_init();
void jmime_shutdown();

gchar *jmime_get_json(gchar *path, gboolean include_content);

GByteArray *jmime_get_part_data(gchar *path, guint part_id, gchar *content_type);
