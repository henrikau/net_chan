project(
 'timedc_demo',
 'c',
 version: '0.0.1',
 license: 'BSD-3-Clause',
)

deps = dependency('threads')

timedc_avtp = static_library('timedc_avtp',
			'src/timedc_avtp.c',
			include_directories: include_directories('include'),
			dependencies: deps,
			install: true
		       )

mrplib = static_library('mrp',
			'srp/mrp_client.c',
			'srp/talker_mrp_client.c',
			'srp/listener_mrp_client.c',
			include_directories: include_directories('include'),
			dependencies: deps,
			install: true
		       )
install_headers('include/timedc_avtp.h')
install_subdir('include/srp', install_dir: 'include/')

executable(
   'talker',
   'examples/talker.c',
   include_directories: include_directories('include'),
   build_by_default: true,
	dependencies: deps,
   link_with : [timedc_avtp, mrplib])

executable(
   'listener',
   'examples/listener.c',
   include_directories: include_directories('include'),
   build_by_default: true,
	dependencies: deps,
   link_with : timedc_avtp)

t_pdu = executable('testpdu',
	       'test/test_pdu.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       dependencies: deps
	      )

t_nh = executable('testprog',
	       'test/test_nh.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       dependencies: deps
	      )

t_net_fifo = executable('testnetfifo',
	       'test/test_net_fifo.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       dependencies: deps
	      )

t_nf_macro = executable('testnfmacro',
	       'test/test_macro.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       dependencies: deps,
	       link_with : timedc_avtp)

t_mrp =  executable('testmrp',
	       'test/test_mrp.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       dependencies: deps,
	       link_with : mrplib)

test('pdu test', t_pdu)
test('nh test', t_nh)
test('nh net fifo', t_net_fifo)
test('nh macro', t_nf_macro)
test('mrp', t_mrp)
