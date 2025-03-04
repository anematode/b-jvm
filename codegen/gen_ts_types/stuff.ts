const _BRAND = Symbol("brand");

type Nominal<N extends string, T> = T & { [_BRAND]: Record<N, true> };
