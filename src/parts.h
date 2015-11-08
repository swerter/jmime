typedef struct CollectedPart {
  guint      part_id;        // the depth within the message where this part is located
  gchar      *content_type;  // content type (text/html, text/plan etc.)
  GByteArray *content;       // content data
  gchar      *content_id;    // for inline content
  gchar      *filename;      // for attachments, inlines and body parts that define filename
  gchar      *disposition;   // for attachments and inlines
} CollectedPart;


typedef struct PartCollectorData {
  guint         recursion_depth;  // We keep track of explicit recursions, and limit them (RECURSION_LIMIT)
  guint         part_id;          // We keep track of the depth within message parts to identify parts later
  CollectedPart *html_body;
  CollectedPart *text_body;
  GPtrArray     *alternative_bodies;
  GPtrArray     *inlines;
  GPtrArray     *attachments;
} PartCollectorData;


typedef struct PartExtractorData {
  guint      recursion_depth;
  guint      part_id;
  gchar      *content_type;
  GByteArray *content;
} PartExtractorData;

void free_collected_part(gpointer part);
void free_part_collector(PartCollectorData *pcdata);

CollectedPart*     new_collected_part(guint part_id);
PartCollectorData* new_part_collector_data(void);