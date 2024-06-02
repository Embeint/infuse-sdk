# Crypto Profiling Results (ascon-128a)

## Summary
Application flash and encryption duration on a Cortex-M33 (nRF5340dk)
|        Backend | Flash | 64 bytes | 1024 bytes |
|---------------:|------:|---------:|-----------:|
|            ref | 35416 |   222 uS |    2039 uS |
|         armv7m | 40140 |    91 uS |     706 uS |
| armv7m_lowsize | 34052 |    96 uS |     811 uS |
|   armv7m_small | 35040 |    91 uS |     759 uS |

Based on the above results the reasonable default is `armv7m_small` which saves
approximately 5kB of ROM for only a 7% performance drop on large packets.

```
CONFIG_CRYPTO_ASCON_128A=y
CONFIG_CRYPTO_ASCON_128=n
CONFIG_CRYPTO_ASCON_80PQ=n
```

## Full Results (ref)
### Application Size
```
[241/242] Linking C executable zephyr/zephyr.elf
Memory region         Used Size  Region Size  %age Used
           FLASH:       35416 B         1 MB      3.38%
             RAM:       12088 B       448 KB      2.63%
        IDT_LIST:          0 GB        32 KB      0.00%
```
### Runtime performance
```
[00:00:00.504,730] <inf> app: ASCON backend - ref
[00:00:00.486,846] <inf> app: ascon-128a
[00:00:00.491,333] <inf> app:   Length |  Enc: Cycles (ns) |  Dec: Cycles (ns)
[00:00:00.499,176] <inf> app:        1 |    7521 ( 117515) |    7663 ( 119734)
[00:00:00.507,019] <inf> app:       16 |    9708 ( 151687) |    9820 ( 153437)
[00:00:00.514,862] <inf> app:       64 |   16380 ( 255937) |   16453 ( 257078)
[00:00:00.537,597] <inf> app:      128 |   25276 ( 394937) |   25297 ( 395265)
[00:00:00.560,302] <inf> app:      256 |   43068 ( 672937) |   42985 ( 671640)
[00:00:00.583,007] <inf> app:      512 |   78652 (1228937) |   78361 (1224390)
[00:00:00.605,712] <inf> app:     1024 |  149820 (2340937) |  149113 (2329890)
```

## Full Results (armv7m)
### Code size
```
[242/243] Linking C executable zephyr/zephyr.elf
Memory region         Used Size  Region Size  %age Used
           FLASH:       40140 B         1 MB      3.83%
             RAM:       12088 B       448 KB      2.63%
        IDT_LIST:          0 GB        32 KB      0.00%
```
### Runtime performance
```
[00:00:00.484,344] <inf> app: ASCON backend - armv7m
[00:00:00.489,685] <inf> app: ascon-128a
[00:00:00.403,900] <inf> app:   Length |  Enc: Cycles (ns) |  Dec: Cycles (ns)
[00:00:00.411,712] <inf> app:        1 |    4619 (  72171) |    4654 (  72718)
[00:00:00.419,555] <inf> app:       16 |    5648 (  88250) |    5631 (  87984)
[00:00:00.427,398] <inf> app:       64 |    8036 ( 125562) |    7998 ( 124968)
[00:00:00.450,134] <inf> app:      128 |   11220 ( 175312) |   11154 ( 174281)
[00:00:00.472,839] <inf> app:      256 |   17588 ( 274812) |   17466 ( 272906)
[00:00:00.495,574] <inf> app:      512 |   30324 ( 473812) |   30090 ( 470156)
[00:00:00.518,280] <inf> app:     1024 |   55796 ( 871812) |   55338 ( 864656)
```

## Full Results (armv7m_lowsize)
### Code size
```
[244/245] Linking C executable zephyr/zephyr.elf
Memory region         Used Size  Region Size  %age Used
           FLASH:       34052 B         1 MB      3.25%
             RAM:       12088 B       448 KB      2.63%
        IDT_LIST:          0 GB        32 KB      0.00%
```
### Runtime performance
```
[00:00:00.507,171] <inf> app: ASCON backend - armv7m_lowsize
[00:00:00.513,214] <inf> app: ascon-128a
[00:00:00.402,374] <inf> app:   Length |  Enc: Cycles (ns) |  Dec: Cycles (ns)
[00:00:00.410,217] <inf> app:        1 |    3321 (  51890) |    3459 (  54046)
[00:00:00.418,060] <inf> app:       16 |    4099 (  64046) |    4216 (  65875)
[00:00:00.425,933] <inf> app:       64 |    6436 ( 100562) |    6553 ( 102390)
[00:00:00.448,669] <inf> app:      128 |    9552 ( 149250) |    9669 ( 151078)
[00:00:00.471,405] <inf> app:      256 |   15784 ( 246625) |   15901 ( 248453)
[00:00:00.494,110] <inf> app:      512 |   28248 ( 441375) |   28365 ( 443203)
[00:00:00.516,845] <inf> app:     1024 |   53176 ( 830875) |   53293 ( 832703)
```
## Full Results (armv7m_small)
### Code size
```
[242/243] Linking C executable zephyr/zephyr.elf
Memory region         Used Size  Region Size  %age Used
           FLASH:       35040 B         1 MB      3.34%
             RAM:       12088 B       448 KB      2.63%
        IDT_LIST:          0 GB        32 KB      0.00%
```
### Runtime performance
```
[00:00:00.479,034] <inf> app: ASCON backend - armv7m_small
[00:00:00.484,893] <inf> app: ascon-128a
[00:00:00.399,932] <inf> app:   Length |  Enc: Cycles (ns) |  Dec: Cycles (ns)
[00:00:00.407,745] <inf> app:        1 |    3239 (  50609) |    3255 (  50859)
[00:00:00.415,588] <inf> app:       16 |    3935 (  61484) |    3953 (  61765)
[00:00:00.423,461] <inf> app:       64 |    6075 (  94921) |    6095 (  95234)
[00:00:00.446,166] <inf> app:      128 |    8927 ( 139484) |    8951 ( 139859)
[00:00:00.468,841] <inf> app:      256 |   14631 ( 228609) |   14663 ( 229109)
[00:00:00.491,546] <inf> app:      512 |   26039 ( 406859) |   26087 ( 407609)
[00:00:00.514,251] <inf> app:     1024 |   48855 ( 763359) |   48935 ( 764609)
```
