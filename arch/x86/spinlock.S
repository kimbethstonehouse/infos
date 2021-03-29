// SPDX-License-Identifier: MIT /

/*
 * arch/x86/spinlock.S
 *
 * InfOS
 * Copyright (C) University of Edinburgh 2021.  All Rights Reserved.
 *
 * Tom Spink <tspink@inf.ed.ac.uk>
 */
.text

.align 16

.globl spinlock_acquire
.type spinlock_acquire, %function
spinlock_acquire:
    lock btsl $0, (%rdi)
    jc 1f
    ret

1:
    pause
    testl $1, (%rdi)
    jnz 1b
    jmp spinlock_acquire
.size spinlock_acquire,.-spinlock_acquire

.align 16

.globl spinlock_release
.type spinlock_release, %function
spinlock_release:
    movl $0, (%rdi)
    ret
.size spinlock_release,.-spinlock_release
