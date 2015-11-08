#include <glib.h>
#include "parts.h"

void free_collected_part(gpointer part) {
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


void free_part_collector(PartCollectorData *pcdata) {
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


CollectedPart* new_collected_part(guint part_id) {
  CollectedPart *part = g_malloc(sizeof(CollectedPart));

  part->part_id      = part_id;
  part->content_type = NULL;
  part->content      = NULL;
  part->content_id   = NULL;
  part->filename     = NULL;
  part->disposition  = NULL;

  return part;
}


PartCollectorData* new_part_collector_data(void) {
  PartCollectorData *pcd = g_malloc(sizeof(PartCollectorData));

  pcd->recursion_depth = 0;
  pcd->part_id         = 0;

  pcd->text_body = NULL;
  pcd->html_body = NULL;

  pcd->alternative_bodies = g_ptr_array_new_with_free_func((GDestroyNotify) free_collected_part);
  pcd->attachments        = g_ptr_array_new_with_free_func((GDestroyNotify) free_collected_part);
  pcd->inlines            = g_ptr_array_new_with_free_func((GDestroyNotify) free_collected_part);

  return pcd;
}
