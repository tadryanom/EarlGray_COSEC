/**********************************************************/
#ifndef __CONF_H__
#define __CONF_H__

#define VERBOSE
#define VIRTUAL_OFFSET  (0xc0000000)

#define INTR_PROFILING  (0)
#define MEM_DEBUG       (1)
#define TASK_DEBUG      (0)
#define INTR_DEBUG      (1)

#endif // __CONF_H__

#ifndef ASM

/**********************************************************/
#ifndef __LANGEXTS__
#define __LANGEXTS__

/*
 *  primitive types
 */
typedef unsigned char	    uint8_t, byte;
typedef unsigned short	    uint16_t;
typedef unsigned int	    uint32_t, uint20_t, uint;
typedef unsigned long long  uint64_t, ulong;

typedef short               int16_t;
typedef long                int32_t;
typedef long long           int64_t;

typedef uint32_t	size_t, ssize_t, ptr_t, index_t, count_t;
typedef int32_t     err_t, off_t;

#define MAX_UINT    (0xFFFFFFFF)
#define MAX_INT    (0x7FFFFFFF)
#define MAX_ULONG   (i(MAX_UINT << 32) | MAX_UINT)

/*
 *  bool type
 */
typedef char bool;
#define true 1
#define false 0
#define not(b) (1-(b))

/*
 *  memory routines and definitions
 */
#define null ((void *)0)

#define reinterpret_cast(t)        *(t *)&

/*
 *  Lists
 */
#define __list

typedef struct {
    void *first;
    void *prev;
    void *next;
} list_node;

#define DLINKED_NAMED_LIST(name)    list_node name;        // named list
#define DLINKED_LIST                DLINKED_NAMED_LIST(link);

#define DLINKED_LIST_INIT(_prev, _next)   \
    .link = { .next = (_next), .prev = (_prev) }
#define DLINKED_NAMED_LIST_INIT(_name, _prev, _next) \
    ._name = { .next = (_next), .prev = (_prev) }

#define list_next(node) (typeof(node))(node->link.next)
#define list_prev(node) (typeof(node))(node->link.prev)

#define nlist_next(name, node) (typeof(node))(node->name.next)
#define nlist_prev(name, node) (typeof(node))(node->name.prev)

#define list_link_next(list, next_node) (list)->link.next = (next_node)
#define list_link_prev(list, prev_node) (list)->link.prev = (prev_node)

#define nlist_link_next(name, node, next_node) (node)->(nlist).next = (next_node)
#define nlist_link_prev(name, node, prev_node) (node)->(nlist).prev = (prev_node)

#define list_insert(_list, new_node)            \
    do {                                        \
        list_link_prev((new_node), null);       \
        list_link_next((new_node), (_list));    \
        if ((_list) != null)                    \
            list_link_prev((_list), (new_node));\
        (_list) = (new_node);                   \
    } while (0)

#define list_foreach(_v, _list) \
    for (_v = (_list); _v; _v = list_next(_v))

#endif // __LANGEXTS__

#ifndef __KSTDLIB_H__
#define __KSTDLIB_H__

#endif //__KSTDLIB_H__

#endif // ASM
