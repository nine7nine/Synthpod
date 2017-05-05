# sandbox_slave
add_library(sandbox_slave.lib OBJECT
	sandbox_slave.c)
set_target_properties(sandbox_slave.lib PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
set_target_properties(sandbox_slave.lib PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto

# sandbox_master
add_library(sandbox_master.lib OBJECT
	sandbox_master.c)
set_target_properties(sandbox_master.lib PROPERTIES POSITION_INDEPENDENT_CODE true) # -fPIC
set_target_properties(sandbox_master.lib PROPERTIES INTERPROCEDURAL_OPTIMIZATION true) # -flto
