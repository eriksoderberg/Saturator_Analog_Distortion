// TinyFmt.h -- header-only, dependency-free stand-in for std::snprintf.
//
// The universal45 (online/u45) toolchain has no C stdio, so <cstdio> /
// snprintf cannot be used in device code. This covers exactly what the
// display needs:  %d %i  %s  %.Nf  %%   with the '+' flag (width is parsed
// and ignored). Always NUL-terminates when n > 0; returns the length the
// full string would have had (like snprintf).
//
// Usage: tfmt::format(buf, sizeof buf, "%+.1fdB", db);
#pragma once

namespace tfmt {

typedef decltype(sizeof 0) Size;

struct Arg
{
  enum Kind { kInt, kDbl, kStr } fKind;
  long long   fI;
  double      fD;
  const char* fS;
  Arg(int v)                : fKind(kInt), fI(v), fD(0.0), fS(nullptr) {}
  Arg(unsigned v)           : fKind(kInt), fI(v), fD(0.0), fS(nullptr) {}
  Arg(long v)               : fKind(kInt), fI(v), fD(0.0), fS(nullptr) {}
  Arg(long long v)          : fKind(kInt), fI(v), fD(0.0), fS(nullptr) {}
  Arg(unsigned long v)      : fKind(kInt), fI(static_cast<long long>(v)), fD(0.0), fS(nullptr) {}
  Arg(double v)             : fKind(kDbl), fI(0), fD(v), fS(nullptr) {}
  Arg(float v)              : fKind(kDbl), fI(0), fD(v), fS(nullptr) {}
  Arg(const char* v)        : fKind(kStr), fI(0), fD(0.0), fS(v) {}
};

struct Out
{
  char* fBuf; Size fCap; Size fLen;
  void put(char c) { if(fCap && fLen + 1 < fCap) fBuf[fLen] = c; ++fLen; }
  void puts(const char* s) { if(!s) s = "(null)"; while(*s) put(*s++); }
  void putu(unsigned long long u)
  {
    char tmp[24]; int k = 0;
    do { tmp[k++] = static_cast<char>('0' + u % 10); u /= 10; } while(u);
    while(k) put(tmp[--k]);
  }
};

inline void putInt(Out& o, long long v, bool plus)
{
  if(v < 0) { o.put('-'); o.putu(0ull - static_cast<unsigned long long>(v)); }
  else      { if(plus) o.put('+'); o.putu(static_cast<unsigned long long>(v)); }
}

inline void putFixed(Out& o, double v, int prec, bool plus)
{
  if(v != v) { o.puts("nan"); return; }
  const bool neg = v < 0.0;
  if(neg) v = -v;
  if(neg) o.put('-'); else if(plus) o.put('+');
  if(v > 1.0e15) { o.puts("inf"); return; }
  if(prec > 9) prec = 9;
  unsigned long long scale = 1;
  for(int i = 0; i < prec; ++i) scale *= 10;
  const unsigned long long r = static_cast<unsigned long long>(v * static_cast<double>(scale) + 0.5);
  o.putu(r / scale);
  if(prec > 0)
  {
    o.put('.');
    unsigned long long frac = r % scale, div = scale / 10;
    for(int i = 0; i < prec; ++i) { o.put(static_cast<char>('0' + (frac / div) % 10)); div /= 10; if(!div) div = 1; }
  }
}

inline int formatArgs(char* buf, Size n, const char* f, const Arg* a, int na)
{
  Out o{ buf, n, 0 };
  int ai = 0;
  while(*f)
  {
    if(*f != '%') { o.put(*f++); continue; }
    ++f;
    if(*f == '%') { o.put('%'); ++f; continue; }
    bool plus = false;
    while(*f == '+' || *f == ' ' || *f == '-' || *f == '0' || *f == '#') { if(*f == '+') plus = true; ++f; }
    while(*f >= '0' && *f <= '9') ++f;                               // width: ignored
    int prec = -1;
    if(*f == '.') { ++f; prec = 0; while(*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
    while(*f == 'l' || *f == 'h' || *f == 'z') ++f;                  // length modifiers: ignored
    const char conv = *f;
    if(!conv) break;
    ++f;
    if(ai >= na) { o.put('?'); continue; }
    const Arg& x = a[ai++];
    switch(conv)
    {
      case 'd': case 'i': case 'u':
        putInt(o, x.fKind == Arg::kDbl ? static_cast<long long>(x.fD) : x.fI, plus); break;
      case 'f': case 'F':
        putFixed(o, x.fKind == Arg::kInt ? static_cast<double>(x.fI) : x.fD, prec < 0 ? 6 : prec, plus); break;
      case 's':
        o.puts(x.fKind == Arg::kStr ? x.fS : "?"); break;
      default:
        o.put('?'); break;
    }
  }
  if(n) buf[o.fLen < n ? o.fLen : n - 1] = '\0';
  return static_cast<int>(o.fLen);
}

template<typename... T>
inline int format(char* buf, Size n, const char* f, T... args)
{
  const Arg a[] = { Arg(args)..., Arg(0) };                         // trailing sentinel: never empty
  return formatArgs(buf, n, f, a, static_cast<int>(sizeof...(T)));
}

} // namespace tfmt
