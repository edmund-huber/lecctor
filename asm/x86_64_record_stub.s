	cmpl	$0, (%r15)
	je	NO_CALL_?NONCE?
	call	WAIT_FOR_TRACER
NO_CALL_?NONCE?:
	movl	?TRACE_BLOCK_ID?, (%r15)
	add	$4, %r15
