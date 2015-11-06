#include <string.h>
#include <gmime/gmime.h>
#include <glib/gprintf.h>
#include <errno.h>
#include "parson/parson.h"
#include <gumbo.h>
#include "sanitizer.h"
#include "textizer.h"

#define UTF8_CHARSET "UTF-8"
#define MAX_EMBEDDED_INLINE_ATTACHMENT_SIZE 65536
#define RECURSION_LIMIT 30
#define CITATION_COLOUR 16711680
#define MAX_HTML_PREVIEW_LENGTH 512


/*
 *
 *
 */
static GMimeMessage* message_from_stream(GMimeStream *stream) {
  GMimeParser *parser = g_mime_parser_new_with_stream (stream);
  if (!parser) {
    g_printerr("failed to create parser\r\n");
    return NULL;
  }

  GMimeMessage *message = g_mime_parser_construct_message (parser);
  g_object_unref (parser);
  if (!message) {
    g_printerr("failed to construct message\r\n");
    return NULL;
  }

  return message;
}



/*
 *
 *
 */
GMimeMessage *jmime_message_from_file(FILE *file) {
  GMimeStream *stream = g_mime_stream_file_new (file);

  // Being owner of the stream will automatically close the file when released
  g_mime_stream_file_set_owner(GMIME_STREAM_FILE(stream), TRUE);

  if (!stream) {
    g_printerr("file stream could not be opened\r\n");
    fclose(file);
    return NULL;
  }

  GMimeMessage *message = message_from_stream(stream);
  g_object_unref (stream);
  if (!message) {
    g_printerr("message could not be constructed from stream\r\n");
    return NULL;
  }

  return message;
}


/*
 *
 *
 */
GMimeMessage *jmime_message_from_path(gchar *path) {
  // Note: we don't need to worry about closing the file, as it will be closed by the
  // stream within message_from_file.
  FILE *file = fopen (path, "r");

  if (!file) {
    g_printerr("cannot open file '%s': %s\r\n", path, g_strerror(errno));
    return NULL;
  }

  GMimeMessage *message = jmime_message_from_file(file);
  if (!message) {
    g_printerr("message could not be constructed from file '%s': %s\r\n", path, g_strerror(errno));
    return NULL;
  }

  return message;
}


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


typedef struct PartCollectorCallbackData {
  // We keep track of explicit recursions, and limit them (RECURSION_LIMIT)
  int recursion_depth;
  // We keep track of the depth within message parts to identify parts later
  int part_id;
  // Actual values we are interested in
  JSON_Value *bodies;
  JSON_Value *attachments;
} PartCollectorCallbackData;


/*
 *
 *
 */
