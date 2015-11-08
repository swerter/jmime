#include <glib.h>
#include <gmime/gmime.h>
#include "parts.h"

#define UTF8_CHARSET "UTF-8"
#define RECURSION_LIMIT 30
#define CITATION_COLOUR 16711680


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


gchar *guess_content_type_extension(const gchar *content_type) {
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
void collector_foreach_callback(GMimeObject *parent, GMimeObject *part, gpointer user_data) {
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

