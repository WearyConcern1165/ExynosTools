project('ExynosTools', ['c'], version: '1.2.1-beta3', default_options: ['c_std=c11'])

cc = meson.get_compiler('c')

vulkan_dep = dependency('vulkan', required: false)
dl_dep = cc.find_library('dl', required: true)
pthread_dep = dependency('threads')

srcs = [
  'src/xeno_wrapper.c',
  'src/bc_emulate.c',
  'src/features_patch.c',
  'src/detect.c',
  'src/perf_conf.c',
  'src/logging.c',
  'src/app_profile.c',
]

lib = shared_library('xeno_wrapper',
  srcs,
  dependencies: [vulkan_dep, dl_dep, pthread_dep],
  install: true,
  install_dir: 'usr/lib',
)

install_data('usr/share/meta.json', install_dir: 'usr/share')
install_data('etc/exynostools/performance_mode.conf', install_dir: 'etc/exynostools')
install_subdir('profiles/winlator', install_dir: 'profiles')
install_subdir('assets/shaders/decode', install_dir: 'assets/shaders')

