#include <string.h>
#include <stdio.h>
#include <glib.h>
#include <gumbo.h>
#include "parts.h"
#include "utils.h"

#define MAX_CID_SIZE 65536
#define MIN_DATA_URI_IMAGE "data:image/gif;base64,R0lGODlhAQABAAAAACwAAAAAAQABAAA="

static gchar* permitted_tags            = "|a|abbr|acronym|address|area|b|bdo|body|big|blockquote|br|button|caption|center|cite|code|col|colgroup|dd|del|dfn|dir|div|dl|dt|em|fieldset|font|form|h1|h2|h3|h4|h5|h6|hr|i|img|input|ins|kbd|label|legend|li|map|menu|ol|optgroup|option|p|pre|q|s|samp|select|small|span|style|strike|strong|sub|sup|table|tbody|td|textarea|tfoot|th|thead|u|tr|tt|u|ul|var|";
static gchar* permitted_attributes      = "|href|src|action|style|color|bgcolor|width|height|colspan|rowspan|cellspacing|cellpadding|border|align|valign|dir|type|";
static gchar* protocol_attributes       = "|href|src|action|";
static gchar* protocol_separators_regex = ":|(&#0*58)|(&#x70)|(&#x0*3a)|(%|&#37;)3A";
static gchar* permitted_protocols       = "|ftp|http|https|cid|data|irc|mailto|news|gopher|nntp|telnet|webcal|xmpp|callto|feed|";
static gchar* empty_tags                = "|area|br|col|hr|img|input|";
static gchar* special_handling          = "|html|body|";
static gchar* no_entity_sub             = "|style|";


// Forward declaration
GString* sanitize(GumboNode* node, GPtrArray* inlines_ary);


static GString *handle_unknown_tag(GumboStringPiece *text) {
  if (text->data == NULL)
    return g_string_new(NULL);

  // work with copy GumboStringPiece to prevent asserts
  // if try to read same unknown tag name more than once
  GumboStringPiece gsp = *text;
  gumbo_tag_from_original_text(&gsp);
  return g_string_new_len(gsp.data, gsp.length);
}


static GString *get_tag_name(GumboNode *node) {
  // work around lack of proper name for document node
  if (node->type == GUMBO_NODE_DOCUMENT)
    return g_string_new("document");

  const gchar *n_tagname = gumbo_normalized_tagname(node->v.element.tag);
  GString *tagname = g_string_new(n_tagname);

  if (!tagname->len) {
    g_string_free(tagname, TRUE);
    return handle_unknown_tag(&node->v.element.original_tag);
  }

  return tagname;
}


static GString *build_doctype(GumboNode *node) {
  GString *results = g_string_new(NULL);
  if (node->v.document.has_doctype) {
    g_string_append(results, "<!DOCTYPE ");
    g_string_append(results, node->v.document.name);
    const gchar *pi = node->v.document.public_identifier;
    if ((node->v.document.public_identifier != NULL) && strlen(pi) ) {
        g_string_append(results, " PUBLIC \"");
        g_string_append(results,node->v.document.public_identifier);
        g_string_append(results,"\" \"");
        g_string_append(results,node->v.document.system_identifier);
        g_string_append(results,"\"");
    }
    g_string_append(results,">\n");
  }
  return results;
}


