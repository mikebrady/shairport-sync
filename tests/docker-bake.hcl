group "default" {
  targets = ["shairport-sync-classic-minimum", "shairport-sync-classic-minimum-metadata", "shairport-sync-classic-ffmpeg-minimum", "shairport-sync-classic-dbus", "shairport-sync-classic-mpris", "shairport-sync-classic-maximum", "shairport-sync-classic-maximum-ffmpeg", "shairport-sync-minimum", "shairport-sync-all"]
}

target "shairport-sync-classic-ffmpeg-minimum" {
  context = ".."
  dockerfile = "tests/Dockerfile.testrig"
  target     = "shairport-sync-classic-ffmpeg-minimum"
  tags       = ["shairport-sync:classic-ffmpeg-minimum"]
}

target "shairport-sync-classic-minimum-metadata" {
  context = ".."
  dockerfile = "tests/Dockerfile.testrig"
  target     = "shairport-sync-classic-minimum-metadata"
  tags       = ["shairport-sync:classic-minimum-metadata"]
}

target "shairport-sync-classic-minimum" {
  context = ".."
  dockerfile = "tests/Dockerfile.testrig"
  target     = "shairport-sync-classic-minimum"
  tags       = ["shairport-sync:classic-minimum"]
}

target "shairport-sync-classic-dbus" {
  context = ".."
  dockerfile = "tests/Dockerfile.testrig"
  target     = "shairport-sync-classic-dbus"
  tags       = ["shairport-sync:classic-dbus"]
}

target "shairport-sync-classic-mpris" {
  context = ".."
  dockerfile = "tests/Dockerfile.testrig"
  target     = "shairport-sync-classic-mpris"
  tags       = ["shairport-sync:classic-mpris"]
}

target "shairport-sync-classic-maximum" {
  context = ".."
  dockerfile = "tests/Dockerfile.testrig"
  target     = "shairport-sync-classic-maximum"
  tags       = ["shairport-sync:classic-maximum"]
}

target "shairport-sync-classic-maximum-ffmpeg" {
  context = ".."
  dockerfile = "tests/Dockerfile.testrig"
  target     = "shairport-sync-classic-maximum-ffmpeg"
  tags       = ["shairport-sync:classic-maximum-ffmpeg"]
}

target "shairport-sync-minimum" {
  context = ".."
  dockerfile = "tests/Dockerfile.testrig"
  target     = "shairport-sync-minimum"
  tags       = ["shairport-sync:ap2-minimum"]
}

target "shairport-sync-all" {
  context = ".."
  dockerfile = "tests/Dockerfile.testrig"
  target     = "shairport-sync-with-everything"
  tags       = ["shairport-sync:ap2-all"]
}
