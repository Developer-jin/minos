# Minos - Type 1 Hypervisor for ARMv8-A

Minos是一款轻量级的开源的面向移动及嵌入式平台的Type 1 Hypervisor, 可以直接运行在裸机环境。Minos实现了一套完整的虚拟化框架，可以在同一硬件平台上运行和管理多个VM. Minos提供了包括cpu虚拟化; 中断虚拟化; 内存虚拟化; Timer虚拟化; 以及一些常用外设虚拟化的支持。

Minos提供一个运行于VM0上的应用程序"mvm"来支持Guest VM的管理。通过mvm，可以方便的创建，销毁以及管理VM,同时MVM提供基于virtio的半虚拟化解决方案, 支持virtio-console, virtio-blk(测试中)，virtio-net(测试中)等设备

Minos适用于移动及嵌入式平台。当前只支持ARMv8-A架构，硬件平台原生支持Marvell的Esspressobin开发板，且理论上ARMv8-A + GICV3组合的平台都可以被支持。软件调试平台支持ARM官方的Fix Virtual Platform (这里简称FVP), 开发者可以用ARM DS5工具来进行仿真和调试, 通过mvm程序创建的VM， 也是根据FVP平台的硬件来进行虚拟

# Download Source Code And Tools for Minos

1. 创建工作目录

        # mkdir ~/minos-workspace
        # cd ~/minos-workspace

2. 下载gcc交叉编译工具

        # wget https://releases.linaro.org/components/toolchain/binaries/latest/aarch64-linux-gnu/gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu.tar.xz
        # tar xjf gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu.tar.xz
        # sudo mv gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu /opt
        # echo "export PATH=/opt/gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu/bin:$PATH" >> ~/.bashrc
        # source ~/.bashrc

3. 下载abootimg

        # sudo apt-get install abootimg

 abootimg 工具用来制作android bootimge，mvm使用此格式image来加载linux内核，ramdisk和dtb文件

4. 下载device tree代码编译工具

        # sudo apt-get install device-tree-compiler

5. 下载minos sample

        # git clone 

 minos-sample提供Guest VM的dts文件，和以及制作好的Guest VM boot.img文件

6. 下载minos hypervisor 源码

        # git clone

7. 下载linux kernel 源码

        # git clone

8. 下载atf源码

        # git clone

在FVP上运行和调试minos时需要用到

# Run Minos on Marvel Esspressobin

1. 编译Minos

        # make

 Minos默认编译平台为Marvel Esspressobin，编译完成后会在 hypervisor/out目录下生成minos.bin以及在mvm目录下生成mvm应用程序

2. 编译Marvel kernel

        # export ARCH=arm64
        # export CROSS_COMPILE=aarch64-linux-gnu-
        # make mvebu_v8_lsp_defconfig
        # make -j4

 编译完成后会在arch/arm64/boot目录下生成Image 内核文件。

3. Esspressobin默认的内核存放在/boot目录下，把minos.bin和新的Image拷贝到/boot目录下

4. 更新uboot 环境设置

 启动开发板到命令行状态，执行以下命令更新uboot启动设置（这里以emmc版本的esspre开发板举例，采用sd卡启动的开发板，方法类似）

5. 设置完之后重启开发板，下次开发板启动将会加载minos.bin, 进入虚拟化环境

 提示: 如果因为配置错误导致系统启动不了，只需要用原来的启动参数先启动到非虚拟化环境，然后把可以正常运行的minos.bin替换就可以

# Run Minos on Arm FVP