static GString *build_attributes(GumboAttribute *at, gboolean no_entities, GPtrArray *inlines_ary) {
  gchar *key = g_strjoin(NULL, "|", at->name, "|", NULL);
  gchar *key_pattern = g_regex_escape_string(key, -1);
  g_free(key);

  gboolean is_permitted_attribute = g_regex_match_simple(key_pattern, permitted_attributes, G_REGEX_CASELESS, 0);
  gboolean is_protocol_attribute  = g_regex_match_simple(key_pattern, protocol_attributes, G_REGEX_CASELESS, 0);
  gchar *cid_content_id = NULL;

  g_free(key_pattern);

  if (!is_permitted_attribute)
    return g_string_new(NULL);

  GString *attr_value = g_string_new(at->value);
  gstr_strip(attr_value);

  if (is_protocol_attribute) {
    gchar **protocol_parts = g_regex_split_simple(protocol_separators_regex, attr_value->str, G_REGEX_CASELESS, 0);
    gchar *attr_protocol = g_strjoin(NULL, "|", protocol_parts[0], "|", NULL);

    gchar *attr_prot_pattern = g_regex_escape_string(attr_protocol, -1);
    g_free(attr_protocol);

    gboolean is_permitted_protocol = g_regex_match_simple(attr_prot_pattern, permitted_protocols, G_REGEX_CASELESS, 0);

    if (is_permitted_protocol && !g_ascii_strcasecmp(protocol_parts[0], "cid"))
      cid_content_id = g_strdup(protocol_parts[1]);

    g_strfreev(protocol_parts);
    g_free(attr_prot_pattern);

    if (!is_permitted_protocol) {
      g_string_free(attr_value, TRUE);
      return g_string_new(NULL);
    }
  }

  if (cid_content_id) {
    gboolean cid_replaced = FALSE;
    if (inlines_ary && inlines_ary->len) {
      for (guint i = 0; i < inlines_ary->len; i++) {
        CollectedPart *inline_body = g_ptr_array_index(inlines_ary, i);
        if (!g_ascii_strcasecmp(inline_body->content_id, cid_content_id)) {
          if (inline_body->content->len < MAX_CID_SIZE) {
            gchar *base64_data = g_base64_encode((const guchar *) inline_body->content->data, inline_body->content->len);
            gchar *new_attr_value = g_strjoin(NULL, "data:", inline_body->content_type, ";base64,", base64_data, NULL);
            g_string_assign(attr_value, new_attr_value);
            g_free(new_attr_value);
            cid_replaced = TRUE;
          }
        }
      }
    }

    // `cid` is not a valid URI schema, so if it was not replaced by the inline content,
    // we replace it with a 1x1 image which should hide it. If there is content and we missed
    // it due to the wrong contentId given, it will be avaialable as a downloadable attachment.
    if (!cid_replaced)
      g_string_assign(attr_value, MIN_DATA_URI_IMAGE);

    g_free(cid_content_id);
  }

  GString *atts = g_string_new(" ");
  g_string_append(atts, at->name);

  // how do we want to handle attributes with empty values
  // <input type="checkbox" checked />  or <input type="checkbox" checked="" />

  gchar quote = at->original_value.data[0];

  if (attr_value->len || (quote == '"') || (quote == '\'')) {

    gchar *qs = "";
    if (quote == '\'')
      qs = "'";

    if (quote == '"')
      qs = "\"";

    g_string_append(atts, "=");
    g_string_append(atts, qs);

    if (no_entities) {
      g_string_append(atts, attr_value->str);
    } else {
      GString *subd = gstr_substitute_xml_entities_into_attributes(quote, attr_value->str);
      g_string_append(atts, subd->str);
      g_string_free(subd, TRUE);
    }
    g_string_append(atts, qs);
  }

  g_string_free(attr_value, TRUE);
  return atts;
}



static GString *sanitize_contents(GumboNode* node, GPtrArray *inlines_ary) {
  GString *contents = g_string_new(NULL);
  GString *tagname  = get_tag_name(node);

  gchar *key = g_strjoin(NULL, "|", tagname->str, "|", NULL);
  g_string_free(tagname, TRUE);

  // Since we include pipes (|) we have to escape the regex string
  gchar *key_pattern = g_regex_escape_string(key, -1);
  g_free(key);

  gboolean no_entity_substitution = g_regex_match_simple(key_pattern, no_entity_sub, G_REGEX_CASELESS, 0);
  g_free(key_pattern);

  // build up result for each child, recursively if need be
  GumboVector* children = &node->v.element.children;

  for (guint i = 0; i < children->length; ++i) {
    GumboNode* child = (GumboNode*) (children->data[i]);

    if (child->type == GUMBO_NODE_TEXT) {
      if (no_entity_substitution) {
        g_string_append(contents, child->v.text.text);
      } else {
        GString *subd = gstr_substitute_xml_entities_into_text(child->v.text.text);
        g_string_append(contents, subd->str);
        g_string_free(subd, TRUE);
      }

    } else if (child->type == GUMBO_NODE_ELEMENT ||
               child->type == GUMBO_NODE_TEMPLATE) {

      GString *child_ser = sanitize(child, inlines_ary);
      g_string_append(contents, child_ser->str);
      g_string_free(child_ser, TRUE);

    } else if (child->type == GUMBO_NODE_WHITESPACE) {
      // keep all whitespace to keep as close to original as possible
      g_string_append(contents, child->v.text.text);
    } else if (child->type != GUMBO_NODE_COMMENT) {
      // Does this actually exist: (child->type == GUMBO_NODE_CDATA)
      fprintf(stderr, "unknown element of type: %d\n", child->type);
    }
  }
  return contents;
}


