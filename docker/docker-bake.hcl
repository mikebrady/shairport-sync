group "default" {
  targets = ["shairport-sync-classic-minimum", "shairport-sync-classic-ffmpeg-minimum", "shairport-sync-minimum", "shairport-sync-all"]
}

target "shairport-sync-classic-ffmpeg-minimum" {
  context = ".."
  dockerfile = "docker/Dockerfile.testrig"
  target     = "shairport-sync-classic-ffmpeg-minimum"
  tags       = ["shairport-sync:classic-ffmpeg-minimum"]
}

target "shairport-sync-classic-minimum" {
  context = ".."
  dockerfile = "docker/Dockerfile.testrig"
  target     = "shairport-sync-classic-minimum"
  tags       = ["shairport-sync:classic-minimum"]
}

target "shairport-sync-minimum" {
  context = ".."
  dockerfile = "docker/Dockerfile.testrig"
  target     = "shairport-sync-minimum"
  tags       = ["shairport-sync:ap2-minimum"]
}

target "shairport-sync-all" {
  context = ".."
  dockerfile = "docker/Dockerfile.testrig"
  target     = "shairport-sync-with-everything"
  tags       = ["shairport-sync:ap2-all"]
}
