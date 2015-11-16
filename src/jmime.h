void jmime_init(void);
void jmime_shutdown(void);

GString*    jmime_get_json(gchar *path, gboolean include_content);
GByteArray* jmime_get_part_data(gchar *path, guint part_id);

void jmime_index_message(const gchar *mailbox_path, const gchar *message_path);
void jmime_index_mailbox(const gchar *mailbox_path);
gchar **jmime_search_mailbox(const gchar *mailbox_path, const gchar *query, const guint max_results);