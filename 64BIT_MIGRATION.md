# Миграция на 64-битный режим

## Обзор изменений

Переход с 32-битного (i386) на 64-битный (x86_64) режим требует значительных изменений во всех компонентах ядра.

---

## 1. Инструменты сборки

### Makefile
```makefile
# Было:
CC = i686-elf-gcc
LD = i686-elf-ld
CFLAGS = -m32 ...
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T link.ld

# Должно быть:
CC = x86_64-elf-gcc
LD = x86_64-elf-ld
CFLAGS = -m64 -mcmodel=kernel -mno-red-zone ...
ASFLAGS = -f elf64
LDFLAGS = -m elf_x86_64 -T link.ld
```

### link.ld
```ld
# Было:
OUTPUT_FORMAT(elf32-i386)
. = 1M;

# Должно быть:
OUTPUT_FORMAT(elf64-x86-64)
. = 0x100000;  /* 1MB, но теперь 64-битные адреса */
```

**Изменения:**
- `OUTPUT_FORMAT(elf64-x86-64)`
- Адреса могут быть выше 4GB
- High-half обычно `0xFFFFFFFF80000000` (канонический адрес)

---

## 2. Типы данных

### include/types.h
```c
// Было:
typedef uint32_t uintptr_t;
typedef uint32_t size_t;

// Должно быть:
typedef uint64_t uintptr_t;
typedef uint64_t size_t;  // или оставить 32-bit для совместимости
```

**Изменения:**
- Все указатели становятся 64-битными
- `uintptr_t` и `intptr_t` должны быть 64-битными
- `size_t` обычно 64-битный в 64-битном режиме

---

## 3. Пейджинг (критическое изменение!)

### Структура страниц

**32-bit (текущая):**
- 2 уровня: Page Directory (1024 PDE) → Page Table (1024 PTE)
- Размер записи: 32 бита (4 байта)
- Размер страницы: 4KB

**64-bit (x86_64):**
- 4 уровня: PML4 → PDPT → PD → PT
- Размер записи: 64 бита (8 байт)
- Размер страницы: 4KB (или 2MB/1GB с большими страницами)

### kernel/paging.c

**Полная переработка структуры:**

```c
// Было:
typedef struct {
    uint32_t present : 1;
    uint32_t rw : 1;
    // ... 32-битные поля
    uint32_t frame : 20;
} pte_t;

// Должно быть:
typedef struct {
    uint64_t present : 1;
    uint64_t rw : 1;
    uint64_t user : 1;
    uint64_t pwt : 1;
    uint64_t pcd : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t pat : 1;
    uint64_t global : 1;
    uint64_t available : 3;
    uint64_t frame : 40;  // 40 бит для физического адреса (до 1TB)
    uint64_t available2 : 11;
    uint64_t nx : 1;  // No-Execute bit (64-bit only)
} pte64_t;

// PML4 Entry (Page Map Level 4)
typedef struct {
    uint64_t present : 1;
    uint64_t rw : 1;
    uint64_t user : 1;
    uint64_t pwt : 1;
    uint64_t pcd : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t size : 1;  // 0 = 4KB, 1 = 1GB
    uint64_t global : 1;
    uint64_t available : 3;
    uint64_t frame : 40;
    uint64_t available2 : 11;
    uint64_t nx : 1;
} pml4e_t;

// PDPT Entry (Page Directory Pointer Table)
typedef struct {
    uint64_t present : 1;
    uint64_t rw : 1;
    uint64_t user : 1;
    uint64_t pwt : 1;
    uint64_t pcd : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t size : 1;  // 0 = 4KB, 1 = 2MB
    uint64_t global : 1;
    uint64_t available : 3;
    uint64_t frame : 40;
    uint64_t available2 : 11;
    uint64_t nx : 1;
} pdpte_t;

// PD Entry (Page Directory)
typedef struct {
    uint64_t present : 1;
    uint64_t rw : 1;
    uint64_t user : 1;
    uint64_t pwt : 1;
    uint64_t pcd : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t size : 1;  // 0 = 4KB, 1 = 2MB
    uint64_t global : 1;
    uint64_t available : 3;
    uint64_t frame : 40;
    uint64_t available2 : 11;
    uint64_t nx : 1;
} pde64_t;
```

