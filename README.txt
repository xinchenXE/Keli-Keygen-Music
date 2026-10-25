Keil 注册机音乐 · 最小程序版（PE32）
================================================

交付物：Keil32_upx.exe —— 10,752 字节，Win11 双击即响。

关于「最小的程序格式」
----------------------
Windows 历史上最小的程序格式是 .COM：没有文件头，代码从偏移 0x100 开始，
几十字节就能是一个程序。但 64 位 Windows 10/11 不再带 NTVDM，
.COM 已经跑不了了（双击只会在记事本里打开，或者直接报错）。

Win11 x64 能双击运行的程序格式里，最小的就是 PE：
  PE32   （32 位，走 WOW64）  ← 本版用的
  PE32+  （64 位）
两者里 PE32 的头更小、代码更紧凑，所以选了 32 位。

顺带说一句：理论上手写的"极小 PE"可以只有几百字节，但我们这个程序背了
9,904 字节的原曲模块数据，那才是体积的主体，PE 头再省也只是零头。

两个成品
--------
  Keil32_upx.exe   10,752 B   UPX 加壳，最小，平时用这个
  Keil32.exe       18,944 B   未加壳，方便查毒/调试

  依赖：KERNEL32.dll / msvcrt.dll / WINMM.DLL（全是系统自带）

为什么比之前的 11,776 又小了 1,024 字节
---------------------------------------
1. 换成 32 位：代码密度更高，PE 头更小，UPX 的 32 位解压桩也比 64 位的短。
2. clang -Oz -flto：LTO 把四个 .c 合并优化，重复代码和未用函数全被抹掉，
   未加壳从 20,992 降到 18,944。
3. -nostdlib + 自己的入口（__start），不链 CRT。
4. 关掉 WAV 录制/文件加载分支（-DMINI_PLAYER），只留播放路径。
5. UPX --best（LZMA）：18,944 → 10,752。

  试过但没用：-ffunction-sections + --gc-sections（反而涨）、
  --section-alignment 512（镜像加载失败）、--build-id=none（无变化）、
  -Os 代替 -Oz（更大）。

体积花在哪
----------
  18,944 字节里：原曲模块 9,904、代码约 5.6 KB、导入表约 1 KB、PE 头与对齐约 2 KB。
  加壳后 10,752：模块压到约 3.2 KB，代码压到约 2.8 KB，剩下是解压桩和头。
  想再往下只能整体手写重放器（约省 1 KB），风险大于收益，没做。

音色
----
和参考渲染逐采样一致：44.1 kHz / 16 bit / 立体声，Amiga Paula 四声道 + BLEP。
验证方法：用同一份源码编一个 fcRecordWAV 版本，渲染完整的 8 分 32 秒
（45,216,664 个采样），与 keil_keygen_full.wav 逐点比对，max diff = 0。

运行
----
双击即可，按 Q 停止。程序会把整首曲子一直循环演奏（原曲本来就是这样）。

构建
----
  build32.bat
  等价命令：
    i686-w64-mingw32-clang -Oz -flto -nostdlib -DMINI_PLAYER -DAUDIODRIVER_NULL \
        -Wl,--entry=__start -Wl,-s -o Keil32.exe \
        main.c replayer_min.c paula.c nulldriver.c -lkernel32 -lmsvcrt -lwinmm
    upx --best Keil32.exe

工具链
------
  E:\project\test\tools\llvm-mingw-20261006-msvcrt-i686   （clang/lld，32 位）
  E:\project\test\tools\upx\upx.exe                        （UPX 5.2.1）
  E:\project\test\tools\tcc                                （试过 tcc，代码太大，弃用）

源码
----
  main.c          播放循环 + 自己的 Win32 声明（不 include windows.h，省头文件依赖）
  replayer_min.c  特化后的 FC14 重放器
  paula.c         Paula 四声道混音
  nulldriver.c    混音驱动桩
  mod.h           原曲模块（9,904 字节）
  rmain.c         仅用于校验音质的渲染入口

许可
----
重放器/混音器源自 https://github.com/8bitbubsy/fc14play（BSD-3-Clause,
Copyright (c) 2026 Olav Sørensen）。

已回退：手写汇编混音器实验
--------------------------
之前做过一版，把 paula.c 整段换成手写 x86 汇编（mixer.asm，4 声道、线性插值），
体积能到 8,704 字节（比现在小 2,048，-19%），但实测有两个致命问题：

  * 音色不对：整曲与参考渲染逐点比对，SNR 只有 1.8 dB；
  * 播放不正常：实际运行无法完整播放，会被判为"没办法正常播放"。

根因是原实现依赖 minBLEP 抗混叠系数表 + 浮点管线 + 按 2 字节为单位的 DMA
取样语义，手写简化版无法等价复现。已整体回退。

实验文件已移到 _exp_mixer\ 存档（AsmMixer.exe / AsmMixer_upx.exe / mixer.asm），
当前目录的构建脚本与产物都是回退后的稳定版本，build32.bat 只编译 paula.c。

踩过的坑（留作记录）：
  1. div 指令的 edx 既是除数又是被除数高 32 位 —— 必须先挪到别的寄存器；
  2. 混音函数里用 ebx 却没保存，cdecl 下 ebx 是 callee-saved；
  3. 局部变量放在 ebp-4/-8/-12，正好覆盖刚压栈的 ebx/esi/edi；
  4. 采样指针可能为 NULL（原实现有 nullSample 兜底）；
  5. 原 paulaInit 末尾会把 samplesPerTickFrac 清零，照抄公式会漂移。
