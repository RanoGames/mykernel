/* setjmp.s — minimal i386 setjmp/longjmp for process_exec return */

.global process_setjmp
.global process_longjmp

/* int process_setjmp(uint32_t* buf);
 * buf[0]=ebx, buf[1]=esi, buf[2]=edi, buf[3]=ebp, buf[4]=esp, buf[5]=eip
 * returns 0
 */
process_setjmp:
    movl 4(%esp), %eax          /* buf */
    movl %ebx, 0(%eax)
    movl %esi, 4(%eax)
    movl %edi, 8(%eax)
    movl %ebp, 12(%eax)
    /* esp as seen by caller after return = current esp + 4 (skip return addr) */
    leal 4(%esp), %ecx
    movl %ecx, 16(%eax)
    movl 0(%esp), %ecx          /* return address */
    movl %ecx, 20(%eax)
    xorl %eax, %eax
    ret

/* void process_longjmp(uint32_t* buf, int val);
 * never returns to caller; returns val to setjmp site
 */
process_longjmp:
    movl 4(%esp), %edx          /* buf */
    movl 8(%esp), %eax          /* val */
    testl %eax, %eax
    jnz 1f
    movl $1, %eax               /* longjmp cannot return 0 */
1:
    movl 0(%edx), %ebx
    movl 4(%edx), %esi
    movl 8(%edx), %edi
    movl 12(%edx), %ebp
    movl 16(%edx), %esp
    movl 20(%edx), %ecx         /* eip */
    jmpl *%ecx
