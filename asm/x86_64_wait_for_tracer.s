	.file	"asm/x86_64_wait_for_tracer.s"
# as-tracer-do-not-instrument
	.text
	.global	WAIT_FOR_TRACER

WAIT_FOR_TRACER:
	push	%r14

	# TODO: if the tracer's there, wait for it to come back to scoop up all
	# the data.

	# Reset r15 to the beginning of the buffer: we should be at
	# buffer_sentinel, so we just need to subtract sizeof(buffer) = 4 * 32
	# = 128.
	# trust-me-i-know-what-im-doing
	movq	%r15, %r14
	# trust-me-i-know-what-im-doing
	subq	$128, %r15

	# Clear out the buffer.
CLEAR_BUFFER_LOOP:
	# trust-me-i-know-what-im-doing
	cmp	%r15, %r14
	je	EXIT_CLEAR_BUFFER_LOOP
	# trust-me-i-know-what-im-doing
	movl	$0, (%r15)
	# trust-me-i-know-what-im-doing
	addq	$4, %r15
	jmp	CLEAR_BUFFER_LOOP
EXIT_CLEAR_BUFFER_LOOP:

	# trust-me-i-know-what-im-doing
	subq	$128, %r15

	pop	%r14
	ret
