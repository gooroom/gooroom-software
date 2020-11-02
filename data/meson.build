subdir('appdata')
subdir('icons')
subdir('assets')

compiled_schemas = gnome.compile_schemas()
install_data('kr.gooroom.software.gschema.xml',
             install_dir : 'share/glib-2.0/schemas')

if get_option('external_appstream')
  # replace @libexecdir@
  conf_data = configuration_data()
  conf_data.set('libexecdir', join_paths(get_option('prefix'),
                                         get_option('libexecdir')))
endif
