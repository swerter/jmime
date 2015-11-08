// #include <string.h>
#include <gmime/gmime.h>
#include <glib/gprintf.h>
#include <errno.h>
#include "parson/parson.h"
#include <gumbo.h>

#include "parts.h"
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
static GMimeMessage* gmime_message_from_stream(GMimeStream *stream) {
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
static GMimeMessage *gmime_message_from_file(FILE *file) {
  GMimeStream *stream = g_mime_stream_file_new (file);

  // Being owner of the stream will automatically close the file when released
  g_mime_stream_file_set_owner(GMIME_STREAM_FILE(stream), TRUE);

  if (!stream) {
    g_printerr("file stream could not be opened\r\n");
    fclose(file);
    return NULL;
  }

  GMimeMessage *message = gmime_message_from_stream(stream);
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
GMimeMessage *gmime_message_from_path(gchar *path) {
  // Note: we don't need to worry about closing the file, as it will be closed by the
  // stream within message_from_file.
  FILE *file = fopen (path, "r");

  if (!file) {
    g_printerr("cannot open file '%s': %s\r\n", path, g_strerror(errno));
    return NULL;
  }

  GMimeMessage *message = gmime_message_from_file(file);
  if (!message) {
    g_printerr("message could not be constructed from file '%s': %s\r\n", path, g_strerror(errno));
    return NULL;
  }

  return message;
}



static void free_collected_part(gpointer part) {
  if (!part)
    return;

  CollectedPart *cpart = (CollectedPart *) part;

  if (cpart->content_type)
    g_free(cpart->content_type);

  if (cpart->content)
     g_byte_array_free(cpart->content, TRUE);

  if (cpart->content_id)
    g_free(cpart->content_id);

  if (cpart->filename)
    g_free(cpart->filename);

  if (cpart->disposition)
    g_free(cpart->disposition);

  g_free(cpart);
}


static void free_part_collector(PartCollectorData *pcdata) {
  if (!pcdata)
    return;

  free_collected_part(pcdata->html_body);
  free_collected_part(pcdata->text_body);

  if (pcdata->alternative_bodies)
    g_ptr_array_free(pcdata->alternative_bodies, TRUE);

  if (pcdata->inlines)
    g_ptr_array_free(pcdata->inlines, TRUE);

  if (pcdata->attachments)
    g_ptr_array_free(pcdata->attachments, TRUE);
}


static CollectedPart *new_collected_part(guint part_id) {
  CollectedPart* part = g_malloc(sizeof(CollectedPart));
  part->part_id = part_id;
  part->content_type = NULL;
  part->content = NULL;
  part->content_id = NULL;
  part->filename = NULL;
  part->disposition = NULL;
  return part;
}


static PartCollectorData* new_part_collector_data(void) {
  PartCollectorData *pcd = g_malloc(sizeof(PartCollectorData));

  pcd->recursion_depth = 0;
  pcd->part_id = 0;

  pcd->text_body = NULL;
  pcd->html_body = NULL;

  pcd->alternative_bodies = g_ptr_array_new_with_free_func((GDestroyNotify) free_collected_part);
  pcd->attachments        = g_ptr_array_new_with_free_func((GDestroyNotify) free_collected_part);
  pcd->inlines            = g_ptr_array_new_with_free_func((GDestroyNotify) free_collected_part);
  return pcd;
}


static gchar *guess_content_type_extension(const gchar *content_type) {
  gchar *extension = "txt";
  if (!g_ascii_strcasecmp(content_type, "text/plain")) {
    extension = "txt";
  } else if (!g_ascii_strcasecmp(content_type, "text/html")) {
    extension = "html";
  } else if (!g_ascii_strcasecmp(content_type, "text/rtf")) {
    extension = "rtf";
  } else if (!g_ascii_strcasecmp(content_type, "text/enriched")) {
    extension = "etf";
  } else if (!g_ascii_strcasecmp(content_type, "text/calendar")) {
    extension = "ics";
  } else if (!g_ascii_strcasecmp(content_type, "image/jpeg") ||
             !g_ascii_strcasecmp(content_type, "image/jpg")) {
    extension = "jpg";
  } else if (!g_ascii_strcasecmp(content_type, "image/pjpeg")) {
    extension = "pjpg";
  } else if (!g_ascii_strcasecmp(content_type, "image/gif")) {
    extension = "gif";
  } else if (!g_ascii_strcasecmp(content_type, "image/png") ||
             !g_ascii_strcasecmp(content_type, "image/x-png")) {
    extension = "png";
  } else if (!g_ascii_strcasecmp(content_type, "image/bmp")) {
    extension = "bmp";
  }
  return extension;
}


/*
 *
 *
 */
static void collect_part(GMimeObject *part, PartCollectorData *fdata, gboolean multipart_parent) {
  GMimeContentType        *content_type = g_mime_object_get_content_type(part);
  GMimeContentDisposition *disposition  = g_mime_object_get_content_disposition(part);

  if (!content_type)
    return;

  GMimeDataWrapper *wrapper = g_mime_part_get_content_object(GMIME_PART(part));
  if (!wrapper)
    return;

  // All the information will be collected in the CollectedPart
  CollectedPart *c_part = new_collected_part(fdata->part_id);

  gboolean is_attachment = FALSE;
  if (disposition) {
    c_part->disposition = g_ascii_strdown(disposition->disposition, -1);
    is_attachment = !g_ascii_strcasecmp(disposition->disposition, GMIME_DISPOSITION_ATTACHMENT);
  }

  // If a filename is given, collect it always
  const gchar *filename = g_mime_part_get_filename(GMIME_PART(part));
  if (filename)
    c_part->filename = g_strdup(filename);

  // If a contentID is given, collect it always
  const char* content_id = g_mime_part_get_content_id (GMIME_PART(part));
  if (content_id)
    c_part->content_id = g_strdup(content_id);

  // Get the contentType in lowercase
  gchar *content_type_str = g_mime_content_type_to_string(content_type);
  c_part->content_type = g_ascii_strdown(content_type_str, -1);
  g_free(content_type_str);

  // To qualify as a message body, a MIME entity MUST NOT have a Content-Disposition header with the value "attachment".
  if (!is_attachment && g_mime_content_type_is_type (content_type, "text", "*")) {
    gboolean is_text_plain    = g_mime_content_type_is_type (content_type, "text", "plain");
    gboolean is_text_html     = g_mime_content_type_is_type (content_type, "text", "html");
    gboolean is_text_rtf      = g_mime_content_type_is_type (content_type, "text", "rtf");
    gboolean is_text_enriched = g_mime_content_type_is_type (content_type, "text", "enriched");

    gboolean is_new_text = !fdata->text_body && is_text_plain;
    gboolean is_new_html = !fdata->html_body && (is_text_html || is_text_enriched || is_text_rtf);

    GMimeStream *mem_stream = g_mime_stream_mem_new();
    g_mime_stream_mem_set_owner (GMIME_STREAM_MEM(mem_stream), FALSE);

    GMimeStreamFilter *mem_stream_filtered;
    mem_stream_filtered = GMIME_STREAM_FILTER(g_mime_stream_filter_new(mem_stream));

    const gchar *charset = g_mime_object_get_content_type_parameter (part, "charset");
    if (charset && g_ascii_strcasecmp(charset, UTF8_CHARSET)) {
      GMimeFilter *utf8_charset_filter = g_mime_filter_charset_new(charset, UTF8_CHARSET);
      g_mime_stream_filter_add(mem_stream_filtered, utf8_charset_filter);
      g_object_unref(utf8_charset_filter);
    }

    if (is_new_text) {
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
    }

    if (is_new_text || is_new_html) {
      GMimeFilter *from_filter = g_mime_filter_from_new (GMIME_FILTER_FROM_MODE_ESCAPE);
      g_mime_stream_filter_add(mem_stream_filtered, from_filter);
      g_object_unref(from_filter);
    }

    // Add Enriched/RTF filter for this content
    if (is_new_html && (is_text_enriched || is_text_rtf)) {
      guint flags = 0;
      if (is_text_rtf)
        flags = GMIME_FILTER_ENRICHED_IS_RICHTEXT;

      GMimeFilter *enriched_filter = g_mime_filter_enriched_new(flags);
      g_mime_stream_filter_add(mem_stream_filtered, enriched_filter);
      g_object_unref(enriched_filter);
    }

    g_mime_data_wrapper_write_to_stream(wrapper, GMIME_STREAM(mem_stream_filtered));
    // Freed by the mem_stream on its own (owner) [transfer none]
    c_part->content = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(mem_stream));

    // After we unref the mem_stream, part_content is NOT available anymore
    g_object_unref(mem_stream_filtered);
    g_object_unref(mem_stream);

    // Without content, the collected body part is of no use, so we ignore it.
    if (c_part->content->len == 0) {
      free_collected_part(c_part);
      return;
    }

    // We accept only the first text and first html content, everything
    // else is considered an alternative body
    if (is_new_text) {
      fdata->text_body = c_part;
    } else if (is_new_html) {
      fdata->html_body = c_part;
    } else {
      g_ptr_array_add(fdata->alternative_bodies, c_part);
    }

  } else {
    GMimeStream *attachment_mem_stream = g_mime_stream_mem_new();
    g_mime_stream_mem_set_owner (GMIME_STREAM_MEM(attachment_mem_stream), FALSE);
    g_mime_data_wrapper_write_to_stream(wrapper, attachment_mem_stream);

    c_part->content = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(attachment_mem_stream));
    g_object_unref(attachment_mem_stream);

    if (!g_ascii_strcasecmp(disposition->disposition, GMIME_DISPOSITION_INLINE)) {
      g_ptr_array_add(fdata->inlines, c_part);
    } else {
      // All other disposition should be kept within attachments
      g_ptr_array_add(fdata->attachments, c_part);
    }

  }
}


