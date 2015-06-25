{
  'targets': [{
    'target_name': 'electron-weak-<@(target_arch)',
    'sources': [ 'src/weakref.cc' ],
    'include_dirs': [
      '<!(node -e "require(\'nan\')")',
      '<!(node -e "require(\'electron-updater-tools\')")'
    ],
    'target_conditions': [
        [ 'OS=="win"', {
            'msvs_settings': {
                'VCLinkerTool': {
                    'DelayLoadDLLs': [ 'node.dll', 'iojs.exe', 'node.exe' ],
                    # Don't print a linker warning when no imports from either .exe
                    # are used.
                    'AdditionalOptions': [ '/ignore:4199' ],
                },
            },
        }],
    ]
  }]
}
