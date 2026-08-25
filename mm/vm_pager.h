#ifndef _RODNIX_VM_PAGER_H
#define _RODNIX_VM_PAGER_H

#include <stdint.h>
#include "vm_object.h"

/* Свежая обнулённая страница с одной ссылкой — ссылкой вызывающего. */
uint64_t vm_pager_alloc_zero_page(void);

/*
 * Наполнить страницу содержимым объекта по смещению obj_off (байты от
 * начала объекта). Для анонимного объекта и за концом файла — ничего не
 * делает: страница уже нулевая. Единственное место, где путь отказа
 * встречается с задником; никаких замков при чтении не держится.
 */
void vm_pager_fill_page(vm_object_t* obj, uint64_t obj_off, uint64_t phys);

#endif /* _RODNIX_VM_PAGER_H */
