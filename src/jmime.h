/*
 * Address
 *
 * Address is a simple struct that represents an email address with
 * name and address. Name is not required but address is.
 *
 */
typedef struct Address {
  gchar *name;
  gchar *address;
} Address;

/*
 * AddressesList
 *
 * AddressesList is an array of Addresses. As such we can reuse GPtrArray.
 */

typedef GPtrArray AddressesList;


/*
 * MessageBody
 *
 * Structure to keep the body (text or html) within the MessageData with its
 * preview and sanitized (HTML) content.
 *
 */
typedef struct MessageBody {
  gchar *content_type;
  gchar *content;
  gchar *preview;
  guint size;
} MessageBody;



/*
 * MessageAttachment
 *
 * Structure to keep the downloadable attachment information, without content.
 *
 */

typedef struct MessageAttachment {
  guint part_id;
  gchar *content_type;
  gchar *filename;
  guint size;
} MessageAttachment;


/*
 * MessageAttachmentsList
 *
 * A list of MessageAttachment, as a wrap around GPtrArray
 *
 */
typedef GPtrArray MessageAttachmentsList;



/*
 * MessageData
 *
 * Intermediate structure in which to keep the message data, already
 * cleaned up, sanitized and normalized; the bodies and attachments have been
 * already detected, and inline content has been injected.
 *
 */
typedef struct MessageData {
  gchar                  *message_id;
  Address                *from;
  AddressesList          *reply_to;
  AddressesList          *to;
  AddressesList          *cc;
  AddressesList          *bcc;
  gchar                  *subject;
  gchar                  *date;
  gchar                  *in_reply_to;
  gchar                  *references;
  MessageBody            *text;
  MessageBody            *html;
  MessageAttachmentsList *attachments;
} MessageData;



void jmime_init(void);
void jmime_shutdown(void);

GString*    jmime_get_json(gchar *path, gboolean include_content);
GByteArray* jmime_get_part_data(gchar *path, guint part_id);

void jmime_index_message(const gchar *index_path, const gchar *message_path);
void jmime_index_maildir(const gchar *maildir_path);
gchar **jmime_search(const gchar *maildir_path, const gchar *query);