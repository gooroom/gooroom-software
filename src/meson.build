cargs = ['-DG_LOG_DOMAIN="Gs"']
cargs += ['-DLOCALPLUGINDIR=""']

resources_src = gnome.compile_resources(
  'gs-resources',
  'gnome-software.gresource.xml',
  source_dir : '.',
  c_name : 'gs'
)

gdbus_src = gnome.gdbus_codegen(
  'gs-shell-search-provider-generated',
  'shell-search-provider-dbus-interfaces.xml',
  interface_prefix : 'org.gnome.',
  namespace : 'Gs'
)

gooroom_software_sources = [
  'gs-app-addon-row.c',
  'gs-app-folder-dialog.c',
  'gs-application.c',
  'gs-app-row.c',
  'gs-app-tile.c',
  'gs-auth-dialog.c',
  'gs-category-page.c',
  'gs-category-tile.c',
  'gs-common.c',
  'gs-css.c',
  'gs-content-rating.c',
  'gs-details-page.c',
  'gs-extras-page.c',
  'gs-feature-tile.c',
  'gs-first-run-dialog.c',
  'gs-fixed-size-bin.c',
  'gs-folders.c',
  'gs-hiding-box.c',
  'gs-history-dialog.c',
  'gs-info-bar.c',
  'gs-installed-page.c',
  'gs-language.c',
  'gs-loading-page.c',
  'gs-main.c',
  'gs-moderate-page.c',
  'gs-overview-page.c',
  'gs-page.c',
  'gs-popular-tile.c',
  'gs-prefs-dialog.c',
  'gs-progress-button.c',
  'gs-removal-dialog.c',
  'gs-repos-dialog.c',
  'gs-repo-row.c',
  'gs-review-bar.c',
  'gs-review-dialog.c',
  'gs-review-histogram.c',
  'gs-review-row.c',
  'gs-screenshot-image.c',
  'gs-search-page.c',
  'gs-shell.c',
  'gs-shell-search-provider.c',
  'gs-star-widget.c',
  'gs-summary-tile.c',
  'gs-third-party-repo-row.c',
  'gs-update-dialog.c',
  'gs-update-list.c',
  'gs-update-monitor.c',
  'gs-updates-page.c',
  'gs-updates-section.c',
  'gs-upgrade-banner.c',
  'gs-vendor.c',
  'gs-feature-tile-small.c',
  'gs-consent-page.c'
]

gooroom_software_dependencies = [
  appstream_glib,
  gio_unix,
  gmodule,
  gtk,
  json_glib,
  libm,
  libsecret,
  libsoup
]

if get_option('packagekit')
  gooroom_software_sources += [
    'gs-dbus-helper.c',
  ]
  gooroom_software_sources += gnome.gdbus_codegen(
    'gs-packagekit-generated',
    'org.freedesktop.PackageKit.xml',
    interface_prefix : 'org.freedesktop.',
    namespace : 'Gs'
  )
  gooroom_software_sources += gnome.gdbus_codegen(
    'gs-packagekit-modify2-generated',
    'org.freedesktop.PackageKit.Modify2.xml',
    interface_prefix : 'org.freedesktop.',
    namespace : 'Gs'
  )
  gooroom_software_dependencies += [packagekit]
endif

if get_option('gnome_desktop')
  gooroom_software_dependencies += [gnome_desktop]
endif

if get_option('gspell')
  gooroom_software_dependencies += [gspell]
endif

executable(
  'gooroom-software',
  resources_src,
  gdbus_src,
  sources : gooroom_software_sources,
  include_directories : [
    include_directories('..'),
    include_directories('../lib'),
  ],
  dependencies : gooroom_software_dependencies,
  link_with : [
    libgnomesoftware
  ],
  c_args : cargs,
  install : true,
  install_dir : get_option('bindir')
)

executable(
  'gooroom-software-restarter',
  sources : 'gs-restarter.c',
  include_directories : [
    include_directories('..'),
  ],
  dependencies : [
    gio_unix,
  ],
  c_args : cargs,
  install : true,
  install_dir : get_option('libexecdir')
)

resources_editor_src = gnome.compile_resources(
  'gs-editor-resources',
  'gnome-software-editor.gresource.xml',
  source_dir : '.',
  c_name : 'gs'
)

