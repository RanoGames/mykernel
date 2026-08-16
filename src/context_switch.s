/* context_switch.s — сохранить ESP текущей задачи, загрузить ESP новой */

.section .text
.global context_switch

/* void context_switch(uint32_t** old_sp, uint32_t* new_sp);
 * cdecl: [esp] = ret, [esp+4] = old_sp, [esp+8] = new_sp
 * после pusha: +32 байт
 */
context_switch:
    pusha
    movl 36(%esp), %eax      /* old_sp (uint32_t**) */
    movl %esp, (%eax)        /* *old_sp = current esp */
    movl 40(%esp), %esp      /* esp = new_sp */
    popa
    ret
