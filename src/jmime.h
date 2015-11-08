void jmime_init(void);
void jmime_shutdown(void);

GString*    jmime_get_json(gchar *path, gboolean include_content);
GByteArray* jmime_get_part_data(gchar *path, guint part_id, gchar *content_type);
