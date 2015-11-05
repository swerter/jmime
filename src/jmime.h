void jmime_init();
void jmime_shutdown();

GMimeMessage *jmime_message_from_path(gchar *path);
GMimeMessage *jmime_message_from_file(FILE *fd);

gchar *jmime_message_to_json(GMimeMessage *message, gboolean include_content);

GByteArray *jmime_message_get_attachment_data(GMimeMessage* message, gchar* disposition, unsigned int part_id, gchar* name);
