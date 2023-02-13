project(
 'netchan',
 'c',
 version: '0.1.2',
 license: 'MPL-2-Clause',
 default_options: ['-Wall -pedantic -std=c11 -Werror'],
)

deps = dependency('threads')

mrplib = static_library('mrp',
			'srp/mrp_client.c',
			'srp/talker_mrp_client.c',
			'srp/listener_mrp_client.c',
			include_directories: include_directories('include'),
			dependencies: deps,
			install: true
		       )

mrplibso = library('mrp',
			'srp/mrp_client.c',
			'srp/talker_mrp_client.c',
			'srp/listener_mrp_client.c',
			include_directories: include_directories('include'),
			dependencies: deps,
			install: true
		       )

netchan = static_library('netchan',
			     'src/netchan.c',
			     'src/ptp_getclock.c',
			     'src/tracebuffer.c',
			     'src/logger.c',
			include_directories: include_directories('include'),
			dependencies: deps,
			link_with: mrplib,
			install: true
		       )

netchan_so = library('netchan',
				'src/netchan.c',
				'src/ptp_getclock.c',
				'src/tracebuffer.c',
				'src/logger.c',
				include_directories: include_directories('include'),
				dependencies: deps,
				link_with: mrplib,
				install: true
			       )

install_headers(['include/netchan.h', 'include/netchan.hpp', 'include/ptp_getclock.h'])
install_subdir('include/srp', install_dir: 'include/')

executable(
   'talker',
   'examples/talker.c',
   'tools/timedc_args.c',
   include_directories: include_directories('include'),
   build_by_default: true,
	dependencies: deps,
   link_with : [netchan, mrplib])

executable(
   'listener',
   'examples/listener.c',
   'tools/timedc_args.c',
   include_directories: include_directories('include'),
   build_by_default: true,
	dependencies: deps,
   link_with : netchan)

# includes netchan.c directly to test internals
t_pdu = executable('testpdu',
	       'test/test_pdu.c',
	       'src/ptp_getclock.c',
	       'src/tracebuffer.c',
	       'src/terminal.c',
	       'src/logger.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       link_with : mrplib,
	       dependencies: deps
	      )

# includes netchan.c directly to test internals
t_nh = executable('testprog',
	       'test/test_nh.c',
	       'src/ptp_getclock.c',
	       'src/tracebuffer.c',
	       'src/terminal.c',
	       'src/logger.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       link_with : mrplib,
	       dependencies: deps
	      )

# includes netchan.c directly to test internals
t_net_fifo = executable('testnetfifo',
	       'test/test_net_fifo.c',
	       'src/ptp_getclock.c',
	       'src/tracebuffer.c',
	       'src/terminal.c',
	       'src/logger.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       link_with : mrplib,
	       dependencies: deps
	      )

t_nf_macro = executable('testnfmacro',
	       'test/test_macro.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       dependencies: deps,
	       link_with : netchan)

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
