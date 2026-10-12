# IMAGE_IMPORT_DESCRIPTOR
	.section	.idata$2
	.global	_head_C__dev_playground_SystemC_build_gcc_libsystemc_3_0_2_dll_a
_head_C__dev_playground_SystemC_build_gcc_libsystemc_3_0_2_dll_a:
	.rva	hname	#Ptr to image import by name list
	#this should be the timestamp, but NT sometimes
	#doesn't load DLLs when this is set.
	.long	0	# loaded time
	.long	0	# Forwarder chain
	.rva	__C__dev_playground_SystemC_build_gcc_libsystemc_3_0_2_dll_a_iname	# imported dll's name
	.rva	fthunk	# pointer to firstthunk
#Stuff for compatibility
	.section	.idata$5
	.p2align 3
fthunk:
	.section	.idata$4
hname:
