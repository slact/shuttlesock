typedef struct sbuf_s sbuf_t;

#define sbuf_list_prepend(list, buf) \
  buf->next = (list).head; \
  if(!(list).tail) { \
    (list).tail = buf; \
  } \
  (list).head = buf

#define sbuf_list_append(list, buf) \
  if((list).tail) { \
    (list).tail->next = buf; \
  } \
  if(!(list).head) { \
    (list).head = buf; \
  } \
  buf->next = NULL;

typedef struct sbuf_s {
  char           *start;
  char           *cur;
  char           *end;
  sbuf_t         *next;
} sbuf_t;
typedef struct {
  sbuf_t         *head;
  sbuf_t         *tail;
} sbuf_list_t;

#define sbuf_list_enqueue sbuf_list_append
#define sbuf_list_dequeue sbuf_list_pop_head

sbuf_t *sbuf_list_pop_head(sbuf_list_t *list);
sbuf_t *sbuf_list_pop_tail(sbuf_list_t *list);
