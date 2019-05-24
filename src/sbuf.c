#include <shuttlesock/sbuf.h>

sbuf_t *sbuf_list_pop_head(sbuf_list_t *list) {
  sbuf_t *cur = list->head;
  if(!cur) {
    return NULL;
  }
  list->head = cur->next;
  if(list->tail == cur) {
    list->tail = NULL;
  }
  sbuf_list_remove(list, cur);
  return cur;
}
