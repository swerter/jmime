GMimeMessage *mime_email_file_to_message(FILE *fd);
char *mime_message_to_json(GMimeMessage *message);

GByteArray *get_inline_content(GMimeMessage* message, char* content_id, int part_id);
GByteArray *get_attachment(GMimeMessage* message, char* filename, int part_id);

