# qemu-passthrough.nix
{ pkgs ? import <nixpkgs> {}, ... }:

pkgs.stdenv.mkDerivation rec {
  pname = "qemu-passthrough";
  version = "8.1.2";

  src = builtins.path {
    path = ~/software/Jetson/Linux_for_Tegra/sources/kernel/qemu-passthrough;
    name = "qemu-source";
  };

  buildInputs = with pkgs; [
    util-linux
    pkg-config
    # mktemp
    autogen
    automake
    flex
    bison
    meson
    ninja
    cmake
    json_c
    gcc
    gnumake
    libtool
    valgrind
    python3
    glib
    dbus
    pixman
    zlib
    bzip2
    lzo
    libgpiod
    snappy
    curl
    libssh
    libcap
    libepoxy
    nettle
    attr
    systemd
    liburing
    makeWrapper
    mktemp
    libdrm
    wayland-protocols
    sphinx
    python311Packages.sphinx-rtd-theme
    SDL2
    gtk3
    gnutls
    libslirp
    libselinux
    alsa-lib
    alsa-oss
    pulseaudio
    pipewire
    acpica-tools
    pam_p11
    pam_u2f
    vte
    libibumad
    libnfs
    libseccomp
    libxkbcommon
    libcacard
    libusb1
    libaio
    libcap_ng
    libtasn1
    libgcrypt
    keyutils
    canokey-qemu
    fuse3
    libbpf
    capstone
    fdtools
    vde2
    texinfo
    spice
    virglrenderer
    multipath-tools
    ncurses
    sealcurses
    lzfse
    gsasl
    xgboost
    libvncserver
    cmocka
    basez
  ];

  configurePhase = ''
    ./configure --target-list=aarch64-softmmu \
    --enable-sdl --enable-gtk \
    --enable-vhost-kernel --enable-vhost-net --enable-vhost-user \
    --enable-vhost-user-blk-server --enable-vfio-user-server \
    --enable-vhost-crypto --enable-vhost-vdpa --enable-virtfs \
    --enable-virtfs-proxy-helper \
    --disable-dbus-display \
    --disable-docs \
    --prefix=$out \
  '';
  /*
   --sysconfdir=${lib}$(if isDebug then "/debug" else "") \
   --disable-werror \
   --without-examples \
   --docdir=${share}/doc/${pname}-${version};
   */


  buildPhase = ''
    make -j12
  '';

  installPhase = ''
    make install && \
    [ -x $out/bin/qemu-system-aarch64 ] && \
    ln -s $out/bin/qemu-system-aarch64 $out/bin/qemu-passthrough;
  '';

  meta = with pkgs.lib; {
    description = "QEMU with passthrough modifications for Ghaf";
    homepage = "https://github.com/KimGSandstrom/qemu-passthrough";
    license = licenses.gpl2Plus;
    maintainers = with maintainers; [ KimGSandstrom ];
    platforms = platforms.linux;
  };
}