GString *sanitize(GumboNode* node, GPtrArray* inlines_ary) {
  // special case the document node
  if (node->type == GUMBO_NODE_DOCUMENT) {
    GString *results = build_doctype(node);
    GString *node_ser = sanitize_contents(node, inlines_ary);
    g_string_append(results, node_ser->str);
    g_string_free(node_ser, TRUE);
    return results;
  }

  GString *tagname = get_tag_name(node);
  gchar *key = g_strjoin(NULL, "|", tagname->str, "|", NULL);

  gchar *key_pattern = g_regex_escape_string(key, -1);
  g_free(key);


  gboolean need_special_handling     = g_regex_match_simple(key_pattern, special_handling,   G_REGEX_CASELESS, 0);
  gboolean is_empty_tag              = g_regex_match_simple(key_pattern, empty_tags,         G_REGEX_CASELESS, 0);
  gboolean no_entity_substitution    = g_regex_match_simple(key_pattern, no_entity_sub,      G_REGEX_CASELESS, 0);
  gboolean tag_permitted             = g_regex_match_simple(key_pattern, permitted_tags,     G_REGEX_CASELESS, 0);

  g_free(key_pattern);

  if (!need_special_handling && !tag_permitted) {
    g_string_free(tagname, TRUE);
    return g_string_new(NULL);
  }

  GString *close = g_string_new(NULL);
  GString *closeTag = g_string_new(NULL);
  GString *atts = g_string_new(NULL);

  const GumboVector *attribs = &node->v.element.attributes;
  for (int i=0; i< attribs->length; ++i) {
    GumboAttribute* at = (GumboAttribute*)(attribs->data[i]);
    GString *attsstr = build_attributes(at, no_entity_substitution, inlines_ary);
    g_string_append(atts, attsstr->str);
    g_string_free(attsstr, TRUE);
  }

  if (node->type == GUMBO_NODE_ELEMENT) {
    if ((node->v.element.tag == GUMBO_TAG_A) ||
        (node->v.element.tag == GUMBO_TAG_FORM))
      g_string_append(atts, " target=\"_blank\"");

    if (node->v.element.tag == GUMBO_TAG_FORM)
      g_string_append(atts, " onSubmit=\"return confirm('This form submits to an external URL. Are you sure you want to proceed?');\"");
  }

  if (is_empty_tag) {
    g_string_append_c(close, '/');
  } else {
    g_string_append_printf(closeTag, "</%s>", tagname->str);
  }

  GString *contents = sanitize_contents(node, inlines_ary);

  if (need_special_handling) {
    gstr_strip(contents);
    g_string_append_c(contents, '\n');
  }

  GString *results = g_string_new(NULL);
  g_string_append_printf(results, "<%s%s%s>", tagname->str, atts->str, close->str);
  g_string_free(atts, TRUE);

  g_string_free(tagname, TRUE);

  if (need_special_handling)
    g_string_append_c(results, '\n');

  g_string_append(results, contents->str);
  g_string_free(contents, TRUE);

  g_string_append(results, closeTag->str);

  if (need_special_handling)
    g_string_append_c(results, '\n');

  g_string_free(close, TRUE);
  g_string_free(closeTag, TRUE);

  return results;
}