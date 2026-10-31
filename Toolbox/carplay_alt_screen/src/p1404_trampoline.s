/*
 * p1404_trampoline.s - arity agnostic wrappers for two libairplay.so ScreenStream
 * entry points, plus real caller capture.
 *
 * Why assembly: ScreenStreamProcessData is called from
 * AirPlayReceiverSessionScreen_ProcessFrames with r0-r3 AND five stack words
 * (measured at 0x2b744..0x2b764), so a C interposer would have to invent a
 * prototype and would corrupt those stack arguments.
 *
 * Why the caller is passed explicitly: inside the C callback our own return
 * address is the trampoline, not the AirPlay code that called us. The original lr
 * is still on the stack here, so it is handed over as a fifth argument, which the
 * observe layer records. sp stays 8-byte aligned across the bl, as AAPCS requires.
 *
 * The verified real entry comes back from the C callback in r0 and is parked over
 * the saved r12 slot; r12 is scratch, so the callee sees an untouched context.
 * If the callback reports 0 the wrapper returns the definite nonzero OSStatus
 * -1 instead of jumping somewhere unproven. Exact stock callers treat zero as
 * success, so a null target must never fabricate success.
 */
    .syntax unified
    .arm
    .text

    /* dio_manager calls this export directly before AirPlay /info. It is an
     * arity-agnostic stock tail-call that only launches the async initializer;
     * r0-r3 and all caller stack words remain untouched for the stock function. */
    .globl  AirPlayReceiverServerCreate
    .type   AirPlayReceiverServerCreate,%function
    .align  2
AirPlayReceiverServerCreate:
    push    {r0-r12, lr}
    bl      alt_bootstrap_server_create
    str     r0, [sp, #48]
    pop     {r0-r11}
    pop     {r12, lr}
    cmp     r12, #0
    beq     .Lserver_create_none
    bx      r12
.Lserver_create_none:
    mvn     r0, #0
    bx      lr
    .size   AirPlayReceiverServerCreate, .-AirPlayReceiverServerCreate

    .globl  ScreenStreamProcessData
    .type   ScreenStreamProcessData,%function
    .align  2
ScreenStreamProcessData:
    push    {r0-r12, lr}
    ldr     r12, [sp, #52]
    sub     sp, sp, #8
    str     r12, [sp]
    bl      alt_observe_proc
    add     sp, sp, #8
    str     r0, [sp, #48]
    pop     {r0-r11}
    pop     {r12, lr}
    cmp     r12, #0
    beq     .Lproc_none
    bx      r12
.Lproc_none:
    mvn     r0, #0
    bx      lr
    .size   ScreenStreamProcessData, .-ScreenStreamProcessData

    .globl  ScreenStreamCreate
    .type   ScreenStreamCreate,%function
    .align  2
ScreenStreamCreate:
    push    {r0-r12, lr}
    ldr     r12, [sp, #52]
    sub     sp, sp, #8
    str     r12, [sp]
    bl      alt_observe_create
    add     sp, sp, #8
    str     r0, [sp, #48]
    pop     {r0-r11}
    pop     {r12, lr}
    cmp     r12, #0
    beq     .Lcrea_none
    bx      r12
.Lcrea_none:
    mvn     r0, #0
    bx      lr
    .size   ScreenStreamCreate, .-ScreenStreamCreate