static void collect_part(GMimeObject *part, PartCollectorCallbackData *fdata) {
  GMimeContentType *content_type = g_mime_object_get_content_type(part);

  if (g_mime_content_type_is_type (content_type, "text", "*")) {

    JSON_Value *body_value = json_value_init_object();
    JSON_Object *body_object = json_value_get_object(body_value);
    JSON_Array *bodies_array = json_value_get_array(fdata->bodies);

    GMimeStream *mem_stream = g_mime_stream_mem_new();
    g_mime_stream_mem_set_owner (GMIME_STREAM_MEM(mem_stream), TRUE);

    GMimeStreamFilter *mem_stream_filtered;
    mem_stream_filtered = GMIME_STREAM_FILTER(g_mime_stream_filter_new(mem_stream));

    const gchar *charset = g_mime_object_get_content_type_parameter (part, "charset");
    if (charset && g_ascii_strcasecmp(charset, UTF8_CHARSET)) {
      GMimeFilter *utf8_charset_filter = g_mime_filter_charset_new(charset, UTF8_CHARSET);
      g_mime_stream_filter_add(mem_stream_filtered, utf8_charset_filter);
      g_object_unref(utf8_charset_filter);
    }

  if (g_mime_content_type_is_type (content_type, "text", "plain")) {
      GMimeFilter *strip_filter = g_mime_filter_strip_new();
      g_mime_stream_filter_add(mem_stream_filtered, strip_filter);
      g_object_unref(strip_filter);

      GMimeFilter *crlf_filter = g_mime_filter_crlf_new(FALSE, FALSE);
      g_mime_stream_filter_add(mem_stream_filtered, crlf_filter);
      g_object_unref(crlf_filter);

      GMimeFilter *html_filter = g_mime_filter_html_new(
         GMIME_FILTER_HTML_CONVERT_NL |
         GMIME_FILTER_HTML_CONVERT_SPACES |
         GMIME_FILTER_HTML_CONVERT_URLS |
         GMIME_FILTER_HTML_MARK_CITATION |
         GMIME_FILTER_HTML_CONVERT_ADDRESSES |
         GMIME_FILTER_HTML_CITE, CITATION_COLOUR);
      g_mime_stream_filter_add(mem_stream_filtered, html_filter);
      g_object_unref(html_filter);

      GMimeFilter *from_filter = g_mime_filter_from_new (GMIME_FILTER_FROM_MODE_ESCAPE);
      g_mime_stream_filter_add(mem_stream_filtered, from_filter);
      g_object_unref(from_filter);
    }

    GMimeDataWrapper *wrapper = g_mime_part_get_content_object (GMIME_PART(part));

    g_mime_data_wrapper_write_to_stream(wrapper, GMIME_STREAM(mem_stream_filtered));

    // Freed by the mem_stream on its own (owner) [transfer none]
    GByteArray *part_content = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(mem_stream));

    gchar *content_data = g_strndup((const gchar *) part_content->data, part_content->len);

    g_object_unref(mem_stream_filtered);
    g_object_unref(mem_stream);

    GumboOutput* output = gumbo_parse_with_options(&kGumboDefaultOptions, content_data, strlen(content_data));

    GString *text_preview = textize(output->root);

    // HTML safe preview of the text content
    if (text_preview->len > MAX_HTML_PREVIEW_LENGTH)
      g_string_truncate(text_preview, MAX_HTML_PREVIEW_LENGTH);
    json_object_set_string(body_object, "preview", text_preview->str);
    g_string_free(text_preview, TRUE);

    // Leave only allowed HTML strings
    GString *sanitized_content = sanitize(output->document);
    json_object_set_string(body_object, "content", sanitized_content->str);
    g_string_free(sanitized_content, TRUE);

    gumbo_destroy_output(&kGumboDefaultOptions, output);
    g_free(content_data);

    // Get the contentType in lowercase
    gchar *content_type_str = g_mime_content_type_to_string(content_type);
    gchar *content_type_str_lowercase = g_ascii_strdown(content_type_str, -1);
    json_object_set_string(body_object, "contentType", content_type_str_lowercase);
    g_free(content_type_str_lowercase);
    g_free(content_type_str);

    json_array_append_value(bodies_array, body_value);
  } else {
    /*
     *
     *
     */
    GMimeContentDisposition *disp = g_mime_object_get_content_disposition(part);

    // Attachment without disposition is not really useful
    if (!disp)
      return;

    // We support only two disposition kinds, inline and attachment
    if (g_ascii_strcasecmp(disp->disposition, GMIME_DISPOSITION_INLINE) &&
        g_ascii_strcasecmp(disp->disposition, GMIME_DISPOSITION_ATTACHMENT))
      return;

    JSON_Array *attachments_array = json_value_get_array(fdata->attachments);

    JSON_Value *attachment_value = json_value_init_object();
    JSON_Object *attachment_object = json_value_get_object(attachment_value);

    gchar *disposition_string = g_ascii_strdown(disp->disposition, -1);
    json_object_set_string(attachment_object, "disposition", disposition_string);
    // JSON set_string will make a copy of the string, so we can free the original
    g_free(disposition_string);

    const gchar *filename = g_mime_part_get_filename(GMIME_PART(part));
    // Attachment without a filename is not really useful
    if (!g_ascii_strcasecmp(disposition_string, GMIME_DISPOSITION_ATTACHMENT) && !filename)
      return;

    const char* content_id = g_mime_part_get_content_id (GMIME_PART(part));
    gboolean may_embed_data = FALSE;

    if (!g_ascii_strcasecmp(disposition_string, GMIME_DISPOSITION_INLINE)) {
      if (content_id) {
        if (g_mime_content_type_is_type (content_type, "image", "jpeg") ||
            g_mime_content_type_is_type (content_type, "image", "jpg") ||
            g_mime_content_type_is_type (content_type, "image", "pjpeg") ||
            g_mime_content_type_is_type (content_type, "image", "gif") ||
            g_mime_content_type_is_type (content_type, "image", "bmp") ||
            g_mime_content_type_is_type (content_type, "image", "png") ||
            g_mime_content_type_is_type (content_type, "image", "x-png"))
          may_embed_data = TRUE;

        json_object_set_string(attachment_object, "contentId", content_id);
      } else if (!filename)
        // Inline attachment without content ID and without filename is not very useful
        return;
    }


    json_object_set_string(attachment_object, "filename", filename);
    json_object_set_number(attachment_object, "partId", fdata->part_id);

    GMimeDataWrapper *attachment_wrapper = g_mime_part_get_content_object (GMIME_PART(part));
    GMimeStream *attachment_mem_stream = g_mime_stream_mem_new();
    g_mime_stream_mem_set_owner (GMIME_STREAM_MEM(attachment_mem_stream), TRUE);
    g_mime_data_wrapper_write_to_stream(attachment_wrapper, attachment_mem_stream);

    GByteArray *attachment_stream_contents = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(attachment_mem_stream));

    json_object_set_number(attachment_object, "size", attachment_stream_contents->len);

    // Get the contentType in lowercase
    gchar *content_type_str = g_mime_content_type_to_string(content_type);
    gchar *content_type_str_lowercase = g_ascii_strdown(content_type_str, -1);
    json_object_set_string(attachment_object, "contentType", content_type_str_lowercase);
    g_free(content_type_str);

    // We'll convert small inline attachments with contentId to data URIs. The others must
    // be requested as linked images / attachments.
    if (may_embed_data && (attachment_stream_contents->len < MAX_EMBEDDED_INLINE_ATTACHMENT_SIZE)) {
      gchar *attachment_data = g_base64_encode(attachment_stream_contents->data, attachment_stream_contents->len);
      gchar *data_uri = g_strjoin(NULL, "data:", content_type_str_lowercase, ";base64,", attachment_data, NULL);
      g_free(attachment_data);
      json_object_set_string(attachment_object, "dataURI", data_uri);
      g_free(data_uri);
    }

    g_free(content_type_str_lowercase);
    g_object_unref(attachment_mem_stream);

    json_array_append_value(attachments_array, attachment_value);
  }
}


