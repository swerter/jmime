// cc -O3 -fPIC -std=c99 -Wall -I-ggdb3 `pkg-config --cflags --libs gmime-2.6` parson/parson.c jmime.c -o jmime
//
#include <string.h>
#include <gmime/gmime.h>
#include <glib/gprintf.h>
#include <locale.h>
#include <errno.h>
#include "parson/parson.h"

#ifndef g_memcpy
# define g_memcpy memcpy
#endif

#define UTF8_CHARSET "UTF-8"
#define MAX_EMBEDDED_INLINE_ATTACHMENT 65536
#define RECURSION_LIMIT 30
#define CITATION_COLOUR 16711680

typedef struct PartCollectorCallbackData {
  // We keep track of explicit recursions, and limit them (RECURSION_LIMIT)
  int recursion_depth;
  // We keep track of the depth within message parts
  int part_id;
  // Actual values we are interested in
  JSON_Value *bodies;
  JSON_Value *inlines;
  JSON_Value *attachments;
} PartCollectorCallbackData;


typedef struct AttachmentCollectorCallbackData {
  int recursion_depth;
  int part_id;
  char *filename;
  char *content_id;
  GByteArray *content;
} AttachmentCollectorCallbackData;


GByteArray *get_inline_content(GMimeMessage* message, char* content_id, int part_id);
GByteArray *get_attachment(GMimeMessage* message, char* filename, int part_id);

GMimeMessage *mime_email_file_to_message(FILE *fd);
char *mime_message_to_json(GMimeMessage *message);

static GMimeMessage* construct_message_from_stream(GMimeStream *stream);

static PartCollectorCallbackData *new_part_collector_data();
static void collect_part(GMimeObject *part, PartCollectorCallbackData *fdata);
static void collector_foreach_callback(GMimeObject *parent, GMimeObject *part, gpointer user_data);

static AttachmentCollectorCallbackData *new_attachment_collector_data(char *filename, char* content_id, int part_id);
static AttachmentCollectorCallbackData *new_attachment_collector_data_with_content_id(char* content_id, int part_id);
static AttachmentCollectorCallbackData *new_attachment_collector_data_with_filename(char* filename, int part_id);

static void extract_attachment(GMimeObject *part, AttachmentCollectorCallbackData *a_data);
static void attachment_foreach_callback(GMimeObject *parent, GMimeObject *part, gpointer user_data);


// Gives new instance to be used as user_data in foreach
static PartCollectorCallbackData *new_part_collector_data() {
  PartCollectorCallbackData *val = g_malloc(sizeof(PartCollectorCallbackData));
  val->bodies = json_value_init_array();
  val->inlines = json_value_init_array();
  val->attachments = json_value_init_array();
  val->recursion_depth = 0;
  val->part_id = 0;
  return val;
}


