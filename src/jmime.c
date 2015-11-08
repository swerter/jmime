#include <gmime/gmime.h>
#include "collector.h"

/*
 *
 *
 */
void jmime_init() {
  g_mime_init(GMIME_ENABLE_RFC2047_WORKAROUNDS);
}


/*
 *
 *
 */
void jmime_shutdown() {
  g_mime_shutdown();
}


/*
 *
 *
 */
gchar *jmime_get_json(gchar *path, gboolean include_content) {
  GMimeMessage *message = gmime_message_from_path(path);
  if (!message)
    return NULL;

  gchar *json_message = gmime_message_to_json(message, include_content);
  g_object_unref(message);

  if (!json_message)
    return NULL;

  return json_message;
}


/*
 *
 *
 */
GByteArray *jmime_get_part_data(gchar *path, guint part_id, gchar *content_type) {
  GMimeMessage *message = gmime_message_from_path(path);
  if (!message)
    return NULL;

  GByteArray *attachment = gmime_message_get_part_data(message, part_id, content_type);
  g_object_unref(message);
  if (!attachment)
    return NULL;

  return attachment;
}
