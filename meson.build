project(
 'netchan',
 'c',
 version: '0.1.2',
 license: 'MPL-2-Clause',
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
			 'src/netchan_socket.c',
			 'src/ptp_getclock.c',
			 'src/netchan_srp_client.c',
			 'src/tracebuffer.c',
			 'src/logger.c',
			include_directories: include_directories('include'),
			dependencies: deps,
			link_with: mrplib,
			install: true
		       )

netchan_so = library('netchan',
		     'src/netchan.c',
		     'src/netchan_socket.c',
		     'src/ptp_getclock.c',
		     'src/tracebuffer.c',
		     'src/logger.c',
		     'src/netchan_srp_client.c',
		     include_directories: include_directories('include'),
		     dependencies: deps,
		     link_with: mrplibso,
		     install: true
		    )

install_headers(['include/netchan.h', 'include/netchan.hpp', 'include/ptp_getclock.h'])
install_subdir('include/srp', install_dir: 'include/')

executable(
   'talker',
   'examples/talker.c',
   'tools/netchan_args.c',
   include_directories: include_directories('include'),
   build_by_default: true,
	dependencies: deps,
   link_with : [netchan, mrplib])

executable(
   'listener',
   'examples/listener.c',
   'tools/netchan_args.c',
   include_directories: include_directories('include'),
   build_by_default: true,
	dependencies: deps,
   link_with : netchan)

# includes netchan.c directly to test internals
t_pdu = executable('testpdu',
		   'test/test_pdu.c',
		   'src/netchan_srp_client.c',
	       'src/ptp_getclock.c',
	       'src/tracebuffer.c',
	       'src/terminal.c',
	       'src/logger.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       link_with : mrplibso,
	       dependencies: deps
	      )

# includes netchan.c directly to test internals
t_nh = executable('testnh',
	       'test/test_nh.c',
	       'src/netchan_srp_client.c',
	       'src/ptp_getclock.c',
	       'src/tracebuffer.c',
	       'src/terminal.c',
	       'src/logger.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       link_with : mrplibso,
	       dependencies: deps
	      )

# includes netchan.c directly to test internals
t_net_fifo = executable('testnetfifo',
	       'test/test_net_fifo.c',
	       'src/netchan_srp_client.c',
	       'src/ptp_getclock.c',
	       'src/tracebuffer.c',
	       'src/terminal.c',
	       'src/logger.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       link_with : mrplibso,
	       dependencies: deps
	      )

t_mrp =  executable('testmrp',
	       'test/test_mrp.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       dependencies: deps,
	       link_with : mrplibso)

t_chan = executable('testchan',
		    'test/test_chan.c',
		    'test/unity.c',
		    include_directories: include_directories('include'),
		    build_by_default: true,
		    dependencies: deps,
		    link_with : netchan)

test('pdu test', t_pdu)
test('nh test', t_nh)
test('nh net fifo', t_net_fifo)
test('channel test', t_chan)

# MRP test is unstable, so it only adds noise
# test('mrp', t_mrp)

# Generate documentation if doxygen is available.
doxygen = find_program('doxygen', required: false)
if doxygen.found()
  message('Doxygen found')
  src_doxygen = files(
    join_paths(meson.source_root(), 'src', 'netchan.c'),
    join_paths(meson.source_root(), 'include', 'logger.h'),
    join_paths(meson.source_root(), 'include', 'netchan.h')
  )

  doc_config = configuration_data()
  doc_config.set('PACKAGE_NAME', meson.project_name())
  doc_config.set('PACKAGE_VERSION', meson.project_version())
  doc_config.set('builddir', meson.current_build_dir())

  config_noop = configuration_data()
  config_noop.set('dummy', 'dummy')
  doxyfiles = []
  foreach f : src_doxygen
	    df = configure_file(input: f,
				output: '@PLAINNAME@',
				configuration: config_noop,
				install: false)
	    doxyfiles += [ df ]
  endforeach

  doxyfile = configure_file(input : 'doxygen.in',
			    output: 'doxygen.out',
			    configuration: doc_config,
			    install: false)

  custom_target('doxygen',
	       input : [doxyfiles, doxyfile] + src_doxygen,
	       output : ['.'],
	       command: [doxygen, doxyfile ],
	       install: false,
	       build_by_default: true)
else
  warning('Docoumentation disabled without doxygen.')
endif
