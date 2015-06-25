{
  'targets': [{
    'target_name': 'weakref-<@(target_arch)',
    'sources': [ 'src/weakref.cc' ],
    'include_dirs': [
      '<!(node -e "require(\'nan\')")',
      '<!(node -e "require(\'electron-updater-tools\')")'
    ]
  }]
}
