# qemu-w-git.nix
# build with command: nix-build qemu-w-git.nix
{ pkgs ? import <nixpkgs> {}, ... }:

pkgs.stdenv.mkDerivation rec {
  pname = "qemu-passthrough";
  version = "1.1";

  # Fetch the QEMU source code from the stable-9.0 branch
  src = pkgs.fetchFromGitHub {
    owner = "KimGSandstrom";
    repo = "qemu-passthrough";
    rev = "ba21ded8858a3e1a97cf30844ad80e7fe0de3acf";
    hash = "sha256-xVxGmcgpNvUIuF7EGv2nPhp7rDSZ4fxT1oWkCZVTTis=";

    # owner = "qemu";
    # repo = "qemu";
    # rev = "v9.0.2";
    # rev = "5ebde3b5c00e15f560f73055fac4ab31c0cac6d2";
    # sha256 = "sha256-bNStaemT9vnAswPTQUHy9ZIILCBd5HzJXt5r6K0lVwk=";

    /*
    # Update the URL for the keycodemapdb subproject
    subprojects = {
      keycodemapdb = pkgs.fetchFromGitHub {
        owner = "qemu";
        repo = "keycodemapdb";
        rev = "22b8996dba9041874845c7446ce89ec4ae2b713d";
        sha256 = "sha256-TDhUF9wxicYezlcJqzXvkrKTt9YjKYiQaHCKNpO6vIU=";
      };
    };
    */
  };

  buildInputs = with pkgs; [
    git
    gitlab
    util-linux
    pkg-config
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
    SDL2
    SDL2_image
    ceph
    gsasl
    xdp-tools
    fdtools
    dtc
  ];

  postFetch = ''
    cd $src
    git submodule update --init --recursive
  '';

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

