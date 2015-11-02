GMimeMessage *jmime_message_from_path(char *path);
GMimeMessage *jmime_message_from_file(FILE *fd);

char *jmime_message_to_json(GMimeMessage *message, gboolean collect_bodies);

GByteArray *jmime_message_get_inline_attachment_data(GMimeMessage* message, char* content_id, int part_id);
GByteArray *jmime_message_get_attachment_data(GMimeMessage* message, char* filename, int part_id);

