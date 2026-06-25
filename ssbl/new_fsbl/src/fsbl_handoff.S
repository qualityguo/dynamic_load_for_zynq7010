#ifdef __GNUC__

	.globl FsblHandoffExit

	.section .handoff,"axS"

FsblHandoffExit:
		cpsid	if						/* disable IRQ and FIQ before handoff */
		mov		lr, r0					/* move the destination address into link register */

		mcr		15,0,r0,cr7,cr5,0		/* Invalidate Instruction cache */
		mcr		15,0,r0,cr7,cr5,6		/* Invalidate branch predictor array */

		dsb
		isb								/* make sure it completes */

		ldr		r4, =0
		mcr		15,0,r4,cr1,cr0,0		/* disable the ICache and MMU */

		isb								/* make sure it completes */


		bx		lr						/* force the switch, destination should have been in r0 */

.Ldone:
		b		.Ldone					/* Paranoia: we should never get here */

	.end

#endif
