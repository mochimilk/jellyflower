# Test executables, built with -DACRYLIC_BUILD_TESTS=ON. See jellyflower_add_test in
# cmake/Jellyflower.cmake. Each test exits nonzero on failure.
#
#   ctest --test-dir <build> -C Release -L ci        what CI runs
#   ctest --test-dir <build> -C Release -L unit      all pure-logic tests
#   ctest --test-dir <build> -C Release -L desktop   fixtures that open windows (XAML Islands)
#
# MANUAL tests are built but not registered: they need arguments or change the
# live system. Run them by hand (see CLAUDE.md, "Build and test").

# Pure logic. "ci" marks the ones the GitHub workflow runs.
jellyflower_add_test(Autocorrect LABELS unit ci LIBS user32
  DEFINES "JELLYFLOWER_DICTIONARIES=L\"${CMAKE_CURRENT_SOURCE_DIR}/assets/dictionaries\"")
jellyflower_add_test(CompactLogic LABELS unit ci LIBS user32 advapi32)
jellyflower_add_test(DesktopOptions CXX20 LABELS unit ci
  LIBS windowsapp user32 ole32 oleaut32 uuid advapi32 shell32)
jellyflower_add_test(KeyboardLayout LABELS unit ci)
jellyflower_add_test(Localization LABELS unit ci LIBS user32)
jellyflower_add_test(SetupLockLogic LABELS unit ci)
jellyflower_add_test(SetupRecovery RESOURCES LABELS unit ci SOURCES src/startup.cpp
  LIBS user32 shell32 ole32 oleaut32 uuid advapi32 taskschd)
jellyflower_add_test(SolarLogic LABELS unit ci)
jellyflower_add_test(StartupLogic LABELS unit ci SOURCES src/startup.cpp
  LIBS ole32 oleaut32 taskschd advapi32 uuid)
jellyflower_add_test(ThemeLogic LABELS unit ci LIBS user32 advapi32)
jellyflower_add_test(TrayRefresh LABELS unit ci LIBS user32 shell32 advapi32)
jellyflower_add_test(UpdateLogic RESOURCES LABELS unit ci SOURCES src/updates.cpp src/update_download.cpp
  LIBS user32 winhttp bcrypt windowsapp shell32 ole32 advapi32 uuid)

jellyflower_add_test(BridgeNotify LABELS unit SOURCES src/bridge_notify.cpp LIBS user32)
jellyflower_add_test(GreetingLogic LABELS unit SOURCES src/search_identity.cpp
  LIBS windowsapp user32 advapi32 netapi32)
jellyflower_add_test(LocationLifetime CXX20 LABELS unit LIBS windowsapp user32 ole32 oleaut32 uuid advapi32)

# Production adapters hosted in hidden XAML Islands with mocked shell class names.
jellyflower_add_test(AcrylicMaterial XAML LABELS desktop LIBS windowsapp ole32 user32)
jellyflower_add_test(AdaptiveContrast XAML LABELS desktop SOURCES src/adaptive_contrast.cpp
  LIBS user32 ole32 windowsapp dxguid windowscodecs shcore)
jellyflower_add_test(ChevronLayout XAML LABELS desktop SOURCES src/chevron.cpp LIBS user32 windowsapp ole32)
jellyflower_add_test(ClockLayout XAML LABELS desktop SOURCES src/clock.cpp src/clock_options.cpp
  LIBS user32 windowsapp ole32 gdi32 advapi32 shell32 uuid dxguid)
jellyflower_add_test(DockGeometry LABELS unit ci LIBS user32)
jellyflower_add_test(DockLayout XAML LABELS desktop SOURCES src/dock.cpp LIBS user32 gdi32 windowsapp ole32)
jellyflower_add_test(BarHeight LABELS unit ci SOURCES src/bar_height.cpp src/taskbar_slot.cpp LIBS user32 shcore dbghelp winhttp shell32 ole32)
jellyflower_add_test(KeyboardMirror XAML LABELS desktop LIBS windowsapp user32 ole32)
jellyflower_add_test(SearchInput XAML LABELS desktop LIBS windowsapp user32 ole32 dxguid)
jellyflower_add_test(StartFooter XAML LABELS desktop LIBS windowsapp user32 ole32 dxguid)
jellyflower_add_test(StartGrid XAML LABELS desktop LIBS windowsapp user32 ole32 dxguid)
jellyflower_add_test(StartSearch XAML LABELS desktop LIBS windowsapp user32 ole32 gdi32 dxguid)
jellyflower_add_test(StartSpacing XAML LABELS desktop LIBS windowsapp user32 ole32 dxguid)
jellyflower_add_test(StartSurface XAML LABELS desktop LIBS windowsapp user32 ole32 dwmapi gdi32 dxguid)
jellyflower_add_test(TrayOnlyLayout XAML LABELS desktop SOURCES src/tray_only.cpp LIBS user32 windowsapp ole32)
jellyflower_add_test(VolumeLayout XAML LABELS desktop SOURCES src/volume.cpp LIBS user32 windowsapp ole32)

# Need arguments: MenuPresentation <output-folder> [--compositor];
# WallpaperLogic <fresh-output-folder> <assets-folder>.
jellyflower_add_test(MenuPresentation MANUAL RESOURCES
  SOURCES src/cursor_icon.cpp src/updates.cpp src/update_download.cpp src/resources.rc
  LIBS user32 gdi32 gdiplus d2d1 dwrite dwmapi comctl32 msimg32 advapi32 shell32 ole32 oleaut32 uuid
    winhttp bcrypt windowsapp)
jellyflower_add_test(WallpaperLogic MANUAL RESOURCES SOURCES src/wallpaper_state.cpp src/resources.rc
  LIBS user32 ole32 shell32 uuid bcrypt)

# Touch the real shell, the work area or a real cursor pack.
jellyflower_add_test(CompactScreen MANUAL SOURCES src/compact_screen.cpp
  LIBS user32 gdi32 shell32 advapi32 dwmapi shcore d2d1 dwrite)
jellyflower_add_test(CursorIcons MANUAL SOURCES src/cursor_icon.cpp LIBS user32 gdi32)
jellyflower_add_test(CursorLogic MANUAL SOURCES src/cursor_icon.cpp
  LIBS user32 gdi32 advapi32 shell32 ole32 uuid jellyflower_cursor_pack)
# CursorPackLogic <pack.zip> <data-folder> | --watch <pack.zip> <data-folder>
jellyflower_add_test(CursorPackLogic MANUAL LIBS jellyflower_cursor_pack)
jellyflower_add_test(StartConnection MANUAL SOURCES src/start_session.cpp src/search_identity.cpp
  LIBS windowsapp user32 advapi32 netapi32 DEPENDS JellyflowerStart)
jellyflower_add_test(TaskbarLifetime MANUAL SOURCES src/taskbar_probe.cpp LIBS user32)
jellyflower_add_test(TaskbarSystem MANUAL LIBS shell32 user32 advapi32)
