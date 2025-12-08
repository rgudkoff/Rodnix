#include "../include/pic.h"
#include "../include/ports.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_ICW4      0x01
#define ICW1_SINGLE    0x02
#define ICW1_INTERVAL4 0x04
#define ICW1_LEVEL     0x08
#define ICW1_INIT      0x10

#define ICW4_8086       0x01
#define ICW4_AUTO       0x02
#define ICW4_BUF_SLAVE 0x08
#define ICW4_BUF_MASTER 0x0C
#define ICW4_SFNM       0x10

void pic_remap(uint8_t offset1, uint8_t offset2)
{
    /* Сохранить старые маски */
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);
    
    /* Инициализация PIC */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    
    outb(PIC1_DATA, offset1);
    outb(PIC2_DATA, offset2);
    
    outb(PIC1_DATA, 4);   /* Master PIC: slave на IRQ2 */
    outb(PIC2_DATA, 2);   /* Slave PIC: cascade identity */
    
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);
    
    /* Восстановить маски (но можно оставить все размаскированными для начала) */
    outb(PIC1_DATA, 0xFF);  /* Маскировать все IRQ на PIC1 */
    outb(PIC2_DATA, 0xFF);  /* Маскировать все IRQ на PIC2 */
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

void pic_mask(uint8_t irq)
{
    uint16_t port;
    uint8_t value;
    
    if (irq < 8)
    {
        port = PIC1_DATA;
    }
    else
    {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) | (1 << irq);
    outb(port, value);
}

void pic_unmask(uint8_t irq)
{
    uint16_t port;
    uint8_t value;
    
    if (irq < 8)
    {
        port = PIC1_DATA;
    }
    else
    {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

