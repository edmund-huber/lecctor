	pushf
	cmpl	$0, (%r15)
	je	NO_CALL_?NONCE?
	call	wait_for_tracer
NO_CALL_?NONCE?:
	movl	?TRACE_BLOCK_ID?, (%r15)
	add	$4, %r15
	popf