static void collect_part(GMimeObject *part, PartCollectorCallbackData *fdata) {
  GMimeContentType *content_type = g_mime_object_get_content_type(part);

  if (g_mime_content_type_is_type (content_type, "text", "*")) {

    JSON_Value *body_value = json_value_init_object();
    JSON_Object *body_object = json_value_get_object(body_value);
    JSON_Array *bodies_array = json_value_get_array(fdata->bodies);

    GMimeStream *mem_stream = g_mime_stream_mem_new();
    g_mime_stream_mem_set_owner ((GMimeStreamMem *) mem_stream, TRUE);

    GMimeStream *mem_stream_filter = g_mime_stream_filter_new(mem_stream);

    const char *charset = g_mime_object_get_content_type_parameter (part, "charset");
    GMimeFilter *utf8_charset_filter = NULL;

    if (charset && g_ascii_strcasecmp(charset, UTF8_CHARSET)) {
      utf8_charset_filter = g_mime_filter_charset_new(charset, UTF8_CHARSET);
      g_mime_stream_filter_add(GMIME_STREAM_FILTER(mem_stream_filter), utf8_charset_filter);
    }

    GMimeFilter *html_filter = NULL;
    GMimeFilter *from_filter;
    if (g_mime_content_type_is_type (content_type, "text", "plain")) {
      html_filter = g_mime_filter_html_new(GMIME_FILTER_HTML_CONVERT_NL |
                                           GMIME_FILTER_HTML_CONVERT_SPACES |
                                           GMIME_FILTER_HTML_CONVERT_URLS |
                                           GMIME_FILTER_HTML_MARK_CITATION |
                                           GMIME_FILTER_HTML_CONVERT_ADDRESSES |
                                           GMIME_FILTER_HTML_CITE, CITATION_COLOUR);
      g_mime_stream_filter_add(GMIME_STREAM_FILTER(mem_stream_filter), html_filter);

      from_filter = g_mime_filter_from_new (GMIME_FILTER_FROM_MODE_ESCAPE);
      g_mime_stream_filter_add(GMIME_STREAM_FILTER(mem_stream_filter), from_filter);
    }

    GMimeDataWrapper *wrapper = g_mime_part_get_content_object ((GMimePart *) part);

    g_mime_data_wrapper_write_to_stream(wrapper, mem_stream_filter);

    // Freed by the mem_stream on its own (owner) [transfer none]
    GByteArray *part_content = g_mime_stream_mem_get_byte_array((GMimeStreamMem *) mem_stream);

    char *content_data = g_malloc(sizeof(char) * (part_content->len + 1));
    content_data[part_content->len] = '\0';
    g_memcpy(content_data, part_content->data, part_content->len);

    if (utf8_charset_filter)
      g_object_unref(utf8_charset_filter);

    if (html_filter) {
      g_object_unref(from_filter);
      g_object_unref(html_filter);
    }

    g_object_unref(mem_stream_filter);
    g_object_unref(mem_stream);

    json_object_set_string(body_object, "content", content_data);
    g_free(content_data);

    // Get the contentType in lowercase
    char *content_type_str = g_mime_content_type_to_string(content_type);
    char *content_type_str_lowercase = g_ascii_strdown(content_type_str, -1);
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

    JSON_Value *attachment_value = json_value_init_object();
    JSON_Object *attachment_object = json_value_get_object(attachment_value);
    JSON_Array *attachments_array = json_value_get_array(fdata->attachments);

    char *disposition_string = g_ascii_strdown(disp->disposition, -1);
    json_object_set_string(attachment_object, "disposition", disposition_string);
    // JSON set_string will make a copy of the string, so we can free the original
    g_free(disposition_string);

    const char *filename = g_mime_part_get_filename((GMimePart *) part);
    // Attachment without a filename is not really useful
    if (!g_ascii_strcasecmp(disposition_string, GMIME_DISPOSITION_ATTACHMENT) && !filename)
      return;

    const char* content_id = g_mime_part_get_content_id ((GMimePart *) part);
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

    GMimeDataWrapper *attachment_wrapper = g_mime_part_get_content_object ((GMimePart *) part);
    GMimeStream *attachment_mem_stream = g_mime_stream_mem_new();
    g_mime_stream_mem_set_owner ((GMimeStreamMem *) attachment_mem_stream, TRUE);
    g_mime_data_wrapper_write_to_stream(attachment_wrapper, attachment_mem_stream);

    GByteArray *attachment_stream_contents = g_mime_stream_mem_get_byte_array((GMimeStreamMem *) attachment_mem_stream);

    json_object_set_number(attachment_object, "size", attachment_stream_contents->len);

    if (may_embed_data && (attachment_stream_contents->len < MAX_EMBEDDED_INLINE_ATTACHMENT)) {
      char *attachment_data = g_base64_encode(attachment_stream_contents->data, attachment_stream_contents->len);
      json_object_set_string(attachment_object, "data", attachment_data);
      g_free(attachment_data);
    }
    g_object_unref(attachment_mem_stream);

    json_array_append_value(attachments_array, attachment_value);
  }
}


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


char *mime_message_to_json(GMimeMessage *message) {
  JSON_Value *root_value = json_value_init_object();
  JSON_Object *root_object = json_value_get_object(root_value);

  JSON_Value *headers_value = json_value_init_object();
  JSON_Object *headers_object = json_value_get_object(headers_value);
  json_object_set_value(root_object, "headers", headers_value);

  json_object_set_string(headers_object, "messageId", g_mime_message_get_message_id(message));
  json_object_set_string(headers_object, "from", g_mime_message_get_sender(message));

  const char *reply_to_string = g_mime_message_get_reply_to (message);
  if (reply_to_string)
    json_object_set_string(headers_object, "replyTo", reply_to_string);

  // To:
  InternetAddressList *recipients_to = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_TO);
  char *recipients_to_string = internet_address_list_to_string(recipients_to, FALSE);
  if (recipients_to_string)
    json_object_set_string(headers_object, "to", recipients_to_string);
  g_free(recipients_to_string);

  // CC:
  InternetAddressList *recipients_cc = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_CC);
  char *recipients_cc_string = internet_address_list_to_string(recipients_cc, FALSE);
  if (recipients_cc_string)
    json_object_set_string(headers_object, "cc", recipients_cc_string);
  g_free(recipients_cc_string);

  // Bcc (on sent messages)
  InternetAddressList *recipients_bcc = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_BCC);
  char *recipients_bcc_string = internet_address_list_to_string(recipients_bcc, FALSE);
  if (recipients_bcc_string)
    json_object_set_string(headers_object, "bcc", recipients_bcc_string);
  g_free(recipients_bcc_string);

  json_object_set_string(headers_object, "subject", g_mime_message_get_subject(message));

  char *message_date = g_mime_message_get_date_as_string(message);
  json_object_set_string(headers_object, "date", message_date);
  g_free(message_date);

  json_object_set_string(headers_object, "inReplyTo", g_mime_object_get_header (GMIME_OBJECT (message), "In-reply-to"));
  json_object_set_string(headers_object, "references", g_mime_object_get_header (GMIME_OBJECT (message), "References"));

  PartCollectorCallbackData *json_collector = new_part_collector_data();

  // Collect parts
  g_mime_message_foreach(message, collector_foreach_callback, json_collector);

  // Use found bodies, and if none, destroy the array
  if (json_array_get_count(json_value_get_array(json_collector->bodies)) > 0) {
    json_object_set_value(root_object, "bodies", json_collector->bodies);
  } else {
    json_value_free(json_collector->bodies);
  }

  // Use found inline attachments, and if none, destroy the array
  if (json_array_get_count(json_value_get_array(json_collector->inlines)) > 0) {
    json_object_set_value(root_object, "inlines", json_collector->inlines);
  } else {
    json_value_free(json_collector->inlines);
  }

  // Use found attachments, and if none, destroy the array
  if (json_array_get_count(json_value_get_array(json_collector->attachments)) > 0) {
    json_object_set_value(root_object, "attachments", json_collector->attachments);
  } else {
    json_value_free(json_collector->attachments);
  }

  char *serialized_string = json_serialize_to_string(root_value);

  g_free(json_collector);
  json_value_free(root_value);

  return serialized_string;
}