/*
 *
 *
 */
static void collector_foreach_callback(GMimeObject *parent, GMimeObject *part, gpointer user_data) {
  g_return_if_fail(user_data != NULL);

  PartCollectorData *fdata = (PartCollectorData *) user_data;

  if (GMIME_IS_MESSAGE_PART(part)) {

    if (fdata->recursion_depth++ < RECURSION_LIMIT) {
      GMimeMessage *message = g_mime_message_part_get_message ((GMimeMessagePart *) part);
      g_mime_message_foreach(message, collector_foreach_callback, user_data);
      g_object_unref(message);
    } else {
      g_printerr("endless recursion detected: %d\r\n", fdata->recursion_depth);
      return;
    }

  } else if (GMIME_IS_MESSAGE_PARTIAL(part)) {
    // Save into an array ? Todo: Look into the specs
  } else if (GMIME_IS_MULTIPART(part)) {
    // Nothing special needed on multipart, let descend further
  } else if (GMIME_IS_PART(part)) {
    collect_part(part, fdata, GMIME_IS_MULTIPART(parent));
    fdata->part_id++;
  } else {
    g_assert_not_reached();
  }
}


/*
 *
 *
 */
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
gchar *gmime_message_to_json(GMimeMessage *message, gboolean include_content) {
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
    InternetAddressList *reply_to_addresses_list = internet_address_list_parse_string (reply_to_string); // transfer-FULL
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
  InternetAddressList *recipients_to = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_TO); // transfer-none
  if (recipients_to && internet_address_list_length(recipients_to)) {
    JSON_Value *addresses_recipients_to = json_value_init_array();
    if (collect_addresses(recipients_to, addresses_recipients_to)) {
      json_object_set_value(root_object, "to", addresses_recipients_to);
    } else {
      json_value_free(addresses_recipients_to);
    }
  }

  // Cc
  InternetAddressList *recipients_cc = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_CC); // transfer-none
  if (recipients_cc && internet_address_list_length(recipients_cc)) {
    JSON_Value *addresses_recipients_cc = json_value_init_array();
    if (collect_addresses(recipients_cc, addresses_recipients_cc)) {
      json_object_set_value(root_object, "cc", addresses_recipients_cc);
    } else {
      json_value_free(addresses_recipients_cc);
    }
  }

  // Bcc (on sent messages)
  InternetAddressList *recipients_bcc = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_BCC); // transfer-none
  if (recipients_bcc && internet_address_list_length(recipients_bcc)) {
    JSON_Value *addresses_recipients_bcc = json_value_init_array();
    if (collect_addresses(recipients_bcc, addresses_recipients_bcc)) {
      json_object_set_value(root_object, "bcc", addresses_recipients_bcc);
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
    PartCollectorData *part_collector = new_part_collector_data();

    // Collect parts
    g_mime_message_foreach(message, collector_foreach_callback, part_collector);

    if (part_collector->text_body) {
      CollectedPart *text_body = part_collector->text_body;

      JSON_Value *json_text_val = json_value_init_object();
      JSON_Object *json_text_obj = json_value_get_object(json_text_val);

      json_object_set_number(json_text_obj, "size", text_body->content->len);

      GString *content = g_string_new_len((const gchar*) text_body->content->data, text_body->content->len);

      // Parse any HTML tags
      GumboOutput* output = gumbo_parse_with_options(&kGumboDefaultOptions, content->str, content->len);

      // Text preview
      GString *text_preview = textize(output->root);
      if (text_preview->len > MAX_HTML_PREVIEW_LENGTH)
        g_string_truncate(text_preview, MAX_HTML_PREVIEW_LENGTH);
      json_object_set_string(json_text_obj, "preview", text_preview->str);
      g_string_free(text_preview, TRUE);

      // Sanitize any present HTML tags
      GString *sanitized_content = sanitize(output->document, NULL);
      json_object_set_string(json_text_obj, "content", sanitized_content->str);
      g_string_free(sanitized_content, TRUE);

      gumbo_destroy_output(&kGumboDefaultOptions, output);

      g_string_free(content, TRUE);

      json_object_set_string(json_text_obj, "type", text_body->content_type);
      json_object_set_value(root_object, "text", json_text_val);
    }

    if (part_collector->html_body) {
      CollectedPart *html_body = part_collector->html_body;

      JSON_Value *json_html_val = json_value_init_object();
      JSON_Object *json_html_obj = json_value_get_object(json_html_val);

      json_object_set_number(json_html_obj, "size", html_body->content->len);

      GString *content = g_string_new_len((const gchar*) html_body->content->data, html_body->content->len);

      GumboOutput* output = gumbo_parse_with_options(&kGumboDefaultOptions, content->str, content->len);

      // Text preview
      GString *text_preview = textize(output->root);
      if (text_preview->len > MAX_HTML_PREVIEW_LENGTH)
        g_string_truncate(text_preview, MAX_HTML_PREVIEW_LENGTH);
      json_object_set_string(json_html_obj, "preview", text_preview->str);
      g_string_free(text_preview, TRUE);

      // Sanitize HTML
      GString *sanitized_content = sanitize(output->document, part_collector->inlines);
      json_object_set_string(json_html_obj, "content", sanitized_content->str);
      g_string_free(sanitized_content, TRUE);

      gumbo_destroy_output(&kGumboDefaultOptions, output);

      g_string_free(content, TRUE);

      json_object_set_string(json_html_obj, "type", html_body->content_type);
      json_object_set_value(root_object, "html", json_html_val);
    }

    // Attachments will include normal attachments, inlines as well alternative
    // contents which did not qualify as main bodies
    JSON_Value *json_atts_value = NULL;

    if (part_collector->attachments->len) {
      json_atts_value = json_value_init_array();
      JSON_Array *json_atts_array = json_value_get_array(json_atts_value);

      for (int i = 0; i < part_collector->attachments->len; i++) {
        CollectedPart *att_body = g_ptr_array_index(part_collector->attachments, i);
        JSON_Value  *json_att_val = json_value_init_object();
        JSON_Object *json_att_obj = json_value_get_object(json_att_val);

        json_object_set_string(json_att_obj, "type", att_body->content_type);
        json_object_set_string(json_att_obj, "disposition", att_body->disposition);
        json_object_set_number(json_att_obj, "partId", att_body->part_id);

        if (!att_body->filename) {
          if (att_body->content_id) {
            att_body->filename = g_strjoin(NULL, "_attachment_", att_body->content_id, ".", guess_content_type_extension(att_body->content_type), NULL);
          } else {
            att_body->filename = g_strjoin(NULL, "_unnamed_attachment.", guess_content_type_extension(att_body->content_type), NULL);
          }
        }
        json_object_set_string(json_att_obj, "filename", att_body->filename);

        json_array_append_value(json_atts_array, json_att_val);
      }
    }

    if (part_collector->inlines->len) {
      if (!json_atts_value)
        json_atts_value = json_value_init_array();

      JSON_Array *json_atts_array = json_value_get_array(json_atts_value);

      for (int i = 0; i < part_collector->inlines->len; i++) {
        CollectedPart *inl_body = g_ptr_array_index(part_collector->inlines, i);
        JSON_Value  *json_inl_val = json_value_init_object();
        JSON_Object *json_inl_obj = json_value_get_object(json_inl_val);

        json_object_set_string(json_inl_obj, "type", inl_body->content_type);
        json_object_set_string(json_inl_obj, "disposition", inl_body->disposition);
        json_object_set_number(json_inl_obj, "partId", inl_body->part_id);

        if (!inl_body->filename) {
          if (inl_body->content_id) {
            inl_body->filename = g_strjoin(NULL, "_inline_", inl_body->content_id, ".", guess_content_type_extension(inl_body->content_type), NULL);
          } else {
            inl_body->filename = g_strjoin(NULL, "_unnamed_inline_content.", guess_content_type_extension(inl_body->content_type), NULL);
          }
        }
        json_object_set_string(json_inl_obj, "filename", inl_body->filename);

        json_array_append_value(json_atts_array, json_inl_val);
      }
    }

    if (part_collector->alternative_bodies->len) {
      if (!json_atts_value)
        json_atts_value = json_value_init_array();

      JSON_Array *json_atts_array = json_value_get_array(json_atts_value);

      for (int i = 0; i < part_collector->alternative_bodies->len; i++) {
        CollectedPart *alt_body = g_ptr_array_index(part_collector->alternative_bodies, i);
        JSON_Value  *json_alt_val = json_value_init_object();
        JSON_Object *json_alt_obj = json_value_get_object(json_alt_val);
        json_object_set_string(json_alt_obj, "type", alt_body->content_type);
        json_object_set_number(json_alt_obj, "partId", alt_body->part_id);

        if (!alt_body->filename) {
          if (alt_body->content_id) {
            // Unlikely, but if contentId is by some case present, use it
            alt_body->filename = g_strjoin(NULL, "_alt_", alt_body->content_id, ".", guess_content_type_extension(alt_body->content_type), NULL);
          } else {
            alt_body->filename = g_strjoin(NULL, "_unnamed_alt_content.", guess_content_type_extension(alt_body->content_type), NULL);
          }
        }
        json_object_set_string(json_alt_obj, "filename", alt_body->filename);

        json_array_append_value(json_atts_array, json_alt_val);
      }
    }

    if (json_atts_value)
      json_object_set_value(root_object, "attachments", json_atts_value);

    free_part_collector(part_collector);
  }

  gchar *serialized_string = json_serialize_to_string(root_value);
  json_value_free(root_value);
  return serialized_string;
}



