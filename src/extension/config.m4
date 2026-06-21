PHP_ARG_ENABLE([yapf_loader],
  [whether to enable YAPF loader],
  [AS_HELP_STRING([--enable-yapf-loader], [Enable YAPF loader])],
  [no])

if test "$PHP_YAPF_LOADER" != "no"; then
  PHP_NEW_EXTENSION([yapf_loader], [loader.c license.c payload.c stream.c container.c crypto.c sha256.c sealbox.c], [$ext_shared])
  PHP_ADD_BUILD_DIR([$ext_builddir])
  PHP_YAPF_LOADER_CFLAGS="-fvisibility=hidden"
  PHP_SUBST([PHP_YAPF_LOADER_CFLAGS])
  CFLAGS="$CFLAGS $PHP_YAPF_LOADER_CFLAGS"
fi
