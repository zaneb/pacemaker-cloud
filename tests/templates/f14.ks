#platform=x86, AMD64, or Intel EM64T
#version=DEVEL
# Install OS instead of upgrade
install
# Firewall configuration
firewall --disabled
# Use CDROM installation media
cdrom
# Root password
rootpw --iscrypted $1$g7wJsgcU$gWTT/UE1AhXF86UB8GuuF/
# Network information
network  @NETWORK@
# System authorization information
auth  --useshadow  --passalgo=md5
# Use text mode install
text
# System keyboard
keyboard us
# System language
lang en_US
# SELinux configuration
selinux --disabled
# Do not configure the X Window System
skipx
# Installation logging level
logging --level=info
# Reboot after installation
reboot
# System timezone
timezone  America/New_York
# System bootloader configuration
bootloader --append="console=tty0 console=ttyS0,115200" --location=mbr
# Clear the Master Boot Record
zerombr
# Partition clearing information
clearpart --all  --drives=vda
# Disk partitioning information
part /boot --fstype="ext4" --ondisk=vda --size=200
part pv.2 --grow --ondisk=vda --size=1
volgroup VolGroup00 --pesize=32768 pv.2
logvol swap --fstype swap --name=LogVol01 --vgname=VolGroup00 --size=768 --grow --maxsize=1536
logvol / --fstype ext4 --name=LogVol00 --vgname=VolGroup00 --size=1024 --grow

%post
%end

%packages
@hardware-support
#matahari
#httpd

%end

