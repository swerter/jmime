GMimeMessage *gmime_message_from_path(gchar *path);
gchar *gmime_message_to_json(GMimeMessage *message, gboolean include_content);
GByteArray *gmime_message_get_part_data(GMimeMessage* message, guint part_id, gchar* content_type);