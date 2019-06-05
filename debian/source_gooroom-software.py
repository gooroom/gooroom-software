import apport.packaging

def add_info(report, ui):
    report["InstalledPlugins"] = apport.hookutils.package_versions(
        'gooroom-software-plugin-flatpak',
        'gooroom-software-plugin-snap')
