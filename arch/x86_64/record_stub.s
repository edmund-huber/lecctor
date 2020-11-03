# The upcoming `add` will affect OF, SF, ZF, AF, CF, PF, all of which reside in
# the flags register, so we need to preserve flags now, which means we need to
# preserve %rax first, since `lahf` will clobber %ah.
	movq	%rax, %fs:per_thread_trace_context@tpoff
	lahf
# Swap in a stack that is completely under our control. (Note: stack grows
# down, so %rsp points at the top of the allocated stack.)
	movq	%rsp, %fs:per_thread_trace_context+8@tpoff
	movq	%fs:0, %rsp
	addq	$per_thread_trace_stack+1024000@tpoff, %rsp
# .. and store just enough context (including rflags) so that we can execute
# the few instructions here without affecting the rest of the program.
	sahf
	pushfq
	pushq	%rbx
# Copy in the pointer (to rbx) and the offset (to rax), and see if we have
# bumped into the sentinel value yet. If we have, it means that we've run out
# of buffer and we need it to get copied/cleared out.
	movq	%fs:0, %rbx
	addq	$per_thread_trace_buffer@tpoff, %rbx
	movq	%fs:per_thread_trace_context+16@tpoff, %rax
	cmpl	$0, (%rbx,%rax)
	je	NO_CALL_?NONCE?
	call	wait_for_tracer_wrapper
# If we got here after a call to wait_for_tracer(_wrapper), rax will be 0 (via
# return value), because the buffer has been reset. Otherwise, it will be
# (remain) the offset value that was fetched from the per_thread_trace struct.
# Now save the TRACE_CHUNK_ID and the new offset.
NO_CALL_?NONCE?:
	movl	?TRACE_CHUNK_ID?, (%rbx,%rax)
	add	$4, %rax
	movq	%rax, %fs:per_thread_trace_context+16@tpoff
# Restore the program state. We were never here.
	popq	%rbx
	popfq
	movq	%fs:per_thread_trace_context+8@tpoff, %rsp
	movq	%fs:per_thread_trace_context@tpoff, %rax
