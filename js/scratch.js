
// Long representation: two 32-bit words, both unsigned.

let a_lo, a_hi, b_lo, b_hi, c_lo, c_hi;

// addition
let tmp = a_lo + b_lo;
c_hi = (a_hi + b_hi + (tmp > (~0 >>> 0))) >>> 0;
c_lo = tmp >>> 0;

// subtraction
tmp = a_lo - b_lo;
c_hi = (a_hi - b_hi - (tmp > a_lo)) >>> 0;
c_lo = tmp >>> 0;

// multiplication
c_lo = Math.imul(a_lo, b_lo) >>> 0;
c_hi = (Math.imul(a_lo, b_hi) >>> 0) + Math.imul(a_hi, b_lo)
