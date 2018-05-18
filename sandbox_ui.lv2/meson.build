sbox_master = static_library('sbox_master', 'sandbox_master.c',
	include_directories : [varchunk_incs, mapper_incs, netatom_incs],
	c_args : c_args,
	dependencies : [lv2_dep, lilv_dep])

sbox_slave = static_library('sbox_slave', 'sandbox_slave.c',
	include_directories : [varchunk_incs, mapper_incs, netatom_incs, xpress_incs],
	c_args : c_args,
	dependencies : [lv2_dep, lilv_dep])
