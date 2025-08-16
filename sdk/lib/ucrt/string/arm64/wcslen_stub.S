/* ARM64 wcslen stub - calls C implementation */
    .text
    .align 2
    
    .global wcslen
wcslen:
    /* For now, just use a simple word-by-word implementation */
    mov x1, x0          /* save start address */
1:
    ldrh w2, [x0], #2   /* load 16-bit word and increment */
    cbnz w2, 1b         /* loop if not zero */
    sub x0, x0, x1      /* calculate byte difference */
    lsr x0, x0, #1      /* divide by 2 to get character count */
    sub x0, x0, #1      /* adjust for post-increment */
    ret

    .global wcsnlen
wcsnlen:
    /* x0 = string, x1 = max length */
    mov x2, x0          /* save start address */
    mov x3, x1          /* save max length */
1:
    cbz x3, 2f          /* if max length reached, exit */
    ldrh w4, [x0], #2   /* load 16-bit word and increment */
    cbz w4, 3f          /* if null terminator, exit */
    sub x3, x3, #1      /* decrement max length */
    b 1b
2:
    mov x0, x1          /* return max length */
    ret
3:
    sub x0, x0, x2      /* calculate byte difference */
    lsr x0, x0, #1      /* divide by 2 to get character count */
    sub x0, x0, #1      /* adjust for post-increment */
    ret