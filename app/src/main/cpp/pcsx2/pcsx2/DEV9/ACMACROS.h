
#define U32FU16(hi, lo) (((uint32_t)(hi) << 16) | (uint16_t)(lo)) // u32 from two u16
#define CLRB(V, n) V &= ~(n) //CLeaR Bit
#define U32FU16(hi, lo) (((uint32_t)(hi) << 16) | (uint16_t)(lo)) // u32 from two u16
#define describe_6u_buff(x)  x[0], x[1], x[2], x[3], x[4], x[5]
#define describe_8u_buff(x)  x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7]
#define describe_16u_buff(x) x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7], x[8], x[9], x[10], x[11], x[12], x[13], x[14], x[15]