**Макросы для адресации:**
```c
// Было:
#define PAGE_DIR_INDEX(addr) (((addr) >> 22) & 0x3FF)
#define PAGE_TABLE_INDEX(addr) (((addr) >> 12) & 0x3FF)

// Должно быть (64-bit):
#define PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)  // 9 бит
#define PDPT_INDEX(addr) (((addr) >> 30) & 0x1FF)  // 9 бит
#define PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)  // 9 бит
#define PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)  // 9 бит
```

**CR3 регистр:**
- В 64-bit режиме CR3 содержит физический адрес PML4 (не Page Directory!)
- CR3 должен быть выровнен на 4KB
- Младшие 12 бит используются для флагов (PCID в новых процессорах)

---

## 4. Регистры и inline assembly

### Регистры

**32-bit:**
- `eax`, `ebx`, `ecx`, `edx`, `esi`, `edi`, `ebp`, `esp`
- `cr0`, `cr3`, `cr4`

**64-bit:**
- `rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi`, `rbp`, `rsp`
- Дополнительные: `r8`, `r9`, `r10`, `r11`, `r12`, `r13`, `r14`, `r15`
- `cr0`, `cr3`, `cr4`, `cr8` (APIC)

### Inline assembly

**kernel/paging.c:**
```c
// Было:
__asm__ volatile (
    "mov %0, %%eax\n\t"
    "mov %%eax, %%cr3"
    :: "r"(phys_dir) : "eax", "memory");

// Должно быть:
__asm__ volatile (
    "mov %0, %%rax\n\t"
    "mov %%rax, %%cr3"
    :: "r"(pml4_phys) : "rax", "memory");
```

**Важно:**
- Использовать `rax` вместо `eax`
- Использовать `rsp` вместо `esp`
- Все регистры 64-битные

---

## 5. GDT (Global Descriptor Table)

### boot/gdt.c и boot/gdt_flush.S

**32-bit:**
- Плоские сегменты (flat segments)
- Базовый адрес = 0, лимит = 0xFFFFFFFF

**64-bit:**
- GDT все еще нужен, но сегменты работают по-другому
- CS и SS сегменты игнорируются (кроме базового адреса для FS/GS)
- Код и данные сегменты должны иметь:
  - `L = 1` (Long mode bit)
  - `D/B = 0` (для 64-bit)
  - `G = 1` (Granularity)

**Пример 64-bit GDT:**
```c
// Null descriptor
gdt[0] = 0;

// Kernel code segment (64-bit)
gdt[1] = 0x00AF9A000000FFFF;  // L=1, D=0, Present, Code, Executable, Readable

// Kernel data segment (64-bit)
gdt[2] = 0x00CF92000000FFFF;  // Present, Data, Writable

// User code segment (64-bit)
gdt[3] = 0x00AFFA000000FFFF;  // User, Code

// User data segment (64-bit)
gdt[4] = 0x00CFF2000000FFFF;  // User, Data
```

---

## 6. IDT (Interrupt Descriptor Table)

### Структура IDT записи

**32-bit:**
```c
struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_high;
};
```

**64-bit:**
```c
struct idt_entry64 {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  ist;      // Interrupt Stack Table
    uint8_t  flags;
    uint16_t base_mid;
    uint32_t base_high;
    uint32_t reserved;
};
```

**Изменения:**
- 16 байт вместо 8 байт
- Поддержка IST (Interrupt Stack Table)
- 64-битные адреса обработчиков

---

## 7. Регистры прерываний

### include/isr.h

**32-bit:**
```c
struct registers {
    uint32_t ds, es, fs, gs;
    uint32_t edi, esi, ebp, esp_orig;
    uint32_t ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
    uint32_t useresp, ss;
};
```

**64-bit:**
```c
struct registers64 {
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rsp_orig;
    uint64_t rbx, rdx, rcx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags;
    uint64_t rsp, ss;
};
```

**Изменения:**
- Все регистры 64-битные
- Добавлены r8-r15
- `eip` → `rip`, `eflags` → `rflags`
- Порядок push/pop другой

---

## 8. Multiboot2

### boot/boot.S

**32-bit:**
- Multiboot2 загружает ядро в 32-bit режиме
- Нужно переключиться в 64-bit режим вручную

