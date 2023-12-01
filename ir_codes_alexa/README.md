# Samsung Code

A good introduction to the 'Pronto format' that you show above is at [Remote Central](https://www.remotecentral.com/features/irdisp2.htm)

For the specific example above, for a Samsung OFF code given at [your remote central link](https://www.remotecentral.com/cgi-bin/mboard/rc-discrete/thread.cgi?5780), the full code is given as a sequence of 16-bit numbers represented in hexadecimal with spaces in between:

```
0000 006D 0000 0022 00AC 00AC 0015 0040 0015 0040 0015 0040 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0040 0015 0040 0015 0040 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0040 0015 0040 0015 0015 0015 0015 0015 0040 0015 0040 0015 0040 0015 0040 0015 0015 0015 0015 0015 0040 0015 0040 0015 0015 0015 0689
```
You can break that down as a preamble (broken out here for interest, but not needed if you already have a working Samsung code):

- 0x0000 - This is raw code format data
- 0x006D - Frequency 109 decimal = 38.028kHz (see above link for calculation)
- 0x0000 - No burst pairs in first sequence
- 0x0022 - 34 decimal - 34 burst pairs of signal follow
- 00AC 00AC - First burst of signal - on for 0xAC (172 decimal) cycles at 38kHz, off for the same amount
- After that comes 32 pairs of data "burst pairs" (which is likely the only bit you need if you already have other codes for the same device)
- 0015 0689 - Final burst of signal - on for 0x15 (21 decimal) cycles, off for 0x689 (1673 decimal) cycles, guaranteeing 44ms without any IR before the next code can be transmitted


To interpret the data manually, copy it out (e.g. into a text editor) in groups of 8 numbers:

```
0015 0040 0015 0040 0015 0040 0015 0015
0015 0015 0015 0015 0015 0015 0015 0015
0015 0040 0015 0040 0015 0040 0015 0015
0015 0015 0015 0015 0015 0015 0015 0015
0015 0015 0015 0015 0015 0015 0015 0040
0015 0040 0015 0015 0015 0015 0015 0040
0015 0040 0015 0040 0015 0040 0015 0015
0015 0015 0015 0040 0015 0040 0015 0015
```

Then:

- Ignore the columns where all the numbers are the same (even columns above, which represent the on time - 0x15 = 21 decimal cycles of IR at 38kHz )
- For the remaining columns (which represent the off-time), replace the big numbers (0x40 in this case) with '1' and the small (0x15) with '0'.


For the first line

```
0015 0040 0015 0040 0015 0040 0015 0015
```

ignoring the even numbered columns leaves:

```
0040 0040 0040 0015
```

replacing those with 1's and 0's

```
1    1    1    0
```

and if you convert that into hexadecimal, it's 'E'

Next line is '0', then 'E' then '0' (already it's comforting to see it starting with the same E0E0 that starts your other Samsung code above...), and the remaining lines make it E0E019E6

Doing the same with the ON code gives you E0E09966

And as I've needed to solve the same problem just recently for the same codes, I can confirm that my Samsung TV responds to those codes as OFF and ON.

Not surprisingly, there are a variety of software tools to convert between formats, and a huge range of formats to describe the same signal (explained very well by [xkcd](https://xkcd.com/927/)). For example, [irdb on GitHUB](https://github.com/probonopd/irdb) will decode the above string to "Protocol NECx2, device 7, subdevice 7, OBC 152". It's up to you to know that you have to

- bit-reverse the device number '07' to get 'E0'
- bit-reverse the subdevice number (also '07') to get 'E0'
- convert 152 to hexadecimal and reverse the bits to get '19'
- calculate the last two digits as ( 0xFF - the bit-reversed OBC ), 0xFF - 0x19 = 0xE6, giving the final 8 bits 'E6'


[Reference](https://stackoverflow.com/questions/60718588/understanding-ir-codes-for-samsung-tv)