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
[00:00:00.509,826] <inf> app: ascon-128a
[00:00:00.514,221] <inf> app:   Length    1 Encrypt   6570 ( 102656 ns) Decrypt   6662 ( 104093 ns)
[00:00:00.523,620] <inf> app:   Length   16 Encrypt   8396 ( 131187 ns) Decrypt   8522 ( 133156 ns)
[00:00:00.533,050] <inf> app:   Length   64 Encrypt  14208 ( 222000 ns) Decrypt  14349 ( 224203 ns)
[00:00:00.542,449] <inf> app:   Length  128 Encrypt  21960 ( 343125 ns) Decrypt  22125 ( 345703 ns)
[00:00:00.551,879] <inf> app:   Length  256 Encrypt  37464 ( 585375 ns) Decrypt  37677 ( 588703 ns)
[00:00:00.561,279] <inf> app:   Length  512 Encrypt  68472 (1069875 ns) Decrypt  68781 (1074703 ns)
[00:00:00.570,709] <inf> app:   Length 1024 Encrypt 130488 (2038875 ns) Decrypt 130989 (2046703 ns)
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
[00:00:00.494,079] <inf> app:   Length    1 Encrypt   3684 (  57562 ns) Decrypt   3266 (  51031 ns)
[00:00:00.503,509] <inf> app:   Length   16 Encrypt   3885 (  60703 ns) Decrypt   3848 (  60125 ns)
[00:00:00.512,908] <inf> app:   Length   64 Encrypt   5852 (  91437 ns) Decrypt   5840 (  91250 ns)
[00:00:00.522,338] <inf> app:   Length  128 Encrypt   8476 ( 132437 ns) Decrypt   8500 ( 132812 ns)
[00:00:00.531,768] <inf> app:   Length  256 Encrypt  13724 ( 214437 ns) Decrypt  13820 ( 215937 ns)
[00:00:00.541,168] <inf> app:   Length  512 Encrypt  24220 ( 378437 ns) Decrypt  24460 ( 382187 ns)
[00:00:00.550,598] <inf> app:   Length 1024 Encrypt  45212 ( 706437 ns) Decrypt  45740 ( 714687 ns)
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
[00:00:00.517,639] <inf> app:   Length    1 Encrypt   3146 (  49156 ns) Decrypt   3244 (  50687 ns)
[00:00:00.527,038] <inf> app:   Length   16 Encrypt   3835 (  59921 ns) Decrypt   3972 (  62062 ns)
[00:00:00.536,468] <inf> app:   Length   64 Encrypt   6118 (  95593 ns) Decrypt   6243 (  97546 ns)
[00:00:00.545,898] <inf> app:   Length  128 Encrypt   9170 ( 143281 ns) Decrypt   9271 ( 144859 ns)
[00:00:00.555,297] <inf> app:   Length  256 Encrypt  15274 ( 238656 ns) Decrypt  15327 ( 239484 ns)
[00:00:00.564,727] <inf> app:   Length  512 Encrypt  27482 ( 429406 ns) Decrypt  27439 ( 428734 ns)
[00:00:00.574,157] <inf> app:   Length 1024 Encrypt  51898 ( 810906 ns) Decrypt  51663 ( 807234 ns)
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
[00:00:00.489,288] <inf> app:   Length    1 Encrypt   3095 (  48359 ns) Decrypt   3091 (  48296 ns)
[00:00:00.498,718] <inf> app:   Length   16 Encrypt   3684 (  57562 ns) Decrypt   3681 (  57515 ns)
[00:00:00.508,117] <inf> app:   Length   64 Encrypt   5820 (  90937 ns) Decrypt   5806 (  90718 ns)
[00:00:00.517,547] <inf> app:   Length  128 Encrypt   8672 ( 135500 ns) Decrypt   8642 ( 135031 ns)
[00:00:00.526,947] <inf> app:   Length  256 Encrypt  14376 ( 224625 ns) Decrypt  14314 ( 223656 ns)
[00:00:00.536,376] <inf> app:   Length  512 Encrypt  25784 ( 402875 ns) Decrypt  25658 ( 400906 ns)
[00:00:00.545,806] <inf> app:   Length 1024 Encrypt  48600 ( 759375 ns) Decrypt  48346 ( 755406 ns)
```
