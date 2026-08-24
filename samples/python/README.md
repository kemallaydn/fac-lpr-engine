# FAC LPR Python ctypes sample

`fac_lpr_ctypes.py` public C ABI v1'i `ctypes` ile doğrudan tüketir.

Örnek explicit native library path alır, ABI version query yapar, opaque handle lifecycle'ını yönetir, caller-owned BGR8 buffer'ını kopyalamadan `ctypes` pointer'ına bağlar, two-call result-buffer sözleşmesini uygular ve native status/last-error bilgisini Python exception'a map eder.

Windows:

```powershell
python samples/python/fac_lpr_ctypes.py --library build/package-windows/artifacts/bin/Release/fac_lpr_engine.dll --iterations 10000
```

Linux:

```bash
python3 samples/python/fac_lpr_ctypes.py --library build/package-linux/artifacts/lib/libfac_lpr_engine.so --iterations 10000
```

Dönen recognition verisi caller-owned flat v1 buffer'dır. Native engine Python tarafına sahipliği belirsiz string/result pointer'ı vermez; buffer yaşam süresi tamamen consumer'a aittir.