/*
 *
 *
 */
static void collector_foreach_callback(GMimeObject *parent, GMimeObject *part, gpointer user_data) {
  g_return_if_fail (user_data != NULL);

  PartCollectorCallbackData *fdata = (PartCollectorCallbackData *) user_data;
  if (GMIME_IS_MESSAGE_PART (part)) {

    if (fdata->recursion_depth++ < RECURSION_LIMIT) {
      GMimeMessage *message = g_mime_message_part_get_message ((GMimeMessagePart *) part);
      g_mime_message_foreach (message, collector_foreach_callback, user_data);
      g_object_unref(message);
    } else {
      g_printerr("endless recursion detected: %d\r\n", fdata->recursion_depth);
      return;
    }

  } else if (GMIME_IS_MESSAGE_PARTIAL (part)) {
    // Save into an array ? Todo: Look into the specs
  } else if (GMIME_IS_MULTIPART (part)) {
    // Nothing special needed on multipart, let descend further
  } else if (GMIME_IS_PART (part)) {

    collect_part(part, fdata);
    fdata->part_id++;

  } else {
    g_assert_not_reached ();
  }
}


static unsigned int collect_addresses(InternetAddressList *list, JSON_Value *addresses_val) {

    JSON_Array *addresses_array = json_value_get_array(addresses_val);
    InternetAddress *address;

    int i = 0;
    int l = internet_address_list_length (list);
    unsigned int n = 0;

    for (; i < l; i++) {
      address = internet_address_list_get_address(list, i);

      if (INTERNET_ADDRESS_IS_GROUP(address)) {
          InternetAddressGroup *group = INTERNET_ADDRESS_GROUP (address);
          InternetAddressList *group_list = internet_address_group_get_members (group);

          if (group_list == NULL)
            continue;

        n += collect_addresses(group_list, addresses_val);

      } else if (INTERNET_ADDRESS_IS_MAILBOX(address)) {
          InternetAddressMailbox *mailbox = INTERNET_ADDRESS_MAILBOX(address);
          const gchar *name = internet_address_get_name(address);
          const gchar *address = internet_address_mailbox_get_addr(mailbox);

          JSON_Value *address_value = json_value_init_object();
          JSON_Object *address_object = json_value_get_object(address_value);

          json_object_set_string(address_object, "name", name);
          json_object_set_string(address_object, "address", address);
          json_array_append_value(addresses_array, address_value);
          n++;
      }
    }

    return n;
}


