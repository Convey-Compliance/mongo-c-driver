dnl Ignore OpenSSL deprecation warnings on OSX
AS_IF([test "$os_darwin" = "yes"],
      [check_cc_cxx_flag([-Wno-deprecated-declarations], [CFLAGS="$CFLAGS -Wno-deprecated-declarations"])])

AS_IF([test "x$enable_maintainer_flags" = "xyes" && test "x$GCC" = "xyes"],
      [check_cc_cxx_flag([-Wall],                         [CFLAGS="$CFLAGS -Wall"])
       check_cc_cxx_flag([-Waggregate-return],            [CFLAGS="$CFLAGS -Waggregate-return"])
       check_cc_cxx_flag([-Wcast-align],                  [CFLAGS="$CFLAGS -Wcast-align"])
       check_cc_cxx_flag([-Wdeclaration-after-statement], [CFLAGS="$CFLAGS -Wdeclaration-after-statement"])
       check_cc_cxx_flag([-Wempty-body],                  [CFLAGS="$CFLAGS -Wempty-body"])
       check_cc_cxx_flag([-Wformat],                      [CFLAGS="$CFLAGS -Wformat"])
       check_cc_cxx_flag([-Wformat-nonliteral],           [CFLAGS="$CFLAGS -Wformat-nonliteral"])
       check_cc_cxx_flag([-Wformat-security],             [CFLAGS="$CFLAGS -Wformat-security"])
       check_cc_cxx_flag([-Winit-self],                   [CFLAGS="$CFLAGS -Winit-self"])
       check_cc_cxx_flag([-Winline],                      [CFLAGS="$CFLAGS -Winline"])
       check_cc_cxx_flag([-Wmissing-include-dirs],        [CFLAGS="$CFLAGS -Wmissing-include-dirs"])
       check_cc_cxx_flag([-Wno-strict-aliasing],          [CFLAGS="$CFLAGS -Wno-strict-aliasing"])
       check_cc_cxx_flag([-Wno-uninitialized],            [CFLAGS="$CFLAGS -Wno-uninitialized"])
       check_cc_cxx_flag([-Wredundant-decls],             [CFLAGS="$CFLAGS -Wredundant-decls"])
       check_cc_cxx_flag([-Wreturn-type],                 [CFLAGS="$CFLAGS -Wreturn-type"])
       check_cc_cxx_flag([-Wshadow],                      [CFLAGS="$CFLAGS -Wshadow"])
       check_cc_cxx_flag([-Wswitch-default],              [CFLAGS="$CFLAGS -Wswitch-default"])
       check_cc_cxx_flag([-Wswitch-enum],                 [CFLAGS="$CFLAGS -Wswitch-enum"])
       check_cc_cxx_flag([-Wundef],                       [CFLAGS="$CFLAGS -Wundef"])
       check_cc_cxx_flag([-Wuninitialized],               [CFLAGS="$CFLAGS -Wuninitialized"])])