/*
 *
 *
 */
static void extract_part(GMimeObject *part, PartExtractorData *a_data) {
  GMimeDataWrapper *attachment_wrapper = g_mime_part_get_content_object (GMIME_PART(part));
  GMimeStream *attachment_mem_stream = g_mime_stream_mem_new();
  g_mime_stream_mem_set_owner(GMIME_STREAM_MEM(attachment_mem_stream), FALSE);
  g_mime_data_wrapper_write_to_stream(attachment_wrapper, attachment_mem_stream);
  a_data->content = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(attachment_mem_stream));
  g_object_unref(attachment_mem_stream);
}


/*
 *
 *
 */
static void part_extractor_foreach_callback(GMimeObject *parent, GMimeObject *part, gpointer user_data) {
  PartExtractorData *a_data = (PartExtractorData *) user_data;

  if (GMIME_IS_MESSAGE_PART (part)) {

    if (a_data->recursion_depth < RECURSION_LIMIT) {
      GMimeMessage *message = g_mime_message_part_get_message ((GMimeMessagePart *) part);
      g_mime_message_foreach (message, part_extractor_foreach_callback, a_data);
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
    if (a_data->part_id == 0) {
      // And only if the content type matches
      GMimeContentType *content_type = g_mime_object_get_content_type(part);
      gchar *content_type_str = g_mime_content_type_to_string(content_type);
      gchar *content_type_lower_str = g_ascii_strdown(content_type_str, -1);
      g_free(content_type_str);
      if (!g_ascii_strcasecmp(content_type_lower_str, a_data->content_type))
        extract_part(part, a_data);
      g_free(content_type_lower_str);
    }

    a_data->part_id--;

  } else {
    g_assert_not_reached ();
  }
}


/*
 *
 *
 */
GByteArray *gmime_message_get_part_data(GMimeMessage* message, guint part_id, gchar* content_type) {
  g_return_val_if_fail(message != NULL, NULL);
  g_return_val_if_fail(content_type != NULL, NULL);

  PartExtractorData *a_data = g_malloc(sizeof(PartExtractorData));
  a_data->recursion_depth = 0;
  a_data->part_id = part_id;
  a_data->content_type = content_type;
  a_data->content = NULL;

  g_mime_message_foreach(message, part_extractor_foreach_callback, a_data);

  GByteArray *content = a_data->content;
  g_free(a_data);

  if (!content)
    g_printerr("could not locate partId %d\r\n", part_id);

  return content;
}