**64-bit:**
- Multiboot2 может загрузить в 64-bit режиме напрямую
- Или загрузить в 32-bit и переключиться в 64-bit

**Переключение в 64-bit режим:**
1. Включить PAE (CR4.PAE = 1)
2. Установить CR3 на PML4
3. Включить Long Mode (EFER.LME = 1)
4. Включить пейджинг (CR0.PG = 1)
5. Переключиться в 64-bit код сегмент

---

## 9. Адресация памяти

### include/vmm.h

**32-bit:**
```c
#define KERNEL_VIRT_BASE 0xC0000000  // 3GB
#define VIRT_TO_PHYS(addr) ((addr) - KERNEL_VIRT_BASE)
#define PHYS_TO_VIRT(addr) ((addr) + KERNEL_VIRT_BASE)
```

**64-bit:**
```c
#define KERNEL_VIRT_BASE 0xFFFFFFFF80000000  // Канонический адрес
#define VIRT_TO_PHYS(addr) ((uint64_t)(addr) - KERNEL_VIRT_BASE)
#define PHYS_TO_VIRT(addr) ((uint64_t)(addr) + KERNEL_VIRT_BASE)
```

**Канонические адреса:**
- В 64-bit режиме используются канонические адреса
- Высокие 16 бит должны быть одинаковыми (0xFFFF или 0x0000)
- Kernel обычно в `0xFFFFFFFF80000000` (высокий канонический адрес)

---

## 10. Calling Conventions

**32-bit (cdecl):**
- Параметры передаются через стек (справа налево)
- Возвращаемое значение в `eax`
- Caller очищает стек

**64-bit (System V ABI):**
- Первые 6 параметров в регистрах: `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`
- Остальные параметры в стеке
- Возвращаемое значение в `rax`
- Caller очищает стек
- Red zone: 128 байт ниже `rsp` зарезервированы

**Важно:**
- Компилятор должен использовать правильные соглашения
- `-mno-red-zone` для kernel кода (kernel не может использовать red zone)

---

## 11. Стек

### boot/boot.S

**32-bit:**
```asm
stack_bottom:
    resb 32768
stack_top:
```

**64-bit:**
```asm
stack_bottom:
    resq 4096  ; 4096 * 8 = 32768 байт
stack_top:
```

**Изменения:**
- Указатели стека 64-битные
- Размер стека обычно такой же, но адреса 64-битные

---

## 12. QEMU

### Makefile

**32-bit:**
```makefile
qemu-system-i386 ...
```

**64-bit:**
```makefile
qemu-system-x86_64 ...
```

---

## 13. Проверка поддержки 64-bit

Перед переключением в 64-bit режим нужно проверить:
- CPUID.80000001.EDX[29] = 1 (Long Mode support)
- CPUID.80000001.EDX[26] = 1 (1GB pages support, опционально)

---

## Приоритет изменений

1. **Критично (не работает без этого):**
   - Пейджинг (4-уровневая структура)
   - GDT (64-bit сегменты)
   - IDT (64-bit записи)
   - Типы данных (uintptr_t, указатели)
   - Inline assembly (регистры)

2. **Важно (нужно для полной функциональности):**
   - Регистры прерываний
   - Адресация памяти
   - Calling conventions
   - Стек

3. **Желательно (улучшения):**
   - Проверка поддержки 64-bit
   - Оптимизация под 64-bit
   - Использование новых регистров (r8-r15)

---

## Рекомендации

1. **Постепенная миграция:**
   - Начать с типов данных и указателей
   - Затем пейджинг
   - Затем GDT/IDT
   - Затем остальное

2. **Тестирование:**
   - Тестировать каждый компонент отдельно
   - Использовать QEMU для отладки
   - Проверять работу в 64-bit режиме

3. **Документация:**
   - AMD64 Architecture Manual
   - Intel 64 and IA-32 Architecture Manual
   - OSDev Wiki (64-bit страницы)

---

## Полезные ссылки

- [OSDev 64-bit Tutorial](https://wiki.osdev.org/Setting_Up_Long_Mode)
- [AMD64 Architecture Manual](https://www.amd.com/en/support/tech-docs)
- [Intel 64 Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)

