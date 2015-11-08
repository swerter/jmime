#include <glib.h>
#include <gmime/gmime.h>
#include <gumbo.h>

#include "parson/parson.h"
#include "parts.h"

#include "collector.h"
#include "sanitizer.h"
#include "textizer.h"


#define MAX_HTML_PREVIEW_LENGTH 512

/*
 *
 *
 */
guint collect_addresses(InternetAddressList *list, JSON_Value *addresses_val) {

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
GString *gmime_message_to_json(GMimeMessage *message, gboolean include_content) {
  JSON_Value *root_value = json_value_init_object();
  JSON_Object *root_object = json_value_get_object(root_value);

  json_object_set_string(root_object, "messageId", g_mime_message_get_message_id(message));

  // From
  const gchar *from = g_mime_message_get_sender(message);
  InternetAddressList *from_addresses = internet_address_list_parse_string(from);

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
        json_object_set_number(json_att_obj, "size",     att_body->content->len);

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
        json_object_set_number(json_inl_obj, "size",     inl_body->content->len);

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
        json_object_set_number(json_alt_obj, "size",     alt_body->content->len);


        json_array_append_value(json_atts_array, json_alt_val);
      }
    }

    if (json_atts_value)
      json_object_set_value(root_object, "attachments", json_atts_value);

    free_part_collector(part_collector);
  }

  gchar *serialized_string = json_serialize_to_string(root_value);
  json_value_free(root_value);

  GString *json_string = g_string_new(serialized_string);
  g_free(serialized_string);

  return json_string;
}