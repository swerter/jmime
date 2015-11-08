#include <gmime/gmime.h>
#include "collector.h"

/*
 *
 *
 */
void jmime_init(void) {
  g_mime_init(GMIME_ENABLE_RFC2047_WORKAROUNDS);
}


/*
 *
 *
 */
void jmime_shutdown(void) {
  g_mime_shutdown();
}


/*
 *
 *
 */
GString *jmime_get_json(gchar *path, gboolean include_content) {
  GMimeMessage *message = NULL;
  message= gmime_message_from_path(path);
  if (!message)
    return NULL;

  GString *json_message = gmime_message_to_json(message, include_content);
  g_object_unref(message);

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

  return attachment;
}
