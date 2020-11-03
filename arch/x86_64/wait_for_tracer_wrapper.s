.global wait_for_tracer_wrapper

# The record stub preserves the registers that it needs for its immediate
# purposes, but doesn't preserve enough registers such that we can call into an
# arbitrary C function without getting some registers clobbered. This saves,
# then restores everything.
wait_for_tracer_wrapper:
	sub	$112, %rsp
	# We own %rax.
	movq	%rbx, 0(%rsp)
	movq	%rcx, 8(%rsp)
	movq	%rdx, 16(%rsp)
	# %rsp is saved by the record stub.
	movq	%rbp, 24(%rsp)
	movq	%rsi, 32(%rsp)
	movq	%rdi, 40(%rsp)
	movq	%r8, 48(%rsp)
	movq	%r9, 56(%rsp)
	movq	%r10, 64(%rsp)
	movq	%r11, 72(%rsp)
	movq	%r12, 80(%rsp)
	movq	%r13, 88(%rsp)
	movq	%r14, 96(%rsp)
	movq	%r15, 104(%rsp)
# https://www.felixcloutier.com/x86/xsave for FPU, MMX, SSE, etc.
	movq	$-1, %rax
	movq	$-1, %rdx
	movq	%fs:0, %rbx
	addq	$per_thread_trace_xsave@tpoff, %rbx
	xsave	(%rbx)
# We're finally ready!
	call wait_for_tracer
# Do the inverse of the above. Note: we need to preserve %rax since that holds
# the return value from wait_for_tracer.
	pushq	%rax
	movq	$-1, %rax
	movq	$-1, %rdx
	movq	%fs:0, %rbx
	addq	$per_thread_trace_xsave@tpoff, %rbx
	xrstor	(%rbx)
	popq	%rax
	# We own %rax.
	movq	0(%rsp), %rbx
	movq	8(%rsp), %rcx
	movq	16(%rsp), %rdx
	# %rsp is saved by the record stub.
	movq	24(%rsp), %rbp
	movq	32(%rsp), %rsi
	movq	40(%rsp), %rdi
	movq	48(%rsp), %r8
	movq	56(%rsp), %r9
	movq	64(%rsp), %r10
	movq	72(%rsp), %r11
	movq	80(%rsp), %r12
	movq	88(%rsp), %r13
	movq	96(%rsp), %r14
	movq	104(%rsp), %r15
	add	$112, %rsp
	ret
