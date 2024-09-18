project(
 'netchan',
 'c',
 version: '0.1.2',
 license: 'MPL-2-Clause',
 # default_options: [
 #   'c_std=c11',
 #   'warning_level=2',
 # ],
)

deps = dependency('threads')

netchan = static_library('netchan',
			 'src/netchan.c',
			 'src/netchan_standalone.c',
			 'src/netchan_socket.c',
			 'src/netchan_utils.c',
			 'src/ptp_getclock.c',
			 'src/netchan_srp_client.c',
			 'src/netchan_srp_helper.c',
			 'src/tracebuffer.c',
			 'src/logger.c',
			include_directories: include_directories('include'),
			dependencies: deps,
			override_options: ['b_coverage=false'],
			install: true
		       )

netchan_so = library('netchan',
		     'src/netchan.c',
		     'src/netchan_standalone.c',
		     'src/netchan_socket.c',
		     'src/netchan_utils.c',
		     'src/ptp_getclock.c',
		     'src/tracebuffer.c',
		     'src/logger.c',
		     'src/netchan_srp_client.c',
		     'src/netchan_srp_helper.c',
		     include_directories: include_directories('include'),
		     dependencies: deps,
		     install: true
		    )

install_headers(['include/netchan.h',
		 'include/netchan.hpp',
		 'include/ptp_getclock.h',
		 'include/netchan_srp_client.h',
		 'include/netchan_standalone.h',
		 'include/netchan_utils.h',
		 'include/tracebuffer.h',
		 'include/logger.h'
		])

executable(
   'talker',
   'examples/talker.c',
   'src/netchan_args.c',
   include_directories: include_directories('include'),
   build_by_default: true,
   dependencies: deps,
   override_options: ['b_coverage=false'],
   link_with : [netchan])

executable(
   'invalid_talker',
   'examples/invalid_talker.c',
   include_directories: include_directories('include'),
   build_by_default: true,
   dependencies: deps,
   override_options: ['b_coverage=false'],
   link_with : [netchan])

executable(
   'listener',
   'examples/listener.c',
   'src/netchan_args.c',
   include_directories: include_directories('include'),
   build_by_default: true,
   dependencies: deps,
   override_options: ['b_coverage=false'],
   link_with : netchan)

executable(
   'manychan',
   'examples/manychan.c',
   include_directories: include_directories('include'),
   build_by_default: true,
   dependencies: deps,
   override_options: ['b_coverage=false'],
   link_with : netchan)

executable(
   'sendat_tx',
   'examples/send_at_talker.c',
   'src/netchan_args.c',
   include_directories: include_directories('include'),
   build_by_default: true,
   dependencies: deps,
   override_options: ['b_coverage=false'],
   link_with : [netchan])
executable(
   'sendat_rx',
   'examples/send_at_recv.c',
   'src/netchan_args.c',
   include_directories: include_directories('include'),
   build_by_default: true,
   dependencies: deps,
   override_options: ['b_coverage=false'],
   link_with : [netchan])

# includes netchan.c directly to test internals
t_pdu = executable('testpdu',
		   'test/test_pdu.c',
		   'src/netchan_srp_client.c',
		   'src/netchan_srp_helper.c',
	       'src/ptp_getclock.c',
	       'src/tracebuffer.c',
	       'src/terminal.c',
	       'src/logger.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       dependencies: deps
	      )

# includes netchan.c directly to test internals
t_nh = executable('testnh',
	       'test/test_nh.c',
	       'src/netchan_srp_client.c',
	       'src/netchan_srp_helper.c',
	       'src/ptp_getclock.c',
	       'src/tracebuffer.c',
	       'src/terminal.c',
	       'src/logger.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       dependencies: deps
	      )

# includes netchan.c directly to test internals
t_net_fifo = executable('testnetfifo',
	       'test/test_net_fifo.c',
	       'src/netchan_srp_client.c',
	       'src/netchan_srp_helper.c',
	       'src/ptp_getclock.c',
	       'src/tracebuffer.c',
	       'src/terminal.c',
	       'src/logger.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       dependencies: deps
	      )

t_chan = executable('testchan',
		    'test/test_chan.c',
		    'test/unity.c',
		    include_directories: include_directories('include'),
		    build_by_default: true,
		    dependencies: deps,
		    link_with : netchan_so)

t_tas = executable('testtas',
		    'test/test_tas.c',
		    'test/unity.c',
		    include_directories: include_directories('include'),
		    build_by_default: true,
		    dependencies: deps,
		    link_with : netchan_so)

# include netchan_utils.c directly into test internals
t_utils = executable('testutils',
		    'test/test_utils.c',
		    'src/ptp_getclock.c',
		    'test/unity.c',
		    include_directories: include_directories('include'),
		    build_by_default: true,
		    dependencies: deps)

t_logger = executable('testlogger',
		      'test/test_logger.c',
		      'test/unity.c',
		      include_directories: include_directories('include'),
		      build_by_default: true,
		      dependencies: deps)

test('pdu test', t_pdu)
test('nh test', t_nh)
test('nh net fifo', t_net_fifo)
test('channel test', t_chan)
test('tas test', t_tas)
test('test utils', t_utils)
test('test logger', t_logger)

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
