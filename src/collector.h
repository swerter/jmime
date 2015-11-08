GMimeMessage *gmime_message_from_path(gchar *path);
GString *gmime_message_to_json(GMimeMessage *message, gboolean include_content);
GByteArray *gmime_message_get_part_data(GMimeMessage* message, guint part_id, gchar* content_type);

gchar *guess_content_type_extension(const gchar *content_type);
void collector_foreach_callback(GMimeObject *parent, GMimeObject *part, gpointer user_data);
