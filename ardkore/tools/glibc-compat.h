/* Force-included by build-handheld.sh. Pins math functions that gained new
 * symbol versions in glibc 2.27/2.29 to the aarch64 baseline (2.17), so the
 * binary loads on any aarch64 glibc. The old versions are the same maths,
 * just with the older errno handling. */
#if defined(__aarch64__) && defined(__linux__)
__asm__(".symver exp,exp@GLIBC_2.17");
__asm__(".symver log,log@GLIBC_2.17");
__asm__(".symver pow,pow@GLIBC_2.17");
__asm__(".symver expf,expf@GLIBC_2.17");
__asm__(".symver logf,logf@GLIBC_2.17");
__asm__(".symver powf,powf@GLIBC_2.17");
__asm__(".symver exp2f,exp2f@GLIBC_2.17");
__asm__(".symver log2f,log2f@GLIBC_2.17");
#endif