/*
 *
 *
 */
gchar *jmime_message_to_json(GMimeMessage *message, gboolean include_content) {
  JSON_Value *root_value = json_value_init_object();
  JSON_Object *root_object = json_value_get_object(root_value);

  json_object_set_string(root_object, "messageId", g_mime_message_get_message_id(message));

  // From
  const gchar *from = g_mime_message_get_sender(message);
  InternetAddressList *from_addresses = internet_address_list_parse_string (from);
  if (from_addresses) {
    if (internet_address_list_length(from_addresses)) {
      InternetAddress *from_address = internet_address_list_get_address(from_addresses, 0);
      if (from_address) {
        InternetAddressMailbox *from_mailbox = INTERNET_ADDRESS_MAILBOX(from_address);
        const gchar *name = internet_address_get_name(from_address);
        const gchar *address = internet_address_mailbox_get_addr(from_mailbox);
        if (address) {
          JSON_Value *from_value = json_value_init_object();
          JSON_Object *from_object = json_value_get_object(from_value);
          json_object_set_string(from_object, "name", name);
          json_object_set_string(from_object, "address", address);
          json_object_set_value(root_object, "from", from_value);
        }
      }
    }
    g_object_unref (from_addresses);
  }

  // Reply-To
  const gchar *reply_to_string = g_mime_message_get_reply_to (message);
  if (reply_to_string) {
    InternetAddressList *reply_to_addresses_list = internet_address_list_parse_string (reply_to_string);
    if (reply_to_addresses_list && internet_address_list_length(reply_to_addresses_list)) {
      JSON_Value *reply_to_addresses = json_value_init_array();
      if (collect_addresses(reply_to_addresses_list, reply_to_addresses)) {
        json_object_set_value(root_object, "replyTo", reply_to_addresses);
      } else {
        json_value_free(reply_to_addresses);
      }
    }
    g_object_unref(reply_to_addresses_list);
  }

  // To
  InternetAddressList *recipients_to = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_TO);
  if (recipients_to && internet_address_list_length(recipients_to)) {
    JSON_Value *addresses_recipients_to = json_value_init_array();
    if (collect_addresses(recipients_to, addresses_recipients_to)) {
      json_object_set_value(root_object, "to", addresses_recipients_to);
    } else {
      json_value_free(addresses_recipients_to);
    }
  }

  // Cc
  InternetAddressList *recipients_cc = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_CC);
  if (recipients_cc && internet_address_list_length(recipients_cc)) {
    JSON_Value *addresses_recipients_cc = json_value_init_array();
    if (collect_addresses(recipients_cc, addresses_recipients_cc)) {
      json_object_set_value(root_object, "to", addresses_recipients_cc);
    } else {
      json_value_free(addresses_recipients_cc);
    }
  }

  // Bcc (on sent messages)
  InternetAddressList *recipients_bcc = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_BCC);
  if (recipients_bcc && internet_address_list_length(recipients_bcc)) {
    JSON_Value *addresses_recipients_bcc = json_value_init_array();
    if (collect_addresses(recipients_bcc, addresses_recipients_bcc)) {
      json_object_set_value(root_object, "to", addresses_recipients_bcc);
    } else {
      json_value_free(addresses_recipients_bcc);
    }
  }

  json_object_set_string(root_object, "subject", g_mime_message_get_subject(message));

  gchar *message_date = g_mime_message_get_date_as_string(message);
  json_object_set_string(root_object, "date", message_date);
  g_free(message_date);

  json_object_set_string(root_object, "inReplyTo", g_mime_object_get_header (GMIME_OBJECT (message), "In-reply-to"));
  json_object_set_string(root_object, "references", g_mime_object_get_header (GMIME_OBJECT (message), "References"));

  if (include_content) {
    PartCollectorCallbackData *part_collector = g_malloc(sizeof(PartCollectorCallbackData));
    part_collector->bodies = json_value_init_array();
    part_collector->attachments = json_value_init_array();
    part_collector->recursion_depth = 0;
    part_collector->part_id = 0;

    // Collect parts
    g_mime_message_foreach(message, collector_foreach_callback, part_collector);

    // Use found bodies, and if none, destroy the array
    if (json_array_get_count(json_value_get_array(part_collector->bodies)) > 0) {
      json_object_set_value(root_object, "bodies", part_collector->bodies);
    } else {
      json_value_free(part_collector->bodies);
    }

    // Use found attachments, and if none, destroy the array
    if (json_array_get_count(json_value_get_array(part_collector->attachments)) > 0) {
      json_object_set_value(root_object, "attachments", part_collector->attachments);
    } else {
      json_value_free(part_collector->attachments);
    }

    g_free(part_collector);
  }

  gchar *serialized_string = json_serialize_to_string(root_value);
  json_value_free(root_value);

  return serialized_string;
}