1. 下载ARM FVP,创建工作目录

        # mkdir arm-fvp

 现在arm fvp可以上arm的官网下载，Minos已经在FVP_Base_AEMv8A 以及FVP_Base_Cortex-A57x2-A53x4 上测试通过，这里我们默认使用FVP_Base_AEMv8A来进行测试。另外如果想基于Minos做相关开发，也可以安装arm ds5调试工具，安装完之后自带以上两个fvp。以下是安装使用ds5的相关教程

 - **ARM FVP(固定虚拟平台)Linux内核调试简明手册:**[https://www.jianshu.com/p/c0a9a4b9569d](https://www.jianshu.com/p/c0a9a4b9569d)
 

2. 编译Minos

        # make PLATFORM=fvp

3. 编译FVP Kernel

        # make ARCH=arm64 defconfig && make ARCH=arm64 -j8 Image

4. 编译arm trust firmware

        # make PLAT=fvp RESET_TO_BL31=1 ARM_LINUX_KERNEL_AS_BL33=1 PRELOADED_BL33_BASE=0xc0000000 ARM_PRELOADED_DTB_BASE=0x83e00000

5. 下载arm64 virtio-block image

        # wget https://releases.linaro.org/archive/14.07/openembedded/aarch64/vexpress64-openembedded_minimal-armv8-gcc-4.9_20140727-682.img.gz
        # gunzip vexpress64-openembedded_minimal-armv8-gcc-4.9_20140727-682.img.gz
        # mv vexpress64-openembedded_minimal-armv8-gcc-4.9_20140727-682.img sd.img

6. 运行

        # /usr/local/DS-5_v5.27.0/bin/FVP_Base_AEMv8A               \
        -C pctl.startup=0.0.0.0                                     \
        -C bp.secure_memory=0                                       \
        -C cluster0.NUM_CORES=4                                     \
        -C cache_state_modelled=1                                   \
        -C cluster0.cpu0.RVBAR=0x04020000                           \
        -C cluster0.cpu1.RVBAR=0x04020000                           \
        -C cluster0.cpu2.RVBAR=0x04020000                           \
        -C cluster0.cpu3.RVBAR=0x04020000                           \
        -C bp.hostbridge.userNetPorts="8022=22"                     \
        -C bp.hostbridge.userNetworking=true                        \
        -C bp.dram_size=8                                           \
        -C bp.smsc_91c111.enabled=true                              \
        -C bp.virtioblockdevice.image_path=sd.img                   \
        --data cluster0.cpu0=bl31.bin@0x04020000                    \
        --data cluster0.cpu0=fdt.dtb@0x83e00000                     \
        --data cluster0.cpu0=Image@0x80080000                       \
        --data cluster0.cpu0=minos.bin@0xc0000000

7. 运行fvp之后，可以在主机上运行以下命令通过ssh来登入fvp

        # ssh -p 8022 root@127.0.0.1

# mvm使用方法

Minos提供两种方式来创建vm, 一种是通过minos源码下config文件夹里面对应的平台配置文件，通过创建一个vmtag的json成员来创建vm，通过此方式创建的vm比较适用于嵌入式系统用来定制特定的vm。

另外一种方式就是通过Minos提供的vm管理工具mvm来配置, Minos代码编译完成之后会在 mvm目录下面生成。把这个二进制程序直接拷贝到开发板上就可以执行了。当前mvm已经支持了VM的重启和关机操作

Usage: mvm [options] 

    -c <vcpu_count>            (set the vcpu numbers of the vm)
    -m <mem_size_in_MB>        (set the memsize of the vm - 2M align)
    -i <boot or kernel image>  (the kernel or bootimage to use)
    -s <mem_start>             (set the membase of the vm if not a boot.img)
    -n <vm name>               (the name of the vm)
    -t <vm type>               (the os type of the vm )
    -b <32 or 64>              (32bit or 64 bit )
    -r                         (do not load ramdisk image)
    -v                         (verbose print debug information)
    -d                         (run as a daemon process)
    -D                         (device argument)
    -C                         (set the cmdline for the os)

例如以下命令用来创建一个 2 vcpu， 84M内存， bootimage为boot.img以及带有virtio-console设备的64位(当前minos只支持64位vm)linux虚拟机.

        #./mvm -c 4 -m 84M -i boot.img -n elinux -t linux -b 64 -v -d -C "console=hvc0 loglevel=8 consolelog=9 loglevel=8 consolelog=9" -D virtio_console,@pty:

创建VM的log如下

        [INFO ] no rootfs is point using ramdisk if exist
        root@genericarmv8:~# [INFO ] boot image infomation :
        [INFO ] magic        - ANDROID!
        [INFO ] kernel_size  - 0x877800
        [INFO ] kernel_addr  - 0x80080000
        [INFO ] ramdisk_size - 0x104e21
        [INFO ] ramdisk_addr - 0x83000000
        [INFO ] dtb_size     - 0xcc4
        [INFO ] dtb_addr     - 0x83e00000
        [INFO ] tags_addr    - 0x0
        [INFO ] page_size    - 0x800
        [INFO ] name         - 
        [INFO ] cmdline      - console=hvc0 loglevel=8 consolelog=9
        [INFO ] create new vm *
        [INFO ]         -name       : elinux
        [INFO ]         -os_type    : linux
        [INFO ]         -nr_vcpus   : 2
        [INFO ]         -bit64      : 1
        [INFO ]         -mem_size   : 0x5400000
        [INFO ]         -mem_start  : 0x80000000
        [INFO ]         -entry      : 0x80080000
        [INFO ]         -setup_data : 0x83e00000
        [DEBUG] load kernel image: 0x80000 0x800 0x877800
        [DEBUG] load ramdisk image:0x3000000 0x878000 0x104e21
        [DEBUG] vdev : irq-32 gpa-0x7fad895000 gva-0x40000000
        [INFO ] ***********************************************
        [INFO ] virt-console backend redirected to /dev/pts/1
        [INFO ] ***********************************************
        [INFO ] add cmdline - console=hvc0 loglevel=8 consolelog=9 loglevel=8 consolelog=9
        [INFO ]         - delete cpu@2
        [INFO ]         - delete cpu@3
        [INFO ]         - delete cpu@4
        [INFO ]         - delete cpu@5
        [INFO ]         - delete cpu@6
        [INFO ]         - delete cpu@7
        [DEBUG] found 1 rsv memory region
        [DEBUG] add rsv memory region : 0x80000000 0x10000
        [INFO ] setup memory 0x0 0x80 0x0 0x4005
        [INFO ] set ramdisk : 0x83000000 0x104e21
        [INFO ] add vdev success addr-0x40000000 virq-32

Minos当前已经支持virtio-console后端驱动，创建完vm之后可以用minicom等终端工具登入到刚才创建的VM

        # minicom /dev/pts/1

# 制作制定义bootimage

Minos默认提供的boot.img的ramdisk使用的是busybox标准rootfs,如果需要自定义自己定制ramdisk,也和简单，只需要制作好ramdisk之后 用以下命令打包:

        # abootimg --create boot.img -c kerneladdr=0x80080000 -c ramdiskaddr=0x83000000 -c secondaddr=0x83e00000 -c cmdline="console=hvc0 loglevel=8 consolelog=9" -k Image -s fvp.dtb -r ramdisk.img