static GMimeMessage* construct_message_from_stream(GMimeStream *stream) {
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



static AttachmentCollectorCallbackData *new_attachment_collector_data(char *filename, char* content_id, int part_id) {
  AttachmentCollectorCallbackData *val = g_malloc(sizeof(AttachmentCollectorCallbackData));
  val->part_id = part_id;
  val->content_id = content_id;
  val->filename = filename;
  val->content = NULL;
  val->recursion_depth = 0;
  return val;
}

static AttachmentCollectorCallbackData *new_attachment_collector_data_with_content_id(char* content_id, int part_id) {
  return new_attachment_collector_data(NULL, content_id, part_id);
}

static AttachmentCollectorCallbackData *new_attachment_collector_data_with_filename(char* filename, int part_id) {
  return new_attachment_collector_data(filename, NULL, part_id);
}


static void extract_attachment(GMimeObject *part, AttachmentCollectorCallbackData *a_data) {
  GMimeContentDisposition *disp = g_mime_object_get_content_disposition(part);

  // Attachment without disposition is not really useful
  if (!disp)
    return;

  // We support only two disposition kinds, inline and attachment
  if (g_ascii_strcasecmp(disp->disposition, GMIME_DISPOSITION_INLINE) &&
      g_ascii_strcasecmp(disp->disposition, GMIME_DISPOSITION_ATTACHMENT))
    return;

  if (a_data->filename) {
    const char *filename = g_mime_part_get_filename((GMimePart *) part);

    if (a_data->filename && (!filename || g_ascii_strcasecmp(filename, a_data->filename)))
      return;

  } else if (a_data->content_id) {
    const char *content_id = g_mime_part_get_content_id((GMimePart *) part);
    if (a_data->content_id && (!content_id || g_ascii_strcasecmp(content_id, a_data->content_id)))
      return;
  }

  GMimeDataWrapper *attachment_wrapper = g_mime_part_get_content_object ((GMimePart *) part);
  GMimeStream *attachment_mem_stream = g_mime_stream_mem_new();

  // We want to keep the byte array after we close the stream
  g_mime_stream_mem_set_owner ((GMimeStreamMem *) attachment_mem_stream, FALSE);
  g_mime_data_wrapper_write_to_stream(attachment_wrapper, attachment_mem_stream);

  a_data->content = g_mime_stream_mem_get_byte_array((GMimeStreamMem *) attachment_mem_stream);

  g_object_unref(attachment_mem_stream);
}


static void attachment_foreach_callback(GMimeObject *parent, GMimeObject *part, gpointer user_data) {
  AttachmentCollectorCallbackData *a_data = (AttachmentCollectorCallbackData *) user_data;

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


GByteArray *get_inline_content(GMimeMessage* message, char* content_id, int part_id) {
  AttachmentCollectorCallbackData *a_data = new_attachment_collector_data_with_content_id(content_id, part_id);

  g_mime_message_foreach(message, attachment_foreach_callback, a_data);

  GByteArray *content = a_data->content;
  g_free(a_data);

  if (!content)
    g_printerr("Attachment with Content-Id `%s` as message part of %d could not be located!\r\n", content_id, part_id);

  return content;
}


GByteArray *get_attachment(GMimeMessage* message, char* filename, int part_id) {
  AttachmentCollectorCallbackData *a_data = new_attachment_collector_data_with_filename(filename, part_id);

  g_mime_message_foreach(message, attachment_foreach_callback, a_data);

  GByteArray *content = a_data->content;
  g_free(a_data);

  if (!content)
    g_printerr("Attachment with filename `%s` as message part of %d could not be located!\r\n", filename, part_id);

  return content;
}


GMimeMessage *mime_email_file_to_message(FILE *fd) {
  GMimeStream *stream = g_mime_stream_file_new (fd);

  // Being owner of the stream will automatically close the file when released
  g_mime_stream_file_set_owner((GMimeStreamFile *) stream, TRUE);

  if (!stream) {
    g_printerr("cannot open stream\r\n");
    goto leave;
  }

  GMimeMessage *message = construct_message_from_stream(stream);
  g_object_unref (stream);
  if (message)
    return message;

  leave:
    fclose(fd);
    return NULL;
}