executable(
  'gooroom-software-editor',
  resources_editor_src,
  sources : [
    'gs-app-tile.c',
    'gs-common.c',
    'gs-css.c',
    'gs-editor.c',
    'gs-summary-tile.c',
    'gs-star-widget.c',
    'gs-feature-tile.c',
    'gs-feature-tile-small.c',
    'gs-upgrade-banner.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../lib'),
  ],
  dependencies : gooroom_software_dependencies,
  link_with : [
    libgnomesoftware
  ],
  c_args : cargs,
  install : true,
  install_dir : get_option('bindir')
)

# no quoting
cdata = configuration_data()
cdata.set('bindir', join_paths(get_option('prefix'),
                               get_option('bindir')))

## replace @bindir@
#configure_file(
#  input : 'kr.gooroom.Software.service.in',
#  output : 'kr.gooroom.Software.service',
#  install: true,
#  install_dir: join_paths(get_option('datadir'), 'dbus-1/services'),
#  configuration : cdata
#)

# replace @bindir@
#configure_file(
#  input : 'gooroom-software-service.desktop.in',
#  output : 'gooroom-software-service.desktop',
#  install: true,
#  install_dir: join_paths(get_option('sysconfdir'), 'xdg/autostart'),
#  configuration : cdata
#)

# replace @bindir@
i18n.merge_file(
  input: 'kr.gooroom.Software.desktop.in',
  output: 'kr.gooroom.Software.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

i18n.merge_file(
  input: 'kr.gooroom.Software.Editor.desktop.in',
  output: 'kr.gooroom.Software.Editor.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

i18n.merge_file(
  input: 'gooroom-software-local-file.desktop.in',
  output: 'gooroom-software-local-file.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

install_data('kr.gooroom.Software-search-provider.ini',
             install_dir : 'share/gnome-shell/search-providers')

if get_option('man')
  xsltproc = find_program('xsltproc')
  custom_target('manfile-gooroom-software',
    input: 'gooroom-software.xml',
    output: 'gooroom-software.1',
    install: true,
    install_dir: join_paths(get_option('mandir'), 'man1'),
    command: [
      xsltproc,
      '--nonet',
      '--stringparam', 'man.output.quietly', '1',
      '--stringparam', 'funcsynopsis.style', 'ansi',
      '--stringparam', 'man.th.extra1.suppress', '1',
      '--stringparam', 'man.authors.section.enabled', '0',
      '--stringparam', 'man.copyright.section.enabled', '0',
      '-o', '@OUTPUT@',
      'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl',
      '@INPUT@'
    ]
  )
  custom_target('manfile-gooroom-software-editor',
    input: 'gooroom-software-editor.xml',
    output: 'gooroom-software-editor.1',
    install: true,
    install_dir: join_paths(get_option('mandir'), 'man1'),
    command: [
      xsltproc,
      '--nonet',
      '--stringparam', 'man.output.quietly', '1',
      '--stringparam', 'funcsynopsis.style', 'ansi',
      '--stringparam', 'man.th.extra1.suppress', '1',
      '--stringparam', 'man.authors.section.enabled', '0',
      '--stringparam', 'man.copyright.section.enabled', '0',
      '-o', '@OUTPUT@',
      'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl',
      '@INPUT@'
    ]
  )
endif

#if get_option('packagekit')
#  # replace @bindir@
#  configure_file(
#    input : 'org.freedesktop.GooroomSoftware.service.in',
#    output : 'org.freedesktop.GooroomSoftware.service',
#    install: true,
#    install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services'),
#    configuration : cdata
#  )
#endif

if get_option('tests')
  cargs += ['-DTESTDATADIR="' + join_paths(meson.current_source_dir(), '..', 'data') + '"']
  e = executable(
    'gs-self-test-src',
    compiled_schemas,
    sources : [
      'gs-css.c',
      'gs-common.c',
      'gs-self-test.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../lib'),
    ],
    dependencies : [
      appstream_glib,
      gio_unix,
      gmodule,
      gtk,
      json_glib,
      libm,
      libsecret,
      libsoup,
    ],
    link_with : [
      libgnomesoftware
    ],
    c_args : cargs
  )
  test('gs-self-test-src', e, env: test_env)
endif
