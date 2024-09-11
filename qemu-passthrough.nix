# qemu-passthrough.nix
# build with command: nix-build qemu-passthrough.nix
{ pkgs ? import <nixpkgs> {}, ... }:

pkgs.stdenv.mkDerivation rec {
  pname = "qemu-passthrough";
  version = "1.1";

  src = builtins.path {
    path = ~/software/Jetson/Linux_for_Tegra/sources/kernel/qemu-passthrough;
    name = "qemu-source";
  };

  buildInputs = with pkgs; [
    acpica-tools
    alsa-lib
    alsa-oss
    attr
    autoconf
    autoconf-archive
    autogen
    automake
    # autoreconf-hook
    baobab
    basez
    bat
    bc
    bintools
    bison
    bzip2
    canokey-qemu
    capstone
    cargo
    ceph
    cmake
    cmocka
    curl
    cyrus_sasl
    dbus
    dtc
    fatresize
    fdtools
    flex
    fuse
    fuse3
    gawk
    gcc
    gcc9  # GCC 9 required for Linux 5.10
    geany
    gh
    git
    gitg
    gitty
    glib
    gnumake
    gnutls
    gparted
    gsasl
    gtk3
    gtk3-x11
    gtk-vnc
    gusb
    iconv
    icu
    json_c
    kconfig-frontends
    keyutils
    lazygit
    libaio
    libbpf
    libcacard
    libcap
    libcap_ng
    libcxx
    libcxxStdenv
    libdrm
    libdwg
    libepoxy
    libevdev
    libevdevc
    libevdevplus
    libgcrypt
    libgpiod
    libibumad
    libiscsi
    libndctl
    libnfs
    libseccomp
    libselinux
    libslirp
    libssh
    libsysprof-capture
    libtasn1
    libtool
    libtpms
    libudev0-shim
    libudev-zero
    liburing
    libusb1
    libusbp
    libvncserver
    libxkbcommon
    libzip
    lynx
    lzfse
    lzo
    makeWrapper
    meld
    meson
    mktemp
    multipath-tools
    ncurses
    neovim
    nettle
    ninja
    nix-prefetch-git
    openssl
    pam_p11
    pam_u2f
    parted
    patchelf
    perl
    picocom
    pipewire
    pixman
    pkg-config
    polkit
    proot
    pulseaudio
    python3
    python311Packages.sphinx-rtd-theme
    qemu_kvm
    qemu-utils
    rdma-core
    ripgrep
    rng-tools
    rutabaga_gfx
    SDL2
    SDL2_image
    sealcurses
    snappy
    sphinx
    spice
    spice-autorandr
    spice-gtk
    spice-protocol
    spice-up
    spice-vdagent
    ssh-agents
    sshpass
    sshs
    ssh-tools
    stdenv.cc
    sysprof
    systemd
    texinfo
    tigervnc
    unixtools.xxd
    usbredir
    util-linux
    valgrind
    vde2
    virglrenderer
    vte
    wayland-protocols
    wget
    xdp-tools
    xgboost
    zlib
  ];

  configurePhase = ''
    ./configure --target-list=aarch64-softmmu \
    --enable-sdl --enable-gtk --enable-opengl \
    --disable-dbus-display \
    --enable-vnc --enable-vnc-jpeg \
    --disable-docs \
    --prefix=$out \
    --enable-vde \
    --enable-vhost-net --enable-vhost-user \
  '';
  /*
    --enable-vhost-kernel --enable-vhost-net --enable-vhost-user \
    --enable-vhost-user-blk-server --enable-vfio-user-server \
    --enable-vhost-crypto --enable-vhost-vdpa --enable-virtfs \
    --enable-virtfs-proxy-helper \
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

