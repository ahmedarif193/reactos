project(
  'paste',
  'rust',
  version : '1.0.14',
  license : '(MIT or Apache-2.0)',
  meson_version : '>= 1.7.0',
)

rust = import('rust')

lib = rust.proc_macro(
  'paste',
  files('src/lib.rs'),
  override_options : ['rust_std=2018', 'build.rust_std=2018', 'warning_level=0']
)

dep_paste = declare_dependency(
  link_with : [lib],
)