typedef struct AttachmentCollectorData {
  int recursion_depth;
  int part_id;
  gchar *name;
  gchar *disposition;
  GByteArray *content;
} AttachmentCollectorData;


/*
 *
 *
 */
static void extract_attachment(GMimeObject *part, AttachmentCollectorData *a_data) {
  GMimeContentDisposition *disp = g_mime_object_get_content_disposition(part);

  // Attachment without disposition is not really useful
  if (!disp)
    return;

  // The attachment disposition does not match the query in a_data
  if (g_ascii_strcasecmp(a_data->disposition, disp->disposition))
    return;

  // Either the filename or the content Id has to match, otherwise bail.
  const gchar *filename = g_mime_part_get_filename(GMIME_PART(part));
  const gchar *content_id = g_mime_part_get_content_id(GMIME_PART(part));
  if (!(filename && !g_ascii_strcasecmp(filename, a_data->name)) &&
      !(content_id && !g_ascii_strcasecmp(content_id, a_data->name)))
    return;

  GMimeDataWrapper *attachment_wrapper = g_mime_part_get_content_object (GMIME_PART(part));
  GMimeStream *attachment_mem_stream = g_mime_stream_mem_new();

  // We want to keep the byte array after we close the stream
  g_mime_stream_mem_set_owner (GMIME_STREAM_MEM(attachment_mem_stream), FALSE);

  g_mime_data_wrapper_write_to_stream(attachment_wrapper, attachment_mem_stream);

  a_data->content = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(attachment_mem_stream));

  g_object_unref(attachment_mem_stream);
}


/*
 *
 *
 */
static void attachment_foreach_callback(GMimeObject *parent, GMimeObject *part, gpointer user_data) {
  AttachmentCollectorData *a_data = (AttachmentCollectorData *) user_data;

  if (GMIME_IS_MESSAGE_PART (part)) {

    if (a_data->recursion_depth < RECURSION_LIMIT) {
      GMimeMessage *message = g_mime_message_part_get_message ((GMimeMessagePart *) part);
      g_mime_message_foreach (message, collector_foreach_callback, a_data);
      g_object_unref(message);
    } else {
      g_printerr("endless recursion detected: %d\r\n", a_data->recursion_depth);
      return;
    }

  } else if (GMIME_IS_MESSAGE_PARTIAL (part)) {
    // Save into an array ? Todo: Look into the specs
  } else if (GMIME_IS_MULTIPART (part)) {
    // Nothing special needed on multipart, let descend further
  } else if (GMIME_IS_PART (part)) {

    // We are interested only in the part 0 (counting down by same logic)
    if (a_data->part_id == 0)
      extract_attachment(part, a_data);

    a_data->part_id--;

  } else {
    g_assert_not_reached ();
  }
}


/*
 *
 *
 */
GByteArray *jmime_message_get_attachment_data(GMimeMessage* message, gchar* disposition, unsigned int part_id, gchar* name) {
  g_return_val_if_fail(message != NULL, NULL);
  g_return_val_if_fail(name != NULL, NULL);
  g_return_val_if_fail(disposition != NULL, NULL);
  g_return_val_if_fail(!g_ascii_strcasecmp(disposition, GMIME_DISPOSITION_ATTACHMENT) ||
                       !g_ascii_strcasecmp(disposition, GMIME_DISPOSITION_INLINE), NULL);

  AttachmentCollectorData *a_data = g_malloc(sizeof(AttachmentCollectorData));
  a_data->recursion_depth = 0;
  a_data->part_id = part_id;
  a_data->name = name;
  a_data->disposition = disposition;
  a_data->content = NULL;

  g_mime_message_foreach(message, attachment_foreach_callback, a_data);

  GByteArray *content = a_data->content;
  g_free(a_data);

  if (!content)
    g_printerr("could not locate %s [%s] as message part %d\r\n", disposition, name, part_id);

  return content;
}

